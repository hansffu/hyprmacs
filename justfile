set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default:
  @just --list

build:
  nix build .#hyprmacs-plugin
  emacs -Q --batch -L emacs -L tests/emacs -l bootstrap-tests.el -f ert-run-tests-batch-and-exit

test:
  emacs -Q --batch -L emacs -L tests/emacs -l protocol-tests.el -f ert-run-tests-batch-and-exit

run:
  nix run .#default

load:
  nix run .#hyprmacs-load

emacs:
  emacs -L emacs --eval "(require 'hyprmacs)"
