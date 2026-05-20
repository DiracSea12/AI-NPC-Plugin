# Artifact Evidence Pointers

Change: `cleanup-redundant-code-and-artifacts`
Captured: 2026-05-19

Scope: pointer-only. No generated files were copied, deleted, moved, or archived for this task. These entries preserve where current evidence lives before any later cleanup task decides whether bulky output can be removed or replaced.

## `TestResults/`

- State: untracked generated test/output material.
- Current compact pointer: `TestResults/index.json` reports `reportCreatedOn: 2026.05.18-14.17.54`, `succeeded: 5`, `failed: 0`, and five successful `AINpc.Integration.MockProvider.*` tests.
- Current pipeline pointer: `TestResults/PipelineLogs/ci_20260518-124417.log` reports `Build exit code: 0`, `Tests: 80 total | 80 passed | 0 failed`, `Pass Rate: 100%`, `RESULT: PASS`, and report/log paths under `TestResults/PipelineLogs/`.
- Risk: this directory contains the current compact proof of build/test runs; do not delete, move, or archive it until a later task captures a replacement summary or records explicit user approval.
- Acceptance boundary: this is generated automation evidence only. It is not final visual NPC behavior acceptance.

## `Build/Windows/FileOpenOrder/`

- State: untracked generated Unreal file-open-order output.
- Current pointer: `Build/Windows/FileOpenOrder/EditorOpenOrder.log` and `CookerOpenOrder.log` were last written on 2026-05-19 03:54. A sampled first entry in `EditorOpenOrder.log` points at `G:/UE5/AI-NPC-Plugin/VerifierHostAutomation.uproject`, confirming this is generated build/editor ordering output rather than source.
- Risk: low source risk, but it can explain recent editor/build ordering state. Ignore by policy now; delete only in a later cleanup task with before/after status evidence.

## `docs/ralph/state/`

- State: untracked workflow/run state.
- Current pointer: `docs/ralph/state/artifacts/verdict.json` reports `storyId: RW-1.1`, `runId: 20260518-012838-211`, `mode: soft`, `reason: iteration_complete`, and `RALPH_TEST_RESULT: PASS`.
- Risk: this may be the only current worker/run-state proof for the Ralph workflow. Keep it separate from tracked `docs/ralph/test-matrix.json` and ignored `docs/ralph/image/`.
- Acceptance boundary: Ralph state is workflow evidence only, not product source or final visible gameplay acceptance.

## `Plugins/AINpc/*.log`

- State: ignored plugin-root logs.
- Current pointer: sampled ignored logs include `Plugins/AINpc/build_us2at4.log` and `Plugins/AINpc/G?:UE5AI-NPC-Pluginbuild_us1t12_fix.log` by directory listing. They were not copied into this change.
- Risk: likely transient build/debug output, but preserve or summarize before any later deletion if a run still depends on them.
