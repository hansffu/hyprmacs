;;; hyprmacs.el --- Hyprland workspace integration bootstrap  -*- lexical-binding: t; -*-

;; Author: Hans Fredrik Furholt
;; Version: 0.1.0
;; Package-Requires: ((emacs "30.1"))

;;; Commentary:

;; Task 1 bootstrap entrypoint for the hyprmacs package.

;;; Code:

(defconst hyprmacs-version "0.1.0"
  "Current hyprmacs bootstrap version.")

(defun hyprmacs-connect ()
  "Placeholder command for connecting to the hyprmacs plugin."
  (interactive)
  (message "hyprmacs: connect not implemented yet"))

(defun hyprmacs-disconnect ()
  "Placeholder command for disconnecting from the hyprmacs plugin."
  (interactive)
  (message "hyprmacs: disconnect not implemented yet"))

(defun hyprmacs-manage-current-workspace ()
  "Placeholder command for marking the current workspace as managed."
  (interactive)
  (message "hyprmacs: manage-current-workspace not implemented yet"))

(provide 'hyprmacs)

;;; hyprmacs.el ends here
