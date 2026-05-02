# Agent Instructions

This repository uses the implementation plan in `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org` as the primary source of truth for execution status, sequencing, and ongoing decisions.

## Required Workflow

1. Read `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org` before starting work.
2. Execute tasks in plan order unless the user explicitly approves reordering.
3. Update TODO states in that org document as work progresses.
4. Stop at every `WAIT` heading and ask the user for review before continuing.
5. Do not silently skip a `WAIT` checkpoint, even if the next task seems straightforward.
6. After the user approves a `WAIT` checkpoint, update that checkpoint from `WAIT` to `DONE` immediately.

## Plan Maintenance Rules

1. Treat the org plan as a living document, not a static artifact.
2. Record any new decision, scope change, constraint, deviation, or discovered implementation detail in the relevant section of the org plan during the same session in which it is discovered.
3. If the work changes the intended scope, sequencing, architecture, validation steps, or file layout, update the org plan before moving to the next task.
4. If a stable design artifact exists for the affected area, keep it consistent with the org plan. This includes:
   - `docs/architecture/...`
   - `docs/protocol/...`
   - `docs/adr/...`
5. If the org plan and code disagree, reconcile them immediately rather than leaving drift for a later session.

## Execution Expectations

1. Prefer small, reviewable increments that map cleanly to the plan.
2. When starting a task, mark it `NEXT` or `TODO` as appropriate and update completed steps to `DONE`.
3. When blocked, mark the relevant item `BLOCKED` and document the reason in the org plan.
4. When the user changes direction, update the org plan first, then continue implementation.
5. Make a git commit when completing each top-level task, unless the user explicitly says not to.
6. Before ending a session, leave the org plan in a state where the next agent can resume work without reconstructing context from chat history.

## Testing Expectations

1. All user-visible functionality and behavior changes must be covered by the nested E2E harness (`just e2e`).
2. Unit tests are supporting checks only; do not treat unit-test coverage as sufficient for Hyprland behavior because those tests encode assumptions about compositor behavior.
3. For behavior regressions, add or restore a failing E2E assertion first, capture the red artifact path in the org plan, then fix the behavior and capture the green artifact path.
4. Do not weaken or remove E2E assertions to make a run pass unless the org plan records an explicit user-approved contract change and replacement E2E coverage for the new contract.

## Current Starting Point

Resume from the first incomplete task in `docs/superpowers/plans/2026-04-15-hyprmacs-iterative-implementation.org`. At the time this file was added, the intended next step is `Task 0: Lock the prototype contract`.
