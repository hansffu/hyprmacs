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
(require 'seq)
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

(defun hyprmacs--client-display-name (client-id)
  "Return a completion label for CLIENT-ID from current session state."
  (let* ((clients (or (plist-get hyprmacs-session-state :known-clients) '()))
         (record (seq-find
                  (lambda (client)
                    (string= client-id (alist-get 'client_id client nil nil #'equal)))
                  clients))
         (app-id (and record (alist-get 'app_id record nil nil #'equal)))
         (title (and record (alist-get 'title record nil nil #'equal))))
    (string-trim
     (format "%s%s%s"
             client-id
             (if app-id (format "  %s" app-id) "")
             (if title (format "  %s" title) "")))))

(defun hyprmacs--managed-client-completion-table ()
  "Return completion candidates for managed clients."
  (mapcar (lambda (client-id)
            (cons (hyprmacs--client-display-name client-id) client-id))
          (or (plist-get hyprmacs-session-state :managed-clients) '())))

(defun hyprmacs--read-managed-client ()
  "Read a managed client id with completion."
  (let* ((candidates (hyprmacs--managed-client-completion-table))
         (choice (completing-read "Managed client: " candidates nil t)))
    (cdr (assoc choice candidates))))

(defun hyprmacs--current-or-selected-managed-client-id ()
  "Return current buffer managed client id or selected managed client id."
  (or (hyprmacs--current-managed-buffer-client-id)
      (let ((selected (plist-get hyprmacs-session-state :selected-client))
            (managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
        (when (and selected (member selected managed))
          selected))))

(defun hyprmacs-unmanage-active-window (&optional workspace-id)
  "Unmanage the current hyprmacs window buffer or selected managed client."
  (interactive)
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (let ((client-id (hyprmacs--current-or-selected-managed-client-id)))
    (unless client-id
      (user-error "hyprmacs: no active managed window"))
    (hyprmacs-session-unmanage-client workspace-id client-id)
    (message "hyprmacs: requested unmanage-client for %s" client-id)))

(defun hyprmacs-unmanage-window (&optional client-id workspace-id)
  "Unmanage CLIENT-ID in WORKSPACE-ID, reading from managed clients interactively."
  (interactive)
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (setq client-id (or client-id (hyprmacs--read-managed-client)))
  (unless client-id
    (user-error "hyprmacs: no managed client selected"))
  (hyprmacs-session-unmanage-client workspace-id client-id)
  (message "hyprmacs: requested unmanage-client for %s" client-id))

(defun hyprmacs-manage-active-window (&optional workspace-id)
  "Ask the Hyprland plugin dispatcher to manage the active window."
  (interactive)
  (setq workspace-id (or workspace-id (hyprmacs--default-workspace-id)))
  (hyprmacs--ensure-hyprland-instance-signature)
  (let* ((command (format "hyprctl dispatch hyprmacs:manage-active-window %s" workspace-id))
         (output (shell-command-to-string command)))
    (message "hyprmacs: manage-active-window: %s" (string-trim output))))

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

(defun hyprmacs--auto-sync-layout-managed-clients-change (&rest _)
  "Immediately sync layout after managed membership changes."
  (when (timerp hyprmacs--layout-sync-timer)
    (cancel-timer hyprmacs--layout-sync-timer)
    (setq hyprmacs--layout-sync-timer nil))
  (hyprmacs--run-auto-sync-layout))

(add-hook 'hyprmacs-window-managed-clients-change-functions
          #'hyprmacs--auto-sync-layout-managed-clients-change)

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

(defun hyprmacs--client-hidden-p (client-id)
  "Return non-nil when CLIENT-ID is marked hidden by Hyprland."
  (let* ((record (hyprmacs--find-client-record client-id))
         (hidden (alist-get 'hidden record nil nil #'equal)))
    (hyprmacs--json-bool hidden)))

(defun hyprmacs--client-center (client-id)
  "Return center point for CLIENT-ID as (X . Y), or nil when unavailable."
  (let* ((record (hyprmacs--find-client-record client-id))
         (at (alist-get 'at record nil nil #'equal))
         (size (alist-get 'size record nil nil #'equal)))
    (when (and (listp at) (>= (length at) 2)
               (listp size) (>= (length size) 2)
               (numberp (nth 0 at)) (numberp (nth 1 at))
               (numberp (nth 0 size)) (numberp (nth 1 size)))
      (cons (+ (nth 0 at) (/ (nth 0 size) 2))
            (+ (nth 1 at) (/ (nth 1 size) 2))))))

(defun hyprmacs--client-rectangle (client-id)
  "Return absolute rectangle alist for CLIENT-ID, or nil when unavailable."
  (let* ((record (hyprmacs--find-client-record client-id))
         (at (alist-get 'at record nil nil #'equal))
         (size (alist-get 'size record nil nil #'equal)))
    (when (and (listp at) (>= (length at) 2)
               (listp size) (>= (length size) 2)
               (numberp (nth 0 at)) (numberp (nth 1 at))
               (numberp (nth 0 size)) (numberp (nth 1 size)))
      `((x . ,(nth 0 at))
        (y . ,(nth 1 at))
        (width . ,(nth 0 size))
        (height . ,(nth 1 size))))))

(defun hyprmacs--window-body-rectangle (&optional window)
  "Return absolute body rectangle alist for WINDOW (or selected window)."
  (let* ((window (or window (selected-window)))
         (edges (window-body-pixel-edges window))
         (origin (condition-case nil
                     (hyprmacs-layout--frame-inner-origin (window-frame window))
                   (error '(0 . 0))))
         (origin-x (car origin))
         (origin-y (cdr origin))
         (left (nth 0 edges))
         (top (nth 1 edges))
         (right (nth 2 edges))
         (bottom (nth 3 edges)))
    `((x . ,(+ origin-x left))
      (y . ,(+ origin-y top))
      (width . ,(max 0 (- right left)))
      (height . ,(max 0 (- bottom top))))))

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
