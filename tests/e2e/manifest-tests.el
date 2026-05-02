;;; manifest-tests.el --- E2E manifest integrity tests  -*- lexical-binding: t; -*-

(require 'ert)
(require 'cl-lib)
(require 'subr-x)

(defconst hyprmacs-e2e-tests--directory
  (file-name-directory (or load-file-name buffer-file-name))
  "Directory containing E2E test support files.")

(load-file (expand-file-name "full-flow.el" hyprmacs-e2e-tests--directory))

(defun hyprmacs-e2e-tests--source-assertion-ids ()
  "Return assertion IDs statically present in full-flow.el."
  (let (ids)
    (with-temp-buffer
      (insert-file-contents (expand-file-name "full-flow.el" hyprmacs-e2e-tests--directory))
      (goto-char (point-min))
      (cl-labels
          ((walk (form)
             (when (consp form)
               (when (eq (car form) 'hyprmacs--e2e-assert)
                 (let ((fmt (nth 3 form)))
                   (when (stringp fmt)
                     (push (hyprmacs-e2e--assertion-id fmt) ids))))
               (let ((tail form))
                 (while (consp tail)
                   (walk (car tail))
                   (setq tail (cdr tail)))
                 (when tail
                   (walk tail))))))
        (condition-case nil
            (while t
              (walk (read (current-buffer))))
          (end-of-file nil))))
    (sort (delete-dups ids) #'string<)))

(ert-deftest hyprmacs-e2e-required-manifest-matches-source-assertions ()
  (let ((required (copy-sequence (hyprmacs-e2e--required-assertion-ids)))
        (source (copy-sequence (hyprmacs-e2e-tests--source-assertion-ids))))
    (should (equal (sort required #'string<)
                   (sort source #'string<)))))

(ert-deftest hyprmacs-e2e-required-manifest-fails-when-assertion-missing ()
  (let ((hyprmacs-e2e--observed-assertion-ids '("observed"))
        (hyprmacs-e2e-required-assertion-file (make-temp-file "hyprmacs-e2e-required")))
    (unwind-protect
        (progn
          (with-temp-file hyprmacs-e2e-required-assertion-file
            (insert "observed\nmissing\n"))
          (should-error (hyprmacs-e2e--verify-required-assertions (make-temp-file "hyprmacs-e2e-log"))))
      (delete-file hyprmacs-e2e-required-assertion-file))))

(ert-deftest hyprmacs-e2e-assertion-records-stable-id ()
  (let ((hyprmacs-e2e--observed-assertion-ids nil)
        (log-file (make-temp-file "hyprmacs-e2e-assert")))
    (unwind-protect
        (progn
          (hyprmacs--e2e-assert t log-file "example assertion for %s" "client")
          (should (equal hyprmacs-e2e--observed-assertion-ids
                         '("example-assertion-for-value")))
          (with-temp-buffer
            (insert-file-contents log-file)
            (should (string-match-p "ok \\[example-assertion-for-value\\]" (buffer-string)))))
      (delete-file log-file))))

;;; manifest-tests.el ends here
