(require 'ert)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-buffers)

(ert-deftest hyprmacs-buffer-create-and-lookup ()
  (let* ((buffer (hyprmacs-buffer-ensure-for-client "0xabc" "foot"))
         (looked-up (hyprmacs-buffer-for-client "0xabc")))
    (should (buffer-live-p buffer))
    (should (eq buffer looked-up))
    (with-current-buffer buffer
      (should (equal hyprmacs-client-id "0xabc"))
      (should (equal hyprmacs-client-app-id "foot")))))

(ert-deftest hyprmacs-buffer-rename-and-cleanup ()
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xdef" "foot")))
    (hyprmacs-buffer-update-title "0xdef" "shell")
    (should (string-match-p "shell" (buffer-name buffer)))
    (hyprmacs-buffer-remove-client "0xdef")
    (should-not (hyprmacs-buffer-for-client "0xdef"))
    (should-not (buffer-live-p buffer))))

(ert-deftest hyprmacs-buffer-recreate-after-kill ()
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0x123" "foot")))
    (kill-buffer buffer)
    (should-not (buffer-live-p buffer))
    (let ((recreated (hyprmacs-buffer-ensure-for-client "0x123" "foot")))
      (should (buffer-live-p recreated))
      (should-not (eq recreated buffer)))))
