# Hyprmacs Usability Ergonomics Design

Date: 2026-04-24

## Goal

Improve daily-use ergonomics now that the backend prototype is mostly working. This pass focuses on Emacs-side window and buffer policy while keeping Hyprland as compositor/input authority.

## Scope

This design covers five must-have usability features:

- Display newly managed clients in the currently active Emacs window.
- Enforce that each `hyprmacs` buffer is visible in at most one Emacs window.
- Add a command that makes a managed buffer floating, effectively unmanaging its client.
- Add a completing-read summon command for eligible tiled clients on other workspaces.
- Handle managed-client focus requests with configurable Emacs buffer switching behavior.

Out of scope for this pass:

- Managing floating clients through summon.
- Coercing dialogs, Emacs auxiliary windows, or other excluded clients into the managed set.
- Moving the primary UX policy into the plugin.
- Multi-controller or multi-managed-workspace semantics beyond existing prototype constraints.

## Architecture Boundary

Use an Emacs-orchestrated design.

Emacs owns day-to-day usability policy:

- Which Emacs window displays a managed buffer.
- How newly managed clients are surfaced.
- The one-window-per-managed-buffer invariant.
- The summon UI.
- Focus-request handling and integration with user workspace packages.

The plugin remains compositor authority and exposes only supporting APIs/events where Emacs cannot act reliably by itself:

- `float-managed-client`: make a currently managed client native/floating and remove it from managed membership.
- `list-summon-candidates`: return eligible tiled clients from other workspaces.
- `summon-client`: move/adopt a summon candidate into the managed workspace.
- `client-focus-requested`: notify Emacs that a managed client requested focus.

The plugin must not decide Emacs window placement.

Summon candidate payloads must include `client_id`, `workspace_id`, `app_id`, and `title` so Emacs can present stable completion labels without querying Hyprland directly.

## Display Policy

When a client becomes newly managed, Emacs creates or updates the corresponding `hyprmacs` buffer and displays it in the currently active Emacs window.

If the active Emacs window already displays another `hyprmacs` buffer, the new buffer replaces it. The previous client becomes hidden by the next layout sync because its buffer is no longer visible.

Each `hyprmacs` buffer may be visible in at most one Emacs window. This is a global invariant, not limited to hyprmacs commands. It applies to ordinary Emacs display paths, including `switch-to-buffer`, `display-buffer`, and workspace/package restore flows.

If a `hyprmacs` buffer becomes visible in a second window, hyprmacs keeps the newest display and replaces the older duplicate window's buffer with `*scratch*`, then schedules layout sync. For explicit hyprmacs display calls, the newest display is the window chosen by `hyprmacs-display-managed-buffer-function`. For ordinary Emacs display hooks, the newest display is the currently selected window when it shows the duplicate buffer; otherwise hyprmacs keeps the first visible window returned by `get-buffer-window-list` and clears the rest.

## Focus Request Policy

The plugin emits `client-focus-requested` when a managed client requests focus.

Emacs handles focus requests through `hyprmacs-focus-request-function`.

Default behavior:

- If the target `hyprmacs` buffer is already visible, select the Emacs window showing it.
- Otherwise, display the target buffer in the currently active Emacs window.

This default mirrors the new-client display policy. Users can replace the function to integrate with workspace packages.

If a focus request targets an unknown client, Emacs requests a fresh state dump and ignores the event.

## User Commands

### `hyprmacs-make-buffer-floating`

Acts on the current managed buffer by default.

Behavior:

- Resolve the current buffer's `hyprmacs-client-id`.
- Send `float-managed-client` to the plugin.
- On success, remove/kill the corresponding `hyprmacs` buffer immediately.
- The Hyprland client becomes a native floating window.
- The next state dump is authoritative and confirms the client is no longer managed.

With a prefix argument, the command prompts for one of the currently managed clients and acts on that client instead of the current buffer.

Failure behavior:

- If the plugin rejects the command, keep or restore the buffer and report the error.
- Do not mutate Emacs window layout on failure.

### `hyprmacs-summon-client`

Uses `completing-read`.

Behavior:

- Request summon candidates from the plugin.
- Show eligible tiled clients from other workspaces as completion candidates.
- Send the selected client ID to the plugin through `summon-client`.
- The plugin moves/adopts the client into the managed workspace.
- Emacs handles the following state dump as a newly managed client and displays it in the active Emacs window.

Summon candidates exclude:

- Floating clients.
- Emacs and Emacs auxiliary windows.
- Dialogs, popups, and other clients excluded by existing classifier rules.
- Clients already managed in the current workspace.

Failure behavior:

- If candidate listing fails, report the error and leave layout untouched.
- If summon fails, report the error and leave layout untouched.

## New-Client Detection

Emacs detects newly managed clients by diffing `managed_clients` from each inbound `state-dump` against the previous session state.

For each newly added client, Emacs:

- Ensures the managed buffer exists.
- Displays the buffer in the active Emacs window using the configured display function.
- Schedules layout sync after the display update.

If multiple clients become newly managed in one state dump, Emacs processes them in payload order. The last processed client remains in the active window.

## Configuration

### `hyprmacs-display-managed-buffer-function`

Function used to display a managed buffer.

Called with exactly four arguments:

- buffer
- client ID
- workspace ID
- reason symbol, such as `new-client`, `focus-request`, or `summon`

Default behavior:

- If the buffer is visible, select its window.
- Otherwise, display it in the selected Emacs window.

### `hyprmacs-focus-request-function`

Function used to handle `client-focus-requested`.

Called with exactly three arguments:

- client ID
- workspace ID
- payload alist from the focus-request event

Default behavior finds the managed buffer for the client ID, then delegates to `hyprmacs-display-managed-buffer-function` with reason `focus-request`.

### `hyprmacs-duplicate-buffer-replacement-buffer`

Buffer used when replacing an older duplicate display of a `hyprmacs` buffer.

Default: `*scratch*`.

## Testing Requirements

This pass must be implemented in TDD style.

For each feature:

- Add or extend the failing ERT tests first.
- Add or extend failing plugin tests first when plugin API behavior changes.
- Add the relevant failing nested E2E assertion before implementation.
- Implement the smallest change that makes the feature pass.
- Restore targeted tests and the nested E2E path before moving to the next feature.

Expected ERT coverage:

- New-client display in the active Emacs window.
- Replacement of an existing managed buffer in the active window.
- Duplicate `hyprmacs` buffer enforcement for ordinary display paths.
- Configurable display function behavior.
- `hyprmacs-make-buffer-floating` success and failure behavior.
- `hyprmacs-summon-client` completing-read flow.
- Configurable focus-request handling.

Expected plugin coverage:

- `float-managed-client` route removes the client from managed membership and makes it native/floating.
- `list-summon-candidates` returns only eligible tiled clients from other workspaces.
- `summon-client` moves/adopts only valid candidates.
- `client-focus-requested` event emission is routed to the controller.

Expected nested E2E coverage:

- Newly managed client appears in the active Emacs window.
- Displaying the same managed buffer in a second Emacs window replaces the older display with `*scratch*`.
- Making a managed buffer floating removes the Emacs buffer and leaves the client native.
- Summon pulls an eligible tiled client from another workspace and displays it through the normal new-client path.
- Focus request selects an already visible managed buffer, or displays it in the active window when not visible.

## Acceptance Criteria

- Day-to-day window placement behavior is controlled from Emacs.
- Plugin additions are narrow support APIs/events, not Emacs-window policy.
- The one-window-per-managed-buffer invariant survives ordinary Emacs display commands and workspace restore flows.
- User configuration can route focus-request display through workspace packages.
- Each feature lands with ERT/plugin coverage and updated nested E2E coverage before the next feature begins.
