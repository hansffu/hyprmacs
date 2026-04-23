(require 'ert)
(require 'subr-x)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(require 'hyprmacs-layout)

(ert-deftest hyprmacs-layout-hyprland-main-frame-origin-prefers-largest-emacs-client ()
  (let ((hyprmacs-layout-use-hyprctl-origin t)
        (hyprmacs-session-state '(:workspace-id "1")))
    (cl-letf (((symbol-function 'emacs-pid)
               (lambda () 1000))
              ((symbol-function 'shell-command-to-string)
               (lambda (_command)
                 (concat
                  "["
                  "{\"pid\":1000,\"class\":\"emacs\",\"floating\":false,\"workspace\":{\"id\":1},\"at\":[10,20],\"size\":[200,100]},"
                  "{\"pid\":1000,\"class\":\"emacs\",\"floating\":false,\"workspace\":{\"id\":1},\"at\":[40,50],\"size\":[800,700]},"
                  "{\"pid\":1000,\"class\":\"foot\",\"floating\":false,\"workspace\":{\"id\":1},\"at\":[1,2],\"size\":[5,6]},"
                  "{\"pid\":1001,\"class\":\"emacs\",\"floating\":false,\"workspace\":{\"id\":1},\"at\":[7,8],\"size\":[900,900]}"
                  "]"))))
      (should (equal (hyprmacs-layout--hyprland-main-frame-origin) '(40 . 50))))))

(ert-deftest hyprmacs-layout-visible-rectangles-from-windows ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-a*"))
        (buffer-b (generate-new-buffer " *hyprmacs-layout-b*")))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))
          (with-current-buffer buffer-b
            (setq-local hyprmacs-client-id "0xbbb"))

          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (window)
                       (pcase window
                         ('w1 buffer-a)
                         ('w2 buffer-b))))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (window)
                       (pcase window
                         ('w1 '(0 0 100 200))
                         ('w2 '(100 0 260 200))))))
            (let ((rectangles (hyprmacs-layout-visible-rectangles '(w1 w2))))
              (should (= (length rectangles) 2))
              (should (equal (alist-get 'client_id (nth 0 rectangles) nil nil #'equal) "0xaaa"))
              (should (= (alist-get 'width (nth 0 rectangles)) 100))
              (should (equal (alist-get 'client_id (nth 1 rectangles) nil nil #'equal) "0xbbb"))
              (should (= (alist-get 'x (nth 1 rectangles)) 100)))))
      (kill-buffer buffer-a)
      (kill-buffer buffer-b))))

(ert-deftest hyprmacs-layout-visible-rectangles-include-frame-offset ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-fa*"))
        (window (selected-window)))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))

          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (_window) buffer-a))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (_window)
                       '(10 20 110 220)))
                    ((symbol-function 'hyprmacs-layout--hyprland-main-frame-origin)
                     (lambda () nil))
                    ((symbol-function 'hyprmacs-layout--frame-inner-origin)
                     (lambda (&optional _frame)
                       '(50 . 70))))
            (let ((rectangle (car (hyprmacs-layout-visible-rectangles (list window)))))
              (should (equal (alist-get 'client_id rectangle nil nil #'equal) "0xaaa"))
              (should (= (alist-get 'x rectangle) 60))
              (should (= (alist-get 'y rectangle) 90))
              (should (= (alist-get 'width rectangle) 100))
              (should (= (alist-get 'height rectangle) 200)))))
      (kill-buffer buffer-a))))

(ert-deftest hyprmacs-layout-window-rectangle-uses-body-edges-not-inside-edges ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-body*"))
        (window (selected-window)))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))
          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (_window) buffer-a))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (_window) '(1 2 101 202)))
                    ((symbol-function 'window-inside-pixel-edges)
                     (lambda (_window) '(1 2 500 600)))
                    ((symbol-function 'hyprmacs-layout--hyprland-main-frame-origin)
                     (lambda () nil))
                    ((symbol-function 'hyprmacs-layout--frame-inner-origin)
                     (lambda (&optional _frame) '(0 . 0))))
            (let ((rectangle (car (hyprmacs-layout-visible-rectangles (list window)))))
              (should (= (alist-get 'width rectangle) 100))
              (should (= (alist-get 'height rectangle) 200)))))
      (kill-buffer buffer-a))))

(ert-deftest hyprmacs-layout-build-payload-includes-visible-hidden-and-mode ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-pa*"))
        (buffer-b (generate-new-buffer " *hyprmacs-layout-pb*")))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))
          (with-current-buffer buffer-b
            (setq-local hyprmacs-client-id "0xbbb"))

          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (window)
                       (pcase window
                         ('w1 buffer-a)
                         ('w2 buffer-b))))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (window)
                       (pcase window
                         ('w1 '(0 0 100 100))
                         ('w2 '(100 0 200 100))))))
            (let* ((payload (hyprmacs-layout-build-payload
                             '("0xaaa" "0xbbb" "0xccc")
                             "0xbbb"
                             'client-control
                             '(w1 w2)))
                   (visible (alist-get 'visible_clients payload nil nil #'equal))
                   (hidden (alist-get 'hidden_clients payload nil nil #'equal)))
              (should (equal (alist-get 'selected_client payload nil nil #'equal) "0xbbb"))
              (should (equal (alist-get 'input_mode payload nil nil #'equal) "client-control"))
              (should (equal visible ["0xaaa" "0xbbb"]))
              (should (equal hidden ["0xccc"]))
              (should (= (length (alist-get 'rectangles payload nil nil #'equal)) 2))
              (should (equal (alist-get 'stacking_order payload nil nil #'equal) visible)))))
      (kill-buffer buffer-a)
      (kill-buffer buffer-b))))

(ert-deftest hyprmacs-layout-build-payload-rejects-overlap ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-oa*"))
        (buffer-b (generate-new-buffer " *hyprmacs-layout-ob*")))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))
          (with-current-buffer buffer-b
            (setq-local hyprmacs-client-id "0xbbb"))

          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (window)
                       (pcase window
                         ('w1 buffer-a)
                         ('w2 buffer-b))))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (_window)
                       '(0 0 100 100))))
            (should-error
             (hyprmacs-layout-build-payload
              '("0xaaa" "0xbbb")
              "0xaaa"
              'emacs-control
              '(w1 w2)))))
      (kill-buffer buffer-a)
      (kill-buffer buffer-b))))

(ert-deftest hyprmacs-layout-build-payload-drops-non-managed-selected-client ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-sa*")))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))

          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (_window) buffer-a))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (_window) '(0 0 200 100))))
            (let ((payload (hyprmacs-layout-build-payload
                            '("0xaaa")
                            "0xemacs"
                            'emacs-control
                            '(w1))))
              (should (null (alist-get 'selected_client payload nil nil #'equal)))
              (should (equal (alist-get 'visible_clients payload nil nil #'equal) ["0xaaa"])))))
      (kill-buffer buffer-a))))

(ert-deftest hyprmacs-layout-build-payload-encodes-empty-collections-as-arrays ()
  (let* ((payload (hyprmacs-layout-build-payload
                   '("0xaaa")
                   "0xaaa"
                   'emacs-control
                   nil))
         (frame (hyprmacs-ipc-encode-message
                 (hyprmacs-ipc-make-envelope "set-layout" "1" payload))))
    (should (string-match-p "\"visible_clients\":\\[\\]" frame))
    (should (string-match-p "\"hidden_clients\":\\[" frame))
    (should (string-match-p "\"rectangles\":\\[\\]" frame))
    (should (string-match-p "\"stacking_order\":\\[\\]" frame))))

(ert-deftest hyprmacs-layout-build-payload-dedupes-duplicate-client-windows ()
  (let ((buffer-a (generate-new-buffer " *hyprmacs-layout-dupe*")))
    (unwind-protect
        (progn
          (with-current-buffer buffer-a
            (setq-local hyprmacs-client-id "0xaaa"))
          (cl-letf (((symbol-function 'window-buffer)
                     (lambda (_window) buffer-a))
                    ((symbol-function 'window-body-pixel-edges)
                     (lambda (window)
                       (if (eq window 'w1)
                           '(0 0 100 100)
                         '(120 0 220 100)))))
            (let* ((payload (hyprmacs-layout-build-payload
                             '("0xaaa")
                             "0xaaa"
                             'emacs-control
                             '(w1 w2)))
                   (visible (alist-get 'visible_clients payload nil nil #'equal))
                   (rectangles (alist-get 'rectangles payload nil nil #'equal)))
              (should (equal visible ["0xaaa"]))
              (should (= (length rectangles) 1)))))
      (kill-buffer buffer-a))))
