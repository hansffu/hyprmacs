;;; hyprmacs.el --- Hyprland workspace integration bootstrap  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Entry point for hyprmacs user commands.

;;; Code:

(require 'hyprmacs-session)
(require 'subr-x)

(defconst hyprmacs-version "0.1.0"
  "Current hyprmacs bootstrap version.")

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
  (interactive (list (read-string "Workspace ID: "
                                  (or (plist-get hyprmacs-session-state :workspace-id) "1"))))
  (hyprmacs-session-manage-workspace workspace-id)
  (message "hyprmacs: requested manage-workspace for %s" workspace-id))

(defun hyprmacs-unmanage-workspace (workspace-id)
  "Mark WORKSPACE-ID as unmanaged in session state."  
  (interactive (list (read-string "Workspace ID: "
                                  (or (plist-get hyprmacs-session-state :workspace-id) "1"))))
  (hyprmacs-session-unmanage-workspace workspace-id "user-request")
  (message "hyprmacs: requested unmanage-workspace for %s" workspace-id))

(defun hyprmacs-request-state (workspace-id)
  "Request state dump for WORKSPACE-ID."  
  (interactive (list (read-string "Workspace ID: "
                                  (or (plist-get hyprmacs-session-state :workspace-id) "1"))))
  (hyprmacs-session-request-state workspace-id)
  (message "hyprmacs: requested state for %s" workspace-id))

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
      (insert (format "last-message-type: %s\n" (or (plist-get hyprmacs-session-state :last-message-type) ""))))
    (pop-to-buffer buffer)))

(provide 'hyprmacs)

;;; hyprmacs.el ends here
