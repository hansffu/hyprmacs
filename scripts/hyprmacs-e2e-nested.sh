#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

ts="$(date +%Y%m%d-%H%M%S)"
run_dir="${HYPRMACS_E2E_ARTIFACT_DIR:-$repo_root/artifacts/e2e/$ts}"
mkdir -p "$run_dir"

unit_log="$run_dir/unit-tests.log"
hyprland_log="$run_dir/hyprland.log"
emacs_daemon_log="$run_dir/emacs-daemon.log"
emacs_eval_log="$run_dir/emacs-eval.log"
session_log="$run_dir/session-e2e.log"
summary_log="$run_dir/summary.log"
layout_payload_log="$run_dir/layout-payloads.log"

nested_pid=""
signature=""

echo "[e2e] artifacts: $run_dir" | tee -a "$summary_log"

cleanup() {
  local rc=$?
  set +e

  if [[ -n "$signature" ]]; then
    HYPRLAND_INSTANCE_SIGNATURE="$signature" hyprctl dispatch exit >/dev/null 2>&1 || true
  fi

  if [[ -n "$nested_pid" ]]; then
    pkill -TERM -P "$nested_pid" >/dev/null 2>&1 || true
    kill "$nested_pid" >/dev/null 2>&1 || true
    for _ in $(seq 1 20); do
      if ! kill -0 "$nested_pid" >/dev/null 2>&1; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "$nested_pid" >/dev/null 2>&1; then
      kill -9 "$nested_pid" >/dev/null 2>&1 || true
    fi
    wait "$nested_pid" >/dev/null 2>&1 || true
    nested_pid=""
  fi

  if pgrep -f "emacs --daemon=hyprmacs-e2e" >/dev/null 2>&1; then
    emacsclient -s hyprmacs-e2e --eval '(kill-emacs)' >/dev/null 2>&1 || true
    pkill -f "emacs --daemon=hyprmacs-e2e" >/dev/null 2>&1 || true
  fi

  if [[ $rc -eq 0 ]]; then
    echo "[e2e] PASS (artifacts: $run_dir)" | tee -a "$summary_log"
  else
    echo "[e2e] FAIL (artifacts: $run_dir)" | tee -a "$summary_log"
  fi
  exit $rc
}
trap cleanup EXIT INT TERM

run_ert_suite() {
  local file="$1"
  emacs -Q --batch -L emacs -L tests/emacs -L tests/e2e -l "$file" -f ert-run-tests-batch-and-exit
}

echo "[e2e] running static/unit suites" | tee -a "$summary_log"
{
  cmake -S plugin -B build/plugin
  cmake --build build/plugin
  ctest --test-dir build/plugin --output-on-failure
  run_ert_suite bootstrap-tests.el
  run_ert_suite buffer-tests.el
  run_ert_suite session-tests.el
  run_ert_suite protocol-tests.el
  run_ert_suite layout-tests.el
  run_ert_suite reconnect-tests.el
  run_ert_suite tests/e2e/manifest-tests.el
} 2>&1 | tee "$unit_log"

echo "[e2e] launching nested hyprland" | tee -a "$summary_log"
nix run .#default >"$hyprland_log" 2>&1 &
nested_pid=$!

for _ in $(seq 1 240); do
  signature="$(awk '/Instance Signature:/ {print $NF; exit}' "$hyprland_log" || true)"
  if [[ -n "$signature" ]]; then
    break
  fi
  sleep 0.25
done

if [[ -z "$signature" ]]; then
  echo "[e2e] failed to detect Hyprland instance signature" | tee -a "$summary_log"
  exit 1
fi

echo "[e2e] instance: $signature" | tee -a "$summary_log"
export HYPRLAND_INSTANCE_SIGNATURE="$signature"

for _ in $(seq 1 120); do
  if [[ -S "/run/user/$(id -u)/hypr/$signature/.socket.sock" ]] && \
     [[ -S "/run/user/$(id -u)/hypr/$signature/hyprmacs-v1.sock" ]]; then
    break
  fi
  sleep 0.25
done

if [[ ! -S "/run/user/$(id -u)/hypr/$signature/hyprmacs-v1.sock" ]]; then
  echo "[e2e] plugin IPC socket did not appear" | tee -a "$summary_log"
  exit 1
fi

# Prime test workspace with clients.
hyprctl dispatch exec foot >/dev/null
hyprctl dispatch exec foot >/dev/null
sleep 1.0

# Start controllable daemon + GUI frame inside nested Hyprland.
emacs --daemon=hyprmacs-e2e >"$emacs_daemon_log" 2>&1
hyprctl dispatch exec "emacsclient -s hyprmacs-e2e -c -F '((name . \"hyprmacs-e2e\"))'" >/dev/null
sleep 1.5

find emacs -type f -name '*.elc' -delete

cat >"$run_dir/e2e-driver.el" <<ELISP
(setq load-prefer-newer t)
(add-to-list 'load-path "$repo_root/emacs")
(load-file "$repo_root/tests/e2e/full-flow.el")
(setq hyprmacs-layout-debug-log-file "$layout_payload_log")
(hyprmacs-run-full-e2e-test "$session_log")
ELISP

emacsclient -s hyprmacs-e2e --eval "(progn (load-file \"$run_dir/e2e-driver.el\") t)" \
  2>&1 | tee "$emacs_eval_log"

echo "[e2e] validating runtime log signals" | tee -a "$summary_log"

grep -q "\\[hyprmacs\\] event: openwindow" "$hyprland_log"
grep -q "\\[hyprmacs\\] event: activewindowv2" "$hyprland_log"
grep -q "\\[hyprmacs\\] ipc recv type=manage-workspace" "$hyprland_log"
grep -q "\\[hyprmacs\\] ipc recv type=set-layout" "$hyprland_log"
grep -q "\\[hyprmacs\\] ipc recv type=unmanage-workspace" "$hyprland_log"
grep -q "result: PASS" "$session_log"

{
  echo "instance-signature: $signature"
  echo "unit-log: $unit_log"
  echo "hyprland-log: $hyprland_log"
  echo "emacs-daemon-log: $emacs_daemon_log"
  echo "emacs-eval-log: $emacs_eval_log"
  echo "session-log: $session_log"
  echo "layout-payload-log: $layout_payload_log"
} | tee -a "$summary_log"
