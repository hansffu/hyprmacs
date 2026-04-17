(require 'ert)

(add-to-list 'load-path
             (expand-file-name "../../emacs" (file-name-directory load-file-name)))

(ert-deftest hyprmacs-package-loads ()
  (should (require 'hyprmacs nil 'noerror)))

(ert-deftest hyprmacs-exposes-bootstrap-commands ()
  (require 'hyprmacs)
  (should (commandp #'hyprmacs-connect))
  (should (commandp #'hyprmacs-disconnect))
  (should (commandp #'hyprmacs-manage-current-workspace))
  (should (commandp #'hyprmacs-set-input-mode))
  (should (commandp #'hyprmacs-set-emacs-control-mode))
  (should (commandp #'hyprmacs-run-smoke-test))
  (should (commandp #'hyprmacs-run-gui-smoke-test)))
