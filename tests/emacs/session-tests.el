(require 'ert)
(require 'subr-x)
(setq load-prefer-newer t)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-session)
(require 'hyprmacs)

(ert-deftest hyprmacs-session-connect-disconnect-transitions ()
  (hyprmacs-session-reset)
  (cl-letf (((symbol-function 'hyprmacs-ipc-resolve-socket-path)
             (lambda (&optional _socket-path) "/tmp/hyprmacs-test.sock"))
            ((symbol-function 'make-network-process)
             (lambda (&rest _args) 'hyprmacs-test-process))
            ((symbol-function 'process-live-p)
             (lambda (_process) t))
            ((symbol-function 'delete-process)
             (lambda (&rest _args) nil))
            ((symbol-function 'process-send-string)
             (lambda (&rest _args) nil)))
    (hyprmacs-session-connect)
    (should (eq (plist-get hyprmacs-session-state :connection-status) 'connected))
    (hyprmacs-session-disconnect)
    (should (eq (plist-get hyprmacs-session-state :connection-status) 'disconnected))))

(ert-deftest hyprmacs-session-commands-enqueue-messages-in-fake-transport ()
  (hyprmacs-session-reset)
  (hyprmacs-session-use-fake-transport)
  (hyprmacs-session-manage-workspace "1")
  (hyprmacs-session-unmanage-workspace "1" "user-request")
  (hyprmacs-session-manage-client "1" "0xaaa")
  (hyprmacs-session-unmanage-client "1" "0xbbb")
  (let ((frames (hyprmacs-session-fake-outbox)))
    (should (= (length frames) 4))
    (should (string-match-p "\\\"type\\\":\\\"manage-workspace\\\"" (car frames)))
    (should (string-match-p "\\\"type\\\":\\\"unmanage-workspace\\\"" (cadr frames)))
    (should (string-match-p "\\\"type\\\":\\\"manage-client\\\"" (nth 2 frames)))
    (should (string-match-p "\\\"client_id\\\":\\\"0xaaa\\\"" (nth 2 frames)))
    (should (string-match-p "\\\"type\\\":\\\"unmanage-client\\\"" (nth 3 frames)))
    (should (string-match-p "\\\"client_id\\\":\\\"0xbbb\\\"" (nth 3 frames)))))

(ert-deftest hyprmacs-state-buffer-renders-known-session-fields ()
  (hyprmacs-session-reset)
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :workspace-id "1"))
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :managed t))
  (setq hyprmacs-session-state
        (plist-put hyprmacs-session-state :associated-buffers '(("0xabc" . "*hyprmacs:0xabc:foot*"))))
  (hyprmacs-dump-state)
  (with-current-buffer "*hyprmacs-state*"
    (should (string-match-p "workspace-id: 1" (buffer-string)))
    (should (string-match-p "managed: t" (buffer-string)))
    (should (string-match-p "associated-buffers:" (buffer-string)))))

(ert-deftest hyprmacs-session-state-dump-selects-window-for-selected-client-buffer ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (unwind-protect
      (progn
        (hyprmacs-session-fake-receive
         "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-18T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xbbb\",\"title\":\"foot-b\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\",\"0xbbb\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"client-control\"}}\n")
        (let ((left (selected-window))
              (right (split-window-right)))
          (set-window-buffer left (hyprmacs-buffer-for-client "0xaaa"))
          (set-window-buffer right (hyprmacs-buffer-for-client "0xbbb"))
          (select-window left)
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-18T12:00:01Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xbbb\",\"title\":\"foot-b\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\",\"0xbbb\"],\"selected_client\":\"0xbbb\",\"input_mode\":\"client-control\"}}\n")
          (should (eq (window-buffer (selected-window))
                      (hyprmacs-buffer-for-client "0xbbb")))))
    (delete-other-windows)
    (hyprmacs-buffer-reset)))

(ert-deftest hyprmacs-session-state-dump-displays-new-managed-client-buffer ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (unwind-protect
      (progn
        (switch-to-buffer (get-buffer-create "*hyprmacs-session-test*"))
        (hyprmacs-session-fake-receive
         "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-05-02T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
        (switch-to-buffer (get-buffer-create "*hyprmacs-session-scratch*"))
        (hyprmacs-session-fake-receive
         "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-05-02T12:00:01Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xbbb\",\"title\":\"foot-b\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\",\"0xbbb\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
        (should (eq (window-buffer (selected-window))
                    (hyprmacs-buffer-for-client "0xbbb"))))
    (delete-other-windows)
    (hyprmacs-buffer-reset)))

(ert-deftest hyprmacs-session-new-managed-client-triggers-immediate-layout-sync ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((hyprmacs-layout-sync-enabled nil)
        (hyprmacs--layout-sync-in-flight nil)
        (hyprmacs--layout-sync-timer nil)
        (sync-count 0))
    (unwind-protect
        (progn
          (switch-to-buffer (get-buffer-create "*hyprmacs-session-test*"))
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-05-02T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
          (setq hyprmacs-session-state
                (plist-put hyprmacs-session-state :connection-status 'connected))
          (setq hyprmacs-layout-sync-enabled t)
          (cl-letf (((symbol-function 'hyprmacs-sync-layout)
                     (lambda (workspace-id silent)
                       (setq sync-count (1+ sync-count))
                       (should (equal workspace-id "1"))
                       (should silent)
                       (should (eq (window-buffer (selected-window))
                                   (hyprmacs-buffer-for-client "0xbbb"))))))
            (hyprmacs-session-fake-receive
             "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-05-02T12:00:01Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xbbb\",\"title\":\"foot-b\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\",\"0xbbb\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n"))
          (should (= sync-count 1)))
      (when (timerp hyprmacs--layout-sync-timer)
        (cancel-timer hyprmacs--layout-sync-timer))
      (delete-other-windows)
      (hyprmacs-buffer-reset))))
