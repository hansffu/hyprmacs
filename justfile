set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default:
  @just --list

build:
  nix build .#hyprmacs-plugin
  emacs -Q --batch -L emacs -L tests/emacs -l bootstrap-tests.el -f ert-run-tests-batch-and-exit

run:
  nix run .#default

load:
  nix run .#hyprmacs-load
