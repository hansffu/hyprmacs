;;; hyprmacs-session.el --- hyprmacs session state and transport  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Session state and transport helpers for fake and real IPC modes.

;;; Code:

(require 'cl-lib)
(require 'hyprmacs-buffers)
(require 'hyprmacs-ipc)

(defvar hyprmacs-session-state nil
  "Current hyprmacs session state plist.")

(defvar hyprmacs-session--transport-send #'ignore
  "Function used to send newline-delimited protocol frames.")

(defvar hyprmacs-session--fake-outbox nil
  "Captured frames sent through fake transport.")

(defvar hyprmacs-session--process nil
  "Active network process for real IPC transport.")

(defvar hyprmacs-session--partial-frame ""
  "Buffered partial frame data for process filter." )

(defun hyprmacs-session-reset ()
  "Reset the session state to defaults."
  (setq hyprmacs-session-state
        '(:connection-status disconnected
          :workspace-id nil
          :managed nil
          :controller-connected nil
          :known-clients nil
          :associated-buffers nil
          :eligible-clients nil
          :managed-clients nil
          :selected-client nil
          :input-mode nil
          :last-message-type nil)))

(defun hyprmacs-session--set-connection-status (status)
  "Set connection STATUS in session state."  
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :connection-status status)))

(defun hyprmacs-session-connect (&optional socket-path)
  "Connect to plugin socket at SOCKET-PATH or default path.
Any existing connection is closed first."  
  (hyprmacs-session-disconnect)
  (let* ((path (or socket-path (hyprmacs-ipc-default-socket-path)))
         (process (make-network-process :name "hyprmacs-ipc"
                                        :family 'local
                                        :service path
                                        :coding 'utf-8-unix
                                        :noquery t
                                        :filter #'hyprmacs-session--process-filter
                                        :sentinel #'hyprmacs-session--process-sentinel)))
    (setq hyprmacs-session--process process)
    (setq hyprmacs-session--partial-frame "")
    (setq hyprmacs-session--transport-send
          (lambda (frame)
            (when (process-live-p hyprmacs-session--process)
              (process-send-string hyprmacs-session--process frame))))
    (hyprmacs-session--set-connection-status 'connected)
    hyprmacs-session-state))

(defun hyprmacs-session-disconnect ()
  "Disconnect and clear transport process state."  
  (when (process-live-p hyprmacs-session--process)
    (delete-process hyprmacs-session--process))
  (setq hyprmacs-session--process nil)
  (setq hyprmacs-session--partial-frame "")
  (setq hyprmacs-session--transport-send #'ignore)
  (hyprmacs-session--set-connection-status 'disconnected)
  hyprmacs-session-state)

(defun hyprmacs-session-use-fake-transport ()
  "Enable fake transport that captures outbound frames in-memory."
  (setq hyprmacs-session--fake-outbox nil)
  (setq hyprmacs-session--transport-send
        (lambda (frame)
          (push frame hyprmacs-session--fake-outbox))))

(defun hyprmacs-session-fake-outbox ()
  "Return fake transport outbound frames in send order."
  (nreverse hyprmacs-session--fake-outbox))

(defun hyprmacs-session--payload-bool (payload key)
  "Extract boolean KEY from PAYLOAD alist and normalize json false marker."  
  (let ((value (alist-get key payload)))
    (not (or (null value) (eq value :json-false)))))

(defun hyprmacs-session--set-common (workspace-id type)
  "Update common state fields from WORKSPACE-ID and TYPE."
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :workspace-id workspace-id))
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :last-message-type type)))

(defun hyprmacs-session-handle-frame (frame)
  "Handle one inbound protocol FRAME and update session state."  
  (let* ((message (hyprmacs-ipc-decode-message frame))
         (type (alist-get 'type message nil nil #'equal))
         (workspace-id (alist-get 'workspace_id message nil nil #'equal))
         (payload (alist-get 'payload message)))
    (hyprmacs-session--set-common workspace-id type)
    (pcase type
      ("workspace-managed"
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :managed
                        (hyprmacs-session--payload-bool payload 'managed)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :controller-connected
                        (hyprmacs-session--payload-bool payload 'controller_connected))))
      ("workspace-unmanaged"
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :managed nil))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :managed-clients nil))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :associated-buffers nil))
       (hyprmacs-buffer-reset))
      ("layout-applied"
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :selected-client
                        (alist-get 'selected_client payload nil nil #'equal))))
      ("mode-changed"
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :input-mode
                        (hyprmacs-ipc-mode-from-wire
                         (alist-get 'mode payload nil nil #'equal)))))
      ("state-dump"
       (let ((eligible-clients (alist-get 'eligible_clients payload nil nil #'equal))
             (managed-clients (alist-get 'managed_clients payload nil nil #'equal)))
         (setq hyprmacs-session-state
               (plist-put hyprmacs-session-state :managed
                          (hyprmacs-session--payload-bool payload 'managed)))
         (setq hyprmacs-session-state
               (plist-put hyprmacs-session-state :controller-connected
                          (hyprmacs-session--payload-bool payload 'controller_connected)))
         (setq hyprmacs-session-state
               (plist-put hyprmacs-session-state :eligible-clients eligible-clients))
         (setq hyprmacs-session-state
               (plist-put hyprmacs-session-state :known-clients eligible-clients))
         (setq hyprmacs-session-state
               (plist-put hyprmacs-session-state :managed-clients managed-clients))
         (setq hyprmacs-session-state
               (plist-put hyprmacs-session-state :associated-buffers
                          (hyprmacs-buffer-sync-managed managed-clients eligible-clients))))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :selected-client
                        (alist-get 'selected_client payload nil nil #'equal)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :input-mode
                        (hyprmacs-ipc-mode-from-wire
                         (alist-get 'input_mode payload nil nil #'equal)))))))
  hyprmacs-session-state)

(defun hyprmacs-session--process-filter (_process chunk)
  "Handle transport CHUNK from process filter."  
  (let* ((split (hyprmacs-ipc-split-frames (concat hyprmacs-session--partial-frame chunk)))
         (frames (car split))
         (tail (cdr split)))
    (setq hyprmacs-session--partial-frame tail)
    (dolist (frame frames)
      (hyprmacs-session-handle-frame frame))))

(defun hyprmacs-session--process-sentinel (_process event)
  "Handle process lifecycle EVENT."  
  (when (string-match-p "closed\\|deleted\\|failed" event)
    (setq hyprmacs-session--process nil)
    (setq hyprmacs-session--partial-frame "")
    (setq hyprmacs-session--transport-send #'ignore)
    (hyprmacs-session--set-connection-status 'disconnected)))

(defun hyprmacs-session-fake-receive (frame)
  "Feed FRAME into the fake transport receive path."
  (hyprmacs-session-handle-frame frame))

(defun hyprmacs-session-send (type workspace-id payload)
  "Send one outbound protocol frame.
TYPE is the message type, WORKSPACE-ID is a string, PAYLOAD is an alist."
  (funcall hyprmacs-session--transport-send
           (hyprmacs-ipc-encode-message
            (hyprmacs-ipc-make-envelope type workspace-id payload))))

(defun hyprmacs-session-request-state (workspace-id)
  "Send a request-state message for WORKSPACE-ID."
  (hyprmacs-session-send "request-state" workspace-id '()))

(defun hyprmacs-session-manage-workspace (workspace-id &optional adopt-existing)
  "Send manage-workspace for WORKSPACE-ID.
When ADOPT-EXISTING is nil, defaults to true."  
  (hyprmacs-session-send
   "manage-workspace"
   workspace-id
   `((adopt_existing_clients . ,(if (null adopt-existing) t adopt-existing)))))

(defun hyprmacs-session-unmanage-workspace (workspace-id &optional reason)
  "Send unmanage-workspace for WORKSPACE-ID with optional REASON."  
  (hyprmacs-session-send
   "unmanage-workspace"
   workspace-id
   (if reason
       `((reason . ,reason))
     '())))

(defun hyprmacs-session-set-selected-client (workspace-id client-id)
  "Send set-selected-client for WORKSPACE-ID and CLIENT-ID."
  (hyprmacs-session-send
   "set-selected-client"
   workspace-id
   `((client_id . ,client-id))))

(defun hyprmacs-session-set-input-mode (workspace-id mode)
  "Send set-input-mode for WORKSPACE-ID and MODE symbol."
  (let ((wire (hyprmacs-ipc-mode-to-wire mode)))
    (when wire
      (hyprmacs-session-send
       "set-input-mode"
       workspace-id
       `((mode . ,wire))))))

(defun hyprmacs-session-seed-client (workspace-id client-id app-id title floating)
  "Send seed-client for WORKSPACE-ID with client metadata."
  (hyprmacs-session-send
   "seed-client"
   workspace-id
   `((client_id . ,client-id)
     (workspace_id . ,workspace-id)
     (app_id . ,app-id)
     (title . ,title)
     (floating . ,(if floating t :json-false)))))

(hyprmacs-session-reset)

(provide 'hyprmacs-session)

;;; hyprmacs-session.el ends here
