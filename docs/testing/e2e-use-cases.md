# E2E Use Cases

This document summarizes the behavior explicitly covered by the nested E2E harness in `tests/e2e/full-flow.el` and its required assertion manifest in `tests/e2e/required-assertions.txt`.

Each use case has a stable index-based ID so it can be referenced in issues, reviews, and plan updates.

## UC-001: Connect Emacs To Hyprmacs

*Given* a running hyprmacs instance
*When* the user connects Emacs to the plugin IPC socket
*Then* the Emacs session reports a connected state

Covered by: `session-connected`

## UC-002: Manage The Active Workspace

*Given* a running hyprmacs instance
*When* the user manages the active workspace
*Then* the workspace is marked managed, the controller is connected, and the active workspace layout switches to `hyprmacs`

Covered by: `workspace-marked-managed`, `controller-connected-true`, `active-workspace-layout-switched-to-hyprmacs`

## UC-003: Apply Managed Workspace Policy

*Given* a managed hyprmacs workspace
*When* the workspace enters managed mode
*Then* compositor settings that conflict with hyprmacs are disabled for the managed session

Covered by: `animations-enabled-forced-to-0-while-managed`, `misc-focus-on-activate-forced-to-0-while-managed`

## UC-004: Adopt Existing Floating Apps On Manage

*Given* a running hyprmacs instance with a non-Emacs app window already open
*When* the app is made floating before the workspace is managed and the user manages the workspace
*Then* the floating app is adopted into the managed set and gets a managed Emacs buffer

Covered by: `pre-manage-non-emacs-client-selected-for-floating-adoption-check`, `pre-manage-togglefloating-succeeded-for-value`, `pre-manage-client-is-floating-before-manage-workspace`, `pre-manage-floating-client-is-adopted-by-explicit-manage-workspace`, `pre-manage-floating-client-gets-a-managed-buffer-after-manage-workspace`

## UC-005: Keep Explicit Membership On Unfloat

*Given* a running hyprmacs instance with a managed app window
*When* Hyprland receives a togglefloating/unfloat request for that managed app
*Then* the app remains managed instead of being implicitly unmanaged

Covered by: `pre-manage-managed-client-togglefloating-request-succeeded`, `pre-manage-managed-client-stays-managed-after-unfloat-attempt`

## UC-006: Close A Transition Probe Window

*Given* a running hyprmacs instance with a managed transition probe app
*When* the user closes that app through Hyprland
*Then* the app closes and disappears before later layering assertions run

Covered by: `pre-manage-transition-probe-client-close-succeeded`, `pre-manage-transition-probe-client-is-closed-before-layering-assertions`

## UC-007: Identify The Managing Emacs Frame

*Given* a running hyprmacs instance
*When* the workspace is managed from Emacs
*Then* hyprmacs records the active Emacs frame as the frame managing the workspace

Covered by: `manage-captured-emacs-as-active-class`

## UC-008: Allow Secondary Emacs Frames To Float

*Given* a running hyprmacs instance with a managing Emacs frame
*When* the user opens a secondary Emacs frame and toggles that secondary frame floating
*Then* the secondary Emacs frame is allowed to become floating and can be toggled back

Covered by: `multiple-emacs-frames-observed-in-compositor`, `secondary-emacs-frame-address-discovered`, `secondary-emacs-frame-togglefloating-request-succeeds`, `secondary-emacs-frame-is-allowed-to-become-floating`, `secondary-emacs-frame-togglefloating-back-request-succeeds`

## UC-009: Repair The Managing Emacs Frame To Tiled

*Given* a running hyprmacs instance with a managing Emacs frame
*When* the user sends togglefloating to the managing Emacs frame and then returns to Emacs-control mode
*Then* the managing Emacs frame is repaired back to tiled

Covered by: `managing-emacs-frame-togglefloating-request-succeeds`, `emacs-control-dispatcher-succeeds-after-managing-frame-togglefloating`, `managing-emacs-frame-is-repaired-to-tiled-after-togglefloating`

## UC-010: Display Newly Opened Apps After Manage

*Given* a running hyprmacs instance with an already managed workspace
*When* the user opens a new app window
*Then* the new app window is adopted into the managed set, gets a managed Emacs buffer, and is visible in the active Emacs window, replacing the previous buffer

Covered by: `spawn-post-manage-client-succeeded`, `post-manage-opened-client-is-adopted-into-managed-set`, `post-manage-opened-client-gets-a-managed-buffer`, `post-manage-opened-client-is-displayed-after-adoption`

## UC-011: Track Duplicate Window Titles By Client ID

*Given* a running hyprmacs instance with multiple app windows that share the same title
*When* hyprmacs tracks those app windows
*Then* each app is tracked as a distinct managed client ID

Covered by: `spawn-duplicate-title-client-number-succeeded`, `multiple-clients-with-same-title-are-tracked-as-distinct-ids`

## UC-012: Maintain A Multi-Client Managed Set

*Given* a running hyprmacs instance
*When* the workspace has multiple managed app windows
*Then* hyprmacs discovers at least two managed clients for layout and input-mode behavior

Covered by: `at-least-two-managed-clients-discovered`

## UC-013: Render Split Emacs Windows As Visible Managed Apps

*Given* a running hyprmacs instance with two managed app buffers
*When* the user displays both managed buffers in an Emacs split
*Then* both buffers exist and the unfocused split client remains visible in client-control mode

Covered by: `split-view-client-control-buffers-exist`, `split-view-unfocused-client-remains-visible-in-client-control`

## UC-014: Click The Unfocused Split Client In Client-Control Mode

*Given* a running hyprmacs instance with two visible managed apps in an Emacs split
*When* the user enters client-control mode and clicks the unfocused visible app
*Then* the unfocused app is above the Emacs frame and receives the click

Covered by: `split-view-unfocused-client-center-available-for-client-control-layering`, `movecursor-succeeded-for-split-view-client-control-layering`, `mouse-click-succeeded-for-split-view-client-control-layering`, `split-view-unfocused-client-is-above-emacs-and-receives-click-in-client-control`

## UC-015: Switch To Client-Control Mode

*Given* a running hyprmacs instance with a selected managed app
*When* the user switches to client-control mode
*Then* the session input mode changes to `client-control`

Covered by: `input-mode-switched-to-client-control`

## UC-016: Publish Layout And Observe State Feedback

*Given* a running hyprmacs instance with managed app buffers visible in Emacs
*When* Emacs publishes a layout snapshot
*Then* hyprmacs processes the layout and Emacs observes a `state-dump` response

Covered by: `state-dump-observed-after-layout-publication`

## UC-017: Show The App For A Visible Managed Buffer

*Given* a running hyprmacs instance with a managed app buffer
*When* the user shows that managed buffer in an Emacs window
*Then* the corresponding app window is visible on the managed workspace

Covered by: `managed-buffer-exists-for-target-client`, `managed-client-is-visible-when-managed-buffer-is-shown`

## UC-018: Match Single-Window Geometry

*Given* a running hyprmacs instance with one visible managed app buffer
*When* Emacs publishes the single-window layout
*Then* the managed app rectangle matches the Emacs body rectangle within the accepted tolerance

Covered by: `single-window-client-rectangle-is-available-for-geometry-assertion`, `single-window-managed-client-rectangle-matches-emacs-body-rectangle`

## UC-019: Click A Managed App While Emacs Is Focused

*Given* a running hyprmacs instance with Emacs focused and a visible managed app
*When* the user clicks the visible managed app
*Then* the managed app is above Emacs and receives focus from the click

Covered by: `dispatcher-hyprmacs-set-emacs-control-mode-succeeded-for-layering-assertion`, `managed-layering-assertion-starts-with-emacs-focused`, `target-client-center-available-for-layering-click-assertion`, `movecursor-succeeded-for-layering-assertion`, `mouse-click-dispatch-succeeded-for-layering-assertion`, `managed-client-is-on-top-and-receives-click-immediately-while-emacs-focused`, `managed-client-receives-click-while-emacs-focused-layer-ordering`

## UC-020: Hide Apps Whose Managed Buffer Is No Longer Visible

*Given* a running hyprmacs instance with a visible managed app buffer
*When* the user switches the Emacs window to a non-managed buffer
*Then* the managed app is hidden from the active workspace

Covered by: `managed-client-is-hidden-when-managed-buffer-window-is-closed`

## UC-021: Restore Hidden Apps When Their Buffer Reopens

*Given* a running hyprmacs instance with a hidden managed app
*When* the user displays that managed app buffer again
*Then* the app returns to the active workspace and becomes visible

Covered by: `managed-client-is-restored-visible-after-reopening-managed-buffer`

## UC-022: Kill Managed Buffers By Closing The Client

*Given* a running hyprmacs instance with a managed app buffer
*When* the user kills the managed buffer
*Then* the buffer kill is intercepted, a close request is sent to the app, and the app is removed from the managed set

Covered by: `managed-buffer-kill-is-intercepted-while-close-request-is-sent`, `killing-managed-buffer-closes-and-removes-target-client`, `managed-buffer-stays-removed-after-kill-command`

## UC-023: Keep Managed Apps Floating After Togglefloating

*Given* a running hyprmacs instance with a managed app window
*When* the user sends togglefloating to that managed app
*Then* the app remains managed and floating for hyprmacs overlay rendering

Covered by: `transition-client-buffer-exists-before-floating-transition-assertion`, `togglefloating-request-for-managed-client-succeeded-for-value`, `managed-client-remains-managed-and-floating-after-togglefloating`

## UC-024: Click A Floating Managed App While Emacs Is Focused

*Given* a running hyprmacs instance with Emacs focused and a floating managed app visible
*When* the user clicks the floating managed app
*Then* the floating app is above Emacs and receives the click

Covered by: `dispatcher-hyprmacs-set-emacs-control-mode-succeeded-for-floating-layering-assertion`, `floating-layering-assertion-starts-with-emacs-focused`, `floating-transition-client-center-available-for-layering-click-assertion`, `movecursor-succeeded-for-floating-layering-assertion`, `mouse-click-dispatch-succeeded-for-floating-layering-assertion`, `floating-client-is-on-top-and-receives-click-immediately-while-emacs-focused`

## UC-025: Repair Managed Apps After Repeated Togglefloating

*Given* a running hyprmacs instance with a managed app window
*When* the user sends togglefloating to that app a second time
*Then* the app still remains managed and floating

Covered by: `second-togglefloating-request-for-managed-client-succeeded-for-value`, `managed-client-remains-managed-and-floating-after-second-togglefloating`

## UC-026: Remove Closed Clients From Managed State

*Given* a running hyprmacs instance with a managed app window selected for closing
*When* the user closes the app window through Hyprland
*Then* the close command succeeds and the app is removed from the managed set

Covered by: `close-target-client-selected`, `closewindow-command-succeeded-for-value`, `closed-client-removed-from-managed-set`

## UC-027: Return To Emacs-Control Mode

*Given* a running hyprmacs instance in client-control mode
*When* the user dispatches Emacs-control mode
*Then* the input mode changes back to `emacs-control` and focus returns to the managing Emacs frame

Covered by: `dispatcher-hyprmacs-set-emacs-control-mode-succeeds`, `input-mode-switched-back-to-emacs-control`, `emacs-control-focuses-the-managing-emacs-frame`

## UC-028: Unmanage The Workspace Cleanly

*Given* a running hyprmacs instance with a managed workspace
*When* the user unmanages the workspace
*Then* the active workspace leaves the `hyprmacs` layout, restores the previous layout value, and clears managed session state

Covered by: `workspace-layout-no-longer-hyprmacs-after-unmanage`, `workspace-layout-restored-to-pre-manage-value`, `workspace-unmanaged-state-cleared`
