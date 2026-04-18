;;; hyprmacs-buffers.el --- managed buffer helpers  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Buffer association primitives for managed hyprmacs client identities.

;;; Code:

(require 'subr-x)

(defvar hyprmacs-buffer-table (make-hash-table :test #'equal)
  "Map client IDs to managed Emacs buffers.")

(defvar-local hyprmacs-client-id nil
  "Associated Hyprland client ID for a managed buffer.")

(defvar-local hyprmacs-client-app-id nil
  "Associated application ID for a managed buffer.")

(defun hyprmacs-buffer--name (client-id title)
  "Return a display name for CLIENT-ID with TITLE."  
  (format "*hyprmacs:%s:%s*" client-id (if (string-empty-p title) "untitled" title)))

(defun hyprmacs-buffer-ensure-for-client (client-id app-id)
  "Create or return the managed buffer for CLIENT-ID and APP-ID."  
  (let ((existing (gethash client-id hyprmacs-buffer-table)))
    (if (buffer-live-p existing)
        existing
      (let ((buffer (generate-new-buffer (hyprmacs-buffer--name client-id app-id))))
        (with-current-buffer buffer
          (setq-local hyprmacs-client-id client-id)
          (setq-local hyprmacs-client-app-id app-id))
        (puthash client-id buffer hyprmacs-buffer-table)
        buffer))))

(defun hyprmacs-buffer-for-client (client-id)
  "Return managed buffer for CLIENT-ID, if present."  
  (gethash client-id hyprmacs-buffer-table))

(defun hyprmacs-buffer-update-title (client-id title)
  "Rename the managed buffer for CLIENT-ID using TITLE."  
  (let ((buffer (hyprmacs-buffer-for-client client-id)))
    (when (buffer-live-p buffer)
      (with-current-buffer buffer
        (rename-buffer (hyprmacs-buffer--name client-id title) t)))))

(defun hyprmacs-buffer-remove-client (client-id)
  "Remove and kill the managed buffer for CLIENT-ID."  
  (let ((buffer (gethash client-id hyprmacs-buffer-table)))
    (remhash client-id hyprmacs-buffer-table)
    (when (buffer-live-p buffer)
      (kill-buffer buffer))))

(defun hyprmacs-buffer-reset ()
  "Reset all managed buffers and clear lookup state."  
  (maphash (lambda (_client-id buffer)
             (when (buffer-live-p buffer)
               (kill-buffer buffer)))
           hyprmacs-buffer-table)
  (clrhash hyprmacs-buffer-table))

(defun hyprmacs-buffer-client-ids ()
  "Return all known managed client IDs."
  (let (out)
    (maphash (lambda (client-id _buffer)
               (push client-id out))
             hyprmacs-buffer-table)
    (nreverse out)))

(defun hyprmacs-buffer-sync-managed (managed-client-ids eligible-clients)
  "Sync managed buffers for MANAGED-CLIENT-IDS using ELIGIBLE-CLIENTS metadata.
ELIGIBLE-CLIENTS should be the decoded `eligible_clients' payload list."
  (let ((managed-set (make-hash-table :test #'equal))
        (metadata-by-client (make-hash-table :test #'equal))
        associated-buffers)
    (dolist (client managed-client-ids)
      (puthash client t managed-set))
    (dolist (entry eligible-clients)
      (let ((client-id (alist-get 'client_id entry nil nil #'equal)))
        (when client-id
          (puthash client-id entry metadata-by-client))))

    (dolist (client-id managed-client-ids)
      (let* ((entry (gethash client-id metadata-by-client))
             (app-id (or (alist-get 'app_id entry nil nil #'equal) "unknown"))
             (title (or (alist-get 'title entry nil nil #'equal) app-id))
             (buffer (hyprmacs-buffer-ensure-for-client client-id app-id)))
        (hyprmacs-buffer-update-title client-id title)
        (push (cons client-id (buffer-name buffer)) associated-buffers)))

    (dolist (client-id (hyprmacs-buffer-client-ids))
      (unless (gethash client-id managed-set)
        (hyprmacs-buffer-remove-client client-id)))

    (nreverse associated-buffers)))

(provide 'hyprmacs-buffers)

;;; hyprmacs-buffers.el ends here
