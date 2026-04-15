;;; hyprmacs-session.el --- hyprmacs session state and transport  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Session state + fake transport helpers used before the live socket server exists.

;;; Code:

(require 'cl-lib)
(require 'hyprmacs-ipc)

(defvar hyprmacs-session-state nil
  "Current hyprmacs session state plist.")

(defvar hyprmacs-session--transport-send #'ignore
  "Function used to send newline-delimited protocol frames.")

(defvar hyprmacs-session--fake-outbox nil
  "Captured frames sent through fake transport.")

(defun hyprmacs-session-reset ()
  "Reset the session state to defaults."
  (setq hyprmacs-session-state
        '(:connection-status disconnected
          :workspace-id nil
          :managed nil
          :controller-connected nil
          :eligible-clients nil
          :managed-clients nil
          :selected-client nil
          :input-mode nil
          :last-message-type nil)))

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
             (plist-put hyprmacs-session-state :managed nil)))
      ("state-dump"
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :managed
                        (hyprmacs-session--payload-bool payload 'managed)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :controller-connected
                        (hyprmacs-session--payload-bool payload 'controller_connected)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :eligible-clients
                        (alist-get 'eligible_clients payload nil nil #'equal)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :managed-clients
                        (alist-get 'managed_clients payload nil nil #'equal)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :selected-client
                        (alist-get 'selected_client payload nil nil #'equal)))
       (setq hyprmacs-session-state
             (plist-put hyprmacs-session-state :input-mode
                        (hyprmacs-ipc-mode-from-wire
                         (alist-get 'input_mode payload nil nil #'equal)))))))
  hyprmacs-session-state)

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

(hyprmacs-session-reset)

(provide 'hyprmacs-session)

;;; hyprmacs-session.el ends here
