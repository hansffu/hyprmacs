(require 'ert)
(require 'subr-x)
(setq load-prefer-newer t)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-session)
(require 'hyprmacs)

(ert-deftest hyprmacs-session-connect-disconnect-transitions ()
  (hyprmacs-session-reset)
  (hyprmacs-session-connect)
  (should (eq (plist-get hyprmacs-session-state :connection-status) 'connected))
  (hyprmacs-session-disconnect)
  (should (eq (plist-get hyprmacs-session-state :connection-status) 'disconnected)))

(ert-deftest hyprmacs-session-commands-enqueue-messages-in-fake-transport ()
  (hyprmacs-session-reset)
  (hyprmacs-session-use-fake-transport)
  (hyprmacs-session-manage-workspace "1")
  (hyprmacs-session-unmanage-workspace "1" "user-request")
  (let ((frames (hyprmacs-session-fake-outbox)))
    (should (= (length frames) 2))
    (should (string-match-p "\\\"type\\\":\\\"manage-workspace\\\"" (car frames)))
    (should (string-match-p "\\\"type\\\":\\\"unmanage-workspace\\\"" (cadr frames)))))

(ert-deftest hyprmacs-state-buffer-renders-known-session-fields ()
  (hyprmacs-session-reset)
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :workspace-id "1"))
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :managed t))
  (hyprmacs-dump-state)
  (with-current-buffer "*hyprmacs-state*"
    (should (string-match-p "workspace-id: 1" (buffer-string)))
    (should (string-match-p "managed: t" (buffer-string)))))
