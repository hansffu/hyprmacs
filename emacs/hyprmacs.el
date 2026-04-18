;;; hyprmacs.el --- Hyprland workspace integration bootstrap  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Entry point for hyprmacs user commands.

;;; Code:

(require 'hyprmacs-session)
(require 'hyprmacs-layout)
(require 'json)
(require 'subr-x)

(defconst hyprmacs-version "0.1.0"
  "Current hyprmacs bootstrap version.")

(defvar hyprmacs-layout-sync-enabled nil
  "Whether automatic window-layout sync hooks are enabled.")

(defvar hyprmacs--layout-sync-in-flight nil
  "Guard flag to prevent recursive automatic layout sync.")

(defvar hyprmacs--layout-sync-timer nil
  "Debounce timer used for automatic layout sync.")

(defvar hyprmacs-layout-sync-initial-delay 0.25
  "Seconds to delay initial auto layout sync after manage-workspace.")

(defvar hyprmacs-layout-sync-debounce 0.03
  "Seconds to debounce automatic layout sync hooks.")

(defvar hyprmacs-layout-debug-log-file nil
  "Optional file path for appending set-layout debug snapshots.")

(defun hyprmacs--detect-hyprland-instance-signature ()
  "Detect a Hyprland instance signature from runtime directories."
  (let* ((uid (string-trim (shell-command-to-string "id -u")))
         (cmd (format "ls -1dt /run/user/%s/hypr/* 2>/dev/null | head -n 1 | xargs -r basename" uid))
         (candidate (string-trim (shell-command-to-string cmd))))
    (unless (string-empty-p candidate)
      candidate)))

(defun hyprmacs--ensure-hyprland-instance-signature ()
  "Ensure HYPRLAND_INSTANCE_SIGNATURE is set when possible."
  (or (getenv "HYPRLAND_INSTANCE_SIGNATURE")
      (let ((detected (hyprmacs--detect-hyprland-instance-signature)))
        (when detected
          (setenv "HYPRLAND_INSTANCE_SIGNATURE" detected))
        detected)))

(defun hyprmacs--active-workspace-id ()
  "Return active Hyprland workspace id as string, or nil."
  (condition-case nil
      (progn
        (hyprmacs--ensure-hyprland-instance-signature)
        (let* ((raw (shell-command-to-string "hyprctl -j activeworkspace"))
             (obj (json-parse-string raw :object-type 'alist))
             (id (alist-get 'id obj nil nil #'equal)))
          (when id
            (format "%s" id))))
    (error nil)))

(defun hyprmacs--default-workspace-id ()
  "Return preferred workspace id for interactive commands."
  (or (hyprmacs--active-workspace-id)
      (plist-get hyprmacs-session-state :workspace-id)
      "1"))

(defun hyprmacs--ensure-connected-for-manage ()
  "Ensure hyprmacs session is connected before manage-workspace commands."
  (if (eq (plist-get hyprmacs-session-state :connection-status) 'connected)
      t
    (hyprmacs-connect)
    (eq (plist-get hyprmacs-session-state :connection-status) 'connected)))

(defun hyprmacs--json-bool (value)
  "Normalize JSON VALUE to a strict Emacs boolean."
  (cond
   ((or (eq value :json-false)
        (eq value :false)
        (eq value 'false))
    nil)
   ((eq value nil) nil)
   ((numberp value) (/= value 0))
   (t (not (null value)))))

(defun hyprmacs-connect (&optional socket-path)
  "Connect to hyprmacs plugin IPC at SOCKET-PATH."  
  (interactive)
  (hyprmacs--ensure-hyprland-instance-signature)
  (condition-case err
      (progn
        (hyprmacs-session-connect socket-path)
        (message "hyprmacs: connected to plugin IPC"))
    (error
     (message "hyprmacs: connect failed: %s" (error-message-string err)))))

(defun hyprmacs-disconnect ()
  "Disconnect from hyprmacs session state."  
  (interactive)
  (hyprmacs-session-disconnect)
  (message "hyprmacs: disconnected"))

(defun hyprmacs-manage-current-workspace (&optional workspace-id)
  "Mark WORKSPACE-ID as managed in session state."  
  (interactive)
  (let ((workspace-id (or workspace-id (hyprmacs--default-workspace-id))))
    (unless (hyprmacs--ensure-connected-for-manage)
      (user-error "hyprmacs: unable to connect to plugin IPC"))
  (hyprmacs-session-manage-workspace workspace-id)
  (hyprmacs--seed-existing-workspace-clients workspace-id)
  (hyprmacs-request-state workspace-id)
  (hyprmacs-enable-layout-sync)
  (unless noninteractive
    (run-at-time
     hyprmacs-layout-sync-initial-delay
     nil
     (lambda (ws)
       (hyprmacs-sync-layout ws t))
     workspace-id))
  (message "hyprmacs: requested manage-workspace for %s" workspace-id)))

(defun hyprmacs-unmanage-workspace (&optional workspace-id)
  "Mark WORKSPACE-ID as unmanaged in session state."  
  (interactive)
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (hyprmacs-session-unmanage-workspace workspace-id "user-request")
  (message "hyprmacs: requested unmanage-workspace for %s" workspace-id))

(defun hyprmacs-request-state (&optional workspace-id)
  "Request state dump for WORKSPACE-ID."  
  (interactive)
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (hyprmacs-session-request-state workspace-id)
  (message "hyprmacs: requested state for %s" workspace-id))

(defun hyprmacs--send-input-mode (mode workspace-id)
  "Send input MODE for WORKSPACE-ID."
  (hyprmacs-session-set-input-mode workspace-id mode)
  (message "hyprmacs: requested input mode %s for %s" mode workspace-id))

(defun hyprmacs--resolve-input-mode-args (arg1 arg2 default-mode)
  "Resolve workspace and mode from ARG1/ARG2, defaulting MODE to DEFAULT-MODE.
Accepted forms:
- (MODE WORKSPACE-ID)
- (WORKSPACE-ID MODE)
- (WORKSPACE-ID)
- nil (interactive default)."
  (let (workspace-id mode)
    (cond
     ((and (symbolp arg1) (or (null arg2) (stringp arg2)))
      (setq mode arg1
            workspace-id arg2))
     ((and (stringp arg1) (or (null arg2) (symbolp arg2)))
      (setq workspace-id arg1
            mode arg2))
     ((null arg1)
      (setq mode default-mode))
     (t
      (user-error "hyprmacs: invalid input-mode arguments")))
    (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
    (setq mode (or mode default-mode))
    (unless (memq mode '(emacs-control client-control))
      (user-error "hyprmacs: unsupported input mode %s" mode))
    (cons workspace-id mode)))

(defun hyprmacs--current-managed-buffer-client-id ()
  "Return managed client ID for current buffer when available."
  (let ((client-id (and (boundp 'hyprmacs-client-id) hyprmacs-client-id))
        (managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
    (when (and (stringp client-id)
               (member client-id managed))
      client-id)))

(defun hyprmacs-set-input-mode (&optional arg1 arg2)
  "Switch to client-control mode for the active workspace.
Non-interactively accepts flexible forms:
- (MODE WORKSPACE-ID)
- (WORKSPACE-ID MODE)
- (WORKSPACE-ID)"
  (interactive)
  (let* ((resolved (hyprmacs--resolve-input-mode-args arg1 arg2 'client-control))
         (workspace-id (car resolved))
         (mode (cdr resolved)))
    (when (eq mode 'client-control)
      (let ((target-client (hyprmacs--current-managed-buffer-client-id)))
        (when (and target-client
                   (not (equal target-client (plist-get hyprmacs-session-state :selected-client))))
          (hyprmacs-session-set-selected-client workspace-id target-client))))
    (hyprmacs--send-input-mode mode workspace-id)))

(defun hyprmacs-set-emacs-control-mode (&optional workspace-id)
  "Switch to emacs-control mode for WORKSPACE-ID (or active workspace)."
  (interactive)
  (hyprmacs--send-input-mode 'emacs-control
                             (or workspace-id (hyprmacs--default-workspace-id))))

(defun hyprmacs-select-managed-client (client-id &optional workspace-id)
  "Select managed CLIENT-ID in WORKSPACE-ID and make it visible."
  (interactive
   (let ((managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
     (list (completing-read "Managed client: " managed nil t))))
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (hyprmacs-session-set-selected-client workspace-id client-id)
  (message "hyprmacs: requested selected managed client %s" client-id))

(defun hyprmacs-sync-layout (&optional workspace-id silent)
  "Publish a set-layout snapshot for WORKSPACE-ID from current Emacs windows."
  (interactive)
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (condition-case err
      (let* ((managed-clients (or (plist-get hyprmacs-session-state :managed-clients) '()))
             (selected-client (plist-get hyprmacs-session-state :selected-client))
             (input-mode (or (plist-get hyprmacs-session-state :input-mode) 'emacs-control))
             (payload (hyprmacs-layout-build-payload managed-clients selected-client input-mode)))
        (when (and hyprmacs-layout-debug-log-file
                   (not (string-empty-p hyprmacs-layout-debug-log-file)))
          (with-temp-buffer
            (insert (format "ts=%s ws=%s payload=%S\n"
                            (format-time-string "%Y-%m-%d %H:%M:%S" (current-time))
                            workspace-id
                            payload))
            (append-to-file (point-min) (point-max) hyprmacs-layout-debug-log-file)))
        (hyprmacs-session-set-layout workspace-id payload)
        (unless silent
          (message "hyprmacs: published layout snapshot for %s (%d visible)"
                   workspace-id
                   (length (alist-get 'visible_clients payload nil nil #'equal)))))
    (error
     (unless silent
       (message "hyprmacs: sync-layout failed: %s" (error-message-string err))))))

(defun hyprmacs--layout-sync-eligible-p ()
  "Return non-nil when automatic layout sync should run."
  (and hyprmacs-layout-sync-enabled
       (not hyprmacs--layout-sync-in-flight)
       (eq (plist-get hyprmacs-session-state :connection-status) 'connected)
       (plist-get hyprmacs-session-state :managed)))

(defun hyprmacs--run-auto-sync-layout ()
  "Perform one automatic layout sync pass."
  (setq hyprmacs--layout-sync-timer nil)
  (when (and hyprmacs-layout-sync-enabled
             (not hyprmacs--layout-sync-in-flight)
             (eq (plist-get hyprmacs-session-state :connection-status) 'connected)
             (plist-get hyprmacs-session-state :managed))
    (let ((workspace-id (or (plist-get hyprmacs-session-state :workspace-id)
                            (hyprmacs--default-workspace-id))))
      (when workspace-id
        (let ((hyprmacs--layout-sync-in-flight t))
          (hyprmacs-sync-layout workspace-id t))))))

(defun hyprmacs--schedule-auto-sync-layout (&rest _)
  "Schedule a debounced automatic layout sync."
  (when (hyprmacs--layout-sync-eligible-p)
    (when (timerp hyprmacs--layout-sync-timer)
      (cancel-timer hyprmacs--layout-sync-timer))
    (setq hyprmacs--layout-sync-timer
          (run-at-time hyprmacs-layout-sync-debounce nil #'hyprmacs--run-auto-sync-layout))))

(defun hyprmacs--auto-sync-layout (&rest _)
  "Compatibility wrapper that schedules automatic layout sync."
  (hyprmacs--schedule-auto-sync-layout))

(defun hyprmacs--auto-sync-layout-size-change (&rest _)
  "Schedule automatic layout sync for frame size changes."
  (hyprmacs--schedule-auto-sync-layout))

(defun hyprmacs--auto-sync-layout-state-change (&rest _)
  "Schedule automatic layout sync for generic window state changes."
  (hyprmacs--schedule-auto-sync-layout))

(defun hyprmacs--auto-sync-layout-buffer-change (&rest _)
  "Schedule automatic layout sync for buffer changes in visible windows."
  (hyprmacs--schedule-auto-sync-layout))

(defun hyprmacs-enable-layout-sync ()
  "Enable automatic set-layout snapshots from Emacs window changes."
  (interactive)
  (unless hyprmacs-layout-sync-enabled
    (add-hook 'window-configuration-change-hook #'hyprmacs--schedule-auto-sync-layout)
    (add-hook 'window-size-change-functions #'hyprmacs--auto-sync-layout-size-change)
    (add-hook 'window-state-change-functions #'hyprmacs--auto-sync-layout-state-change)
    (add-hook 'window-buffer-change-functions #'hyprmacs--auto-sync-layout-buffer-change)
    (setq hyprmacs-layout-sync-enabled t))
  (message "hyprmacs: automatic layout sync enabled"))

(defun hyprmacs-disable-layout-sync ()
  "Disable automatic set-layout snapshots from Emacs window changes."
  (interactive)
  (remove-hook 'window-configuration-change-hook #'hyprmacs--schedule-auto-sync-layout)
  (remove-hook 'window-size-change-functions #'hyprmacs--auto-sync-layout-size-change)
  (remove-hook 'window-state-change-functions #'hyprmacs--auto-sync-layout-state-change)
  (remove-hook 'window-buffer-change-functions #'hyprmacs--auto-sync-layout-buffer-change)
  (when (timerp hyprmacs--layout-sync-timer)
    (cancel-timer hyprmacs--layout-sync-timer))
  (setq hyprmacs--layout-sync-timer nil)
  (setq hyprmacs-layout-sync-enabled nil)
  (message "hyprmacs: automatic layout sync disabled"))

(defun hyprmacs--seed-existing-workspace-clients (workspace-id)
  "Seed currently existing Hyprland clients for WORKSPACE-ID into plugin state."
  (condition-case err
      (progn
        (hyprmacs--ensure-hyprland-instance-signature)
        (let* ((raw (shell-command-to-string "hyprctl -j clients"))
             (clients (json-parse-string raw :array-type 'list :object-type 'alist)))
          (dolist (client clients)
            (let* ((address (alist-get 'address client nil nil #'equal))
                   (app-id (or (alist-get 'class client nil nil #'equal) "unknown"))
                   (title (or (alist-get 'title client nil nil #'equal) "untitled"))
                   (floating (hyprmacs--json-bool (alist-get 'floating client nil nil #'equal)))
                   (workspace (alist-get 'workspace client nil nil #'equal))
                   (workspace-id-value (alist-get 'id workspace nil nil #'equal))
                   (workspace-id-string (format "%s" workspace-id-value)))
              (when (and address (equal workspace-id-string workspace-id))
                (hyprmacs-session-seed-client workspace-id address app-id title floating))))))
    (error
     (message "hyprmacs: seed existing clients failed: %s" (error-message-string err)))))

(defun hyprmacs--wait-seconds (seconds)
  "Allow async IPC filters to run for SECONDS."
  (let ((deadline (+ (float-time) seconds)))
    (while (< (float-time) deadline)
      (accept-process-output nil 0.05))))

(defun hyprmacs--format-state-lines ()
  "Return current session state as newline-terminated text."
  (concat
   (format "connection-status: %s\n" (plist-get hyprmacs-session-state :connection-status))
   (format "workspace-id: %s\n" (or (plist-get hyprmacs-session-state :workspace-id) ""))
   (format "managed: %s\n" (plist-get hyprmacs-session-state :managed))
   (format "controller-connected: %s\n" (plist-get hyprmacs-session-state :controller-connected))
   (format "selected-client: %s\n" (or (plist-get hyprmacs-session-state :selected-client) ""))
   (format "input-mode: %s\n" (or (plist-get hyprmacs-session-state :input-mode) ""))
   (format "known-clients: %s\n" (plist-get hyprmacs-session-state :known-clients))
   (format "managed-clients: %s\n" (plist-get hyprmacs-session-state :managed-clients))
   (format "associated-buffers: %s\n" (plist-get hyprmacs-session-state :associated-buffers))
   (format "last-message-type: %s\n" (or (plist-get hyprmacs-session-state :last-message-type) ""))))

(defun hyprmacs-run-smoke-test (&optional log-path)
  "Run automated hyprmacs smoke flow and write debug output to LOG-PATH.
The flow mirrors manual validation: connect, manage, request state, select client,
switch modes, and collect state plus `hyprctl clients` output."
  (interactive)
  (let* ((path (or log-path "logs.txt"))
         (workspace-id (hyprmacs--default-workspace-id)))
    (hyprmacs--ensure-hyprland-instance-signature)
    (with-temp-file path
      (insert ""))
    (hyprmacs-connect)
    (hyprmacs--wait-seconds 0.25)
    (hyprmacs-manage-current-workspace workspace-id)
    (hyprmacs--wait-seconds 0.40)
    (hyprmacs-request-state workspace-id)
    (hyprmacs--wait-seconds 0.40)
    (append-to-file "initial-state:\n" nil path)
    (append-to-file (hyprmacs--format-state-lines) nil path)
    (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

    (let ((managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
      (when (< (length managed) 2)
        (append-to-file "\nspawn-second-client-attempt:\n" nil path)
        (hyprmacs--append-command-output path "hyprctl dispatch exec foot" "hyprctl dispatch exec foot")
        (hyprmacs--wait-seconds 0.80)
        (hyprmacs--seed-existing-workspace-clients workspace-id)
        (hyprmacs--wait-seconds 0.20)
        (hyprmacs-request-state workspace-id)
        (hyprmacs--wait-seconds 0.40)
        (setq managed (or (plist-get hyprmacs-session-state :managed-clients) '()))
        (append-to-file "after-spawn-state:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow"))

      (append-to-file (format "\nmanaged-client-count: %d\n" (length managed)) nil path)
      (when managed
        (let ((target-client (if (> (length managed) 1)
                                 (nth 1 managed)
                               (car managed))))
          (append-to-file (format "selection-target: %s\n" target-client) nil path)
          (hyprmacs-select-managed-client target-client workspace-id))
        (hyprmacs--wait-seconds 0.25)
        (hyprmacs-set-input-mode 'client-control workspace-id)
        (hyprmacs--wait-seconds 0.20)
        (hyprmacs-request-state workspace-id)
        (hyprmacs--wait-seconds 0.25)
        (append-to-file "\nafter-client-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

        (hyprmacs-set-input-mode 'emacs-control workspace-id)
        (hyprmacs--wait-seconds 0.20)
        (hyprmacs-request-state workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-emacs-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)))

    (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")
    (hyprmacs--append-command-output path "hyprctl clients" "hyprctl clients")
    (message "hyprmacs: smoke test complete, wrote %s" path)))

(defun hyprmacs--append-command-output (path title command)
  "Append COMMAND output with TITLE heading to PATH."
  (hyprmacs--ensure-hyprland-instance-signature)
  (pcase-let ((`(:exit ,exit :out ,out) (hyprmacs--run-command command)))
    (append-to-file (format "\n%s (exit=%s)\n" title exit) nil path)
    (append-to-file out nil path)))

(defun hyprmacs--run-command (command)
  "Run shell COMMAND and return plist with :exit and :out."
  (hyprmacs--ensure-hyprland-instance-signature)
  (with-temp-buffer
    (let ((exit-code (call-process-shell-command command nil (current-buffer) t)))
      (list :exit exit-code :out (buffer-string)))))

(defun hyprmacs--hyprctl-json (request)
  "Return decoded JSON for `hyprctl -j REQUEST`, or nil on parse failure."
  (condition-case nil
      (json-parse-string
       (shell-command-to-string (format "hyprctl -j %s" request))
       :object-type 'alist
       :array-type 'list)
    (error nil)))

(defun hyprmacs--hyprctl-clients ()
  "Return `hyprctl -j clients` as alist list."
  (or (hyprmacs--hyprctl-json "clients") '()))

(defun hyprmacs--hyprctl-activewindow ()
  "Return `hyprctl -j activewindow` as an alist."
  (hyprmacs--hyprctl-json "activewindow"))

(defun hyprmacs--find-client-record (client-id)
  "Return decoded client record for CLIENT-ID, if present."
  (let ((target (downcase client-id)))
    (cl-find-if
     (lambda (entry)
       (string= (downcase (format "%s" (alist-get 'address entry nil nil #'equal))) target))
     (hyprmacs--hyprctl-clients))))

(defun hyprmacs--client-workspace-name (client-id)
  "Return workspace name for CLIENT-ID, or nil if not found."
  (let* ((record (hyprmacs--find-client-record client-id))
         (workspace (alist-get 'workspace record nil nil #'equal))
         (name (alist-get 'name workspace nil nil #'equal)))
    (when name
      (format "%s" name))))

(defun hyprmacs--wait-until (predicate timeout-seconds &optional interval-seconds)
  "Poll PREDICATE until true or TIMEOUT-SECONDS elapses."
  (let* ((deadline (+ (float-time) timeout-seconds))
         (sleep-seconds (or interval-seconds 0.10))
         (ok nil))
    (while (and (not ok) (< (float-time) deadline))
      (setq ok (ignore-errors (funcall predicate)))
      (unless ok
        (accept-process-output nil sleep-seconds)))
    ok))

(defun hyprmacs--hyprctl-option-int (option)
  "Return integer value from `hyprctl getoption` for OPTION.
Returns nil when the value cannot be parsed."
  (hyprmacs--ensure-hyprland-instance-signature)
  (let ((out (shell-command-to-string (format "hyprctl getoption %s" option))))
    (when (string-match "int:[[:space:]]*\\(-?[0-9]+\\)" out)
      (string-to-number (match-string 1 out)))))

(defun hyprmacs--activeworkspace-layout ()
  "Return the active workspace tiled layout name."
  (hyprmacs--ensure-hyprland-instance-signature)
  (condition-case nil
      (let* ((obj (json-parse-string
                   (shell-command-to-string "hyprctl -j activeworkspace")
                   :object-type 'alist))
             (layout (or (alist-get 'tiledLayout obj nil nil #'equal)
                         (alist-get 'tiledlayout obj nil nil #'equal))))
        (when layout
          (format "%s" layout)))
    (error nil)))

(defun hyprmacs--e2e-assert (condition path fmt &rest args)
  "Assert CONDITION, write status to PATH, and raise on failure.
FMT and ARGS describe the assertion."
  (let ((message (apply #'format fmt args)))
    (append-to-file
     (format "%s: %s\n" (if condition "ok" "fail") message)
     nil path)
    (unless condition
      (error "hyprmacs e2e assertion failed: %s" message))))

(defun hyprmacs-run-full-e2e-test (&optional log-path)
  "Run full scripted nested E2E validation and write log to LOG-PATH.
This covers the implemented runtime contract through Task 11."
  (interactive)
  (let* ((path (or log-path "logs-e2e.txt"))
         (workspace-id (hyprmacs--default-workspace-id))
         (layout-before (or (hyprmacs--activeworkspace-layout) "")))
    (hyprmacs--ensure-hyprland-instance-signature)
    (with-temp-file path
      (insert "hyprmacs nested e2e\n"))
    (append-to-file (format "workspace-id: %s\n" workspace-id) nil path)
    (append-to-file (format "layout-before: %s\n" layout-before) nil path)

    (hyprmacs-connect)
    (hyprmacs--wait-seconds 0.25)
    (hyprmacs--e2e-assert
     (eq (plist-get hyprmacs-session-state :connection-status) 'connected)
     path "session connected")

    ;; Name the primary frame explicitly and spawn duplicate-title clients.
    (set-frame-name "hyprmacs-e2e-main")
    (dotimes (idx 2)
      (pcase-let ((`(:exit ,exit :out ,out)
                   (hyprmacs--run-command "hyprctl dispatch exec \"foot -T hyprmacs-dup\"")))
        (append-to-file (format "spawn-dup-%d-exit: %s\n" idx exit) nil path)
        (append-to-file out nil path)
        (hyprmacs--e2e-assert (zerop exit) path "spawn duplicate-title client %d succeeded" idx)))
    (hyprmacs--wait-seconds 1.0)

    (hyprmacs-manage-current-workspace workspace-id)
    (hyprmacs--wait-seconds 0.80)
    (hyprmacs-request-state workspace-id)
    (hyprmacs--wait-seconds 0.50)
    (hyprmacs--e2e-assert (plist-get hyprmacs-session-state :managed) path "workspace marked managed")
    (hyprmacs--e2e-assert (plist-get hyprmacs-session-state :controller-connected) path "controller connected true")
    (hyprmacs--e2e-assert
     (equal (hyprmacs--activeworkspace-layout) "hyprmacs")
     path "active workspace layout switched to hyprmacs")
    (hyprmacs--e2e-assert
     (= (or (hyprmacs--hyprctl-option-int "animations:enabled") -1) 0)
     path "animations:enabled forced to 0 while managed")
    (hyprmacs--e2e-assert
     (= (or (hyprmacs--hyprctl-option-int "misc:focus_on_activate") -1) 0)
     path "misc:focus_on_activate forced to 0 while managed")

    (let* ((active (hyprmacs--hyprctl-activewindow))
           (managing-emacs-address (format "%s" (alist-get 'address active nil nil #'equal)))
           (managing-class (format "%s" (alist-get 'class active nil nil #'equal))))
      (hyprmacs--e2e-assert (string= managing-class "emacs") path "manage captured emacs as active class")
      (append-to-file (format "managing-emacs-address: %s\n" managing-emacs-address) nil path)

      ;; Multi-frame scenario.
      (ignore-errors (make-frame '((name . "hyprmacs-e2e-secondary"))))
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (>= (length (cl-remove-if-not
                       (lambda (entry)
                         (string= (format "%s" (alist-get 'class entry nil nil #'equal)) "emacs"))
                       (hyprmacs--hyprctl-clients)))
              2))
        5.0 0.15)
       path "multiple emacs frames observed in compositor")

      (cl-labels
          ((refresh-state ()
             (hyprmacs-request-state workspace-id)
             (hyprmacs--wait-seconds 0.25))
           (managed-ids ()
             (or (plist-get hyprmacs-session-state :managed-clients) '()))
           (eligible-clients ()
             (or (plist-get hyprmacs-session-state :eligible-clients) '()))
           (wait-managed-membership (client-id expected)
             (hyprmacs--wait-until
              (lambda ()
                (refresh-state)
                (if expected
                    (member client-id (managed-ids))
                  (not (member client-id (managed-ids)))))
              5.0 0.20)))
        (let ((managed (managed-ids)))
      (when (< (length managed) 1)
        (hyprmacs--seed-existing-workspace-clients workspace-id)
        (hyprmacs--wait-seconds 0.25)
        (refresh-state)
        (setq managed (managed-ids)))
          (hyprmacs--e2e-assert (>= (length managed) 2) path "at least two managed clients discovered")

          ;; Duplicate-title scenario (same title, different client IDs).
          (let ((title-map (make-hash-table :test #'equal))
                (has-duplicates nil))
            (dolist (entry (eligible-clients))
              (let* ((title (format "%s" (alist-get 'title entry nil nil #'equal)))
                     (client-id (format "%s" (alist-get 'client_id entry nil nil #'equal)))
                     (existing (gethash title title-map)))
                (puthash title (cons client-id existing) title-map)))
            (maphash
             (lambda (_title ids)
               (when (>= (length (delete-dups ids)) 2)
                 (setq has-duplicates t)))
             title-map)
            (hyprmacs--e2e-assert has-duplicates
                                  path "multiple clients with same title are tracked as distinct ids"))

          (let* ((target-client (car managed))
                 (close-client (cadr managed)))
        (append-to-file (format "target-client: %s\n" target-client) nil path)
        (append-to-file (format "close-client: %s\n" close-client) nil path)
        (hyprmacs-select-managed-client target-client workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (hyprmacs-set-input-mode 'client-control workspace-id)
        (hyprmacs--wait-seconds 0.25)
            (refresh-state)
        (hyprmacs--e2e-assert
         (eq (plist-get hyprmacs-session-state :input-mode) 'client-control)
         path "input mode switched to client-control")

        (hyprmacs-sync-layout workspace-id t)
        (hyprmacs--wait-seconds 0.35)
        (hyprmacs--e2e-assert
         (equal (plist-get hyprmacs-session-state :last-message-type) "state-dump")
         path "state-dump observed after layout publication")

            ;; Buffer visibility lifecycle + command kill/recreate scenario.
            (let ((buffer (hyprmacs-buffer-for-client target-client)))
              (hyprmacs--e2e-assert (buffer-live-p buffer) path "managed buffer exists for target client")
              (delete-other-windows)
              (switch-to-buffer buffer)
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.25)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((workspace-name (hyprmacs--client-workspace-name target-client)))
                    (and workspace-name
                         (not (string= workspace-name "special:hyprmacs-hidden")))))
                5.0 0.20)
               path "managed client is visible when managed buffer is shown")
              (switch-to-buffer (get-buffer-create "*hyprmacs-e2e-scratch*"))
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.25)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (string=
                   (or (hyprmacs--client-workspace-name target-client) "")
                   "special:hyprmacs-hidden"))
                5.0 0.20)
               path "managed client is hidden when managed buffer window is closed")
              (switch-to-buffer buffer)
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.25)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((workspace-name (hyprmacs--client-workspace-name target-client)))
                    (and workspace-name
                         (not (string= workspace-name "special:hyprmacs-hidden")))))
                5.0 0.20)
               path "managed client is restored visible after reopening managed buffer")
              (call-interactively #'kill-current-buffer)
              (hyprmacs--e2e-assert (not (buffer-live-p buffer)) path "managed buffer can be killed explicitly")
              (refresh-state)
              (hyprmacs--e2e-assert
               (buffer-live-p (hyprmacs-buffer-for-client target-client))
               path "managed buffer is recreated from state after kill command"))

        ;; Task 6 debug hide/show round-trip.
        (hyprmacs-debug-hide-client target-client workspace-id)
        (hyprmacs--wait-seconds 0.25)
        (hyprmacs-debug-show-client target-client workspace-id)
        (hyprmacs--wait-seconds 0.25)

            ;; Managed -> floating -> managed transition scenario.
            (let ((transition-client (or close-client target-client)))
              (append-to-file (format "transition-client: %s\n" transition-client) nil path)
              (pcase-let ((`(:exit ,float-exit :out ,float-out)
                           (hyprmacs--run-command
                            (format "hyprctl dispatch togglefloating address:%s" transition-client))))
                (append-to-file (format "togglefloating-out-1:\n%s\n" float-out) nil path)
                (hyprmacs--e2e-assert
                 (zerop float-exit)
                 path "togglefloating to floating succeeded for %s" transition-client))
              (hyprmacs--wait-seconds 0.20)
              (hyprmacs--e2e-assert
               (let ((record (hyprmacs--find-client-record transition-client)))
                 (and record (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal))))
               path "compositor marks transition client floating after toggle")
              (pcase-let ((`(:exit ,tile-exit :out ,tile-out)
                           (hyprmacs--run-command
                            (format "hyprctl dispatch togglefloating address:%s" transition-client))))
                (append-to-file (format "togglefloating-out-2:\n%s\n" tile-out) nil path)
                (hyprmacs--e2e-assert
                 (zerop tile-exit)
                 path "togglefloating back to tiled succeeded for %s" transition-client))
              (hyprmacs--wait-seconds 0.20)
              (hyprmacs--e2e-assert
               (let ((record (hyprmacs--find-client-record transition-client)))
                 (and record (not (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal)))))
               path "compositor marks transition client tiled after second toggle")
              (refresh-state))

            ;; Close-by-command scenario.
            (let* ((current-managed (managed-ids))
                   (close-target (or close-client
                                     (car (cl-remove-if (lambda (id) (string= id target-client)) current-managed)))))
              (hyprmacs--e2e-assert close-target path "close target client selected")
              (pcase-let ((`(:exit ,close-exit :out ,close-out)
                           (hyprmacs--run-command
                            (format "hyprctl dispatch closewindow address:%s" close-target))))
                (append-to-file (format "closewindow-out:\n%s\n" close-out) nil path)
                (hyprmacs--e2e-assert (zerop close-exit) path "closewindow command succeeded for %s" close-target))
              (hyprmacs--e2e-assert
               (wait-managed-membership close-target nil)
               path "closed client removed from managed set"))

        (hyprmacs-set-emacs-control-mode workspace-id)
        (hyprmacs--wait-seconds 0.25)
            (refresh-state)
        (hyprmacs--e2e-assert
         (eq (plist-get hyprmacs-session-state :input-mode) 'emacs-control)
         path "input mode switched back to emacs-control")
            (hyprmacs--e2e-assert
             (hyprmacs--wait-until
              (lambda ()
                (let ((aw (hyprmacs--hyprctl-activewindow)))
                  (and aw
                       (string= (format "%s" (alist-get 'class aw nil nil #'equal)) "emacs")
                       (string= (format "%s" (alist-get 'address aw nil nil #'equal))
                                managing-emacs-address))))
              4.0 0.10)
             path "emacs-control focuses the managing emacs frame")))))

    (hyprmacs-unmanage-workspace workspace-id)
    (hyprmacs--wait-seconds 0.50)
    (hyprmacs-request-state workspace-id)
    (hyprmacs--wait-seconds 0.30)
    (let ((layout-after (or (hyprmacs--activeworkspace-layout) "")))
      (append-to-file (format "layout-after-unmanage: %s\n" layout-after) nil path)
      (hyprmacs--e2e-assert
       (not (equal layout-after "hyprmacs"))
       path "workspace layout no longer hyprmacs after unmanage")
      (when (not (string-empty-p layout-before))
        (hyprmacs--e2e-assert
         (equal layout-after layout-before)
         path "workspace layout restored to pre-manage value")))
    (hyprmacs--e2e-assert
     (not (plist-get hyprmacs-session-state :managed))
     path "workspace unmanaged state cleared")

    (hyprmacs--append-command-output path "hyprctl activeworkspace" "hyprctl activeworkspace")
    (hyprmacs--append-command-output path "hyprctl clients" "hyprctl clients")
    (hyprmacs-disconnect)
    (append-to-file "result: PASS\n" nil path)
    (message "hyprmacs: full nested e2e complete, wrote %s" path)))

(defun hyprmacs-run-gui-smoke-test (&optional log-path)
  "Run a GUI-oriented smoke flow and write rich debug output to LOG-PATH."
  (interactive)
  (let* ((path (or log-path "logs.txt"))
         (workspace-id (hyprmacs--default-workspace-id)))
    (hyprmacs--ensure-hyprland-instance-signature)
    (with-temp-file path
      (insert ""))

    ;; Open a second client so mode and selection effects are visible.
    (start-process-shell-command
     "hyprmacs-smoke-foot" nil
     "foot -a hyprmacs-smoke-foot -T hyprmacs-smoke-foot >/dev/null 2>&1 &")
    (hyprmacs--wait-seconds 0.60)

    (hyprmacs-connect)
    (hyprmacs--wait-seconds 0.25)
    (hyprmacs-manage-current-workspace workspace-id)
    (hyprmacs--wait-seconds 0.50)
    (hyprmacs-request-state workspace-id)
    (hyprmacs--wait-seconds 0.40)

    (append-to-file "initial-state:\n" nil path)
    (append-to-file (hyprmacs--format-state-lines) nil path)
    (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

    (let ((managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
      (when managed
        (hyprmacs-select-managed-client (car managed) workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-select:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

        (hyprmacs-set-input-mode 'client-control workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-client-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

        (hyprmacs-set-input-mode 'emacs-control workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (hyprmacs-request-state workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-emacs-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")))

    (hyprmacs--append-command-output path "hyprctl clients" "hyprctl clients")
    (message "hyprmacs: GUI smoke test complete, wrote %s" path)))

(defun hyprmacs-debug-hide-client (client-id &optional workspace-id)
  "Task 6 temporary command to hide CLIENT-ID in WORKSPACE-ID."  
  (interactive (list (read-string "Client ID (e.g. 0xabc): ")))
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (hyprmacs-session-send "debug-hide-client" workspace-id `((client_id . ,client-id)))
  (message "hyprmacs: requested debug hide for %s" client-id))

(defun hyprmacs-debug-show-client (client-id &optional workspace-id)
  "Task 6 temporary command to restore CLIENT-ID in WORKSPACE-ID."  
  (interactive (list (read-string "Client ID (e.g. 0xabc): ")))
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (hyprmacs-session-send "debug-show-client" workspace-id `((client_id . ,client-id)))
  (message "hyprmacs: requested debug show for %s" client-id))

(defun hyprmacs-debug-open-layout-log (&optional path)
  "Open Task 6 layout debug log file at PATH.
Defaults to /tmp/hyprmacs-layout.log."
  (interactive)
  (let ((target (or path "/tmp/hyprmacs-layout.log")))
    (find-file target)))

(defun hyprmacs-dump-state ()
  "Render current session state in a debug buffer."  
  (interactive)
  (let ((buffer (get-buffer-create "*hyprmacs-state*")))
    (with-current-buffer buffer
      (erase-buffer)
      (insert (format "connection-status: %s\n" (plist-get hyprmacs-session-state :connection-status)))
      (insert (format "workspace-id: %s\n" (or (plist-get hyprmacs-session-state :workspace-id) "")))
      (insert (format "managed: %s\n" (plist-get hyprmacs-session-state :managed)))
      (insert (format "controller-connected: %s\n" (plist-get hyprmacs-session-state :controller-connected)))
      (insert (format "selected-client: %s\n" (or (plist-get hyprmacs-session-state :selected-client) "")))
      (insert (format "input-mode: %s\n" (or (plist-get hyprmacs-session-state :input-mode) "")))
      (insert (format "known-clients: %s\n" (plist-get hyprmacs-session-state :known-clients)))
      (insert (format "managed-clients: %s\n" (plist-get hyprmacs-session-state :managed-clients)))
      (insert (format "associated-buffers: %s\n" (plist-get hyprmacs-session-state :associated-buffers)))
      (insert (format "last-message-type: %s\n" (or (plist-get hyprmacs-session-state :last-message-type) ""))))
    (pop-to-buffer buffer)))

(provide 'hyprmacs)

;;; hyprmacs.el ends here
