;;; hyprmacs-layout.el --- managed layout snapshot helpers  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Compute rectangle-first layout snapshots from the current Emacs window tree.

;;; Code:

(require 'cl-lib)
(require 'hyprmacs-ipc)
(require 'json)
(require 'subr-x)

(defvar hyprmacs-layout-use-hyprctl-origin t
  "When non-nil, resolve Emacs frame origin from Hyprland client state first.")

(defun hyprmacs-layout--normalize-json-bool (value)
  "Normalize JSON VALUE to a strict Emacs boolean."
  (not (or (null value)
           (eq value :json-false)
           (eq value :false)
           (eq value 'false))))

(defun hyprmacs-layout--workspace-id ()
  "Return current hyprmacs workspace id string, or nil."
  (when (boundp 'hyprmacs-session-state)
    (let ((workspace-id (plist-get hyprmacs-session-state :workspace-id)))
      (when workspace-id
        (format "%s" workspace-id)))))

(defun hyprmacs-layout--hyprland-main-frame-origin ()
  "Return Emacs main-frame origin from `hyprctl -j clients` as (X . Y), or nil."
  (when hyprmacs-layout-use-hyprctl-origin
    (condition-case nil
        (let* ((workspace-id (hyprmacs-layout--workspace-id))
               (self-pid (emacs-pid))
               (raw (shell-command-to-string "hyprctl -j clients 2>/dev/null"))
               (clients (json-parse-string raw :array-type 'list :object-type 'alist))
               (best-origin nil)
               (best-area -1))
          (dolist (client clients)
            (let* ((client-pid (alist-get 'pid client nil nil #'equal))
                   (class (downcase (or (alist-get 'class client nil nil #'equal) "")))
                   (floating (hyprmacs-layout--normalize-json-bool
                              (alist-get 'floating client nil nil #'equal)))
                   (workspace (alist-get 'workspace client nil nil #'equal))
                   (client-workspace-id (alist-get 'id workspace nil nil #'equal))
                   (at (alist-get 'at client nil nil #'equal))
                   (size (alist-get 'size client nil nil #'equal))
                   (workspace-match (or (null workspace-id)
                                        (equal (format "%s" client-workspace-id) workspace-id))))
              (when (and (numberp client-pid)
                         (= client-pid self-pid)
                         (string-match-p "emacs" class)
                         workspace-match
                         (not floating)
                         (listp at)
                         (>= (length at) 2)
                         (listp size)
                         (>= (length size) 2))
                (let* ((width (or (nth 0 size) 0))
                       (height (or (nth 1 size) 0))
                       (area (* (max 0 width) (max 0 height))))
                  (when (> area best-area)
                    (setq best-area area)
                    (setq best-origin (cons (or (nth 0 at) 0)
                                            (or (nth 1 at) 0))))))))
          best-origin)
      (error nil))))

(defun hyprmacs-layout--frame-inner-origin (&optional frame)
  "Return absolute pixel origin for FRAME inner area as (X . Y)."
  (let* ((frame (or frame (selected-frame)))
         (hypr-origin (hyprmacs-layout--hyprland-main-frame-origin))
         (geometry (frame-geometry frame))
         (inner (alist-get 'inner-position geometry))
         (outer (alist-get 'outer-position geometry))
         (fallback (frame-position frame))
         (origin (or hypr-origin inner outer fallback '(0 . 0))))
    (if (consp origin)
        (cons (or (car origin) 0)
              (or (cdr origin) 0))
      '(0 . 0))))

(defun hyprmacs-layout--window-client-id (window)
  "Return managed client ID for WINDOW, or nil when not a managed buffer."
  (let ((buffer (window-buffer window)))
    (when (buffer-live-p buffer)
      (with-current-buffer buffer
        (when (boundp 'hyprmacs-client-id)
          hyprmacs-client-id)))))

(defun hyprmacs-layout--window-rectangle (window)
  "Return WINDOW geometry as an alist with x/y/width/height."
  (let* ((edges (window-inside-pixel-edges window))
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

(defun hyprmacs-layout-visible-rectangles (&optional windows)
  "Return visible managed client rectangles from WINDOWS.
Each item is an alist with client_id/x/y/width/height."
  (let ((out nil))
    (dolist (window (or windows (window-list nil 'no-minibuffer)))
      (let ((client-id (hyprmacs-layout--window-client-id window)))
        (when client-id
          (push (append `((client_id . ,client-id))
                        (hyprmacs-layout--window-rectangle window))
                out))))
    (nreverse out)))

(defun hyprmacs-layout--dedupe-rectangles-by-client (rectangles)
  "Return RECTANGLES keeping only the first rectangle per client_id."
  (let ((seen (make-hash-table :test 'equal))
        (out nil))
    (dolist (rectangle rectangles)
      (let ((client-id (alist-get 'client_id rectangle nil nil #'equal)))
        (unless (gethash client-id seen)
          (puthash client-id t seen)
          (push rectangle out))))
    (nreverse out)))

(defun hyprmacs-layout--rectangles-overlap-p (a b)
  "Return non-nil when rectangle A overlaps rectangle B."
  (let* ((ax (alist-get 'x a))
         (ay (alist-get 'y a))
         (aw (alist-get 'width a))
         (ah (alist-get 'height a))
         (bx (alist-get 'x b))
         (by (alist-get 'y b))
         (bw (alist-get 'width b))
         (bh (alist-get 'height b))
         (ar (+ ax aw))
         (ab (+ ay ah))
         (br (+ bx bw))
         (bb (+ by bh)))
    (not (or (<= ar bx)
             (<= br ax)
             (<= ab by)
             (<= bb ay)))))

(defun hyprmacs-layout--validate-rectangles (rectangles)
  "Signal an error when RECTANGLES are invalid."
  (let ((index 0))
    (dolist (rectangle rectangles)
      (when (or (<= (alist-get 'width rectangle) 0)
                (<= (alist-get 'height rectangle) 0))
        (error "Invalid rectangle size for client %s" (alist-get 'client_id rectangle)))
      (dolist (other (nthcdr (1+ index) rectangles))
        (when (hyprmacs-layout--rectangles-overlap-p rectangle other)
          (error "Overlapping rectangles for clients %s and %s"
                 (alist-get 'client_id rectangle)
                 (alist-get 'client_id other))))
      (setq index (1+ index)))))

(defun hyprmacs-layout-build-payload (managed-clients selected-client input-mode &optional windows)
  "Build a set-layout payload from current Emacs windows.
MANAGED-CLIENTS is the full managed set. SELECTED-CLIENT and INPUT-MODE
come from session state. WINDOWS is for tests and defaults to the live tree."
  (let* ((effective-selected (when (member selected-client managed-clients) selected-client))
         (rectangles (hyprmacs-layout--dedupe-rectangles-by-client
                      (hyprmacs-layout-visible-rectangles windows)))
         (visible-clients (mapcar (lambda (rectangle) (alist-get 'client_id rectangle))
                                  rectangles))
         (hidden-clients (cl-remove-if (lambda (client-id)
                                         (member client-id visible-clients))
                                       managed-clients))
         (stacking-order (copy-sequence visible-clients))
         (wire-mode (hyprmacs-ipc-mode-to-wire input-mode)))
    (hyprmacs-layout--validate-rectangles rectangles)
    `((selected_client . ,effective-selected)
      (input_mode . ,wire-mode)
      ;; Encode list fields as vectors so empty collections become [] (not null).
      (visible_clients . ,(vconcat visible-clients))
      (hidden_clients . ,(vconcat hidden-clients))
      (rectangles . ,(vconcat rectangles))
      (stacking_order . ,(vconcat stacking-order)))))

(provide 'hyprmacs-layout)

;;; hyprmacs-layout.el ends here
