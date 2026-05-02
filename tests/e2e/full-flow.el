;;; full-flow.el --- Nested Hyprland E2E flow for hyprmacs  -*- lexical-binding: t; -*-

;;; Commentary:
;; This file is intentionally outside the runtime package.  It defines the
;; nested compositor behavior contract used by `just e2e`.

;;; Code:

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs)
(require 'cl-lib)
(require 'subr-x)

(defvar hyprmacs-e2e-required-assertion-file
  (expand-file-name "required-assertions.txt" (file-name-directory load-file-name))
  "Manifest of assertion IDs that must be observed by the nested E2E run.")

(defvar hyprmacs-e2e--observed-assertion-ids nil
  "Assertion IDs observed in the current nested E2E run.")

(defun hyprmacs-e2e--assertion-id (fmt)
  "Return stable assertion ID derived from assertion format FMT."
  (let* ((id (downcase fmt))
         (id (replace-regexp-in-string "%[-+ #0-9.]*[dioux]" "number" id))
         (id (replace-regexp-in-string "%[-+ #0-9.]*[a-zA-Z]" "value" id))
         (id (replace-regexp-in-string "[^a-z0-9]+" "-" id))
         (id (replace-regexp-in-string "^-+" "" id))
         (id (replace-regexp-in-string "-+$" "" id)))
    id))

(defun hyprmacs-e2e--required-assertion-ids ()
  "Return required assertion IDs from `hyprmacs-e2e-required-assertion-file'."
  (unless (file-readable-p hyprmacs-e2e-required-assertion-file)
    (error "hyprmacs e2e required assertion manifest is missing: %s"
           hyprmacs-e2e-required-assertion-file))
  (with-temp-buffer
    (insert-file-contents hyprmacs-e2e-required-assertion-file)
    (let (ids)
      (dolist (line (split-string (buffer-string) "\n" t))
        (let ((trimmed (string-trim line)))
          (unless (or (string-empty-p trimmed)
                      (string-prefix-p "#" trimmed))
            (push trimmed ids))))
      (nreverse ids))))

(defun hyprmacs-e2e--verify-required-assertions (path)
  "Fail unless every required assertion ID was observed; append result to PATH."
  (let* ((required (hyprmacs-e2e--required-assertion-ids))
         (observed (delete-dups (copy-sequence hyprmacs-e2e--observed-assertion-ids)))
         (missing (cl-set-difference required observed :test #'string=)))
    (append-to-file
     (format "e2e-required-assertions: required=%d observed=%d missing=%S\n"
             (length required) (length observed) missing)
     nil path)
    (when missing
      (error "hyprmacs e2e required assertions were not observed: %s"
             (string-join missing ", ")))))

(defun hyprmacs--e2e-assert (condition path fmt &rest args)
  "Assert CONDITION, record a manifest-backed assertion ID, and write to PATH."
  (let* ((id (hyprmacs-e2e--assertion-id fmt))
         (message (apply #'format fmt args)))
    (push id hyprmacs-e2e--observed-assertion-ids)
    (append-to-file
     (format "%s [%s]: %s\n" (if condition "ok" "fail") id message)
     nil path)
    (unless condition
      (error "hyprmacs e2e assertion failed [%s]: %s" id message))))

(defun hyprmacs-run-full-e2e-test (&optional log-path)
  "Run full scripted nested E2E validation and write log to LOG-PATH.
This covers the implemented runtime contract through Task 11."
  (interactive)
  (let* ((path (or log-path "logs-e2e.txt"))
         (workspace-id (hyprmacs--default-workspace-id))
         (layout-before (or (hyprmacs--activeworkspace-layout) ""))
         (regression-failures nil)
         (floating-before-manage-client nil))
    (setq hyprmacs-e2e--observed-assertion-ids nil)
    (hyprmacs--ensure-hyprland-instance-signature)
    (with-temp-file path
      (insert "hyprmacs nested e2e\n"))
    (append-to-file (format "workspace-id: %s\n" workspace-id) nil path)
    (append-to-file (format "layout-before: %s\n" layout-before) nil path)

    (hyprmacs-connect)
    (hyprmacs--wait-seconds 0.25)
    (hyprmacs--e2e-assert
     (eq (plist-get hyprmacs-session-state :connection-status) 'connected)
     path "session connected")

    ;; Name the primary frame explicitly and spawn duplicate-title clients.
    (set-frame-name "hyprmacs-e2e-main")
    (dotimes (idx 2)
      (pcase-let ((`(:exit ,exit :out ,out)
                   (hyprmacs--run-command "hyprctl dispatch exec \"foot -T hyprmacs-dup\"")))
        (append-to-file (format "spawn-dup-%d-exit: %s\n" idx exit) nil path)
        (append-to-file out nil path)
        (hyprmacs--e2e-assert (zerop exit) path "spawn duplicate-title client %d succeeded" idx)))
    (hyprmacs--wait-seconds 1.0)

    ;; Pre-manage floating invariant: explicit workspace manage adopts already-floating clients.
    (dolist (entry (hyprmacs--hyprctl-clients))
      (when (null floating-before-manage-client)
        (let* ((address (format "%s" (alist-get 'address entry nil nil #'equal)))
               (workspace (alist-get 'workspace entry nil nil #'equal))
               (entry-workspace-id (alist-get 'id workspace nil nil #'equal))
               (entry-workspace (format "%s" entry-workspace-id))
               (class (downcase (format "%s" (alist-get 'class entry nil nil #'equal))))
               (floating (hyprmacs--json-bool (alist-get 'floating entry nil nil #'equal))))
          (when (and (not (string-empty-p address))
                     (equal entry-workspace workspace-id)
                     (not (string-match-p "emacs" class))
                     (not floating))
            (setq floating-before-manage-client address)))))
    (hyprmacs--e2e-assert
     floating-before-manage-client
     path
     "pre-manage non-emacs client selected for floating adoption check")
    (when floating-before-manage-client
      (append-to-file (format "floating-before-manage-client: %s\n" floating-before-manage-client) nil path)
      (pcase-let ((`(:exit ,float-exit :out ,float-out)
                   (hyprmacs--run-command
                    (format "hyprctl dispatch togglefloating address:%s" floating-before-manage-client))))
        (append-to-file (format "pre-manage-togglefloating-out:\n%s\n" float-out) nil path)
        (hyprmacs--e2e-assert
         (zerop float-exit)
         path
         "pre-manage togglefloating succeeded for %s"
         floating-before-manage-client))
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (let ((record (hyprmacs--find-client-record floating-before-manage-client)))
            (and record
                 (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal)))))
        4.0 0.10)
       path
       "pre-manage client is floating before manage-workspace"))

    (hyprmacs-manage-current-workspace workspace-id)
    (hyprmacs--wait-seconds 0.80)
    (hyprmacs-request-state workspace-id)
    (hyprmacs--wait-seconds 0.50)
    (hyprmacs--e2e-assert (plist-get hyprmacs-session-state :managed) path "workspace marked managed")
    (hyprmacs--e2e-assert (plist-get hyprmacs-session-state :controller-connected) path "controller connected true")
    (let ((layout-after-manage (or (hyprmacs--activeworkspace-layout) "")))
      (pcase-let ((`(:exit ,layout-exit :out ,layout-out)
                   (hyprmacs--run-command "hyprctl -j activeworkspace")))
        (append-to-file (format "hyprctl-activeworkspace-after-manage-exit: %s\n" layout-exit) nil path)
        (append-to-file (format "hyprctl-activeworkspace-after-manage-out:\n%s\n" layout-out) nil path))
      (append-to-file (format "layout-after-manage: %s\n" layout-after-manage) nil path)
      (hyprmacs--e2e-assert
       (equal layout-after-manage "hyprmacs")
       path "active workspace layout switched to hyprmacs"))
    (hyprmacs--e2e-assert
     (= (or (hyprmacs--hyprctl-option-int "animations:enabled") -1) 0)
     path "animations:enabled forced to 0 while managed")
    (hyprmacs--e2e-assert
     (= (or (hyprmacs--hyprctl-option-int "misc:focus_on_activate") -1) 0)
     path "misc:focus_on_activate forced to 0 while managed")
    (when floating-before-manage-client
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (hyprmacs-request-state workspace-id)
          (hyprmacs--wait-seconds 0.15)
          (member floating-before-manage-client
                  (or (plist-get hyprmacs-session-state :managed-clients) '())))
        4.0 0.20)
       path
       "pre-manage floating client is adopted by explicit manage-workspace")
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (let ((buffer (hyprmacs-buffer-for-client floating-before-manage-client)))
            (and buffer (buffer-live-p buffer))))
        4.0 0.20)
       path
       "pre-manage floating client gets a managed buffer after manage-workspace"))
    (when floating-before-manage-client
      (pcase-let ((`(:exit ,tile-exit :out ,tile-out)
                   (hyprmacs--run-command
                    (format "hyprctl dispatch togglefloating address:%s" floating-before-manage-client))))
        (append-to-file (format "pre-manage-togglefloating-back-out:\n%s\n" tile-out) nil path)
        (hyprmacs--e2e-assert
         (zerop tile-exit)
         path
         "pre-manage managed client togglefloating request succeeded"))
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (hyprmacs-request-state workspace-id)
          (hyprmacs--wait-seconds 0.15)
          (member floating-before-manage-client
                  (or (plist-get hyprmacs-session-state :managed-clients) '())))
        4.0 0.20)
       path
       "pre-manage managed client stays managed after unfloat attempt")
      (pcase-let ((`(:exit ,focus-exit :out ,focus-out)
                   (hyprmacs--run-command "hyprctl dispatch hyprmacs:set-emacs-control-mode")))
        (append-to-file (format "pre-manage-focus-emacs-out:\n%s\n" focus-out) nil path)
        (hyprmacs--e2e-assert
         (zerop focus-exit)
         path
         "dispatcher hyprmacs:set-emacs-control-mode succeeded for pre-manage transition check"))
      (pcase-let ((`(:exit ,close-exit :out ,close-out)
                   (hyprmacs--run-command
                    (format "hyprctl dispatch closewindow address:%s" floating-before-manage-client))))
        (append-to-file (format "pre-manage-floating-close-out:\n%s\n" close-out) nil path)
        (hyprmacs--e2e-assert
         (zerop close-exit)
         path
         "pre-manage transition probe client close succeeded"))
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (null (hyprmacs--find-client-record floating-before-manage-client)))
        4.0 0.15)
       path
       "pre-manage transition probe client is closed before layering assertions"))

    (let* ((active (hyprmacs--hyprctl-activewindow))
           (managing-emacs-address (format "%s" (alist-get 'address active nil nil #'equal)))
           (managing-class (format "%s" (alist-get 'class active nil nil #'equal))))
      (hyprmacs--e2e-assert (string= managing-class "emacs") path "manage captured emacs as active class")
      (append-to-file (format "managing-emacs-address: %s\n" managing-emacs-address) nil path)

      ;; Multi-frame scenario.
      (ignore-errors (make-frame '((name . "hyprmacs-e2e-secondary"))))
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (>= (length (cl-remove-if-not
                       (lambda (entry)
                         (string= (format "%s" (alist-get 'class entry nil nil #'equal)) "emacs"))
                       (hyprmacs--hyprctl-clients)))
              2))
        5.0 0.15)
       path "multiple emacs frames observed in compositor")
      (let ((secondary-emacs-address nil))
        (dolist (entry (hyprmacs--hyprctl-clients))
          (let ((address (format "%s" (alist-get 'address entry nil nil #'equal)))
                (class (format "%s" (alist-get 'class entry nil nil #'equal)))
                (title (format "%s" (alist-get 'title entry nil nil #'equal))))
            (when (and (not secondary-emacs-address)
                       (string= class "emacs")
                       (not (string= address managing-emacs-address))
                       (string= title "hyprmacs-e2e-secondary"))
              (setq secondary-emacs-address address))))
        (hyprmacs--e2e-assert secondary-emacs-address path "secondary emacs frame address discovered")
        (when secondary-emacs-address
          (append-to-file (format "secondary-emacs-address: %s\n" secondary-emacs-address) nil path)
          (pcase-let ((`(:exit ,secondary-float-exit :out ,secondary-float-out)
                       (hyprmacs--run-command
                        (format "hyprctl dispatch togglefloating address:%s" secondary-emacs-address))))
            (append-to-file (format "secondary-emacs-togglefloating-out:\n%s\n" secondary-float-out) nil path)
            (hyprmacs--e2e-assert
             (zerop secondary-float-exit)
             path
             "secondary emacs frame togglefloating request succeeds"))
          (hyprmacs--e2e-assert
           (hyprmacs--wait-until
            (lambda ()
              (let ((record (hyprmacs--find-client-record secondary-emacs-address)))
                (and record
                     (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal)))))
            4.0 0.15)
           path
           "secondary emacs frame is allowed to become floating")
          (pcase-let ((`(:exit ,secondary-tile-exit :out ,secondary-tile-out)
                       (hyprmacs--run-command
                        (format "hyprctl dispatch togglefloating address:%s" secondary-emacs-address))))
            (append-to-file (format "secondary-emacs-togglefloating-back-out:\n%s\n" secondary-tile-out) nil path)
            (hyprmacs--e2e-assert
             (zerop secondary-tile-exit)
             path
             "secondary emacs frame togglefloating back request succeeds"))))
      (pcase-let ((`(:exit ,managing-float-exit :out ,managing-float-out)
                   (hyprmacs--run-command
                    (format "hyprctl dispatch togglefloating address:%s" managing-emacs-address))))
        (append-to-file (format "managing-emacs-togglefloating-out:\n%s\n" managing-float-out) nil path)
        (hyprmacs--e2e-assert
         (zerop managing-float-exit)
         path
         "managing emacs frame togglefloating request succeeds"))
      (pcase-let ((`(:exit ,repair-exit :out ,repair-out)
                   (hyprmacs--run-command "hyprctl dispatch hyprmacs:set-emacs-control-mode")))
        (append-to-file (format "managing-emacs-repair-emacs-control-out:\n%s\n" repair-out) nil path)
        (hyprmacs--e2e-assert
         (zerop repair-exit)
         path
         "emacs-control dispatcher succeeds after managing frame togglefloating"))
      (hyprmacs--e2e-assert
       (hyprmacs--wait-until
        (lambda ()
          (let ((record (hyprmacs--find-client-record managing-emacs-address)))
            (and record
                 (not (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal))))))
        4.0 0.15)
       path
       "managing emacs frame is repaired to tiled after togglefloating")

      (cl-labels
          ((refresh-state ()
             (hyprmacs-request-state workspace-id)
             (hyprmacs--wait-seconds 0.25))
           (managed-ids ()
             (or (plist-get hyprmacs-session-state :managed-clients) '()))
           (eligible-clients ()
             (or (plist-get hyprmacs-session-state :eligible-clients) '()))
           (wait-managed-membership (client-id expected)
             (hyprmacs--wait-until
              (lambda ()
                (refresh-state)
                (if expected
                    (member client-id (managed-ids))
                  (not (member client-id (managed-ids)))))
              5.0 0.20)))
        (refresh-state)
        (let ((post-manage-client nil))
          (pcase-let ((`(:exit ,post-exit :out ,post-out)
                       (hyprmacs--run-command
                        "hyprctl dispatch exec \"foot -a hyprmacs-post-manage -T hyprmacs-post-manage\"")))
            (append-to-file (format "spawn-post-manage-exit: %s\n" post-exit) nil path)
            (append-to-file post-out nil path)
            (hyprmacs--e2e-assert
             (zerop post-exit)
             path
             "spawn post-manage client succeeded"))
          (hyprmacs--e2e-assert
           (hyprmacs--wait-until
            (lambda ()
              (refresh-state)
              (setq post-manage-client nil)
              (dolist (entry (eligible-clients))
                (when (and (null post-manage-client)
                           (string= (format "%s" (alist-get 'app_id entry nil nil #'equal))
                                    "hyprmacs-post-manage"))
                  (setq post-manage-client
                        (format "%s" (alist-get 'client_id entry nil nil #'equal)))))
              (and post-manage-client
                   (member post-manage-client (managed-ids))))
            5.0 0.20)
           path
           "post-manage opened client is adopted into managed set")
          (append-to-file (format "post-manage-client: %s\n" post-manage-client) nil path)
          (hyprmacs--e2e-assert
           (and post-manage-client
                (let ((buffer (hyprmacs-buffer-for-client post-manage-client)))
                  (and buffer (buffer-live-p buffer))))
           path
           "post-manage opened client gets a managed buffer")
          (hyprmacs--e2e-assert
           (hyprmacs--wait-until
            (lambda ()
              (let ((workspace-name (hyprmacs--client-workspace-name post-manage-client)))
                (and workspace-name
                     (not (string= workspace-name "special:hyprmacs-hidden")))))
            5.0 0.20)
           path
           "post-manage opened client is displayed after adoption"))
        (let ((managed (managed-ids)))
      (when (< (length managed) 1)
        (hyprmacs--seed-existing-workspace-clients workspace-id)
        (hyprmacs--wait-seconds 0.25)
        (refresh-state)
        (setq managed (managed-ids)))
          (hyprmacs--e2e-assert (>= (length managed) 2) path "at least two managed clients discovered")

          ;; Duplicate-title scenario (same title, different client IDs).
          (let ((title-map (make-hash-table :test #'equal))
                (has-duplicates nil))
            (dolist (entry (eligible-clients))
              (let* ((title (format "%s" (alist-get 'title entry nil nil #'equal)))
                     (client-id (format "%s" (alist-get 'client_id entry nil nil #'equal)))
                     (existing (gethash title title-map)))
                (puthash title (cons client-id existing) title-map)))
            (maphash
             (lambda (_title ids)
               (when (>= (length (delete-dups ids)) 2)
                 (setq has-duplicates t)))
             title-map)
            (hyprmacs--e2e-assert has-duplicates
                                  path "multiple clients with same title are tracked as distinct ids"))

          (let* ((target-client (car managed))
                 (close-client (cadr managed)))
        (append-to-file (format "target-client: %s\n" target-client) nil path)
        (append-to-file (format "close-client: %s\n" close-client) nil path)
            (let ((target-buffer (hyprmacs-buffer-for-client target-client))
                  (split-buffer (hyprmacs-buffer-for-client close-client)))
              (hyprmacs--e2e-assert
               (and (buffer-live-p target-buffer) (buffer-live-p split-buffer))
               path
               "split-view client-control buffers exist")
              (delete-other-windows)
              (switch-to-buffer target-buffer)
              (split-window-right)
              (other-window 1)
              (switch-to-buffer split-buffer)
              (other-window -1)
              (hyprmacs-select-managed-client target-client workspace-id)
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.35)
              (hyprmacs-set-input-mode 'client-control workspace-id)
              (hyprmacs--wait-seconds 0.35)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((workspace-name (hyprmacs--client-workspace-name close-client)))
                    (and workspace-name
                         (not (string= workspace-name "special:hyprmacs-hidden")))))
                5.0 0.20)
               path
               "split-view unfocused client remains visible in client-control")
              (let ((split-center (hyprmacs--client-center close-client)))
                (hyprmacs--e2e-assert
                 split-center
                 path
                 "split-view unfocused client center available for client-control layering")
                (when split-center
                  (pcase-let ((`(:exit ,split-move-exit :out ,split-move-out)
                               (hyprmacs--run-command
                                (format "hyprctl dispatch movecursor %d %d"
                                        (car split-center)
                                        (cdr split-center)))))
                    (append-to-file (format "split-client-control-movecursor-out:\n%s\n" split-move-out) nil path)
                    (hyprmacs--e2e-assert
                     (zerop split-move-exit)
                     path
                     "movecursor succeeded for split-view client-control layering"))
                  (pcase-let ((`(:exit ,split-click-exit :out ,split-click-out)
                               (hyprmacs--run-command "hyprctl dispatch mouse 1")))
                    (append-to-file (format "split-client-control-click-out:\n%s\n" split-click-out) nil path)
                    (hyprmacs--e2e-assert
                     (zerop split-click-exit)
                     path
                     "mouse click succeeded for split-view client-control layering"))))
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((aw (hyprmacs--hyprctl-activewindow)))
                    (and aw
                         (string= (format "%s" (alist-get 'address aw nil nil #'equal))
                                  close-client))))
                4.0 0.10)
               path
               "split-view unfocused client is above emacs and receives click in client-control"))
        (hyprmacs-select-managed-client target-client workspace-id)
        (hyprmacs--wait-seconds 0.30)
        (hyprmacs-set-input-mode 'client-control workspace-id)
        (hyprmacs--wait-seconds 0.25)
            (refresh-state)
        (hyprmacs--e2e-assert
         (eq (plist-get hyprmacs-session-state :input-mode) 'client-control)
         path "input mode switched to client-control")

        (hyprmacs-sync-layout workspace-id t)
        (hyprmacs--wait-seconds 0.35)
        (hyprmacs--e2e-assert
         (equal (plist-get hyprmacs-session-state :last-message-type) "state-dump")
         path "state-dump observed after layout publication")

            ;; Buffer visibility lifecycle + command kill/recreate scenario.
            (let ((buffer (hyprmacs-buffer-for-client target-client)))
              (hyprmacs--e2e-assert (buffer-live-p buffer) path "managed buffer exists for target client")
              (delete-other-windows)
              (switch-to-buffer buffer)
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.25)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((workspace-name (hyprmacs--client-workspace-name target-client)))
                    (and workspace-name
                         (not (string= workspace-name "special:hyprmacs-hidden")))))
                5.0 0.20)
               path "managed client is visible when managed buffer is shown")
              (let* ((window-body (hyprmacs--window-body-rectangle (selected-window)))
                     (client-rect nil)
                     (client-ready
                      (hyprmacs--wait-until
                       (lambda ()
                         (setq client-rect (hyprmacs--client-rectangle target-client))
                         client-rect)
                       3.0 0.10))
                     (x-delta nil)
                     (y-delta nil)
                     (width-delta nil)
                     (height-delta nil)
                     (geometry-ok nil))
                (when client-ready
                  (setq x-delta (abs (- (alist-get 'x client-rect) (alist-get 'x window-body))))
                  (setq y-delta (abs (- (alist-get 'y client-rect) (alist-get 'y window-body))))
                  (setq width-delta (abs (- (alist-get 'width client-rect) (alist-get 'width window-body))))
                  (setq height-delta (abs (- (alist-get 'height client-rect) (alist-get 'height window-body))))
                  (setq geometry-ok
                        (and (<= x-delta 2)
                             (<= y-delta 2)
                             (<= width-delta 2)
                             (<= height-delta 2))))
                (append-to-file (format "single-window-body-rect: %S\n" window-body) nil path)
                (append-to-file (format "single-window-client-rect: %S\n" client-rect) nil path)
                (append-to-file
                 (format "single-window-rect-delta: ((x . %S) (y . %S) (width . %S) (height . %S))\n"
                         x-delta y-delta width-delta height-delta)
                 nil path)
                (condition-case err
                    (progn
                      (hyprmacs--e2e-assert
                       client-ready
                       path
                       "single-window client rectangle is available for geometry assertion")
                      (hyprmacs--e2e-assert
                       geometry-ok
                       path
                       "single-window managed client rectangle matches Emacs body rectangle"))
                  (error (push (error-message-string err) regression-failures)))
                )
              (pcase-let ((`(:exit ,focus-exit :out ,focus-out)
                           (hyprmacs--run-command "hyprctl dispatch hyprmacs:set-emacs-control-mode")))
                (append-to-file (format "focus-emacs-out:\n%s\n" focus-out) nil path)
                (hyprmacs--e2e-assert
                 (zerop focus-exit)
                 path
                 "dispatcher hyprmacs:set-emacs-control-mode succeeded for layering assertion"))
              (hyprmacs--wait-seconds 0.30)
              (let ((active-before-layering-click (hyprmacs--hyprctl-activewindow)))
                (append-to-file (format "active-before-layering-click: %S\n" active-before-layering-click) nil path)
                (condition-case err
                    (hyprmacs--e2e-assert
                     (and active-before-layering-click
                          (string= (format "%s" (alist-get 'class active-before-layering-click nil nil #'equal)) "emacs"))
                     path
                     "managed layering assertion starts with emacs focused")
                  (error (push (error-message-string err) regression-failures))))
              (let ((center (hyprmacs--client-center target-client)))
                (hyprmacs--e2e-assert center path "target client center available for layering click assertion")
                (when center
                  (pcase-let ((`(:exit ,move-exit :out ,move-out)
                               (hyprmacs--run-command
                                (format "hyprctl dispatch movecursor %d %d" (car center) (cdr center)))))
                    (append-to-file (format "movecursor-out:\n%s\n" move-out) nil path)
                    (hyprmacs--e2e-assert (zerop move-exit) path "movecursor succeeded for layering assertion"))
                  (pcase-let ((`(:exit ,click-exit :out ,click-out)
                               (hyprmacs--run-command "hyprctl dispatch mouse 1")))
                    (append-to-file (format "mouse-click-out:\n%s\n" click-out) nil path)
                    (hyprmacs--e2e-assert (zerop click-exit) path "mouse click dispatch succeeded for layering assertion")))
                (let ((active-immediate (hyprmacs--hyprctl-activewindow)))
                  (append-to-file (format "active-immediate-after-managed-click: %S\n" active-immediate) nil path)
                  (condition-case err
                      (hyprmacs--e2e-assert
                       (and active-immediate
                            (string= (format "%s" (alist-get 'address active-immediate nil nil #'equal))
                                     target-client))
                       path
                       "managed client is on top and receives click immediately while emacs focused")
                    (error (push (error-message-string err) regression-failures)))))
              (hyprmacs--wait-seconds 0.20)
              (condition-case err
                  (hyprmacs--e2e-assert
                   (hyprmacs--wait-until
                    (lambda ()
                      (let ((aw (hyprmacs--hyprctl-activewindow)))
                        (and aw
                             (string= (format "%s" (alist-get 'address aw nil nil #'equal))
                                      target-client))))
                    4.0 0.10)
                   path "managed client receives click while emacs focused (layer ordering)")
                (error (push (error-message-string err) regression-failures)))
              (switch-to-buffer (get-buffer-create "*hyprmacs-e2e-scratch*"))
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.25)
              (append-to-file
               (format "target-client-after-hide-check: %S\n"
                       (hyprmacs--find-client-record target-client))
               nil path)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((workspace-name (or (hyprmacs--client-workspace-name target-client) "")))
                    (or (string= workspace-name "special:hyprmacs-hidden")
                        (hyprmacs--client-hidden-p target-client))))
                5.0 0.20)
               path "managed client is hidden when managed buffer window is closed")
              (switch-to-buffer buffer)
              (hyprmacs-sync-layout workspace-id t)
              (hyprmacs--wait-seconds 0.25)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (let ((workspace-name (hyprmacs--client-workspace-name target-client)))
                    (and workspace-name
                         (not (string= workspace-name "special:hyprmacs-hidden")))))
                5.0 0.20)
               path "managed client is restored visible after reopening managed buffer")
              (let ((kill-result (kill-current-buffer)))
                (append-to-file (format "kill-current-buffer-result: %S\n" kill-result) nil path)
                (hyprmacs--e2e-assert (not kill-result)
                                      path
                                      "managed buffer kill is intercepted while close request is sent"))
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (refresh-state)
                  (not (member target-client (managed-ids))))
                5.0 0.20)
               path "killing managed buffer closes and removes target client")
              (hyprmacs--e2e-assert
               (not (buffer-live-p (hyprmacs-buffer-for-client target-client)))
               path "managed buffer stays removed after kill command"))

        ;; Task 6 debug hide/show round-trip.
        (hyprmacs-debug-hide-client target-client workspace-id)
        (hyprmacs--wait-seconds 0.25)
        (hyprmacs-debug-show-client target-client workspace-id)
        (hyprmacs--wait-seconds 0.25)

            ;; Managed -> floating -> managed transition scenario.
            (let ((transition-client (or close-client target-client)))
              (append-to-file (format "transition-client: %s\n" transition-client) nil path)
              (let ((transition-buffer (hyprmacs-buffer-for-client transition-client)))
                (hyprmacs--e2e-assert
                 (buffer-live-p transition-buffer)
                 path
                 "transition client buffer exists before floating transition assertion")
                (when (buffer-live-p transition-buffer)
                  (switch-to-buffer transition-buffer)
                  (hyprmacs-sync-layout workspace-id t)
                  (hyprmacs--wait-seconds 0.25)))
              (pcase-let ((`(:exit ,float-exit :out ,float-out)
                           (hyprmacs--run-command
                            (format "hyprctl dispatch togglefloating address:%s" transition-client))))
                (append-to-file (format "togglefloating-out-1:\n%s\n" float-out) nil path)
                (hyprmacs--e2e-assert
                 (zerop float-exit)
                 path "togglefloating request for managed client succeeded for %s" transition-client))
              (hyprmacs--wait-seconds 0.50)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (refresh-state)
                  (let ((record (hyprmacs--find-client-record transition-client)))
                    (and record
                         (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal))
                         (member transition-client (managed-ids)))))
                5.0 0.20)
               path "managed client remains managed and floating after togglefloating")
              (pcase-let ((`(:exit ,focus-emacs-exit :out ,focus-emacs-out)
                           (hyprmacs--run-command "hyprctl dispatch hyprmacs:set-emacs-control-mode")))
                (append-to-file (format "focus-emacs-before-floating-click-out:\n%s\n" focus-emacs-out) nil path)
                (hyprmacs--e2e-assert
                 (zerop focus-emacs-exit)
                 path
                 "dispatcher hyprmacs:set-emacs-control-mode succeeded for floating layering assertion"))
              (hyprmacs--wait-seconds 0.25)
              (let ((active-before-floating-click (hyprmacs--hyprctl-activewindow)))
                (append-to-file (format "active-before-floating-click: %S\n" active-before-floating-click) nil path)
                (condition-case err
                    (hyprmacs--e2e-assert
                     (and active-before-floating-click
                          (string= (format "%s" (alist-get 'class active-before-floating-click nil nil #'equal)) "emacs"))
                     path
                     "floating layering assertion starts with emacs focused")
                  (error (push (error-message-string err) regression-failures))))
              (let ((floating-center (hyprmacs--client-center transition-client)))
                (hyprmacs--e2e-assert
                 floating-center
                 path
                 "floating transition client center available for layering click assertion")
                (when floating-center
                  (pcase-let ((`(:exit ,move-exit :out ,move-out)
                               (hyprmacs--run-command
                                (format "hyprctl dispatch movecursor %d %d"
                                        (car floating-center)
                                        (cdr floating-center)))))
                    (append-to-file (format "movecursor-floating-out:\n%s\n" move-out) nil path)
                    (hyprmacs--e2e-assert
                     (zerop move-exit)
                     path
                     "movecursor succeeded for floating layering assertion"))
                  (pcase-let ((`(:exit ,click-exit :out ,click-out)
                               (hyprmacs--run-command "hyprctl dispatch mouse 1")))
                    (append-to-file (format "mouse-click-floating-out:\n%s\n" click-out) nil path)
                    (hyprmacs--e2e-assert
                     (zerop click-exit)
                     path
                     "mouse click dispatch succeeded for floating layering assertion"))))
              (let ((active-immediate-after-floating-click (hyprmacs--hyprctl-activewindow)))
                (append-to-file
                 (format "active-immediate-after-floating-click: %S\n"
                         active-immediate-after-floating-click)
                 nil path)
                (condition-case err
                    (hyprmacs--e2e-assert
                     (and active-immediate-after-floating-click
                          (string= (format "%s" (alist-get 'address active-immediate-after-floating-click nil nil #'equal))
                                   transition-client))
                     path
                     "floating client is on top and receives click immediately while emacs focused")
                  (error (push (error-message-string err) regression-failures))))
              (pcase-let ((`(:exit ,tile-exit :out ,tile-out)
                           (hyprmacs--run-command
                            (format "hyprctl dispatch togglefloating address:%s" transition-client))))
                (append-to-file (format "togglefloating-out-2:\n%s\n" tile-out) nil path)
                (hyprmacs--e2e-assert
                 (zerop tile-exit)
                 path "second togglefloating request for managed client succeeded for %s" transition-client))
              (hyprmacs--wait-seconds 0.50)
              (hyprmacs--e2e-assert
               (hyprmacs--wait-until
                (lambda ()
                  (refresh-state)
                  (let ((record (hyprmacs--find-client-record transition-client)))
                    (and record
                         (hyprmacs--json-bool (alist-get 'floating record nil nil #'equal))
                         (member transition-client (managed-ids)))))
                5.0 0.20)
               path "managed client remains managed and floating after second togglefloating")
              (refresh-state))

            ;; Close-by-command scenario.
            (let* ((current-managed (managed-ids))
                   (close-target (or close-client
                                     (car (cl-remove-if (lambda (id) (string= id target-client)) current-managed)))))
              (hyprmacs--e2e-assert close-target path "close target client selected")
              (pcase-let ((`(:exit ,close-exit :out ,close-out)
                           (hyprmacs--run-command
                            (format "hyprctl dispatch closewindow address:%s" close-target))))
                (append-to-file (format "closewindow-out:\n%s\n" close-out) nil path)
                (hyprmacs--e2e-assert (zerop close-exit) path "closewindow command succeeded for %s" close-target))
              (hyprmacs--e2e-assert
               (wait-managed-membership close-target nil)
               path "closed client removed from managed set"))

        (pcase-let ((`(:exit ,dispatcher-exit :out ,dispatcher-out)
                     (hyprmacs--run-command "hyprctl dispatch hyprmacs:set-emacs-control-mode")))
          (append-to-file (format "dispatcher-set-emacs-control-out:\n%s\n" dispatcher-out) nil path)
          (hyprmacs--e2e-assert
           (zerop dispatcher-exit)
           path "dispatcher hyprmacs:set-emacs-control-mode succeeds"))
        (hyprmacs--wait-seconds 0.35)
            (refresh-state)
        (hyprmacs--e2e-assert
         (eq (plist-get hyprmacs-session-state :input-mode) 'emacs-control)
         path "input mode switched back to emacs-control")
            (hyprmacs--e2e-assert
             (hyprmacs--wait-until
              (lambda ()
                (let ((aw (hyprmacs--hyprctl-activewindow)))
                  (and aw
                       (string= (format "%s" (alist-get 'class aw nil nil #'equal)) "emacs")
                       (string= (format "%s" (alist-get 'address aw nil nil #'equal))
                                managing-emacs-address))))
              4.0 0.10)
             path "emacs-control focuses the managing emacs frame"))
          (when regression-failures
            (error "hyprmacs e2e regression assertions failed: %s"
                   (string-join (nreverse regression-failures) " | "))))))

    (hyprmacs-unmanage-workspace workspace-id)
    (hyprmacs--wait-seconds 0.50)
    (hyprmacs-request-state workspace-id)
    (hyprmacs--wait-seconds 0.30)
    (let ((layout-after (or (hyprmacs--activeworkspace-layout) "")))
      (append-to-file (format "layout-after-unmanage: %s\n" layout-after) nil path)
      (hyprmacs--e2e-assert
       (not (equal layout-after "hyprmacs"))
       path "workspace layout no longer hyprmacs after unmanage")
      (when (not (string-empty-p layout-before))
        (hyprmacs--e2e-assert
         (equal layout-after layout-before)
         path "workspace layout restored to pre-manage value")))
    (hyprmacs--e2e-assert
     (not (plist-get hyprmacs-session-state :managed))
     path "workspace unmanaged state cleared")

    (hyprmacs--append-command-output path "hyprctl activeworkspace" "hyprctl activeworkspace")
    (hyprmacs--append-command-output path "hyprctl clients" "hyprctl clients")
    (hyprmacs-disconnect)
    (hyprmacs-e2e--verify-required-assertions path)
    (append-to-file "result: PASS\n" nil path)
    (message "hyprmacs: full nested e2e complete, wrote %s" path)))

(provide 'hyprmacs-e2e-full-flow)
;;; full-flow.el ends here
