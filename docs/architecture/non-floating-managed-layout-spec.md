# Non-Floating Managed Layout Specification

## Status
Draft proposal for replacing the current floating-based managed geometry path with a tiled-algorithm path in Hyprland.

## Problem Statement
The current managed layout implementation positions clients by:

1. Toggling each managed client to floating mode.
2. Applying `movewindowpixel` / `resizewindowpixel`.
3. Toggling back on cleanup.

This achieves geometry control, but it couples managed layout semantics to floating behavior and causes classification and transition complexity. The target behavior is Emacs-directed geometry while clients remain tiled in the compositor model.

## Goals
- Keep managed clients tiled (non-floating) while applying Emacs rectangle snapshots.
- Keep the managing Emacs window fullscreen in the managed workspace.
- Preserve existing v1 boundaries:
  - one managed workspace
  - one controller
  - no overlap support
  - native floating/dialog/popup behavior outside managed set
- Preserve existing IPC message contract where possible.
- Keep unmanage/disconnect cleanup deterministic.

## Non-Goals
- Redesigning multi-workspace management.
- Introducing overlapping layouts.
- Replacing current transport/protocol framing.
- Full rewrite of workspace/client lifecycle code.

## Hyprland API Surface (Current Pinned Runtime)
The plugin already uses Hyprland tiled algorithm registration:

- `HyprlandAPI::addTiledAlgo(HANDLE, name, typeInfo, factory)`
- `HyprlandAPI::removeAlgo(HANDLE, name)`

Relevant interfaces:

- `Layout::ITiledAlgorithm` (extends `Layout::IModeAlgorithm`)
  - `newTarget`
  - `movedTarget`
  - `removeTarget`
  - `resizeTarget`
  - `recalculate`
  - `swapTargets`
  - `moveTargetInDirection`
  - `getNextCandidate`
- `Layout::ITarget`
  - `setPositionGlobal(...)`
  - `floating()`, `setFloating(...)`
  - `workspace()`, `space()`, `window()`

Current plugin registers `hyprmacs` tiled algo but leaves `recalculate()` non-intrusive; this proposal makes it geometry-authoritative for managed tiled targets.

## Architecture Overview
### High-Level Direction
- Emacs remains the source of rectangle snapshots (`set-layout` payload).
- Workspace manager remains source of managed membership.
- New tiled-layout state provider in plugin exposes latest accepted snapshot to the registered `hyprmacs` tiled algorithm.
- The tiled algorithm applies geometry with `ITarget::setPositionGlobal` in `recalculate()` for managed tiled targets only.
- Plugin also enforces the managing Emacs window geometry to match full workspace work area while managed.

### Key Shift
- Remove geometry ownership from `LayoutApplier::move_resize_client` (pixel dispatch path).
- Move geometry ownership to `CHyprmacsAlgorithm::recalculate`.

## Data Model Changes
Add workspace-scoped layout state in plugin:

- `workspace_id`
- `version` (monotonic counter or timestamp)
- `rectangles_by_client_id`
- `visible_client_ids`
- `hidden_client_ids`
- `stacking_order`
- `selected_client`
- `managing_emacs_client_id`
- `managing_emacs_fullscreen` (bool invariant, always true while managed)

Recommended ownership:
- `WorkspaceManager` stores canonical accepted snapshot per managed workspace.
- `CHyprmacsAlgorithm` queries snapshot on each `recalculate()`.

## Control Flow
### On `set-layout` IPC
1. Validate payload (required arrays, one rectangle per visible client, non-overlap).
2. Validate clients are managed and tiled-eligible.
3. Store snapshot in workspace layout state.
4. Trigger workspace recalculation (algorithm recalc).
5. Reply `layout-applied` and `state-dump`.

### In `CHyprmacsAlgorithm::recalculate()`
1. Build active target list from current tiled targets in space/workspace.
2. Find managing Emacs target and place it to full work area (`space()->workArea()`).
3. Place managed non-Emacs visible targets by rectangle snapshot.
4. Handle hidden managed clients with existing hidden-workspace behavior.
5. Ignore unmanaged targets.

### On manage and workspace/monitor changes
- On `manage-workspace`, plugin must resolve the controlling Emacs target and pin it to full workspace area.
- On workspace geometry changes (monitor change, work area change), plugin must re-run recalculate and preserve Emacs fullscreen invariant.
- If managing Emacs target is replaced (new frame, old frame closed), plugin updates `managing_emacs_client_id` and reapplies fullscreen placement.

### On client lifecycle events
- `openwindow`: register, classify, reconcile managed membership.
- Floating-state truth remains refreshed from `j/clients` on reliable lifecycle events (`openwindow`, `activewindowv2`, `windowtitlev2`) because dedicated floating events are not reliable in current nested runtime.
- If a managed client becomes floating, it leaves managed set and is excluded from layout snapshots.

## Behavior Rules
### Managed Clients
- Must be tiled in compositor model while managed.
- Positioned by tiled algorithm using snapshot rectangles.
- Never toggled to floating by hyprmacs.

### Emacs Windows
- Managing Emacs window is always fullscreen in the managed workspace.
- Plugin owns placement of the managing Emacs window while managed.
- Non-managing Emacs windows remain unmanaged unless explicitly promoted by workspace manager rules.

### Native Floating/Dialogs/Popups
- Must stay outside managed set.
- Must remain native compositor-managed.
- User-driven move/resize for these floating windows must remain allowed.

### Interactive Repositioning Policy
#### Desired policy
- User repositioning of managed non-Emacs clients is not allowed.
- User repositioning of the managing Emacs window is not allowed while managed.
- User repositioning of native floating windows remains allowed.

#### Enforcement model
1. Prevent first:
   - Keep managed clients and managing Emacs tiled (non-floating) so normal floating drag/resize paths do not apply.
   - Keep dedicated `hyprmacs` layout active for managed workspace.
2. Corrective fallback:
   - If compositor/user actions still produce geometry drift for managed targets, plugin detects drift and snaps target back to authoritative geometry on next recalc.

#### Drift detection and snapback
- Maintain per-managed-target geometry cache:
  - `expected_box`
  - `last_applied_layout_version`
  - `last_applied_timestamp`
- On relevant lifecycle events (`movewindow*`, `activewindowv2`, `windowtitlev2`, workspace/monitor changes), compare current target box with `expected_box`.
- If mismatch is detected for a managed target:
  - schedule one recalc/snapback (debounced), do not recurse immediately inside event handler.

#### Infinite-loop protection
- Snapback actions may themselves produce move events. To avoid loops:
  - Ignore drift events that match `expected_box` within epsilon after recalc.
  - Track `snapback_in_progress` with layout version token; suppress re-entrant scheduling for same token.
  - Use bounded retry/backoff (`N` retries per target per interval) and emit diagnostic log on exhaustion.
- Loop guard is applied only to managed targets; native floating targets are excluded from drift correction entirely.

## Compatibility With Existing Protocol
No mandatory wire changes required:
- Existing `set-layout` fields are sufficient.
- Existing transition/state messages remain valid.

Optional future extension:
- Add `layout_engine` diagnostics field in `layout-applied` payload for debugging (`"tiled-algo"` vs `"pixel-dispatch"` during migration period).

## Migration Plan
### Phase 1: Dual Path Behind Feature Flag
- Add plugin feature flag: `HYPRMACS_TILED_GEOMETRY=1`.
- Keep existing `LayoutApplier` path as fallback.
- When enabled:
  - bypass `ensure_positioning_mode` / `togglefloating` path for managed geometry.
  - use tiled algorithm recalc path.

### Phase 2: Default to Tiled Path
- Make tiled path default in nested config and e2e runs.
- Keep fallback path only for rollback safety.

### Phase 3: Remove Floating Geometry Path
- Remove `togglefloating`-based positioning mode for managed clients.
- Keep hide/show cleanup and native restoration semantics.

## Validation Plan
### Automated
- Extend plugin tests:
  - recalc applies rectangles to tiled targets
  - unmanaged targets unaffected
  - hidden clients excluded from visible geometry application
  - no floating toggle dispatches when tiled path active
  - drift detection schedules snapback for managed targets
  - snapback loop guard suppresses re-entrant oscillation
  - floating unmanaged targets do not trigger snapback
- Extend e2e:
  - managed clients remain non-floating through layout updates
  - floating dialog/utility windows remain native floating and unmanaged
  - managing Emacs window remains fullscreen across layout syncs and workspace geometry changes

### Manual (Nested Runtime)
- Manage workspace with multiple tiled clients.
- Apply split changes in Emacs.
- Confirm:
  - managed clients move/resize correctly
  - `hyprctl -j clients` reports managed clients as non-floating
  - floating dialogs/utilities remain native and top-layer usable

## Risks and Mitigations
- Risk: Hyprland internal layout API changes between versions.
  - Mitigation: pin Hyprland input and keep ABI-compatibility checks in build flow.
- Risk: Target ordering ambiguity in `recalculate`.
  - Mitigation: use client-id keyed rectangle map and explicit target filtering by workspace + managed membership.
- Risk: Hidden workspace moves racing with recalc.
  - Mitigation: preserve current internal-workspace guard logic and keep membership reconciliation authoritative.

## Acceptance Criteria
- Managed clients are positioned from Emacs rectangles without entering floating mode.
- Managing Emacs window is fullscreen for the full managed-session lifetime.
- Native floating/dialog surfaces remain unmanaged.
- User reposition attempts on managed targets are either blocked by tiled behavior or corrected by bounded snapback without infinite event loops.
- Existing one-workspace IPC flow and recovery behavior continue to pass current regression suites.
