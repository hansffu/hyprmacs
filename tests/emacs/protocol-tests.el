(require 'ert)
(require 'json)
(require 'subr-x)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-ipc)
(require 'hyprmacs-session)
(require 'hyprmacs-buffers)

(defun hyprmacs-test--decode (frame)
  "Decode FRAME and return message alist."  
  (json-parse-string (string-trim-right frame)
                     :object-type 'alist
                     :array-type 'list
                     :null-object nil
                     :false-object :json-false))

(ert-deftest hyprmacs-ipc-encodes-manage-workspace-envelope ()
  (let* ((message (hyprmacs-ipc-make-envelope
                   "manage-workspace"
                   "1"
                   '((adopt_existing_clients . t))
                   "2026-04-15T12:00:00Z"))
         (encoded (hyprmacs-ipc-encode-message message))
         (decoded (hyprmacs-test--decode encoded)))
    (should (equal (alist-get 'version decoded) 1))
    (should (equal (alist-get 'type decoded nil nil #'equal) "manage-workspace"))
    (should (equal (alist-get 'workspace_id decoded nil nil #'equal) "1"))
    (should (equal (alist-get 'timestamp decoded nil nil #'equal)
                   "2026-04-15T12:00:00Z"))
    (should (equal
             (alist-get 'adopt_existing_clients
                        (alist-get 'payload decoded)
                        nil nil #'equal)
             t))))

(ert-deftest hyprmacs-ipc-decodes-state-dump-shape ()
  (let* ((fixture "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-15T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xabc123\",\"title\":\"foot\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[],\"selected_client\":null,\"input_mode\":null}}\n")
         (decoded (hyprmacs-ipc-decode-message fixture))
         (payload (alist-get 'payload decoded)))
    (should (equal (alist-get 'type decoded nil nil #'equal) "state-dump"))
    (should (equal (alist-get 'managed payload nil nil #'equal) t))
    (should (equal (alist-get 'controller_connected payload nil nil #'equal) t))
    (should (equal (length (alist-get 'eligible_clients payload nil nil #'equal)) 1))
    (should (equal (alist-get 'managed_clients payload nil nil #'equal) '()))
    (should-not (alist-get 'selected_client payload nil nil #'equal))
    (should-not (alist-get 'input_mode payload nil nil #'equal))))

(ert-deftest hyprmacs-ipc-mode-roundtrip ()
  (should (equal (hyprmacs-ipc-mode-to-wire 'emacs-control) "emacs-control"))
  (should (equal (hyprmacs-ipc-mode-to-wire 'client-control) "client-control"))
  (should-not (hyprmacs-ipc-mode-to-wire 'other-mode))
  (should (eq (hyprmacs-ipc-mode-from-wire "emacs-control") 'emacs-control))
  (should (eq (hyprmacs-ipc-mode-from-wire "client-control") 'client-control))
  (should-not (hyprmacs-ipc-mode-from-wire "other-mode")))

(ert-deftest hyprmacs-session-fake-transport-captures-and-applies-fixtures ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)
  (hyprmacs-session-use-fake-transport)
  (hyprmacs-session-request-state "1")
  (let* ((outbox (hyprmacs-session-fake-outbox))
         (request (hyprmacs-test--decode (car outbox))))
    (should (equal (length outbox) 1))
    (should (equal (alist-get 'type request nil nil #'equal) "request-state"))
    (should (equal (alist-get 'workspace_id request nil nil #'equal) "1")))

  (hyprmacs-session-fake-receive
   "{\"version\":1,\"type\":\"workspace-managed\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-15T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true}}\n")
  (should (eq (plist-get hyprmacs-session-state :managed) t))
  (should (eq (plist-get hyprmacs-session-state :controller-connected) t))

  (hyprmacs-session-fake-receive
   "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-15T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xabc123\",\"title\":\"foot\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xabc123\"],\"selected_client\":\"0xabc123\",\"input_mode\":\"emacs-control\"}}\n")
  (should (equal (plist-get hyprmacs-session-state :workspace-id) "1"))
  (should (equal (plist-get hyprmacs-session-state :last-message-type) "state-dump"))
  (should (equal (plist-get hyprmacs-session-state :managed-clients) '("0xabc123")))
  (should (equal (length (plist-get hyprmacs-session-state :associated-buffers)) 1))
  (should (buffer-live-p (hyprmacs-buffer-for-client "0xabc123")))
  (should (equal (plist-get hyprmacs-session-state :selected-client) "0xabc123"))
  (should (eq (plist-get hyprmacs-session-state :input-mode) 'emacs-control))
  (hyprmacs-buffer-reset))

(ert-deftest hyprmacs-session-state-dump-syncs-managed-buffer-lifecycle ()
  (hyprmacs-session-reset)
  (hyprmacs-buffer-reset)

  (hyprmacs-session-fake-receive
   "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-15T12:00:00Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false},{\"client_id\":\"0xbbb\",\"title\":\"foot-b\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\",\"0xbbb\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
  (should (buffer-live-p (hyprmacs-buffer-for-client "0xaaa")))
  (should (buffer-live-p (hyprmacs-buffer-for-client "0xbbb")))

  (hyprmacs-session-fake-receive
   "{\"version\":1,\"type\":\"state-dump\",\"workspace_id\":\"1\",\"timestamp\":\"2026-04-15T12:00:01Z\",\"payload\":{\"managed\":true,\"controller_connected\":true,\"eligible_clients\":[{\"client_id\":\"0xaaa\",\"title\":\"foot-a\",\"app_id\":\"foot\",\"floating\":false}],\"managed_clients\":[\"0xaaa\"],\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\"}}\n")
  (should (buffer-live-p (hyprmacs-buffer-for-client "0xaaa")))
  (should-not (hyprmacs-buffer-for-client "0xbbb"))
  (hyprmacs-buffer-reset))
