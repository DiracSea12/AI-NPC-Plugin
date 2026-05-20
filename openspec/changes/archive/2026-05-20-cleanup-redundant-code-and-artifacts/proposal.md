## Why

The project currently mixes real source surfaces with generated outputs, test artifacts, duplicate headers, public test seams, and prompt/default text placement debt, which makes cleanup risky without a shared ledger. This change creates a cleanup-first OpenSpec scope so the redundant code and files can be classified, counted, and removed or routed without accidentally deleting useful evidence or weakening real gameplay verification.

## What Changes

1. Add a repository cleanup governance capability for auditing redundant files, generated artifacts, duplicate code paths, and misplaced runtime text.
2. Require a cleanup inventory before deletion or refactor, with category, owner, recommended action, risk, and verification evidence for each candidate.
3. Define cleanup boundaries that preserve visual editor/game verification harnesses and forbid claiming player-visible acceptance from headless, mock, bypass, or static checks.
4. Identify current high-priority cleanup candidates for implementation planning:
   - generated/output clutter such as `Intermediate/`, `Saved/`, `Binaries/`, `TestResults/`, `Build/Windows/FileOpenOrder/`, plugin-local generated outputs, and run-state artifacts under `docs/ralph/`
   - duplicate or contradictory declarations such as public/private provider config resolver headers
   - duplicate structured-output schema construction between provider and shared helper code
   - public `ForTest` seams and counters that may belong behind narrower build/test-only boundaries
   - runtime prompt, fallback, and product-facing default text that must be classified by layer, preserving valid settings/DataAsset/policy ownership while moving misplaced provider or infrastructure literals
   - current verification harness churn, including deleted Gauntlet controller files, the replacement wiring files that make the new harness work (`Config/DefaultEngine.ini`, `Plugins/AINpc/AINpc.uplugin`, `Plugins/AINpc/Source/AINpcCore/AINpcCore.Build.cs`, `Plugins/AINpc/Source/AINpcEditor/AINpcEditor.Build.cs`, `VerifierHost.uproject`, `scripts/dev/build-editor.ps1`), and the untracked `VerifierHostAutomation` project, test map, test module, and host target files
5. Keep this change behavior-preserving unless a later task explicitly calls out a behavior change and its required verification path.

## Capabilities

### New Capabilities
- `repo-cleanup-governance`: inventory-driven cleanup of generated artifacts, redundant code, misplaced text, and public test seams with explicit risk and verification rules.

### Modified Capabilities
- `<none>`

## Impact

- Primary: repository cleanup process, `.gitignore` policy, generated artifact routing, and cleanup documentation under this OpenSpec change.
- Source candidates for later implementation: `Plugins/AINpc/Source/...`, `Config/`, `Source/VerifierHost`, `scripts/dev/`, and docs/run-state paths.
- Verification impact: cleanup-only changes may use build and fast contract checks, but any change touching player-visible NPC behavior, dialogue, memory, state, or visual harnesses must also document and run the real visual editor/game test chain before final acceptance.
- Guardrail impact: no engine source edits, no legacy compatibility expansion in any cleanup lane, no silent deletion of user-owned or currently useful artifacts, and no removal of real automation harnesses without an explicit replacement.
