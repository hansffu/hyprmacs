;;; hyprmacs-ipc.el --- hyprmacs IPC protocol helpers  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Shared IPC encoding/decoding helpers for the hyprmacs v1 wire contract.

;;; Code:

(require 'json)
(require 'subr-x)

(defconst hyprmacs-ipc-version 1
  "Supported hyprmacs IPC protocol version.")

(defun hyprmacs-ipc-default-socket-path ()
  "Return the default plugin-owned Unix socket path."
  (let ((runtime-dir (getenv "XDG_RUNTIME_DIR")))
    (unless (and runtime-dir (not (string-empty-p runtime-dir)))
      (error "XDG_RUNTIME_DIR is not set"))
    (let ((instance (getenv "HYPRLAND_INSTANCE_SIGNATURE")))
      (if (and instance (not (string-empty-p instance)))
          (expand-file-name (format "hypr/%s/hyprmacs-v1.sock" instance) runtime-dir)
        ;; Fallback for environments where signature is unavailable.
        (expand-file-name "hyprmacs-v1.sock" runtime-dir)))))

(defun hyprmacs-ipc--rfc3339-now ()
  "Return current UTC time in RFC3339 format."  
  (format-time-string "%Y-%m-%dT%H:%M:%SZ" (current-time) t))

(defun hyprmacs-ipc-mode-to-wire (mode)
  "Encode MODE symbol for the wire protocol."
  (pcase mode
    ('emacs-control "emacs-control")
    ('client-control "client-control")
    (_ nil)))

(defun hyprmacs-ipc-mode-from-wire (mode)
  "Decode wire MODE string into an Emacs symbol."
  (cond
   ((equal mode "emacs-control") 'emacs-control)
   ((equal mode "client-control") 'client-control)
   (t nil)))

(defun hyprmacs-ipc-make-envelope (type workspace-id payload &optional timestamp)
  "Create a protocol envelope.
TYPE is the message type string. WORKSPACE-ID is the workspace identifier.
PAYLOAD is an alist that becomes the payload object. TIMESTAMP defaults to now."
  `((version . ,hyprmacs-ipc-version)
    (type . ,type)
    (workspace_id . ,workspace-id)
    (timestamp . ,(or timestamp (hyprmacs-ipc--rfc3339-now)))
    (payload . ,(or payload '()))))

(defun hyprmacs-ipc-encode-message (message)
  "Encode MESSAGE alist to one newline-delimited JSON frame."
  (concat (json-encode message) "\n"))

(defun hyprmacs-ipc-decode-message (frame)
  "Decode newline-delimited JSON FRAME into an alist envelope."
  (json-parse-string (string-trim-right frame)
                     :object-type 'alist
                     :array-type 'list
                     :null-object nil
                     :false-object :json-false))

(defun hyprmacs-ipc-split-frames (chunk)
  "Split CHUNK into complete newline-delimited frames.
Returns a cons cell (FRAMES . TAIL), where FRAMES excludes trailing newlines."
  (let ((parts (split-string chunk "\n"))
        frames)
    (while (cdr parts)
      (let ((frame (car parts)))
        (unless (string-empty-p frame)
          (push frame frames)))
      (setq parts (cdr parts)))
    (cons (nreverse frames) (car parts))))

(provide 'hyprmacs-ipc)

;;; hyprmacs-ipc.el ends here
