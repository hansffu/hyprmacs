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
    (switch-to-buffer (get-buffer-create "*scratch*"))
    (hyprmacs-buffer-reset)))

(ert-deftest hyprmacs-state-dump-displays-newly-managed-client-in-selected-window ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((sync-count 0))
    (cl-letf (((symbol-function 'hyprmacs--schedule-auto-sync-layout)
               (lambda (&rest _) (setq sync-count (1+ sync-count)))))
      (unwind-protect
          (progn
            (switch-to-buffer (get-buffer-create "*before-new-client*"))
            (hyprmacs-session-fake-receive
             "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
            (should (eq (window-buffer (selected-window))
                        (hyprmacs-buffer-for-client "0xaaa")))
            (should (= sync-count 1)))
        (delete-other-windows)
        (switch-to-buffer (get-buffer-create "*scratch*"))
        (when-let ((buf (get-buffer "*before-new-client*")))
          (kill-buffer buf))
        (hyprmacs-buffer-reset)))))

(ert-deftest hyprmacs-state-dump-displays-new-client-added-to-existing-managed-set ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((sync-count 0))
    (cl-letf (((symbol-function 'hyprmacs--schedule-auto-sync-layout)
               (lambda (&rest _) (setq sync-count (1+ sync-count)))))
      (unwind-protect
          (progn
            (hyprmacs-session-fake-receive
             "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xold\",\"title\":\"old\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xold\"],\"selected_client\":\"0xold\",\"input_mode\":\"emacs-control\"}}\n")
            (setq sync-count 0)
            (switch-to-buffer (get-buffer-create "*before-added-client*"))
            (hyprmacs-session-fake-receive
             "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:01Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xold\",\"title\":\"old\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xnew\",\"title\":\"new\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xold\",\"0xnew\"],\"selected_client\":\"0xold\",\"input_mode\":\"emacs-control\"}}\n")
            (should (eq (window-buffer (selected-window))
                        (hyprmacs-buffer-for-client "0xnew")))
            (should (= sync-count 1)))
        (delete-other-windows)
        (switch-to-buffer (get-buffer-create "*scratch*"))
        (when-let ((buf (get-buffer "*before-added-client*")))
          (kill-buffer buf))
        (hyprmacs-buffer-reset)))))

(ert-deftest hyprmacs-state-dump-new-client-display-not-undone-by-selected-old-client ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (unwind-protect
      (progn
        (hyprmacs-session-fake-receive
         "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xold\",\"title\":\"old\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xold\"],\"selected_client\":\"0xold\",\"input_mode\":\"emacs-control\"}}\n")
        (let ((left (selected-window))
              (right (split-window-right)))
          (set-window-buffer left (hyprmacs-buffer-for-client "0xold"))
          (set-window-buffer right (get-buffer-create "*before-selected-old*"))
          (select-window right)
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:01Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xold\",\"title\":\"old\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xnew\",\"title\":\"new\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xold\",\"0xnew\"],\"selected_client\":\"0xold\",\"input_mode\":\"emacs-control\"}}\n")
          (should (eq (selected-window) right))
          (should (eq (window-buffer right)
                      (hyprmacs-buffer-for-client "0xnew")))))
    (delete-other-windows)
    (switch-to-buffer (get-buffer-create "*scratch*"))
    (when-let ((buf (get-buffer "*before-selected-old*")))
      (kill-buffer buf))
    (hyprmacs-buffer-reset)))

(ert-deftest hyprmacs-focus-request-selects-visible-buffer-window ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let* ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1"))
         (left (selected-window))
         (right (split-window-right)))
    (unwind-protect
        (progn
          (set-window-buffer left (get-buffer-create "*left-focus*"))
          (set-window-buffer right buffer)
          (select-window left)
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"client-focus-requested\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"client_id\":\"0xaaa\"}}\n")
          (should (eq (selected-window) right)))
      (delete-other-windows)
      (when-let ((buf (get-buffer "*left-focus*")))
        (kill-buffer buf))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-focus-request-displays-hidden-buffer-in-selected-window ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (delete-other-windows)
  (let ((buffer (hyprmacs-buffer-ensure-for-client "0xaaa" "foot" "shell" "1")))
    (unwind-protect
        (progn
          (switch-to-buffer (get-buffer-create "*focus-source*"))
          (hyprmacs-session-fake-receive
           "{\"version\":1,\"type\":\"client-focus-requested\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"client_id\":\"0xaaa\"}}\n")
          (should (eq (window-buffer (selected-window)) buffer))
          (should (eq (current-buffer) buffer)))
      (delete-other-windows)
      (when-let ((buf (get-buffer "*focus-source*")))
        (kill-buffer buf))
      (hyprmacs-buffer-reset))))

(ert-deftest hyprmacs-focus-request-uses-custom-handler ()
  (hyprmacs-session-reset)
  (let (seen)
    (let ((hyprmacs-focus-request-function
           (lambda (client-id workspace-id payload)
             (setq seen (list client-id workspace-id payload)))))
      (hyprmacs-session-fake-receive
       "{\"version\":1,\"type\":\"client-focus-requested\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-24T12:00:00Z\",\"payload\":{\"client_id\":\"0xaaa\",\"reason\":\"urgent\"}}\n"))
    (should (equal (car seen) "0xaaa"))
    (should (equal (cadr seen) "1"))
    (should (equal (alist-get 'reason (caddr seen) nil nil #'equal) "urgent"))))
