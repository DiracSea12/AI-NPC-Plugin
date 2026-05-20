## Context

The repository is already carrying visible cleanup pressure across source, tests, generated artifacts, and docs/run-state material. Current scan points include:

- Generated/output directories by current size:
  - `Intermediate/`: 17003 files, about 19314.10 MB, ignored generated output.
  - `Saved/`: 1939 files, about 3035.81 MB, ignored generated output.
  - `TestResults/`: 509 files, about 2671.30 MB, untracked generated test/output material.
  - `Binaries/`: 18 files, about 2496.98 MB, ignored generated output.
  - `Plugins/AINpc/Binaries/`: 11 files, about 293.18 MB, ignored plugin generated output.
  - `Plugins/AINpc/Intermediate/`: 241 files, about 48.34 MB, ignored plugin generated output.
  - `DerivedDataCache/`: ignored generated cache output that should stay out of cleanup scope unless explicitly targeted.
  - `Plugins/AINpc/*.log`: ignored plugin-root logs that may contain useful evidence and should be classified before deletion.
  - `docs/ralph/`: 31 files, about 3.61 MB, mixed workflow state/artifact material; only `docs/ralph/test-matrix.json` is tracked, `docs/ralph/state/` is untracked, and `docs/ralph/image/` is ignored.
  - `Build/`: 2 files, about 0.87 MB, untracked `Build/Windows/FileOpenOrder/` output.
- `openspec/` is ignored by `.gitignore`, so this change's files need explicit handling if they must be committed or packaged as evidence.
- Current validation harness churn needs explicit inventory treatment:
  - `Plugins/AINpc/Source/AINpcCore/Private/Tests/AINpcGauntletTestController.cpp` is deleted in the worktree.
  - `Plugins/AINpc/Source/AINpcCore/Private/Tests/AINpcGauntletTestController.h` is deleted in the worktree.
  - `VerifierHostAutomation.uproject`, `Content/Maps/AINpcTestMap.umap`, `Plugins/AINpc/Source/AINpcCore/Private/Test/`, `Plugins/AINpc/Source/AINpcCore/Public/Test/`, and `Source/VerifierHost.Target.cs` are untracked new harness-related surfaces.
  - `Config/DefaultEngine.ini`, `Plugins/AINpc/AINpc.uplugin`, `Plugins/AINpc/Source/AINpcCore/AINpcCore.Build.cs`, `Plugins/AINpc/Source/AINpcEditor/AINpcEditor.Build.cs`, `VerifierHost.uproject`, and `scripts/dev/build-editor.ps1` are existing wiring files that form the replacement harness chain and must be inventoried together with the harness files themselves.
- duplicate header names:
  - `Plugins/AINpc/Source/AINpcCore/Public/AINpcComponent.h`
  - `Plugins/AINpc/Source/AINpcCore/Public/Components/AINpcComponent.h`
  - `Plugins/AINpc/Source/AINpcCore/Public/Components/AINpcProviderConfigResolver.h`
  - `Plugins/AINpc/Source/AINpcCore/Private/Components/AINpcProviderConfigResolver.h`
- hotspot files by current nonblank line count include `AINpcReliabilityAutomationTests.cpp` (2039), `AnthropicProvider.cpp` (808), `SmartObjectBridgeContext.cpp` (633), `NpcDialogueBubbleWidgetAutomationTests.cpp` (592), `OpenAIProvider.cpp` (586), `LLMResponseParser.cpp` (492), and `AINpcComponent.cpp` (433).
- repeated structured-output schema construction exists between `StructuredOutputSchemaHelpers.h` and `AnthropicProvider.cpp`.
- public `ForTest` APIs and counters exist in runtime-facing headers and need a placement review before any removal.
- runtime default/fallback/product-facing text still exists in source, including settings/DataAsset-owned fallback templates, degradation copy, and `Action.DefaultTalk` literals; this lane must classify ownership before deciding whether to move anything.

The repo rules require runtime prompt text and output-contract wording to live in tracked config/template files, generated Unreal artifacts to stay out of git, and player-visible NPC behavior to be verified by real visual editor/game automation rather than headless or mock-only checks.

## Goals / Non-Goals

**Goals:**

- Produce an implementation-ready cleanup plan that classifies redundant files and code before changing or deleting them.
- Separate generated artifacts, useful evidence, source code, test harnesses, docs, and run-state material into explicit categories.
- Remove or route duplicate code only after proving the surviving owner and verifying include/build impact.
- Reduce test seam leakage where safe without weakening coverage or deleting required automation hooks.
- Move or centralize runtime text/prompt/default contracts into the correct config/template/policy layer.
- Preserve the real visual editor/game verification path for player-visible NPC behavior.

**Non-Goals:**

- Do not delete artifacts or code as part of this proposal.
- Do not modify engine source.
- Do not add legacy compatibility, migration fallback, or dual-read behavior.
- Do not remove visual automation harnesses merely because they look like test-only code.
- Do not claim final acceptance for NPC/player-visible behavior using static checks, headless runs, mock providers, bypasses, or injected `FLLMResponse`.
- Do not refactor large files for aesthetics unless the task ties the change to a concrete redundancy, ownership, or verification problem.

## Decisions

1. Use an inventory ledger as the first implementation artifact.

   Each cleanup candidate must record path, category, git state, ignore state, ownership/source, evidence, recommended action, approval requirement, risk, and verification. Replacement harness wiring files must be grouped with the old and new harness surfaces they connect. This is safer than deleting by directory name because current untracked/generated-looking paths may contain useful evidence, user-created files, or parallel harness work.

2. Split cleanup into five lanes.

   - Artifact hygiene: generated outputs and run-state material.
   - Duplicate source surfaces: headers, helper implementations, and contradictory declarations.
   - Test seam placement: public `ForTest` APIs, test counters, and bypass hooks.
   - Runtime text placement: prompt/default/fallback/output-contract literals, after classifying whether the text already lives in an allowed settings, DataAsset, template, or policy layer.
   - Hotspot triage: large files that are cleanup candidates only when a lane exposes duplicated or misplaced responsibility.

   This keeps the work coherent while avoiding a single broad refactor that changes behavior accidentally.

3. Treat source cleanup as behavior-preserving by default.

   If a cleanup task changes NPC dialogue, memory, behavior state, UI, or any player-visible result, it must be promoted to a behavior-affecting task and use the required visual verification chain.

4. Prefer ownership consolidation over compatibility.

   When duplicate declarations, harness replacements, schema builders, artifact policies, or runtime text ownership conflict, choose one owner and update callers. Do not add compatibility wrappers, old-path fallbacks, dual behavior, dual-read/write paths, silent migration, or legacy behavior preservation unless the user explicitly approves.

5. Keep final deletion separate from classification.

   The first pass may recommend delete, ignore, archive, move, or keep. Actual deletion happens only after the ledger has enough evidence and the task identifies the verification needed for that path.

6. Write verification evidence into a change-local directory before closeout.

   Provider/request contract checks should leave behind before/after serialized payloads, and any visual verification should leave behind launch-path and observation notes under the cleanup change directory so review and handoff do not depend on memory.

## Risks / Trade-offs

- Removing generated outputs could discard useful failure evidence -> preserve short textual pointers or explicit evidence copies before deletion.
- Cleaning while the worktree is dirty could delete user or parallel-agent work -> record tracked/untracked/ignored/modified/deleted state and approval requirement before action.
- Replacing validation harnesses could weaken real runtime proof -> inventory old and new harness surfaces together and require replacement evidence before deleting either side.
- Tightening public test seams could break existing automation -> update tests in the same task and keep necessary hooks behind the narrowest viable boundary.
- Consolidating duplicate provider/schema logic could change request JSON shape -> compare serialized request bodies before/after and run provider contract checks.
- Moving runtime text could change packaging behavior -> first classify whether settings/DataAsset/policy ownership is already valid, then add runtime dependency or config-load checks for any new template/config file.
- Large-file cleanup can become an unrelated refactor -> require each edit to map to an inventory item and a verification line.
- Visual verification is slower than headless checks -> only require it for player-visible behavior changes, but never present headless/mock checks as final gameplay acceptance.
