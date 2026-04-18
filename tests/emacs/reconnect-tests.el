(require 'ert)
(setq load-prefer-newer t)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-session)
(require 'hyprmacs-ipc)
(require 'hyprmacs-buffers)

(ert-deftest hyprmacs-session-transport-drop-clears-managed-state ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (hyprmacs-buffer-ensure-for-client "0xaaa" "foot")
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :workspace-id "1"))
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :managed t))
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :associated-buffers '(("0xaaa" . "*hyprmacs:0xaaa:foot*"))))

  (hyprmacs-session--handle-transport-drop "closed")

  (should (eq (plist-get hyprmacs-session-state :connection-status) 'disconnected))
  (should-not (plist-get hyprmacs-session-state :managed))
  (should-not (plist-get hyprmacs-session-state :managed-clients))
  (should-not (plist-get hyprmacs-session-state :associated-buffers))
  (should (equal (plist-get hyprmacs-session-state :last-error) "closed"))
  (should-not (hyprmacs-buffer-for-client "0xaaa")))

(ert-deftest hyprmacs-session-reconnect-and-resync-calls-connect-then-request-state ()
  (let (calls)
    (cl-letf (((symbol-function 'hyprmacs-session-connect)
               (lambda (&optional socket-path)
                 (push (cons 'connect socket-path) calls)
                 hyprmacs-session-state))
              ((symbol-function 'hyprmacs-session-request-state)
               (lambda (workspace-id)
                 (push (cons 'request-state workspace-id) calls))))
      (hyprmacs-session-reconnect-and-resync "7" "/tmp/hyprmacs-test.sock")
      (should (equal (nreverse calls)
                     '((connect . "/tmp/hyprmacs-test.sock")
                       (request-state . "7")))))))

(ert-deftest hyprmacs-session-protocol-error-updates-last-error ()
  (hyprmacs-session-reset)
  (hyprmacs-session-fake-receive
   "{\"version\":1,\"type\":\"protocol-error\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-18T00:00:00Z\",\"payload\":{\"code\":\"unsupported-version\",\"message\":\"unsupported protocol version\"}}")
  (should (equal (plist-get hyprmacs-session-state :last-error)
                 "unsupported protocol version")))

(ert-deftest hyprmacs-ipc-resolve-socket-path-reports-missing-socket ()
  (let ((missing (make-temp-name (expand-file-name "hyprmacs-missing-" temporary-file-directory))))
    (should-error (hyprmacs-ipc-resolve-socket-path missing))))

(ert-deftest hyprmacs-ipc-resolve-socket-path-accepts-existing-path ()
  (let ((socket-file (make-temp-file "hyprmacs-sock-")))
    (unwind-protect
        (should (equal (hyprmacs-ipc-resolve-socket-path socket-file)
                       socket-file))
      (when (file-exists-p socket-file)
        (delete-file socket-file)))))
