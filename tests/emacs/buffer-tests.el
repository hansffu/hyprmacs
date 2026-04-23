(require 'ert)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-buffers)
(require 'hyprmacs-session)

(ert-deftest hyprmacs-buffer-create-and-lookup ()
  (let* ((buffer (hyprmacs-buffer-ensure-for-client "0xabc" "foot"))
         (looked-up (hyprmacs-buffer-for-client "0xabc")))
    (unwind-protect
        (progn
          (should (buffer-live-p buffer))
          (should (eq buffer looked-up))
          (with-current-buffer buffer
            (should (equal hyprmacs-client-id "0xabc"))
            (should (equal hyprmacs-client-app-id "foot"))
            (should (eq major-mode 'hyprmacs-window-mode))
            (should (equal (buffer-name buffer) "*hyprmacs:foot: foot*"))
            (should (eq (key-binding (kbd "C-c C-c")) #'hyprmacs-set-input-mode))))
      (when (buffer-live-p buffer)
        (kill-buffer buffer))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-buffer-rename-and-cleanup ()
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xdef" "foot"))
        (seen nil))
    (unwind-protect
        (progn
          (let ((hyprmacs-window-title-change-functions
                 (list (lambda (client-id old-title new-title _buffer)
                         (setq seen (list client-id old-title new-title))))))
            (hyprmacs-buffer-update-title "0xdef" "shell")
            (should (string-match-p "shell" (buffer-name buffer)))
            (should (equal seen '("0xdef" "foot" "shell"))))
          (hyprmacs-buffer-remove-client "0xdef")
          (should-not (hyprmacs-buffer-for-client "0xdef"))
          (should-not (buffer-live-p buffer)))
      (when (buffer-live-p buffer)
        (kill-buffer buffer))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-buffer-recreate-after-kill ()
  (let ((hyprmacs-window-close-on-buffer-kill nil)
        (buffer (hyprmacs-buffer-ensure-for-client "0x123" "foot")))
    (unwind-protect
        (progn
          (kill-buffer buffer)
          (should-not (buffer-live-p buffer))
          (let ((recreated (hyprmacs-buffer-ensure-for-client "0x123" "foot")))
            (should (buffer-live-p recreated))
            (should-not (eq recreated buffer))
            (with-current-buffer recreated
              (should (eq major-mode 'hyprmacs-window-mode)))))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-buffer-custom-rename-handler-is-used ()
  (let ((hyprmacs-window-rename-function
         (lambda (client-id app-id title _workspace-id)
           (format "*hm:%s:%s:%s*" app-id client-id title))))
    (let ((buffer (hyprmacs-buffer-ensure-for-client "0x999" "foot")))
      (unwind-protect
          (progn
            (hyprmacs-buffer-update-title "0x999" "shell")
            (should (equal (buffer-name buffer) "*hm:foot:0x999:shell*")))
        (hyprmacs-buffer-reset)))))

(ert-deftest hyprmacs-buffer-kill-closes-client-window ()
  (let ((calls nil)
        (hyprmacs-window-close-on-buffer-kill t)
        (hyprmacs-window-close-client-function
         (lambda (client-id workspace-id)
           (push (list client-id workspace-id) calls)
           t)))
    (let ((buffer (hyprmacs-buffer-ensure-for-client "0xabc" "foot" "shell" "1")))
      (should-not (kill-buffer buffer))
      (should (equal calls '(("0xabc" "1"))))
      (should (buffer-live-p buffer))
      (should (eq (hyprmacs-buffer-for-client "0xabc") buffer))
      (with-current-buffer buffer
        (setq-local hyprmacs--suppress-close-on-kill t))
      (kill-buffer buffer)
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-buffer-remove-client-does-not-close-window ()
  (let ((calls nil)
        (hyprmacs-window-close-on-buffer-kill t)
        (hyprmacs-window-close-client-function
         (lambda (client-id workspace-id)
           (push (list client-id workspace-id) calls)
           t)))
    (let ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1")))
      (hyprmacs-buffer-remove-client "0xaaa")
      (should-not (buffer-live-p buffer))
      (should (equal calls nil))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-window-mode-name-reflects-input-mode ()
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xbee" "foot")))
    (unwind-protect
        (progn
          (setq hyprmacs-session-state
                (plist-put hyprmacs-session-state :input-mode 'emacs-control))
          (hyprmacs-window-mode-refresh)
          (with-current-buffer buffer
            (should (string-match-p "\\[E\\]" mode-name)))
          (setq hyprmacs-session-state
                (plist-put hyprmacs-session-state :input-mode 'client-control))
          (hyprmacs-window-mode-refresh)
          (with-current-buffer buffer
            (should (string-match-p "\\[C\\]" mode-name))))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-buffer-session-change-hooks-fire ()
  (let ((buffer-a (hyprmacs-buffer-ensure-for-client "0xaaa" "foot"))
        (buffer-b (hyprmacs-buffer-ensure-for-client "0xbbb" "foot"))
        selected-events
        input-events)
    (unwind-protect
        (let ((hyprmacs-window-selected-functions
               (list (lambda (new-client old-client buffer)
                       (push (list new-client old-client buffer) selected-events))))
              (hyprmacs-window-input-mode-change-functions
               (list (lambda (new-mode old-mode)
                       (push (list new-mode old-mode) input-events)))))
          (hyprmacs-buffer-notify-session-state-changed
           '(:selected-client "0xaaa" :input-mode emacs-control)
           '(:selected-client "0xbbb" :input-mode client-control))
          (should (equal (length selected-events) 1))
          (should (equal (caar selected-events) "0xbbb"))
          (should (eq (caddar selected-events) buffer-b))
          (should (equal input-events '((client-control emacs-control)))))
      (when (buffer-live-p buffer-a)
        (kill-buffer buffer-a))
      (when (buffer-live-p buffer-b)
        (kill-buffer buffer-b))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-display-managed-buffer-uses-selected-window ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1")))
    (unwind-protect
        (progn
          (switch-to-buffer (get-buffer-create "*hyprmacs-display-source*"))
          (hyprmacs-display-managed-buffer buffer "0xaaa" "1" 'new-client)
          (should (eq (window-buffer (selected-window)) buffer)))
      (delete-other-windows)
      (mapc (lambda (name)
              (when-let ((buf (get-buffer name)))
                (kill-buffer buf)))
            '("*hyprmacs-display-source*"))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-display-managed-buffer-selects-existing-visible-window ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left (get-buffer-create "*left*"))
          (set-window-buffer right buffer)
          (select-window left)
          (hyprmacs-display-managed-buffer buffer "0xaaa" "1" 'focus-request)
          (should (eq (selected-window) right)))
      (delete-other-windows)
      (mapc (lambda (name)
              (when-let ((buf (get-buffer name)))
                (kill-buffer buf)))
            '("*left*"))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-enforce-single-visible-buffer-replaces-older-duplicate ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((hyprmacs-duplicate-buffer-replacement-buffer "*scratch*")
         (buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left buffer)
          (set-window-buffer right buffer)
          (select-window right)
          (hyprmacs-enforce-single-visible-managed-buffer buffer)
          (should (eq (window-buffer right) buffer))
          (should (eq (window-buffer left) (get-buffer "*scratch*"))))
      (delete-other-windows)
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-visible-uniqueness-prefers-changed-non-selected-window ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((hyprmacs-duplicate-buffer-replacement-buffer "*scratch*")
         (buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left buffer)
          (select-window left)
          ;; Simulate ordinary Emacs display paths that put the same buffer in
          ;; a non-selected window, then run the buffer-change hook callback.
          (set-window-buffer right buffer)
          (hyprmacs-enforce-visible-managed-buffer-uniqueness right)
          (should (eq (window-buffer right) buffer))
          (should (eq (window-buffer left) (get-buffer "*scratch*"))))
      (delete-other-windows)
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-buffer-local-change-hook-prefers-changed-window ()
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((hyprmacs-duplicate-buffer-replacement-buffer "*scratch*")
         (buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left buffer)
          (select-window left)
          (set-window-buffer right buffer)
          (with-current-buffer buffer
            (run-hook-with-args 'window-buffer-change-functions right))
          (should (eq (window-buffer right) buffer))
          (should (eq (window-buffer left) (get-buffer "*scratch*"))))
      (delete-other-windows)
      (hyprmacs-buffer-reset))))
