;;; hyprmacs-buffers.el --- managed buffer mode and lifecycle helpers  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Mode and buffer association primitives for managed hyprmacs client identities.

;;; Code:

(require 'subr-x)
(require 'cl-lib)

(defvar hyprmacs-buffer-table (make-hash-table :test #'equal)
  "Map client IDs to managed Emacs buffers.")

(defvar hyprmacs-session-state nil
  "Current hyprmacs session state plist.
Declared here to avoid requiring `hyprmacs-session' from this module.")

(defgroup hyprmacs nil
  "Hyprmacs workspace integration."
  :group 'applications)

(defcustom hyprmacs-window-rename-function #'hyprmacs-window-default-rename-function
  "Function used to compute managed buffer names.
Called with four arguments: CLIENT-ID, APP-ID, TITLE, and WORKSPACE-ID.
Should return a non-empty string. If nil/empty is returned, the default
`hyprmacs-window-default-rename-function' result is used."
  :type 'function
  :group 'hyprmacs)

(defcustom hyprmacs-window-close-on-buffer-kill t
  "Whether killing a managed buffer should also close its Hyprland client."
  :type 'boolean
  :group 'hyprmacs)

(defcustom hyprmacs-window-close-client-function #'hyprmacs-window-default-close-client-function
  "Function used to close a managed client when its buffer is killed.
Called with two arguments: CLIENT-ID and WORKSPACE-ID.
Should return non-nil on success."
  :type 'function
  :group 'hyprmacs)

(defcustom hyprmacs-duplicate-buffer-replacement-buffer "*scratch*"
  "Buffer name used when replacing duplicate visible managed buffers."
  :type 'string
  :group 'hyprmacs)

(defcustom hyprmacs-display-managed-buffer-function
  #'hyprmacs-display-managed-buffer-default
  "Function used to display managed buffers.
Called with BUFFER, CLIENT-ID, WORKSPACE-ID, and REASON."
  :type 'function
  :group 'hyprmacs)

(defvar hyprmacs-window-title-change-functions nil
  "Hook run when a managed client title changes.
Functions receive: CLIENT-ID OLD-TITLE NEW-TITLE BUFFER.")

(defvar hyprmacs-window-selected-functions nil
  "Hook run when selected managed client changes.
Functions receive: NEW-CLIENT-ID OLD-CLIENT-ID BUFFER.")

(defvar hyprmacs-window-input-mode-change-functions nil
  "Hook run when hyprmacs input mode changes.
Functions receive: NEW-MODE OLD-MODE.")

(defvar hyprmacs-window-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-c") #'hyprmacs-set-input-mode)
    map)
  "Keymap for `hyprmacs-window-mode'.")

(defvar-local hyprmacs-client-id nil
  "Associated Hyprland client ID for a managed buffer.")

(defvar-local hyprmacs-client-app-id nil
  "Associated application ID for a managed buffer.")

(defvar-local hyprmacs-client-title nil
  "Associated client title for a managed buffer.")

(defvar-local hyprmacs-workspace-id nil
  "Associated workspace ID for a managed buffer.")

(defvar-local hyprmacs--suppress-close-on-kill nil
  "Internal guard to avoid close dispatch during internal cleanup kills.")

(declare-function hyprmacs-set-input-mode "hyprmacs")
(declare-function hyprmacs--schedule-auto-sync-layout "hyprmacs")

(defun hyprmacs--normalize-client-id (client-id)
  "Return CLIENT-ID as a normalized Hyprland address string."
  (let ((normalized (or client-id "")))
    (when (string-prefix-p "address:" normalized)
      (setq normalized (substring normalized (length "address:"))))
    (unless (string-prefix-p "0x" normalized)
      (setq normalized (concat "0x" normalized)))
    normalized))

(defun hyprmacs-window-default-close-client-function (client-id _workspace-id)
  "Close Hyprland CLIENT-ID using hyprctl. Returns non-nil on success."
  (let ((normalized (hyprmacs--normalize-client-id client-id)))
    (condition-case nil
        (let ((rc (call-process "hyprctl" nil nil nil
                                "dispatch"
                                "closewindow"
                                (concat "address:" normalized))))
          (and (integerp rc) (zerop rc)))
      (error nil))))

(defun hyprmacs-window-mode--kill-buffer-query ()
  "Intercept managed-buffer kill and request client close.
Returns non-nil to allow normal kill to continue, nil to cancel."
  (if (or hyprmacs--suppress-close-on-kill
          (not hyprmacs-window-close-on-buffer-kill)
          (not (stringp hyprmacs-client-id))
          (string-empty-p hyprmacs-client-id)
          (not (functionp hyprmacs-window-close-client-function)))
      t
    (let ((closed (ignore-errors
                    (funcall hyprmacs-window-close-client-function
                             hyprmacs-client-id
                             hyprmacs-workspace-id))))
      ;; On successful close request, keep buffer alive until the compositor
      ;; reports the client is actually gone.
      (not closed))))

(defun hyprmacs-window-mode--kill-buffer-hook ()
  "Cleanup mapping when a managed buffer is really killed."
  (when (and (stringp hyprmacs-client-id)
             (not (string-empty-p hyprmacs-client-id)))
    (remhash hyprmacs-client-id hyprmacs-buffer-table)))

(defun hyprmacs-window-default-rename-function (_client-id app-id title _workspace-id)
  "Return default managed buffer name for APP-ID and TITLE."
  (format "*hyprmacs:%s: %s*" app-id (if (string-empty-p title) "untitled" title)))

(defun hyprmacs-buffer--name (client-id app-id title workspace-id)
  "Return display name for managed CLIENT-ID from APP-ID/TITLE/WORKSPACE-ID."
  (let* ((safe-title (if (string-empty-p (or title "")) "untitled" title))
         (default-name (hyprmacs-window-default-rename-function client-id app-id safe-title workspace-id))
         (custom-name (ignore-errors
                        (funcall hyprmacs-window-rename-function client-id app-id safe-title workspace-id))))
    (if (and (stringp custom-name) (not (string-empty-p custom-name)))
        custom-name
      default-name)))

(defun hyprmacs-window-mode--input-mode-tag ()
  "Return short input-mode tag for managed buffer mode line."
  (pcase (plist-get hyprmacs-session-state :input-mode)
    ('client-control "C")
    ('emacs-control "E")
    (_ "?")))

(defun hyprmacs-window-mode--mode-name ()
  "Return mode name including current hyprmacs input mode."
  (format "Hyprmacs[%s]" (hyprmacs-window-mode--input-mode-tag)))

(define-derived-mode hyprmacs-window-mode fundamental-mode "Hyprmacs"
  "Major mode for buffers associated with managed Hyprland clients."
  (setq-local mode-name (hyprmacs-window-mode--mode-name))
  (add-hook 'kill-buffer-query-functions #'hyprmacs-window-mode--kill-buffer-query nil t)
  (add-hook 'kill-buffer-hook #'hyprmacs-window-mode--kill-buffer-hook nil t))

(defun hyprmacs-window-mode-refresh ()
  "Refresh mode line in all live managed window-mode buffers."
  (maphash
   (lambda (_client-id buffer)
     (when (buffer-live-p buffer)
       (with-current-buffer buffer
         (when (eq major-mode 'hyprmacs-window-mode)
           (setq-local mode-name (hyprmacs-window-mode--mode-name))
           (force-mode-line-update t)))))
   hyprmacs-buffer-table))

(defun hyprmacs-buffer-ensure-for-client (client-id app-id &optional title workspace-id)
  "Create or return managed buffer for CLIENT-ID.
APP-ID identifies the application. Optional TITLE and WORKSPACE-ID update
buffer-local metadata when available."
  (let ((existing (gethash client-id hyprmacs-buffer-table)))
    (if (buffer-live-p existing)
        (progn
          (with-current-buffer existing
            (unless (eq major-mode 'hyprmacs-window-mode)
              (hyprmacs-window-mode))
            (setq-local hyprmacs-client-id client-id)
            (setq-local hyprmacs-client-app-id app-id)
            (setq-local hyprmacs-client-title (or title app-id))
            (setq-local hyprmacs-workspace-id workspace-id))
          existing)
      (let ((buffer (generate-new-buffer
                     (hyprmacs-buffer--name client-id app-id (or title app-id) workspace-id))))
        (with-current-buffer buffer
          (hyprmacs-window-mode)
          (setq-local hyprmacs-client-id client-id)
          (setq-local hyprmacs-client-app-id app-id)
          (setq-local hyprmacs-client-title (or title app-id))
          (setq-local hyprmacs-workspace-id workspace-id))
        (puthash client-id buffer hyprmacs-buffer-table)
        buffer))))

(defun hyprmacs-buffer-for-client (client-id)
  "Return managed buffer for CLIENT-ID, if present."  
  (gethash client-id hyprmacs-buffer-table))

(defun hyprmacs-buffer-update-title (client-id title)
  "Rename managed buffer for CLIENT-ID using TITLE."
  (let ((buffer (hyprmacs-buffer-for-client client-id)))
    (when (buffer-live-p buffer)
      (with-current-buffer buffer
        (let* ((old-title (or hyprmacs-client-title ""))
               (new-title (or title ""))
               (new-name (hyprmacs-buffer--name
                          client-id
                          (or hyprmacs-client-app-id "unknown")
                          new-title
                          hyprmacs-workspace-id)))
          (setq-local hyprmacs-client-title new-title)
          (rename-buffer new-name t)
          (unless (equal old-title new-title)
            (run-hook-with-args 'hyprmacs-window-title-change-functions
                                client-id old-title new-title buffer)))))))

(defun hyprmacs-buffer-remove-client (client-id)
  "Remove and kill the managed buffer for CLIENT-ID."  
  (let ((buffer (gethash client-id hyprmacs-buffer-table)))
    (remhash client-id hyprmacs-buffer-table)
    (when (buffer-live-p buffer)
      (with-current-buffer buffer
        (setq-local hyprmacs--suppress-close-on-kill t)
        (kill-buffer buffer)))))

(defun hyprmacs-buffer-reset ()
  "Reset all managed buffers and clear lookup state."  
  (maphash (lambda (_client-id buffer)
             (when (buffer-live-p buffer)
               (with-current-buffer buffer
                 (setq-local hyprmacs--suppress-close-on-kill t)
                 (kill-buffer buffer))))
           hyprmacs-buffer-table)
  (clrhash hyprmacs-buffer-table))

(defun hyprmacs-buffer-client-ids ()
  "Return all known managed client IDs."
  (let (out)
    (maphash (lambda (client-id _buffer)
               (push client-id out))
             hyprmacs-buffer-table)
    (nreverse out)))

(defun hyprmacs-display-managed-buffer-default (buffer _client-id _workspace-id _reason)
  "Display managed BUFFER using the default hyprmacs window policy.
If BUFFER is already visible, select its window. Otherwise, show it in the
currently selected window. Return the window that displays BUFFER."
  (let ((visible-window (get-buffer-window buffer t)))
    (if (window-live-p visible-window)
        (progn
          (select-window visible-window)
          visible-window)
      (set-window-buffer (selected-window) buffer)
      (selected-window))))

(defun hyprmacs--managed-buffer-p (buffer)
  "Return non-nil when BUFFER is a live hyprmacs managed client buffer."
  (and (buffer-live-p buffer)
       (with-current-buffer buffer
         (eq major-mode 'hyprmacs-window-mode))
       (with-current-buffer buffer
         (and (stringp hyprmacs-client-id)
              (not (string-empty-p hyprmacs-client-id))))))

(defun hyprmacs--visible-windows-for-buffer (buffer)
  "Return visible non-minibuffer windows currently displaying BUFFER."
  (cl-remove-if-not
   (lambda (window)
     (and (window-live-p window)
          (not (window-minibuffer-p window))
          (eq (window-buffer window) buffer)))
   (window-list nil 'no-minibuf)))

(defun hyprmacs--duplicate-buffer-replacement ()
  "Return the buffer used to replace duplicate managed-buffer displays."
  (get-buffer-create hyprmacs-duplicate-buffer-replacement-buffer))

(defun hyprmacs-enforce-single-visible-managed-buffer (buffer &optional preferred-window)
  "Ensure managed BUFFER is visible in at most one Emacs window.
Keep PREFERRED-WINDOW when it displays BUFFER. If omitted, prefer the selected
window when it displays BUFFER, then the first visible window. Replace duplicate
windows with `hyprmacs-duplicate-buffer-replacement-buffer'. Return the kept
window, or nil when BUFFER is not visible."
  (when (hyprmacs--managed-buffer-p buffer)
    (let* ((windows (hyprmacs--visible-windows-for-buffer buffer))
           (keep (cond
                  ((and (window-live-p preferred-window)
                        (eq (window-buffer preferred-window) buffer))
                   preferred-window)
                  ((and (memq (selected-window) windows)
                        (eq (window-buffer (selected-window)) buffer))
                   (selected-window))
                  (t (car windows))))
           (replacement (hyprmacs--duplicate-buffer-replacement))
           (replaced nil))
      (dolist (window windows)
        (unless (eq window keep)
          (set-window-buffer window replacement)
          (setq replaced t)))
      (when (and replaced (fboundp 'hyprmacs--schedule-auto-sync-layout))
        (hyprmacs--schedule-auto-sync-layout))
      keep)))

(defun hyprmacs-enforce-visible-managed-buffer-uniqueness (&rest args)
  "Enforce one-visible-window invariant for all visible hyprmacs buffers.
When ARGS include the window whose buffer changed, prefer that changed window
for the buffer it displays. This preserves ordinary Emacs display paths that
create a newer duplicate in a non-selected window."
  (let ((seen (make-hash-table :test #'eq))
        (changed-window (cl-find-if #'window-live-p args)))
    (dolist (window (window-list nil 'no-minibuf))
      (let ((buffer (window-buffer window)))
        (when (hyprmacs--managed-buffer-p buffer)
          (puthash buffer t seen))))
    (maphash
     (lambda (buffer _)
       (hyprmacs-enforce-single-visible-managed-buffer
        buffer
        (and (window-live-p changed-window)
             (eq (window-buffer changed-window) buffer)
             changed-window)))
     seen)))

(defun hyprmacs-display-managed-buffer (buffer client-id workspace-id reason)
  "Display managed BUFFER for CLIENT-ID on WORKSPACE-ID because of REASON.
Delegates initial display to `hyprmacs-display-managed-buffer-function', then
enforces the one-visible-window invariant for BUFFER."
  (let ((window (when (and (buffer-live-p buffer)
                           (functionp hyprmacs-display-managed-buffer-function))
                  (funcall hyprmacs-display-managed-buffer-function
                           buffer client-id workspace-id reason))))
    (hyprmacs-enforce-single-visible-managed-buffer
     buffer
     (and (window-live-p window) window))))

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
             (workspace-id (alist-get 'workspace_id entry nil nil #'equal))
             (buffer (hyprmacs-buffer-ensure-for-client client-id app-id title workspace-id)))
        (hyprmacs-buffer-update-title client-id title)
        (push (cons client-id (buffer-name buffer)) associated-buffers)))

    (dolist (client-id (hyprmacs-buffer-client-ids))
      (unless (gethash client-id managed-set)
        (hyprmacs-buffer-remove-client client-id)))

    (nreverse associated-buffers)))

(defun hyprmacs-buffer-notify-session-state-changed (old-state new-state)
  "Emit buffer hooks for meaningful OLD-STATE -> NEW-STATE transitions."
  (let ((old-selected (plist-get old-state :selected-client))
        (new-selected (plist-get new-state :selected-client))
        (old-input (plist-get old-state :input-mode))
        (new-input (plist-get new-state :input-mode)))
    (unless (equal old-selected new-selected)
      (run-hook-with-args
       'hyprmacs-window-selected-functions
       new-selected
       old-selected
       (and new-selected (hyprmacs-buffer-for-client new-selected))))
    (unless (equal old-input new-input)
      (run-hook-with-args 'hyprmacs-window-input-mode-change-functions new-input old-input))
    (when (or (not (equal old-selected new-selected))
              (not (equal old-input new-input)))
      (hyprmacs-window-mode-refresh))))

(provide 'hyprmacs-buffers)

;;; hyprmacs-buffers.el ends here
