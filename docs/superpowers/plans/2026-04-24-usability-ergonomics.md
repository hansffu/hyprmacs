# Usability Ergonomics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make daily hyprmacs control feel Emacs-native by letting Emacs own managed-buffer display policy, summon, floating/unmanage, and focus-request behavior.

**Architecture:** Emacs owns usability policy and window placement. The plugin remains compositor authority and adds narrow support APIs/events only where Emacs cannot act reliably from current state dumps. Every feature is implemented TDD-style with ERT/plugin tests and nested E2E assertions added before implementation.

**Tech Stack:** Emacs Lisp package modules and ERT tests, C++ Hyprland plugin IPC/workspace tests, existing `just test` and `just e2e` workflows.

---

## Branch And Commit Rules

- Work only on branch `usability-ergonomics`.
- Do not commit anything to `main`.
- Make commits only while on `usability-ergonomics`.
- If Git metadata is read-only in the agent sandbox, leave changes uncommitted and ask the user to commit from their shell.

Before implementation, verify:

```bash
git branch --show-current
```

Expected: `usability-ergonomics`

## File Structure

- Modify `emacs/hyprmacs-buffers.el`: managed-buffer display helpers, duplicate visible-buffer enforcement, configurable replacement buffer.
- Modify `emacs/hyprmacs-session.el`: state-dump diffing for newly managed clients, inbound protocol handlers for new plugin responses/events.
- Modify `emacs/hyprmacs.el`: user commands, auto-sync hook integration, full nested E2E assertions.
- Modify `emacs/hyprmacs-ipc.el`: protocol encode/decode helpers only if new payloads need helpers beyond generic envelopes.
- Modify `tests/emacs/buffer-tests.el`: display policy and duplicate enforcement unit tests.
- Modify `tests/emacs/session-tests.el`: state-dump/new-client/focus-event behavior tests.
- Modify `tests/emacs/protocol-tests.el`: outbound command and inbound event protocol tests.
- Modify `plugin/include/hyprmacs/workspace_manager.hpp`: declarations for float/summon candidate support if state operations belong in the manager.
- Modify `plugin/src/workspace_manager.cpp`: workspace-state operations for floating a managed client, listing summon candidates, and summoning a client.
- Modify `plugin/include/hyprmacs/ipc_server.hpp`: route declarations and focus-request test helper declarations.
- Modify `plugin/src/ipc_server.cpp`: IPC routes for `float-managed-client`, `list-summon-candidates`, `summon-client`, and `client-focus-requested` event forwarding.
- Modify `plugin/tests/workspace_manager_tests.cpp`: manager behavior for candidate filtering and summon/float state changes.
- Modify `plugin/tests/ipc_server_tests.cpp`: route and response/event tests.
- Modify `docs/protocol/ipc-v1.org`: document new messages and payloads.
- Modify `docs/testing/manual-smoke-tests.org`: add manual usability scenarios.
- Modify `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`: record task progress, decisions, and review checkpoints.

## Task 1: Emacs Display Policy And Duplicate Enforcement

**Files:**

- Modify `emacs/hyprmacs-buffers.el`
- Modify `emacs/hyprmacs-session.el`
- Modify `emacs/hyprmacs.el`
- Modify `tests/emacs/buffer-tests.el`
- Modify `tests/emacs/session-tests.el`
- Modify `emacs/hyprmacs.el` E2E function `hyprmacs-run-full-e2e-test`
- Modify `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`

- [ ] **Step 1: Mark Task 1 as in progress in the org plan**

Add a Task 17 section to `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org` if it does not exist yet:

```org
** TODO Task 17: Implement usability ergonomics pass
Purpose:
- Implement Emacs-orchestrated day-to-day control ergonomics from =docs/superpowers/specs/2026-04-24-usability-ergonomics-design.md=.

Steps:
*** NEXT Task 17.1: Emacs display policy and duplicate enforcement
*** TODO Task 17.2: Managed buffer floating/unmanage command
*** TODO Task 17.3: Completing-read summon command
*** TODO Task 17.4: Managed-client focus request handling
*** WAIT Review task 17 with user
- Stop when all usability features pass ERT/plugin tests and nested E2E.
```

- [ ] **Step 2: Write failing ERT tests for display policy**

Add tests to `tests/emacs/buffer-tests.el`:

```elisp
(ert-deftest hyprmacs-display-managed-buffer-uses-selected-window ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1")))
    (unwind-protect
        (progn
          (switch-to-buffer (get-buffer-create "*hyprmacs-display-source*"))
          (hyprmacs-display-managed-buffer buffer "0xaaa" "1" 'new-client)
          (should (eq (window-buffer (selected-window)) buffer)))
      (delete-other-windows)
      (mapc (lambda (name)
              (when-let ((buf (get-buffer name)))
                (kill-buffer buf)))
            '("*hyprmacs-display-source*"))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-display-managed-buffer-selects-existing-visible-window ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left (get-buffer-create "*left*"))
          (set-window-buffer right buffer)
          (select-window left)
          (hyprmacs-display-managed-buffer buffer "0xaaa" "1" 'focus-request)
          (should (eq (selected-window) right)))
      (delete-other-windows)
      (mapc (lambda (name)
              (when-let ((buf (get-buffer name)))
                (kill-buffer buf)))
            '("*left*"))
      (hyprmacs-buffer-reset))))
```

- [ ] **Step 3: Write failing ERT tests for duplicate enforcement**

Add tests to `tests/emacs/buffer-tests.el`:

```elisp
(ert-deftest hyprmacs-enforce-single-visible-buffer-replaces-older-duplicate ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((hyprmacs-duplicate-buffer-replacement-buffer "*scratch*")
         (buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left buffer)
          (set-window-buffer right buffer)
          (select-window right)
          (hyprmacs-enforce-single-visible-managed-buffer buffer)
          (should (eq (window-buffer right) buffer))
          (should (eq (window-buffer left) (get-buffer "*scratch*"))))
      (delete-other-windows)
      (hyprmacs-buffer-reset))))
```

- [ ] **Step 4: Write failing ERT test for newly managed client display**

Add a test to `tests/emacs/session-tests.el`:

```elisp
(ert-deftest hyprmacs-state-dump-displays-newly-managed-client-in-selected-window ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((sync-count 0))
    (cl-letf (((symbol-function 'hyprmacs--schedule-auto-sync-layout)
               (lambda (&rest _) (setq sync-count (1+ sync-count)))))
      (unwind-protect
          (progn
            (switch-to-buffer (get-buffer-create "*before-new-client*"))
            (hyprmacs-session-fake-receive
             "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
            (should (eq (window-buffer (selected-window))
                        (hyprmacs-buffer-for-client "0xaaa")))
            (should (= sync-count 1)))
        (delete-other-windows)
        (when-let ((buf (get-buffer "*before-new-client*")))
          (kill-buffer buf))
        (hyprmacs-buffer-reset)))))
```

- [ ] **Step 5: Add red nested E2E assertions**

In `emacs/hyprmacs.el`, extend `hyprmacs-run-full-e2e-test` after the first managed-client state is observed:

```elisp
(let* ((managed (plist-get hyprmacs-session-state :managed-clients))
       (first-client (car managed))
       (first-buffer (and first-client (hyprmacs-buffer-for-client first-client))))
  (hyprmacs--e2e-assert
   (and first-buffer (eq (window-buffer (selected-window)) first-buffer))
   path
   "newly managed client is displayed in selected Emacs window"))
```

Add a duplicate-display assertion near the managed-buffer window roundtrip section:

```elisp
(let* ((client-id (car (plist-get hyprmacs-session-state :managed-clients)))
       (buffer (hyprmacs-buffer-for-client client-id))
       (left (selected-window))
       (right (split-window-right)))
  (set-window-buffer left buffer)
  (set-window-buffer right buffer)
  (select-window right)
  (hyprmacs-enforce-single-visible-managed-buffer buffer)
  (hyprmacs--e2e-assert
   (and (eq (window-buffer right) buffer)
        (eq (window-buffer left) (get-buffer "*scratch*")))
   path
   "duplicate managed buffer display replaces older window with scratch"))
```

Run:

```bash
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/buffer-tests.el -f ert-run-tests-batch-and-exit
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/session-tests.el -f ert-run-tests-batch-and-exit
just e2e
```

Expected before implementation: ERT fails with undefined functions such as `hyprmacs-display-managed-buffer`; E2E fails on the new display assertion.

- [ ] **Step 6: Implement display customizations and helpers**

In `emacs/hyprmacs-buffers.el`, add:

```elisp
(defcustom hyprmacs-duplicate-buffer-replacement-buffer "*scratch*"
  "Buffer name used when replacing duplicate visible hyprmacs buffers."
  :type 'string
  :group 'hyprmacs)

(defcustom hyprmacs-display-managed-buffer-function #'hyprmacs-display-managed-buffer-default
  "Function used to display a managed hyprmacs buffer.
Called with BUFFER, CLIENT-ID, WORKSPACE-ID, and REASON."
  :type 'function
  :group 'hyprmacs)

(defvar hyprmacs--display-generation 0
  "Monotonic counter used to identify explicit hyprmacs display calls.")

(defvar-local hyprmacs--last-display-generation nil
  "Most recent explicit hyprmacs display generation for this buffer.")

(defun hyprmacs-display-managed-buffer-default (buffer _client-id _workspace-id _reason)
  "Display BUFFER using the default hyprmacs selected-window policy."
  (let ((existing (get-buffer-window buffer t)))
    (if (window-live-p existing)
        (select-window existing)
      (set-window-buffer (selected-window) buffer)
      (selected-window))))

(defun hyprmacs-display-managed-buffer (buffer client-id workspace-id reason)
  "Display managed BUFFER for CLIENT-ID in WORKSPACE-ID for REASON."
  (setq hyprmacs--display-generation (1+ hyprmacs--display-generation))
  (let ((window (funcall hyprmacs-display-managed-buffer-function
                         buffer client-id workspace-id reason)))
    (with-current-buffer buffer
      (setq-local hyprmacs--last-display-generation hyprmacs--display-generation))
    (hyprmacs-enforce-single-visible-managed-buffer buffer window)
    window))

(defun hyprmacs--replacement-buffer ()
  "Return the buffer used to replace duplicate managed-buffer displays."
  (get-buffer-create hyprmacs-duplicate-buffer-replacement-buffer))

(defun hyprmacs-enforce-single-visible-managed-buffer (buffer &optional preferred-window)
  "Ensure managed BUFFER is visible in at most one window."
  (when (and (buffer-live-p buffer)
             (with-current-buffer buffer
               (eq major-mode 'hyprmacs-window-mode)))
    (let* ((windows (get-buffer-window-list buffer nil t))
           (keep (or (and (window-live-p preferred-window) preferred-window)
                     (and (memq (selected-window) windows) (selected-window))
                     (car windows))))
      (dolist (window windows)
        (unless (eq window keep)
          (set-window-buffer window (hyprmacs--replacement-buffer))))
      keep)))

(defun hyprmacs-enforce-visible-managed-buffer-uniqueness (&rest _)
  "Enforce uniqueness for every visible hyprmacs buffer."
  (let ((seen nil))
    (dolist (window (window-list nil 'no-minibuf t))
      (let ((buffer (window-buffer window)))
        (when (and (buffer-live-p buffer)
                   (with-current-buffer buffer
                     (eq major-mode 'hyprmacs-window-mode)))
          (unless (memq buffer seen)
            (push buffer seen)
            (hyprmacs-enforce-single-visible-managed-buffer buffer)))))))
```

- [ ] **Step 7: Wire global uniqueness hook**

In `emacs/hyprmacs.el`, update `hyprmacs-enable-layout-sync`:

```elisp
(add-hook 'window-buffer-change-functions #'hyprmacs-enforce-visible-managed-buffer-uniqueness)
```

Update `hyprmacs-disable-layout-sync`:

```elisp
(remove-hook 'window-buffer-change-functions #'hyprmacs-enforce-visible-managed-buffer-uniqueness)
```

Keep the existing `hyprmacs--auto-sync-layout-buffer-change` hook in place.

- [ ] **Step 8: Wire state-dump new-client detection**

In `emacs/hyprmacs-session.el`, before mutating `:managed-clients` in the `state-dump` branch, capture the old managed list:

```elisp
(let* ((old-managed-clients (or (plist-get old-state :managed-clients) '()))
       (new-managed-clients (or managed-clients '()))
       (new-client-ids (seq-filter
                        (lambda (client-id)
                          (not (member client-id old-managed-clients)))
                        new-managed-clients)))
  ;; existing state update and `hyprmacs-buffer-sync-managed` call first
  (dolist (client-id new-client-ids)
    (when-let ((buffer (hyprmacs-buffer-for-client client-id)))
      (hyprmacs-display-managed-buffer buffer client-id workspace-id 'new-client)))
  (when new-client-ids
    (hyprmacs--schedule-auto-sync-layout)))
```

Add required declarations to avoid load-order warnings:

```elisp
(declare-function hyprmacs-display-managed-buffer "hyprmacs-buffers")
(declare-function hyprmacs--schedule-auto-sync-layout "hyprmacs")
(require 'seq)
```

- [ ] **Step 9: Run targeted tests and E2E**

Run:

```bash
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/buffer-tests.el -f ert-run-tests-batch-and-exit
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/session-tests.el -f ert-run-tests-batch-and-exit
just e2e
```

Expected after implementation: all targeted ERT tests pass and `just e2e` passes.

- [ ] **Step 10: Commit Task 1**

```bash
git branch --show-current
git add emacs/hyprmacs-buffers.el emacs/hyprmacs-session.el emacs/hyprmacs.el tests/emacs/buffer-tests.el tests/emacs/session-tests.el docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org
git commit -m "feat: add managed buffer display policy"
```

Expected branch: `usability-ergonomics`

## Task 2: Managed Buffer Floating/Unmanage Command

**Files:**

- Modify `emacs/hyprmacs.el`
- Modify `emacs/hyprmacs-session.el`
- Modify `tests/emacs/protocol-tests.el`
- Modify `tests/emacs/buffer-tests.el`
- Modify `plugin/include/hyprmacs/workspace_manager.hpp`
- Modify `plugin/src/workspace_manager.cpp`
- Modify `plugin/src/ipc_server.cpp`
- Modify `plugin/tests/workspace_manager_tests.cpp`
- Modify `plugin/tests/ipc_server_tests.cpp`
- Modify `docs/protocol/ipc-v1.org`
- Modify `emacs/hyprmacs.el` E2E function
- Modify `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`

- [ ] **Step 1: Mark Task 17.2 as NEXT in the org plan**

Set Task 17.1 to `DONE` and Task 17.2 to `NEXT`.

- [ ] **Step 2: Add failing Emacs protocol/command tests**

In `tests/emacs/protocol-tests.el`, add:

```elisp
(ert-deftest hyprmacs-session-sends-float-managed-client ()
  (hyprmacs-session-reset)
  (hyprmacs-session-use-fake-transport)
  (hyprmacs-session-float-managed-client "1" "0xabc")
  (let* ((frame (hyprmacs-test--decode (car (hyprmacs-session-fake-outbox))))
         (payload (alist-get 'payload frame)))
    (should (equal (alist-get 'type frame nil nil #'equal) "float-managed-client"))
    (should (equal (alist-get 'workspace_id frame nil nil #'equal) "1"))
    (should (equal (alist-get 'client_id payload nil nil #'equal) "0xabc"))))
```

In `tests/emacs/buffer-tests.el`, add:

```elisp
(ert-deftest hyprmacs-make-buffer-floating-sends-command-and-removes-buffer ()
  (hyprmacs-session-reset)
  (hyprmacs-session-use-fake-transport)
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xabc" "foot" "shell" "1")))
    (unwind-protect
        (progn
          (switch-to-buffer buffer)
          (setq hyprmacs-session-state
                (plist-put hyprmacs-session-state :workspace-id "1"))
          (setq hyprmacs-session-state
                (plist-put hyprmacs-session-state :managed-clients '("0xabc")))
          (hyprmacs-make-buffer-floating)
          (should-not (buffer-live-p buffer))
          (let* ((frame (hyprmacs-test--decode (car (hyprmacs-session-fake-outbox)))))
            (should (equal (alist-get 'type frame nil nil #'equal) "float-managed-client"))))
      (hyprmacs-buffer-reset))))
```

- [ ] **Step 3: Add failing plugin tests**

In `plugin/tests/workspace_manager_tests.cpp`, add a test:

```cpp
bool test_float_managed_client_marks_client_floating_and_unmanaged() {
    hyprmacs::WorkspaceManager manager;
    manager.seed_client("0xaaa", "1", "foot", "foot-a", false);
    manager.manage_workspace("1");

    const bool floated = manager.float_managed_client("1", "0xaaa");
    auto state = manager.build_state_dump("1");

    bool ok = true;
    ok &= expect(floated, "float_managed_client should accept managed client");
    ok &= expect(state.managed_clients.empty(), "floated client should leave managed set");
    ok &= expect(state.eligible_clients.empty(), "floated client should leave eligible set");
    return ok;
}
```

Register it in `main()`.

In `plugin/tests/ipc_server_tests.cpp`, add a route test:

```cpp
bool test_route_float_managed_client_returns_ack_and_state_dump() {
    hyprmacs::WorkspaceManager manager;
    RecordingLayoutApplier applier;
    manager.seed_client("0xaaa", "1", "foot", "foot-a", false);
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage incoming {
        .type = "float-managed-client",
        .workspace_id = "1",
        .payload_json = "{\"client_id\":\"0xaaa\"}",
    };
    auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier);

    bool ok = true;
    ok &= expect(responses.size() == 2, "float-managed-client should return ack and state-dump");
    ok &= expect(responses[0].type == "client-floated", "first response should be client-floated");
    ok &= expect(responses[1].type == "state-dump", "second response should be state-dump");
    ok &= expect(manager.build_state_dump("1").managed_clients.empty(), "client should be unmanaged after route");
    return ok;
}
```

Register it in `main()`.

- [ ] **Step 4: Add red E2E assertion**

In `hyprmacs-run-full-e2e-test`, add a path that selects a managed client, calls `hyprmacs-make-buffer-floating`, then asserts:

```elisp
(hyprmacs--e2e-assert
 (not (buffer-live-p floated-buffer))
 path
 "make-buffer-floating removes managed Emacs buffer")
(hyprmacs-request-state workspace-id)
(hyprmacs--wait-seconds 0.60)
(hyprmacs--e2e-assert
 (not (member floated-client (plist-get hyprmacs-session-state :managed-clients)))
 path
 "make-buffer-floating removes client from managed set")
```

Run:

```bash
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/protocol-tests.el -f ert-run-tests-batch-and-exit
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/buffer-tests.el -f ert-run-tests-batch-and-exit
nix develop -c sh -lc 'cmake --build build/plugin --target hyprmacs_workspace_manager_tests hyprmacs_ipc_server_tests -j8 && ctest --test-dir build/plugin --output-on-failure -R "hyprmacs_(workspace_manager_tests|ipc_server_tests)"'
just e2e
```

Expected before implementation: failures for missing command/route.

- [ ] **Step 5: Implement Emacs command send path**

In `emacs/hyprmacs-session.el`, add:

```elisp
(defun hyprmacs-session-float-managed-client (workspace-id client-id)
  "Send float-managed-client for CLIENT-ID in WORKSPACE-ID."
  (hyprmacs-session-send
   "float-managed-client"
   workspace-id
   `((client_id . ,client-id))))
```

In `emacs/hyprmacs.el`, add:

```elisp
(defun hyprmacs--read-managed-client-id ()
  "Prompt for a currently managed client ID."
  (let ((managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
    (completing-read "Managed client: " managed nil t)))

(defun hyprmacs-make-buffer-floating (&optional prompt)
  "Make current managed buffer's client native floating.
With PROMPT, ask for a managed client."
  (interactive "P")
  (let* ((workspace-id (hyprmacs--default-workspace-id))
         (client-id (if prompt
                        (hyprmacs--read-managed-client-id)
                      (or (hyprmacs--current-managed-buffer-client-id)
                          (user-error "hyprmacs: current buffer is not managed"))))
         (buffer (hyprmacs-buffer-for-client client-id)))
    (hyprmacs-session-float-managed-client workspace-id client-id)
    (when (buffer-live-p buffer)
      (hyprmacs-buffer-remove-client client-id))
    (hyprmacs--schedule-auto-sync-layout)
    (message "hyprmacs: requested floating native client %s" client-id)))
```

- [ ] **Step 6: Implement plugin manager operation**

In `plugin/include/hyprmacs/workspace_manager.hpp`, add:

```cpp
bool float_managed_client(const WorkspaceId& workspace_id, const ClientId& client_id);
```

In `plugin/src/workspace_manager.cpp`, implement:

```cpp
bool WorkspaceManager::float_managed_client(const WorkspaceId& workspace_id, const ClientId& client_id) {
    std::lock_guard lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }
    const auto normalized = ClientRegistry::normalize_client_id(client_id);
    const ClientRecord* before = client_registry_.find(normalized);
    if (before == nullptr || !before->managed) {
        return false;
    }
    note_overlay_float_request(workspace_id, normalized);
    client_registry_.set_floating(normalized, true);
    client_registry_.reconcile_management(managed_workspace_id_);
    sync_committed_layout_snapshot_locked();
    return true;
}
```

- [ ] **Step 7: Implement plugin route**

In `plugin/src/ipc_server.cpp`, add route handling for `float-managed-client` near other command routes:

```cpp
if (incoming.type == "float-managed-client") {
    const auto client_id = parse_string_field_from_payload(incoming.payload_json, "client_id");
    if (!client_id.has_value()) {
        out.push_back(make_message("protocol-error", incoming.workspace_id,
            "{\"message\":\"float-managed-client missing client_id\"}"));
        return out;
    }
    const bool ok = workspace_manager.float_managed_client(incoming.workspace_id, *client_id);
    out.push_back(make_message("client-floated", incoming.workspace_id,
        std::string("{\"client_id\":\"") + escape_json(*client_id) + "\",\"ok\":" + (ok ? "true" : "false") + "}"));
    out.push_back(make_message("state-dump", incoming.workspace_id,
        serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))));
    return out;
}
```

Use the existing `escape_json` helper already defined in `plugin/src/ipc_server.cpp`.

- [ ] **Step 8: Document protocol**

In `docs/protocol/ipc-v1.org`, add `float-managed-client` and `client-floated` sections with payloads:

```json
{"client_id":"0xabc123"}
```

```json
{"client_id":"0xabc123","ok":true}
```

- [ ] **Step 9: Run targeted tests and E2E**

Run the commands from Step 4 again.

Expected after implementation: all targeted tests and `just e2e` pass.

- [ ] **Step 10: Commit Task 2**

```bash
git branch --show-current
git add emacs/hyprmacs.el emacs/hyprmacs-session.el tests/emacs/protocol-tests.el tests/emacs/buffer-tests.el plugin/include/hyprmacs/workspace_manager.hpp plugin/src/workspace_manager.cpp plugin/src/ipc_server.cpp plugin/tests/workspace_manager_tests.cpp plugin/tests/ipc_server_tests.cpp docs/protocol/ipc-v1.org docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org
git commit -m "feat: float managed clients from Emacs"
```

## Task 3: Completing-Read Summon Command

**Files:**

- Modify `emacs/hyprmacs.el`
- Modify `emacs/hyprmacs-session.el`
- Modify `tests/emacs/protocol-tests.el`
- Modify `plugin/include/hyprmacs/workspace_manager.hpp`
- Modify `plugin/src/workspace_manager.cpp`
- Modify `plugin/src/ipc_server.cpp`
- Modify `plugin/tests/workspace_manager_tests.cpp`
- Modify `plugin/tests/ipc_server_tests.cpp`
- Modify `docs/protocol/ipc-v1.org`
- Modify `emacs/hyprmacs.el` E2E function
- Modify `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`

- [ ] **Step 1: Mark Task 17.3 as NEXT in the org plan**

Set Task 17.2 to `DONE` and Task 17.3 to `NEXT`.

- [ ] **Step 2: Add failing Emacs protocol tests**

In `tests/emacs/protocol-tests.el`, add:

```elisp
(ert-deftest hyprmacs-session-sends-summon-candidate-requests ()
  (hyprmacs-session-reset)
  (hyprmacs-session-use-fake-transport)
  (hyprmacs-session-list-summon-candidates "1")
  (hyprmacs-session-summon-client "1" "0xabc")
  (let* ((frames (hyprmacs-session-fake-outbox))
         (first (hyprmacs-test--decode (nth 0 frames)))
         (second (hyprmacs-test--decode (nth 1 frames))))
    (should (equal (alist-get 'type first nil nil #'equal) "list-summon-candidates"))
    (should (equal (alist-get 'type second nil nil #'equal) "summon-client"))
    (should (equal (alist-get 'client_id (alist-get 'payload second) nil nil #'equal) "0xabc"))))
```

Add a command-level test:

```elisp
(ert-deftest hyprmacs-summon-client-uses-completing-read-choice ()
  (hyprmacs-session-reset)
  (hyprmacs-session-use-fake-transport)
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :summon-candidates
                   '(((client_id . "0xabc") (workspace_id . "2") (app_id . "foot") (title . "remote shell")))))
  (cl-letf (((symbol-function 'completing-read)
             (lambda (_prompt choices &rest _)
               (caar choices))))
    (hyprmacs-summon-client "1"))
  (let* ((frame (hyprmacs-test--decode (car (hyprmacs-session-fake-outbox)))))
    (should (equal (alist-get 'type frame nil nil #'equal) "summon-client"))
    (should (equal (alist-get 'client_id (alist-get 'payload frame) nil nil #'equal) "0xabc"))))
```

- [ ] **Step 3: Add failing plugin tests**

In `plugin/tests/workspace_manager_tests.cpp`, add:

```cpp
bool test_summon_candidates_include_only_other_workspace_tiled_eligible_clients() {
    hyprmacs::WorkspaceManager manager;
    manager.seed_client("0xaaa", "1", "foot", "local", false);
    manager.seed_client("0xbbb", "2", "foot", "remote", false);
    manager.seed_client("0xccc", "3", "foot", "floating", true);
    manager.seed_client("0xeee", "2", "emacs", "emacs", false);
    manager.manage_workspace("1");

    auto candidates = manager.summon_candidates("1");
    bool ok = true;
    ok &= expect(candidates.size() == 1, "only one summon candidate expected");
    if (!candidates.empty()) {
        ok &= expect(candidates[0].client_id == "0xbbb", "remote tiled foot should be candidate");
        ok &= expect(candidates[0].workspace_id == "2", "candidate workspace should be preserved");
    }
    return ok;
}

bool test_summon_client_moves_and_adopts_candidate() {
    std::vector<std::string> commands;
    hyprmacs::WorkspaceManager manager(
        [&commands](const std::string& command) {
            commands.push_back(command);
            return 0;
        });
    manager.seed_client("0xaaa", "1", "foot", "local", false);
    manager.seed_client("0xbbb", "2", "foot", "remote", false);
    manager.manage_workspace("1");

    const bool summoned = manager.summon_client("1", "0xbbb");
    auto state = manager.build_state_dump("1");

    bool ok = true;
    ok &= expect(summoned, "summon_client should accept remote eligible tiled client");
    ok &= expect(std::find(state.managed_clients.begin(), state.managed_clients.end(), "0xbbb") != state.managed_clients.end(),
                 "summoned client should be managed in target workspace");
    ok &= expect(!commands.empty(), "summon should dispatch workspace move");
    return ok;
}
```

Register tests in `main()`.

In `plugin/tests/ipc_server_tests.cpp`, add route tests for `list-summon-candidates` and `summon-client`.

- [ ] **Step 4: Add red E2E assertion**

In `hyprmacs-run-full-e2e-test`:

- Spawn a new `foot` on workspace 2 using `hyprctl dispatch workspace 2`, `hyprctl dispatch exec foot`, then return to workspace 1.
- Run `hyprmacs-summon-client` with a stubbed `completing-read` selecting that client's label.
- Assert the client appears in `:managed-clients`.
- Assert its buffer is displayed in the selected Emacs window.

Use this shape:

```elisp
(let ((summon-choice nil))
  ;; set summon-choice from candidate labels after list response
  (cl-letf (((symbol-function 'completing-read)
             (lambda (_prompt choices &rest _)
               (or summon-choice (caar choices)))))
    (hyprmacs-summon-client workspace-id)))
```

Run `just e2e`; expected before implementation: failure on summon command/API.

- [ ] **Step 5: Implement Emacs session helpers and inbound candidates**

In `emacs/hyprmacs-session.el`, add state field `:summon-candidates nil` to `hyprmacs-session-reset`.

Add send helpers:

```elisp
(defun hyprmacs-session-list-summon-candidates (workspace-id)
  "Request summon candidates for WORKSPACE-ID."
  (hyprmacs-session-send "list-summon-candidates" workspace-id '()))

(defun hyprmacs-session-summon-client (workspace-id client-id)
  "Request summon of CLIENT-ID into WORKSPACE-ID."
  (hyprmacs-session-send "summon-client" workspace-id `((client_id . ,client-id))))
```

Handle inbound `summon-candidates`:

```elisp
("summon-candidates"
 (setq hyprmacs-session-state
       (plist-put hyprmacs-session-state :summon-candidates
                  (alist-get 'candidates payload nil nil #'equal))))
```

- [ ] **Step 6: Implement Emacs command**

In `emacs/hyprmacs.el`, add:

```elisp
(defun hyprmacs--summon-candidate-label (candidate)
  "Return completion label for summon CANDIDATE."
  (format "%s [%s] - %s"
          (or (alist-get 'app_id candidate nil nil #'equal) "unknown")
          (or (alist-get 'workspace_id candidate nil nil #'equal) "?")
          (or (alist-get 'title candidate nil nil #'equal) "untitled")))

(defun hyprmacs-summon-client (&optional workspace-id)
  "Summon an eligible tiled client from another workspace."
  (interactive)
  (let* ((workspace-id (or workspace-id (hyprmacs--default-workspace-id))))
    (hyprmacs-session-list-summon-candidates workspace-id)
    (hyprmacs--wait-seconds 0.20)
    (let* ((candidates (or (plist-get hyprmacs-session-state :summon-candidates) '()))
           (choices (mapcar (lambda (candidate)
                              (cons (hyprmacs--summon-candidate-label candidate) candidate))
                            candidates))
           (label (completing-read "Summon client: " choices nil t))
           (candidate (cdr (assoc label choices)))
           (client-id (alist-get 'client_id candidate nil nil #'equal)))
      (unless client-id
        (user-error "hyprmacs: no summon candidate selected"))
      (hyprmacs-session-summon-client workspace-id client-id)
      (message "hyprmacs: requested summon for %s" client-id))))
```

- [ ] **Step 7: Implement plugin manager summon support**

Add a struct to `plugin/include/hyprmacs/workspace_manager.hpp`:

```cpp
struct SummonCandidate {
    ClientId client_id;
    WorkspaceId workspace_id;
    std::string app_id;
    std::string title;
};
```

Add declarations:

```cpp
std::vector<SummonCandidate> summon_candidates(const WorkspaceId& target_workspace_id) const;
bool summon_client(const WorkspaceId& target_workspace_id, const ClientId& client_id);
```

Implement by filtering registry clients:

- workspace differs from target
- not floating
- eligible
- not Emacs/auxiliary by current classifier result

For `summon_client`, dispatch:

```text
dispatch movetoworkspacesilent <target>,address:<client>
```

Then update registry workspace, reconcile management, sync snapshot, and return true.

- [ ] **Step 8: Implement plugin IPC routes**

In `plugin/src/ipc_server.cpp`:

- `list-summon-candidates` returns `summon-candidates` with `candidates` array.
- `summon-client` returns `client-summoned` ack plus `state-dump`.

Payload examples:

```json
{"candidates":[{"client_id":"0xbbb","workspace_id":"2","app_id":"foot","title":"remote"}]}
```

```json
{"client_id":"0xbbb","ok":true}
```

- [ ] **Step 9: Document protocol**

Update `docs/protocol/ipc-v1.org` with `list-summon-candidates`, `summon-candidates`, `summon-client`, and `client-summoned`.

- [ ] **Step 10: Run targeted tests and E2E**

Run:

```bash
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/protocol-tests.el -f ert-run-tests-batch-and-exit
nix develop -c sh -lc 'cmake --build build/plugin --target hyprmacs_workspace_manager_tests hyprmacs_ipc_server_tests -j8 && ctest --test-dir build/plugin --output-on-failure -R "hyprmacs_(workspace_manager_tests|ipc_server_tests)"'
just e2e
```

Expected after implementation: targeted tests and `just e2e` pass.

- [ ] **Step 11: Commit Task 3**

```bash
git branch --show-current
git add emacs/hyprmacs.el emacs/hyprmacs-session.el tests/emacs/protocol-tests.el plugin/include/hyprmacs/workspace_manager.hpp plugin/src/workspace_manager.cpp plugin/src/ipc_server.cpp plugin/tests/workspace_manager_tests.cpp plugin/tests/ipc_server_tests.cpp docs/protocol/ipc-v1.org docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org
git commit -m "feat: summon clients from other workspaces"
```

## Task 4: Managed-Client Focus Request Handling

**Files:**

- Modify `emacs/hyprmacs-buffers.el`
- Modify `emacs/hyprmacs-session.el`
- Modify `tests/emacs/session-tests.el`
- Modify `plugin/src/workspace_manager.cpp`
- Modify `plugin/src/ipc_server.cpp`
- Modify `plugin/tests/ipc_server_tests.cpp`
- Modify `docs/protocol/ipc-v1.org`
- Modify `emacs/hyprmacs.el` E2E function
- Modify `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`

- [ ] **Step 1: Mark Task 17.4 as NEXT in the org plan**

Set Task 17.3 to `DONE` and Task 17.4 to `NEXT`.

- [ ] **Step 2: Add failing Emacs focus-request tests**

In `tests/emacs/session-tests.el`, add:

```elisp
(ert-deftest hyprmacs-focus-request-selects-visible-buffer-window ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left (get-buffer-create "*left-focus*"))
          (set-window-buffer right buffer)
          (select-window left)
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"client-focus-requested\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"client_id\":\"0xaaa\"}}\n")
          (should (eq (selected-window) right)))
      (delete-other-windows)
      (when-let ((buf (get-buffer "*left-focus*")))
        (kill-buffer buf))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-focus-request-displays-hidden-buffer-in-selected-window ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1")))
    (unwind-protect
        (progn
          (switch-to-buffer (get-buffer-create "*focus-source*"))
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"client-focus-requested\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"client_id\":\"0xaaa\"}}\n")
          (should (eq (window-buffer (selected-window)) buffer)))
      (delete-other-windows)
      (when-let ((buf (get-buffer "*focus-source*")))
        (kill-buffer buf))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-focus-request-uses-custom-handler ()
  (hyprmacs-session-reset)
  (let (seen)
    (let ((hyprmacs-focus-request-function
           (lambda (client-id workspace-id payload)
             (setq seen (list client-id workspace-id payload)))))
      (hyprmacs-session-fake-receive
       "{\"version\":1,\"type\":\"client-focus-requested\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"client_id\":\"0xaaa\",\"reason\":\"urgent\"}}\n"))
    (should (equal (car seen) "0xaaa"))
    (should (equal (cadr seen) "1"))))
```

- [ ] **Step 3: Add failing plugin event test**

In `plugin/include/hyprmacs/ipc_server.hpp`, add a narrow test helper declaration:

```cpp
ProtocolMessage focus_request_message_for_tests(const WorkspaceId& workspace_id, const ClientId& client_id);
```

In `plugin/src/ipc_server.cpp`, implement it using the same message builder used by the live server:

```cpp
ProtocolMessage focus_request_message_for_tests(const WorkspaceId& workspace_id, const ClientId& client_id) {
    return make_message(
        "client-focus-requested",
        workspace_id,
        std::string("{\"client_id\":\"") + escape_json(client_id) + "\"}"
    );
}
```

In `plugin/tests/ipc_server_tests.cpp`, add:

```cpp
bool test_focus_request_message_for_tests_builds_client_focus_requested_event() {
    const auto message = hyprmacs::focus_request_message_for_tests("1", "0xaaa");
    bool ok = true;
    ok &= expect(message.type == "client-focus-requested", "focus request event type mismatch");
    ok &= expect(message.workspace_id == "1", "focus request workspace mismatch");
    ok &= expect(message.payload_json.find("\"client_id\":\"0xaaa\"") != std::string::npos,
                 "focus request payload should include client_id");
    return ok;
}
```

Expected response:

```json
{"client_id":"0xaaa"}
```

- [ ] **Step 4: Add red E2E focus-request assertion**

In E2E, trigger the production focus-request path through Hyprland focus:

- Put target managed buffer in one Emacs window and another buffer in the selected window.
- Run `hyprctl dispatch focuswindow address:<target-client>`.
- Wait for the plugin event tap and IPC frame to reach Emacs.
- Assert the visible target window is selected.
- Then hide target buffer from Emacs windows, run `hyprctl dispatch focuswindow address:<target-client>` again, and assert the selected Emacs window now displays target buffer.

- [ ] **Step 5: Implement Emacs configurable handler**

In `emacs/hyprmacs-buffers.el`, add:

```elisp
(defcustom hyprmacs-focus-request-function #'hyprmacs-focus-request-default
  "Function called when a managed client requests focus.
Called with CLIENT-ID, WORKSPACE-ID, and PAYLOAD."
  :type 'function
  :group 'hyprmacs)

(defun hyprmacs-focus-request-default (client-id workspace-id _payload)
  "Default focus request behavior for CLIENT-ID in WORKSPACE-ID."
  (let ((buffer (hyprmacs-buffer-for-client client-id)))
    (if (buffer-live-p buffer)
        (hyprmacs-display-managed-buffer buffer client-id workspace-id 'focus-request)
      (hyprmacs-session-request-state workspace-id))))
```

Add declarations:

```elisp
(declare-function hyprmacs-session-request-state "hyprmacs-session")
```

In `emacs/hyprmacs-session.el`, handle inbound event:

```elisp
("client-focus-requested"
 (let ((client-id (alist-get 'client_id payload nil nil #'equal)))
   (if (and client-id (functionp hyprmacs-focus-request-function))
       (funcall hyprmacs-focus-request-function client-id workspace-id payload)
     (hyprmacs-session-request-state workspace-id))))
```

- [ ] **Step 6: Implement plugin event emission**

In `WorkspaceManager::handle_line`, when `activewindowv2` indicates a managed client requested/received focus outside Emacs's current selected state, call a new focus-request notifier instead of directly deciding Emacs behavior.

Add to `WorkspaceManager`:

```cpp
using FocusRequestNotifier = std::function<void(const WorkspaceId&, const ClientId&)>;
void set_focus_request_notifier(FocusRequestNotifier notifier);
```

In `IpcServer`, publish:

```cpp
send_message(fd, make_message("client-focus-requested", workspace_id,
    std::string("{\"client_id\":\"") + escape_json(client_id) + "\"}"));
```

Avoid sending this event for Emacs clients, floating clients, or unmanaged clients.

- [ ] **Step 7: Document protocol**

Update `docs/protocol/ipc-v1.org` with `client-focus-requested`:

```json
{"client_id":"0xabc123"}
```

- [ ] **Step 8: Run targeted tests and E2E**

Run:

```bash
emacs -Q --batch -L emacs -L tests/emacs -l tests/emacs/session-tests.el -f ert-run-tests-batch-and-exit
nix develop -c sh -lc 'cmake --build build/plugin --target hyprmacs_ipc_server_tests hyprmacs_workspace_manager_tests -j8 && ctest --test-dir build/plugin --output-on-failure -R "hyprmacs_(ipc_server_tests|workspace_manager_tests)"'
just e2e
```

Expected after implementation: targeted tests and `just e2e` pass.

- [ ] **Step 9: Commit Task 4**

```bash
git branch --show-current
git add emacs/hyprmacs-buffers.el emacs/hyprmacs-session.el tests/emacs/session-tests.el plugin/include/hyprmacs/workspace_manager.hpp plugin/src/workspace_manager.cpp plugin/src/ipc_server.cpp plugin/tests/ipc_server_tests.cpp docs/protocol/ipc-v1.org docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org
git commit -m "feat: handle managed client focus requests"
```

## Task 5: Full Usability Documentation And Final Review Gate

**Files:**

- Modify `README.org`
- Modify `docs/testing/demo-script.org`
- Modify `docs/testing/manual-smoke-tests.org`
- Modify `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`

- [ ] **Step 1: Mark Task 17 docs/final validation as NEXT**

In the org plan, set Task 17.4 to `DONE` and add/mark:

```org
*** NEXT Task 17.5: Documentation and final validation
```

- [ ] **Step 2: Update user docs**

Update `README.org` controls section with:

```org
- Make current managed buffer native floating:
  - ~M-x hyprmacs-make-buffer-floating~
- Summon an eligible tiled client from another workspace:
  - ~M-x hyprmacs-summon-client~
```

Add a short note that managed buffers are displayed in one Emacs window at a time.

- [ ] **Step 3: Update demo script**

In `docs/testing/demo-script.org`, add steps for:

- opening a new client and observing it appears in the active Emacs window
- displaying the same managed buffer in another Emacs split and seeing the old display replaced by `*scratch*`
- summoning a client from workspace 2
- making a managed buffer floating

- [ ] **Step 4: Update manual smoke tests**

In `docs/testing/manual-smoke-tests.org`, add a `Usability Ergonomics` section with the same scenarios and expected outcomes.

- [ ] **Step 5: Run full verification**

Run:

```bash
just test
just e2e
```

Expected: both pass.

- [ ] **Step 6: Mark WAIT checkpoint**

In the org plan:

```org
*** WAIT Review task 17 with user
- Stop when display policy, duplicate enforcement, floating/unmanage, summon, and focus-request behavior pass automated and nested E2E validation.
```

Stop here and ask the user to review. Do not continue past this WAIT until the user approves.

- [ ] **Step 7: Commit Task 5**

```bash
git branch --show-current
git add README.org docs/testing/demo-script.org docs/testing/manual-smoke-tests.org docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org
git commit -m "docs: document usability ergonomics workflows"
```

## Self-Review Checklist

- Spec coverage:
  - New-client active-window display: Task 1.
  - One visible Emacs window per `hyprmacs` buffer: Task 1.
  - Make managed buffer floating/unmanaged: Task 2.
  - Completing-read summon from other workspaces: Task 3.
  - Configurable focus request behavior: Task 4.
  - TDD plus E2E per feature: every task starts with red tests/E2E and ends with green verification.

- Type/name consistency:
  - Emacs command names: `hyprmacs-make-buffer-floating`, `hyprmacs-summon-client`.
  - Emacs customization names: `hyprmacs-display-managed-buffer-function`, `hyprmacs-focus-request-function`, `hyprmacs-duplicate-buffer-replacement-buffer`.
  - Plugin message names: `float-managed-client`, `client-floated`, `list-summon-candidates`, `summon-candidates`, `summon-client`, `client-summoned`, `client-focus-requested`.

- Review gates:
  - Stop at the Task 17 WAIT checkpoint.
  - Do not commit to `main`.
