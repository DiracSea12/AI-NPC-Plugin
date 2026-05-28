# Phase 2.9B Start Readiness

Date: 2026-05-27
Change: `evolve-visual-acceptance-dsl-adapters`
Scope: Phase 2.9B tasks 3.7 through 3.10 only.
Verdict: CANDIDATE 2.9B DEVELOPMENT HANDOFF PACKAGE. The actual start decision requires a zero-context reviewer `READY` verdict and is not made by this file.

## Current Progress Baseline

- Active OpenSpec progress after this readiness cleanup: 44/68 tasks complete.
- Local implementation baseline includes:
  - `fd49456` `Finalize phase 2.9A1 visual extension registry`
  - `bd06612` `Finalize phase 2.9A2 visual adapter lifecycle`
- Phase 2.9B implementation tasks remain unchecked:
  - 3.7 fixture schema delta
  - 3.8 `project.action.execute` action schema delta
  - 3.9 typed project observation declaration contract
  - 3.10 staged validation and diagnostics
- This file does not authorize 2.9C door scenario closure, 2.9D full negative matrix, or Phase 2.95 hardening.

## Predecessor Evidence Checked

Phase 2.8d prerequisite evidence is present and readable; artifact directory names still use the historical `phase-2-8c-20260524` label:

- Static/contract transcript: `.artifacts/gates/phase-2-8c-20260524/test-static.log`
- Static result: `Saved/TestLogs/static/static-20260524-042437-b191d32b/result.json`
- Editor automation transcript: `.artifacts/gates/phase-2-8c-20260524/test-editor-context.log`
- Editor automation result: `Saved/TestLogs/editor-automation/editor-automation-20260524-042438-6734076f/result.json`
- UBT build transcript: `.artifacts/gates/phase-2-8c-20260524/ubt-build.log`
- Visible game transcript: `.artifacts/gates/phase-2-8c-20260524/test-game.log`
- Visible suite result: `Saved/TestLogs/visual-game/visual-game-20260524-042521-660c67f8/result.json`
- Visible suite log: `Saved/TestLogs/visual-game/visual-game-20260524-042521-660c67f8-harness/visual-game-suite.log`
- Runtime result evidence:
  - `Saved/TestLogs/visual-game/visual-game-20260524-042521-660c67f8-us1.dialogue-action/runtime-result.json`
  - `Saved/TestLogs/visual-game/visual-game-20260524-042521-660c67f8-us2.perception-behavior/runtime-result.json`
  - `Saved/TestLogs/visual-game/visual-game-20260524-042521-660c67f8-phase27.prompt-only-dialogue-action/runtime-result.json`

Phase 2.9A2 successor baseline evidence is present and readable:

- Static result: `Saved/TestLogs/static/phase29a2-rework9-static/result.json` (`PASS`)
- Build result: `Saved/TestLogs/build/phase29a2-rework9-build/result.json` (`PASS`)
- Final editor automation result: `Saved/TestLogs/editor-automation/phase29a2-final-editor/result.json` (`PASS`)
- Final editor build result: `Saved/TestLogs/build/phase29a2-final-editor-build/result.json` (`PASS`)

These artifacts are predecessor and baseline evidence. They are not proof that 2.9B is implemented.

## 2.9B Complexity Contract

Task type: small-feature

Goal in one sentence: implement the minimum Phase 2.9B fixture/action/observation schema and staged validation needed for project adapter extension without turning the visual harness into a generic adapter platform.

Expected diff shape: bounded positive, concentrated in existing visual test extension, scenario registry/runner, visual test structs, static contract checks, and focused automation tests.

Dynamic implementation budget for this 2.9B subtask:
- Complexity alarm command for implementation review: `python C:\Users\Administrator\.codex\skills\formal-gates\scripts\complexity_gate.py --task-type small-feature --max-net 1800 --max-new-prod-files 0 --max-prod-insertions 1100`.
- Production insertions budget: <= 1100 lines. 1101-1400 lines requires `REVIEW` with shrink-before-grow proof. > 1400 lines is `FAIL/BLOCKED` unless a Budget Expansion Request is approved.
- Net diff budget: <= 1800 lines. 1801-2200 lines requires `REVIEW`; > 2200 lines is `FAIL/BLOCKED` unless approved.
- Test insertion budget: <= 1200 lines, with at most one new test file. Prefer existing automation test files first.
- Script/static-contract insertion budget: <= 250 lines, only in existing scripts.
- Touched production file budget: <= 6 existing C++ production/test-boundary files under `Plugins/AINpc/Source/**` excluding `Private/Tests/**`. More requires `REVIEW`.
- Touched test file budget: <= 3 existing files under `Plugins/AINpc/Source/**/Private/Tests/`, or one new focused test file if reuse would make an existing test file unreadable.
- Touched script file budget: <= 1 existing script file, expected to be `scripts/dev/verify-test-system-contract.ps1`.

Production/script/test file budget:
- New production files: 0.
- New public API files: 0.
- New script files: 0.
- New test files: 0 by default; at most 1 focused automation test file if the worker proves existing test files would become unreadable.
- Existing production/test-boundary files may be edited only where they already own visual scenario schema, adapter extension registry, runtime scenario execution, observation records, diagnostics, or static contract checks.
- A new test/example actor file is not authorized in 2.9B. If a real door/example actor is needed, stop and move that work to 2.9C.

Public API/config budget:
- Public API files: no new public header files.
- Allowed public API growth inside `Plugins/AINpc/Source/AINpcCore/Public/Test/AINpcVisualTestExtension.h` only:
  - `FAINpcVisualFixtureResolveRequest`
  - `FAINpcVisualFixtureResolveResult`
  - `FAINpcVisualActionExecuteRequest`
  - `FAINpcVisualActionExecuteResult`
  - `FAINpcVisualObservationSampleRequest`
  - `FAINpcVisualObservationSampleResult`
  - `FAINpcVisualObservationDeclaration`
  - one narrow virtual behavior method on each existing public adapter interface
- Allowed config/schema growth:
  - fixture fields `actorClass` and `actorTag` for `kind: "existingActor"`
  - step type `project.action.execute`
  - payload fields `adapterId`, `actionName`, `targetRef`
  - project observation name shape `project.<domain>.<name>` plus declaration-aware validation
- Forbidden config/API growth: Blueprint adapter type, public character driver, event seam public API, generic resolver strategy, object reference fixture, soft object path fixture, component tag, GameplayTag, cross-map reference, inventory/quest/combat/perception categories.

New subsystem budget: 0. Reuse the existing visual extension registry, scenario registry, scenario runtime/runner, observation store, diagnostics carrier, and static contract script.

Allowed new concepts:
- existing-actor fixture resolver request/result
- project action execute request/result
- project observation sample request/result
- typed observation declaration
- fixed per-run target ref `fixture.actor`
- staged validation labels: `SchemaParse`, `ExtensionDeclaration`, `RuntimeStartup`, `StepExecution`, `FinalAssertion`

Forbidden concepts:
- `Manager`, `Service`, `Policy`, `Cache`, `Report`, `Evidence`, `Orchestrator`, or new framework-like classes
- any new generic `Provider` framework/class outside the already-budgeted `ObservationProvider` adapter category
- new global mutable runtime state outside the existing extension descriptor registry bookkeeping
- module-level actor refs, observation stores, adapter instances, or fixture refs
- load fallback for actor classes
- subclass/`IsA`/`IsChildOf` actor matching
- test-id, door, or project-domain branches in the core parser/runner
- PowerShell-owned project observation whitelist or door business logic
- action adapter writing final-success observations
- old schema compatibility, dual-read, silent migration, or fallback

Existing structures to reuse first:
- `Plugins/AINpc/Source/AINpcCore/Public/Test/AINpcVisualTest.h`
- `Plugins/AINpc/Source/AINpcCore/Public/Test/AINpcVisualTestExtension.h`
- `Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcVisualTestRegistry.cpp`
- `Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcDataDrivenVisualScenarioTest.cpp`
- `Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcVisualTestExtension.cpp`
- `Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcVisualTestExtensionInternal.h`
- existing `FAINpcVisualTestStepDiagnostic`
- existing visual observation store/source metadata
- `scripts/dev/verify-test-system-contract.ps1`
- existing automation test files under `Plugins/AINpc/Source/AINpcCore/Private/Tests/`

Expected baseline file movement:
- 3.7 fixture schema: `AINpcVisualTest.h`, `AINpcVisualTestRegistry.cpp`, `AINpcDataDrivenVisualScenarioTest.cpp`, focused parser/runtime tests.
- 3.8 project action step: `AINpcVisualTest.h`, `AINpcVisualTestExtension.h`, `AINpcDataDrivenVisualScenarioTest.cpp`, focused action adapter tests.
- 3.9 typed observation declaration: `AINpcVisualTestExtension.h`, `AINpcVisualTestExtension.cpp`, `AINpcVisualTestRegistry.cpp`, observation contract tests.
- 3.10 staged diagnostics/static contract: `AINpcVisualTest.h`, `AINpcVisualTestRegistry.cpp`, `AINpcDataDrivenVisualScenarioTest.cpp`, `verify-test-system-contract.ps1`, focused diagnostics/static tests.

2.9B public API contract table:

| Type/member | Exact contract |
| --- | --- |
| `FAINpcVisualFixtureResolveRequest` | Fields: `UWorld* World`, `FString TestId`, `FString RunId`, `FName AdapterId`, `FString FixtureKind`, `FString ActorClass`, `FString ActorTag`, `FString TargetRef`. |
| `FAINpcVisualFixtureResolveResult` | Fields: `bool bSuccess`, `TWeakObjectPtr<AActor> Actor`, `FString TargetRef`, `FString Diagnostic`. No observation writer, no result writer, no fixture map. |
| `IAINpcVisualFixtureResolverAdapter::ResolveFixture` | Exact signature: `virtual FAINpcVisualFixtureResolveResult ResolveFixture(const FAINpcVisualFixtureResolveRequest& Request) = 0;`. |
| `FAINpcVisualActionExecuteRequest` | Fields: `FString TestId`, `FString RunId`, `int32 StepIndex`, `FName AdapterId`, `FString ActionName`, `FString TargetRef`, `TWeakObjectPtr<AActor> TargetActor`. |
| `FAINpcVisualActionExecuteResult` | Fields: `bool bAccepted`, `bool bSucceeded`, `FString Diagnostic`, `FString FailureReason`. No observation record/writer. |
| `IAINpcVisualActionAdapter::ExecuteAction` | Exact signature: `virtual FAINpcVisualActionExecuteResult ExecuteAction(const FAINpcVisualActionExecuteRequest& Request) = 0;`. |
| `FAINpcVisualObservationSampleRequest` | Fields: `FString TestId`, `FString RunId`, `FName AdapterId`, `FString ObservationName`, `TWeakObjectPtr<AActor> SourceActor`. |
| `FAINpcVisualObservationSampleResult` | Fields: `bool bSuccess`, `FAINpcVisualObservationRecord Observation`, `FString Diagnostic`, `FString FailureReason`. |
| `IAINpcVisualObservationProviderAdapter::SampleObservation` | Exact signature: `virtual FAINpcVisualObservationSampleResult SampleObservation(const FAINpcVisualObservationSampleRequest& Request) = 0;`. |
| `FAINpcVisualObservationDeclaration` | Fields: `FString ObservationName`, `EAINpcVisualObservationValueType ValueType`, `FString SourceKind`, `FString SamplingMethod`, `FString Capability`, `bool bRequiresSourceObjectPath`, `bool bRequiresSourceClass`. |
| `FAINpcVisualAdapterDescriptor::ObservationDeclarations` | Must change from `TArray<FString>` to `TArray<FAINpcVisualObservationDeclaration>`. Provider id remains descriptor `AdapterId`; no second provider id field. |

Forbidden public API additions: new public header file, UObject-owning adapter interface, Blueprint-exposed adapter type, public character driver, public event seam, generic capability matcher, generic fixture resolver strategy object.

Diagnostic field budget:
- Reuse existing `FAINpcVisualTestStepDiagnostic` fields first: `StepIndex`, `StepType`, `Status`, `FailureReason`, `FailureCategory`, `ObservationName`, `SourceKind`, `SourceId`.
- Allowed new diagnostic fields on that existing carrier only: `Stage`, `AdapterCategory`, `AdapterId`, `OwnerModuleName`, `ActorClass`, `ActorTag`, `TargetRef`, `ActionName`, `FieldName`, `Capability`.
- These fields are "available field" diagnostics: a row only fills fields known at that stage. Do not add a new report layer, evidence object, per-stage diagnostic type, or JSON side-channel to make every row look symmetrical.

2.9B diagnostic field matrix:

| Stage | Required fields | Optional when available | Unavailable / must not fake |
| --- | --- | --- | --- |
| `SchemaParse` | `Stage`, `TestId`, `FailureReason`, `FieldName` | `StepIndex`, `StepType`, `AdapterId`, `ActorClass`, `ActorTag`, `TargetRef`, `ActionName`, `ObservationName` | `AdapterCategory`, `OwnerModuleName`, runtime source fields. |
| `ExtensionDeclaration` | `Stage`, `TestId`, `FailureReason`, `AdapterCategory`, `AdapterId` or `ObservationName` | `OwnerModuleName`, `Capability`, `FieldName`, `ActionName`, `SourceKind`, `SourceId` | runtime actor refs, runtime target actor identity. |
| `RuntimeStartup` | `Stage`, `TestId`, `FailureReason` | `AdapterCategory`, `AdapterId`, `OwnerModuleName`, `ActorClass`, `ActorTag`, `TargetRef`, `ObservationName` | action attempt result fields. |
| `StepExecution` | `Stage`, `TestId`, `StepIndex`, `StepType`, `FailureReason` | `AdapterCategory`, `AdapterId`, `OwnerModuleName`, `ActionName`, `TargetRef`, `Capability` | final assertion-only source facts unless produced by the step. |
| `FinalAssertion` | `Stage`, `TestId`, `FailureReason`, `ObservationName` | `StepIndex`, `SourceKind`, `SourceId`, `AdapterId`, `Capability`, `FieldName` | action acceptance as final success. |

2.9B static-contract script delta:
- `verify-test-system-contract.ps1` may keep hardcoded 2.8 built-in ids, built-in fixture kinds and built-in observation names as checks for existing built-in scenarios.
- It must not add `project.door.isOpen`, `project.door.action`, `project.door.fixture`, `Interact`, or any `project.<domain>.*` value to a PowerShell-owned validity whitelist.
- It may source-scan C++ for the authoritative 2.9B contract: presence of `project.action.execute`, `existingActor`, typed declaration struct, project observation shape helper, and absence of hardcoded project-domain validity branches in core runner/parser.
- It may reject known forbidden patterns: project observation whitelist in PowerShell, door-state business logic in scripts, test-id-specific runner/script branches, Blueprint adapter surface, public character driver, generic resolver strategy, compatibility fallback.
- It must leave project observation validity to C++ shape validation plus descriptor declaration validation.

Project identifier placement rule:
- `project.door.fixture`, `project.door.action`, `project.door.isOpen`, `projectAction.doorInteract`, `observation.project.door.isOpen`, and `Interact` may appear in focused tests, descriptors, QA cases, docs, and example data.
- They must not be added as built-in parser constants, built-in known observation names, core runner branches, PowerShell validity whitelist entries, or static script business logic.

Capability validation rule:
- Extension declaration validation for fixture checks that `adapterId` resolves to a `FixtureResolver` and that the descriptor `Capabilities` contains `existingActor.classTag`; wrong category, unregistered adapter id, or missing `existingActor.classTag` fails at `ExtensionDeclaration`.
- 2.9B does not infer a capability from `actionName`.
- `project.action.execute` schema validation only checks payload shape and non-empty `adapterId`/`actionName`.
- Extension declaration validation for action checks that `adapterId` resolves to an `ActionAdapter`. 2.9B adds no scenario field or descriptor field that expresses required action capability, so core must not perform project action capability matching beyond descriptor registration requiring non-empty capabilities.
- Unsupported `actionName` semantics are reported by the project action adapter during `StepExecution`.
- Observation declaration validation checks that each `FAINpcVisualObservationDeclaration.Capability` is non-empty and is present exactly in the owning `ObservationProvider` descriptor's `Capabilities`.
- `projectAction.doorInteract` remains descriptor capability sample data for the focused action adapter tests; it is not a required-capability input and not a matcher key in 2.9B.

2.9B final assertion boundary:
- 2.9B may use `project.door.isOpen` as focused descriptor/test data to prove typed declaration and state-read final assertion mechanics.
- The positive final assertion must stay inside an existing automation/test boundary with a test-owned actor/object/state source. It must not add a visible door actor, new map, new scenario entry, or user-facing door example.
- During `FinalAssertion`, after step execution and before expected-value comparison, the runner calls `SampleObservation` with `SourceActor` set to the per-run `fixture.actor` weak actor ref resolved during `RuntimeStartup`; if `fixture.actor` is not bound or is no longer valid, final assertion fails and the observation provider is not allowed to invent another source.
- A real visible door actor/world-state scenario belongs to 2.9C.

Expansion evidence required:
- If 2.9B cannot be completed without a new public file, new adapter category, new project actor/example, new runner branch, new registry/cache, or new diagnostic/report layer, stop and submit a Budget Expansion Request.
- The request must show what was deleted, reused, narrowed, or downgraded first.
- Without explicit approval, the change is blocked.

Stop triggers:
- Need to implement a door actor or visible door scenario.
- Need to expose public character driver.
- Need to create a generic actor resolver or multi-strategy resolver.
- Need to add project-domain strings to core parser/runner validity rules.
- Need to let action adapter success satisfy final observation.
- Need to maintain project observation validity in PowerShell.
- Need to edit engine source.
- Need to claim final visible game acceptance for 2.9B.
- Need to cover scenario end/world teardown/full stale-owner matrix in 2.9B instead of the limited factory/current-owner checks.

## Required Handoff Inputs

A 2.9B development handoff must include:

- This file.
- `proposal.md`
- `design.md`
- `tasks.md`
- All specs under `specs/`
- `qa-phase-2-9b-cases.md`
- The predecessor evidence paths listed above.
- The current diff or repo pack manifest used for the worker context.

The handoff must say explicitly: implement tasks 3.7 through 3.10 only, update checkboxes only after implementation and self-test, and stop on any budget expansion trigger.

## QA Case Baseline

The case design artifact is `qa-phase-2-9b-cases.md`.

It covers:

- exact native class + actor tag fixture success
- invalid class path, Blueprint path, short name, valid-shaped unresolved native class, empty tag, zero match, multiple match, subclass-only match, pending-kill/invalid actor
- valid `project.action.execute`
- fixture adapter declaration validation for unregistered id, wrong category, and missing `existingActor.classTag`
- forbidden action payload fields
- invalid or unresolved target refs
- action success without final state-read observation
- missing/empty action fields
- wrong action adapter category, plus a positive control that a valid `ActionAdapter` descriptor passes extension declaration without actionName capability matching
- observation declaration capability missing from descriptor capabilities
- unsupported action name reported by the action adapter, without generic actionName-to-capability matching
- typed observation declaration
- malformed `project.<domain>.<name>` observation shape failure at `SchemaParse`
- undeclared project observation failure before startup
- capability/name confusion
- valid state-read observation final assertion using `SampleObservation` with `SourceActor == fixture.actor`
- incomplete declaration fields
- lifecycle owner/factory failures limited to factory creation and current owner availability, not scenario end/world teardown/full stale matrix
- staged diagnostics matrix
- PowerShell not owning a project observation whitelist, and covering the stated static contract scan surface from this file

This is pre-implementation QA Design input only. It is not QA Execution PASS.

## Start Decision Boundary

This package is only a candidate handoff. 2.9B may be handed off for development only after an independent zero-context reviewer returns `READY` on the current bundle. Development still needs worker self-test, main-agent review, and the normal verification loop before any task checkbox 3.7-3.10 is marked complete.
