# Non-Floating Managed Layout Specification

## Status
Authoritative specification for managed layout behavior and ownership. This document defines the target contract for the next implementation pass.

## Problem Statement
The current managed geometry path depends on floating-mode dispatch (`togglefloating`, `movewindowpixel`, `resizewindowpixel`) to place managed clients. That conflicts with the intended model:

- Emacs should own managed layout planning.
- Hyprmacs should execute that plan for tiled managed clients.
- Native floating windows should remain native and never be absorbed by managed layout.

## Normative Contract
### Ownership
- Emacs is the only layout planner for managed non-floating clients.
- Plugin is the layout executor and state authority.
- `set-layout` remains the planning input (`visible_clients`, `hidden_clients`, `rectangles`, `stacking_order`, `selected_client`, `input_mode`).

### Hard Cutover
- No dual path and no feature-flagged fallback for managed geometry.
- Managed geometry must not depend on `togglefloating`, `movewindowpixel`, or `resizewindowpixel`.
- Geometry authority moves to the registered `hyprmacs` tiled algorithm (`CHyprmacsAlgorithm::recalculate()`).

## Window Classes and Layering
### Class A: Managing Emacs window
- Must be non-floating.
- Must occupy workspace usable area (`space()->workArea()`), respecting bars/workarea constraints.
- Plugin owns this invariant and re-applies it on recalc.
- If compositor/user drift occurs, plugin snaps it back with bounded loop-guarded correction.

### Class B: Managed non-floating clients
- Eligibility: tiled/non-floating, non-popup/transient, non-auxiliary, in managed workspace.
- Visibility is controlled by Emacs buffer visibility through `set-layout`.
- If hidden in Emacs: hidden in compositor (kept alive).
- If visible in Emacs: shown above Emacs and placed to rectangle.

### Class C: Native floating clients
- Always unmanaged by hyprmacs layout placement.
- Must remain compositor-native and rendered above Emacs and managed non-floating clients.
- Floating-to-tiled transitions re-enter normal managed eligibility flow.

### Layering Rule (normative)
`floating windows > managed non-floating windows > managing Emacs window`.

## Data Model and Module Ownership
### Workspace layout state (plugin-owned, workspace-scoped)
- `workspace_id`
- `layout_version` (monotonic counter)
- `rectangles_by_client_id`
- `visible_client_ids`
- `hidden_client_ids`
- `stacking_order`
- `selected_client`
- `input_mode`
- `managing_emacs_client_id`

### Ownership by module
- `WorkspaceManager`:
  - canonical managed membership
  - canonical accepted snapshot state
  - layout versioning
  - managing Emacs resolution
- `IpcServer`:
  - parse/validate `set-layout`
  - route snapshot apply to workspace manager
  - emit `layout-applied` + `state-dump`
  - schedule state notifications (debounced/coalesced)
- `CHyprmacsAlgorithm`:
  - consume latest accepted snapshot
  - enforce Emacs fullscreen invariant
  - place visible managed non-Emacs clients
- `LayoutApplier`:
  - visibility/cleanup only (`hide_client`, `show_client`, native restore)
  - no managed geometry execution

## Control Flow
### On `set-layout`
1. Parse required arrays and fields.
2. Validate one rectangle per visible client and non-overlap.
3. Validate client membership/eligibility constraints.
4. Commit snapshot to workspace layout state and increment `layout_version`.
5. Trigger recalc.
6. Reply `layout-applied` then `state-dump`.

### In `CHyprmacsAlgorithm::recalculate()`
1. Resolve targets for active managed workspace.
2. Pin managing Emacs target to `space()->workArea()`.
3. Apply snapshot rectangles for visible managed non-Emacs targets.
4. Keep hidden managed targets hidden via existing hidden-workspace mechanism.
5. Ignore unmanaged targets and native floating targets.

## Floating and Membership Transitions
### Managed -> floating
- Plugin removes client from managed set.
- Plugin emits debounced/coalesced state notification.
- Emacs waits for next `state-dump` and removes buffer association when client is absent from `managed_clients`.

### Floating -> non-floating
- Client re-enters eligibility evaluation.
- Plugin emits debounced/coalesced state notification.
- Emacs may adopt and place it on next planning cycle.

### Event source note
- Dedicated floating events are not reliable in current nested runtime.
- Floating truth should continue to be refreshed from `j/clients` on reliable lifecycle triggers (open/focus/title/workspace transitions).

## State Notification Contract
### Trigger set
- Manage/unmanage.
- Client open/close/move/workspace change.
- Focus/title/classification changes that affect managed membership/selection.
- Floating/tiled eligibility transitions.
- Monitor/workarea changes affecting managed workspace.

### Debounce/coalesce behavior
- Notifications are coalesced into one publish window.
- Timer model is coalescing and non-resetting (new events merge; they do not indefinitely postpone publish).
- Publish payload is authoritative `state-dump` for managed workspace.

### Plugin setting
- Debounce is configured through plugin option:
  - `plugin:hyprmacs:state_notify_debounce_ms`
- Defaults and validation:
  - default: `30`
  - `0`: immediate publish
  - clamp range: `0..1000`
  - invalid/missing: fallback to default with one warning

## Interactive Repositioning and Snapback
- Repositioning of managed non-Emacs clients is disallowed by managed tiled model.
- Repositioning of managing Emacs window is disallowed while managed.
- Repositioning of native floating clients remains native/allowed.
- If drift still appears for managed targets:
  - detect via expected-vs-actual geometry check
  - schedule snapback (debounced)
  - use loop guards (`layout_version` token, re-entrant suppression, bounded retry/backoff)
  - exclude native floating targets from correction path

## Protocol Compatibility
- Existing `set-layout` payload fields are sufficient.
- Existing `layout-applied` and `state-dump` flow remains valid.
- No mandatory protocol version bump in this change.

## Validation Plan
### Automated
- Plugin tests:
  - snapshot validation and apply routing
  - recalc applies rectangles without floating dispatches
  - Emacs fullscreen invariant holds across recalc/workarea changes
  - transition handling (`managed -> floating`, `floating -> tiled`)
  - debounced state publish behavior
  - snapback loop-guard behavior
- E2E:
  - managed clients remain non-floating through updates
  - floating windows stay unmanaged and top-layer
  - managed client becomes floating and disappears from next `state-dump` `managed_clients`
  - floating client becomes non-floating and can be adopted by next layout cycle

### Manual
- Manage workspace, create splits, and verify managed clients track Emacs rectangles.
- Verify managing Emacs window stays at workspace work area even after attempted drift.
- Toggle client float state and verify membership/association transitions through `state-dump`.

## Acceptance Criteria
- Emacs is the sole planner for managed non-floating client visibility and placement.
- Plugin executes accepted snapshots through tiled algorithm recalc.
- Managing Emacs window is pinned to usable workspace area for entire managed session.
- Floating windows remain native, unmanaged, and above managed/non-floating and Emacs layers.
- Managed/floating transitions are reflected via debounced authoritative `state-dump` updates.
- Unmanage/disconnect restores native behavior and leaves no stale managed geometry state.
