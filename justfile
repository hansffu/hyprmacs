set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default:
  @just --list

build:
  nix build .#hyprmacs-plugin
  emacs -Q --batch -L emacs -L tests/emacs -l bootstrap-tests.el -f ert-run-tests-batch-and-exit

test:
  emacs -Q --batch -L emacs -L tests/emacs -l bootstrap-tests.el -f ert-run-tests-batch-and-exit
  emacs -Q --batch -L emacs -L tests/emacs -l buffer-tests.el -f ert-run-tests-batch-and-exit
  emacs -Q --batch -L emacs -L tests/emacs -l session-tests.el -f ert-run-tests-batch-and-exit
  emacs -Q --batch -L emacs -L tests/emacs -l protocol-tests.el -f ert-run-tests-batch-and-exit
  emacs -Q --batch -L emacs -L tests/emacs -l layout-tests.el -f ert-run-tests-batch-and-exit
  emacs -Q --batch -L emacs -L tests/emacs -l reconnect-tests.el -f ert-run-tests-batch-and-exit
  emacs -Q --batch -L emacs -L tests/emacs -L tests/e2e -l tests/e2e/manifest-tests.el -f ert-run-tests-batch-and-exit

run:
  nix run .#default

load:
  nix run .#hyprmacs-load

e2e:
  nix develop -c bash scripts/hyprmacs-e2e-nested.sh

emacs:
  find emacs -type f -name '*.elc' -delete
  emacs -L emacs --eval "(setq load-prefer-newer t)" --eval "(require 'hyprmacs)"

emacs-test:
  rm -f logs.txt
  find emacs -type f -name '*.elc' -delete
  emacs -Q --batch -L emacs --eval "(setq load-prefer-newer t)" --eval "(require 'hyprmacs)" --eval "(hyprmacs-run-smoke-test \"logs.txt\")"
