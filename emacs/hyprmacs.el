;;; hyprmacs.el --- Hyprland workspace integration bootstrap  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Entry point for hyprmacs user commands.

;;; Code:

(require 'hyprmacs-session)
(require 'json)
(require 'subr-x)

(defconst hyprmacs-version "0.1.0"
  "Current hyprmacs bootstrap version.")

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
  (or (plist-get hyprmacs-session-state :workspace-id)
      (hyprmacs--active-workspace-id)
      "1"))

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

(defun hyprmacs-manage-current-workspace (workspace-id)
  "Mark WORKSPACE-ID as managed in session state."  
  (interactive (list (read-string "Workspace ID: " (hyprmacs--default-workspace-id))))
  (hyprmacs-session-manage-workspace workspace-id)
  (hyprmacs--seed-existing-workspace-clients workspace-id)
  (message "hyprmacs: requested manage-workspace for %s" workspace-id))

(defun hyprmacs-unmanage-workspace (workspace-id)
  "Mark WORKSPACE-ID as unmanaged in session state."  
  (interactive (list (read-string "Workspace ID: " (hyprmacs--default-workspace-id))))
  (hyprmacs-session-unmanage-workspace workspace-id "user-request")
  (message "hyprmacs: requested unmanage-workspace for %s" workspace-id))

(defun hyprmacs-request-state (workspace-id)
  "Request state dump for WORKSPACE-ID."  
  (interactive (list (read-string "Workspace ID: " (hyprmacs--default-workspace-id))))
  (hyprmacs-session-request-state workspace-id)
  (message "hyprmacs: requested state for %s" workspace-id))

(defun hyprmacs-set-input-mode (workspace-id mode)
  "Set hyprmacs input MODE for WORKSPACE-ID."
  (interactive
   (list (read-string "Workspace ID: " (hyprmacs--default-workspace-id))
         (intern (completing-read "Mode: " '("emacs-control" "client-control") nil t nil nil "emacs-control"))))
  (hyprmacs-session-set-input-mode workspace-id mode)
  (message "hyprmacs: requested input mode %s for %s" mode workspace-id))

(defun hyprmacs-select-managed-client (workspace-id client-id)
  "Select managed CLIENT-ID in WORKSPACE-ID and make it visible."
  (interactive
   (let* ((workspace (read-string "Workspace ID: " (hyprmacs--default-workspace-id)))
          (managed (or (plist-get hyprmacs-session-state :managed-clients) '())))
     (list workspace
           (completing-read "Managed client: " managed nil t))))
  (hyprmacs-session-set-selected-client workspace-id client-id)
  (message "hyprmacs: requested selected managed client %s" client-id))

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
          (hyprmacs-select-managed-client workspace-id target-client))
        (hyprmacs--wait-seconds 0.25)
        (hyprmacs-set-input-mode workspace-id 'client-control)
        (hyprmacs--wait-seconds 0.20)
        (hyprmacs-request-state workspace-id)
        (hyprmacs--wait-seconds 0.25)
        (append-to-file "\nafter-client-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

        (hyprmacs-set-input-mode workspace-id 'emacs-control)
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
  (append-to-file (format "\n%s\n" title) nil path)
  (append-to-file (shell-command-to-string command) nil path))

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
        (hyprmacs-select-managed-client workspace-id (car managed))
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-select:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

        (hyprmacs-set-input-mode workspace-id 'client-control)
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-client-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")

        (hyprmacs-set-input-mode workspace-id 'emacs-control)
        (hyprmacs--wait-seconds 0.30)
        (hyprmacs-request-state workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (append-to-file "\nafter-emacs-control:\n" nil path)
        (append-to-file (hyprmacs--format-state-lines) nil path)
        (hyprmacs--append-command-output path "hyprctl activewindow" "hyprctl activewindow")))

    (hyprmacs--append-command-output path "hyprctl clients" "hyprctl clients")
    (message "hyprmacs: GUI smoke test complete, wrote %s" path)))

(defun hyprmacs-debug-hide-client (workspace-id client-id)
  "Task 6 temporary command to hide CLIENT-ID in WORKSPACE-ID."  
  (interactive (list (read-string "Workspace ID: "
                                  (or (plist-get hyprmacs-session-state :workspace-id) "1"))
                     (read-string "Client ID (e.g. 0xabc): ")))
  (hyprmacs-session-send "debug-hide-client" workspace-id `((client_id . ,client-id)))
  (message "hyprmacs: requested debug hide for %s" client-id))

(defun hyprmacs-debug-show-client (workspace-id client-id)
  "Task 6 temporary command to restore CLIENT-ID in WORKSPACE-ID."  
  (interactive (list (read-string "Workspace ID: "
                                  (or (plist-get hyprmacs-session-state :workspace-id) "1"))
                     (read-string "Client ID (e.g. 0xabc): ")))
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
