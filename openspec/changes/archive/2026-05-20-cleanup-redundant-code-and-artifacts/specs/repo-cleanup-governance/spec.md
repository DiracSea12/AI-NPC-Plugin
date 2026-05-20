## ADDED Requirements

### Requirement: Cleanup inventory precedes destructive or structural cleanup
The cleanup workflow MUST create an inventory entry for each candidate before deleting files, moving files, removing code, or changing ownership boundaries.

#### Scenario: Candidate is identified
- **WHEN** a file, directory, function, class, header, test seam, prompt literal, fallback literal, or generated artifact is proposed for cleanup
- **THEN** the inventory records its path or symbol, category, tracked/untracked/ignored/modified/deleted state, ownership/source, evidence, recommended action, explicit approval requirement, risk, and required verification

#### Scenario: Candidate is not yet safe to change
- **WHEN** the owner, purpose, or verification impact of a candidate is unclear
- **THEN** the workflow marks the candidate as investigate or keep-for-now instead of deleting or refactoring it

#### Scenario: Candidate belongs to a dirty or ignored worktree surface
- **WHEN** a cleanup candidate is untracked, ignored, modified, deleted, or located under an ignored policy path such as `openspec/`
- **THEN** the inventory records whether the candidate is user or tool output, whether it needs explicit approval, and whether it needs force-add, archive, ignore, or preserve handling

#### Scenario: Replacement harness wiring is part of the same cleanup story
- **WHEN** a cleanup candidate is part of a harness replacement chain rather than a standalone file
- **THEN** the inventory groups the old harness, the new harness, and the wiring files that connect them so none of the replacement surfaces are omitted from later cleanup or review

#### Scenario: Dirty worktree deletion or move is proposed
- **WHEN** a cleanup step would delete, move, or archive a path that is dirty, untracked, ignored, or already deleted in the worktree
- **THEN** the workflow MUST stop until explicit user approval is recorded and the inventory contains before-and-after `git status --short` evidence for that path group

### Requirement: Generated artifact cleanup preserves useful evidence
The cleanup workflow MUST distinguish generated artifacts from source, configuration, docs, visual automation assets, and useful evidence before removing or ignoring generated output paths.

#### Scenario: Generated output directory is found
- **WHEN** a directory such as test output, build ordering output, intermediate output, saved logs, derived data, or run-state material is classified as generated
- **THEN** the workflow records whether it should be deleted, ignored, archived, summarized, or preserved as evidence

#### Scenario: Generated output contains validation evidence
- **WHEN** generated output is the only current proof for a build, test, or runtime behavior
- **THEN** the workflow preserves a small human-readable evidence pointer before deleting or ignoring the bulky output

### Requirement: Duplicate code cleanup has a single surviving owner
The cleanup workflow MUST identify one surviving owner before removing duplicate headers, duplicate declarations, duplicate schema builders, duplicated constants, or contradictory source surfaces.

#### Scenario: Duplicate declarations are found
- **WHEN** public and private headers or same-name files declare overlapping responsibilities
- **THEN** the workflow selects the correct owner, updates callers to that owner, and removes only the redundant surface after build impact is verified

#### Scenario: Public include-path surface is removed
- **WHEN** cleanup would remove a public forwarding header or another public include-path surface
- **THEN** the workflow records whether the surface is public API, and if it is public or uncertain the removal stops for explicit user approval instead of assuming compatibility

#### Scenario: Duplicate request schema logic is found
- **WHEN** provider-specific code duplicates shared structured-output schema construction
- **THEN** the workflow consolidates request schema ownership without changing the serialized contract unless the task explicitly declares a behavior change

#### Scenario: Compatibility would be added or preserved
- **WHEN** duplicate cleanup would add, preserve, or expand compatibility wrappers, old include-path shims, old fields, old paths, dual-read/write behavior, silent migration, or legacy fallback behavior
- **THEN** the workflow stops and records that explicit user approval is required before implementing that compatibility

### Requirement: Test seams stay narrow and do not replace real acceptance
The cleanup workflow MUST reduce or guard public test seams where safe, while preserving necessary automation coverage and never treating mock, bypass, injected, headless, or static checks as final acceptance for player-visible NPC behavior.

#### Scenario: Public ForTest API is reviewed
- **WHEN** a `ForTest` function, counter, bypass, or test-only hook is exposed through a runtime-facing public header
- **THEN** the workflow records whether it can move to a narrower boundary, be guarded, remain public for automation, or be removed with updated tests, and the only allowed move targets are private test headers, automation-only modules, or build/test-only compile guards

#### Scenario: Player-visible behavior is affected
- **WHEN** cleanup touches NPC dialogue, memory, behavior state, UI, prompts, provider output interpretation, or any feature a player can see, hear, trigger, or perceive
- **THEN** final acceptance requires the real visual editor/game verification chain in addition to any build, static, unit, or headless checks

#### Scenario: Validation harness is replaced or removed
- **WHEN** cleanup touches existing or replacement gameplay/editor verification harness files
- **THEN** the workflow records the old harness, new harness, replacement rationale, and verification evidence before deleting, accepting, or ignoring either side

### Requirement: Runtime text and prompt contracts live in owned templates or policy
Runtime prompt text, output-contract wording, fallback copy, degradation copy, and product-facing default responses MUST live in tracked config, template, settings, DataAsset, or dedicated policy objects rather than provider, adapter, HTTP, DB, or infrastructure logic.

#### Scenario: Runtime text literal is found in source
- **WHEN** cleanup finds a prompt body, output-contract sentence, fallback response, degradation message, or user-facing default literal in source code
- **THEN** the workflow classifies it as already-owned-by-settings/DataAsset/policy, keep-in-policy, move-to-config, move-to-template, or convert-to-symbolic-domain-constant with packaging and load verification

#### Scenario: Text move would add fallback compatibility
- **WHEN** moving text would require old-path compatibility, dual-read behavior, silent migration, or legacy fallback
- **THEN** the workflow stops and records that user approval is required before adding compatibility behavior

### Requirement: Cleanup verification is proportional to behavior impact
Cleanup implementation MUST run verification that matches the risk and behavior impact of each lane.

#### Scenario: Cleanup is source-only and behavior-preserving
- **WHEN** a cleanup task only removes generated output, narrows duplicate declarations, or consolidates behavior-equivalent code
- **THEN** the workflow runs the relevant build and fast contract checks, or records the current blocker with exact process or command evidence

#### Scenario: Provider or request contract changed
- **WHEN** schema, provider, prompt, or fallback placement changes alter serialized request bodies
- **THEN** the workflow stores before and after payloads in a change-local verification artifact directory and compares them before accepting the cleanup

#### Scenario: Cleanup changes player-visible behavior
- **WHEN** a cleanup task changes behavior that can surface in-game
- **THEN** the workflow runs the real visual editor/game verification path in a visible window and records the launch path, observed NPC behavior, dialogue, state changes, exit path, screenshots or log pointers, and why no headless, mock, bypass, or injected-response run was used as final acceptance

#### Scenario: Engine source would be modified
- **WHEN** a cleanup task would require changing files outside the plugin, scripts, config, parser, runtime, docs, or test harness surfaces and into the Unreal Engine source tree
- **THEN** the workflow stops and records that engine source edits are forbidden for this plugin cleanup
