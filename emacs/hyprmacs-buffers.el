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
  (or (gethash client-id hyprmacs-buffer-table)
      (let ((buffer (generate-new-buffer (hyprmacs-buffer--name client-id app-id))))
        (with-current-buffer buffer
          (setq-local hyprmacs-client-id client-id)
          (setq-local hyprmacs-client-app-id app-id))
        (puthash client-id buffer hyprmacs-buffer-table)
        buffer)))

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

(provide 'hyprmacs-buffers)

;;; hyprmacs-buffers.el ends here
