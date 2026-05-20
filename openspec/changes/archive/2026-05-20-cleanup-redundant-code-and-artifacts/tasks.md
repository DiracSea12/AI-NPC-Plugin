## 1. Inventory and Classification

- [x] 1.1 Create a cleanup inventory ledger under the change directory with columns for path/symbol, category, tracked/untracked/ignored/modified/deleted state, ownership/source, evidence, recommended action, explicit approval requirement, risk, and required verification.
- [x] 1.2 Populate generated artifact candidates, including `Intermediate/`, `Saved/`, `Binaries/`, `Plugins/AINpc/Binaries/`, `Plugins/AINpc/Intermediate/`, `TestResults/`, `Build/Windows/FileOpenOrder/`, `docs/ralph/`, saved logs, derived data, and other bulky generated paths found by current scan.
- [x] 1.3 Populate duplicate source candidates, including same-name forwarding headers, public/private duplicate provider config resolver headers, duplicate structured-output schema builders, duplicated action/fallback constants, and contradictory declarations.
- [x] 1.4 Populate public test seam candidates, including runtime-facing `ForTest` APIs, counters, bypass hooks, provider request builders, and SmartObject test hooks.
- [x] 1.5 Populate runtime text candidates, including prompt fragments, output-contract wording, fallback response templates, degradation copy, and product-facing default literals, and classify whether each already belongs to settings, DataAsset, template, or policy ownership.
- [x] 1.6 Populate current validation harness churn as one linked inventory group, including the deleted Gauntlet controller files and untracked VerifierHostAutomation project, test map, test module files, and host target file.
- [x] 1.7 Populate current validation harness churn as one linked inventory group, including the deleted Gauntlet controller files, the replacement wiring files (`Config/DefaultEngine.ini`, `Plugins/AINpc/AINpc.uplugin`, `Plugins/AINpc/Source/AINpcCore/AINpcCore.Build.cs`, `Plugins/AINpc/Source/AINpcEditor/AINpcEditor.Build.cs`, `VerifierHost.uproject`, `scripts/dev/build-editor.ps1`), and the untracked VerifierHostAutomation project, test map, test module files, and host target file.
- [x] 1.8 Record that `openspec/` is ignored and note whether this change's OpenSpec files require explicit force-add or another evidence handoff path.

## 2. Artifact Hygiene

- [x] 2.1 Decide delete/ignore/archive/preserve actions for each generated artifact candidate in the inventory before touching files.
- [x] 2.2 Update ignore or cleanup policy for generated outputs that should never be tracked, without hiding real source, config, docs, or visual automation assets.
- [x] 2.3 Preserve small textual evidence pointers for any generated outputs that are currently the only proof of a build, test, or runtime run.
- [x] 2.4 Remove or relocate only those generated artifacts whose inventory entries are marked safe and verified.
- [x] 2.5 For mixed paths such as `docs/ralph/`, separately decide tracked source/docs, untracked run state, and ignored images before any cleanup action.
- [x] 2.6 Do not delete, move, or archive any untracked, ignored, modified, or deleted path until the inventory records explicit user approval and a before/after `git status --short` snapshot for that path group.

## 3. Duplicate Source Cleanup

- [x] 3.1 Resolve the `AINpcProviderConfigResolver.h` public/private duplication by selecting one owner, updating includes, and removing the redundant declaration surface after build impact is checked.
- [x] 3.2 Review `Public/AINpcComponent.h` as a forwarding header and either document why it stays or remove/update includes if it is redundant and safe.
- [x] 3.3 Consolidate structured-output schema construction so OpenAI, Local, and Anthropic request bodies share one schema owner or clearly documented provider-specific adapters.
- [x] 3.4 Audit `Action.DefaultTalk` and related symbolic constants for one canonical owner, without changing behavior unless a behavior-affecting task and verification path are declared.
- [x] 3.5 Stop and request user approval before adding, preserving, or expanding compatibility wrappers, old include-path shims, old field/path fallbacks, dual-read/write behavior, or silent migration in any duplicate source cleanup.

## 4. Test Seam and Runtime Text Placement

- [x] 4.1 Review public `ForTest` APIs and counters in runtime-facing headers and move, guard, keep, or remove each one according to the inventory decision; allowed destinations are private test headers, automation-only modules, or build/test-only compile guards, and otherwise the public surface must stay with rationale.
- [x] 4.2 Update affected automation tests when a test seam moves or narrows, preserving required coverage.
- [x] 4.3 Review the old and new validation harness surfaces together and do not delete a harness or accept a replacement until the inventory records the replacement path and verification evidence.
- [x] 4.4 Move runtime prompt/default/fallback/output-contract text into tracked config, template, settings, DataAsset, or dedicated policy objects only where the inventory marks the source literal as misplaced; keep valid settings/DataAsset/policy text in place unless there is a concrete ownership problem.
- [x] 4.5 Stop and request user approval before adding compatibility paths, dual-read behavior, silent migration, or legacy fallback while moving runtime text.
- [x] 4.6 Add packaging/load verification for any new or moved config/template files.

## 5. Hotspot Triage

- [x] 5.1 Review hotspot files listed in the design doc and map any edits to a concrete inventory item before changing them.
- [x] 5.2 Split or extract only when it removes real duplication, fixes ownership, or reduces misplaced responsibility; do not refactor large files for aesthetics alone.
- [x] 5.3 Keep visual automation and gameplay smoke-test harness code unless the inventory names a safe replacement and required verification.

## 6. Verification and Closeout

- [x] 6.1 Run `git diff --check` on the touched files and record the result in the inventory ledger.
- [x] 6.2 Run `pwsh ./scripts/dev/test-fast.ps1` after meaningful source changes, or record the exact blocker if Live Coding or another active editor state prevents it.
- [x] 6.3 Run provider/request contract checks after schema, provider, prompt, or fallback placement changes, including before/after serialized request-body comparison for affected OpenAI, Local, and Anthropic paths, and store those payloads in a change-local verification artifact directory before acceptance.
- [x] 6.4 Verify no engine source files were changed for this plugin cleanup, or record the exact offending diff and stop.
- [x] 6.5 If any cleanup changes player-visible NPC behavior, dialogue, memory, UI, provider output interpretation, prompts, or state transitions, run the real visual editor/game verification chain in a visible window, record the command or launch path, observed NPC behavior, dialogue, state changes, exit path, screenshots/log pointers, and explicitly reject headless, `UnrealEditor-Cmd`, `-unattended`, NullRHI, mock, bypass, or injected-response runs as final acceptance.
- [x] 6.6 Update the inventory ledger with final action, verification result, and remaining follow-up for every candidate discovered in this change, including candidates intentionally left as keep or investigate.
