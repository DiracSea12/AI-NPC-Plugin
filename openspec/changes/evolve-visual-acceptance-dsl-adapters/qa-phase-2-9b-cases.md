# Phase 2.9B QA Cases

Workflow id: `phase-2-9b-20260525`
Scope: tasks 3.7 through 3.10 only.
Mode: pre-implementation QA Design artifact.
Handoff status: required input for Phase 2.9B development handoff alongside `phase-2-9b-start-readiness.md`.

These cases are the pre-implementation candidate set for Phase 2.9B implementation. They do not cover 2.9C door scenario closure or the full 2.9D negative matrix.
This file is not QA Execution PASS, not final verification, and not permission to mark tasks 3.7 through 3.10 complete before implementation and self-test.

Scope boundaries:
- `project.door.*` identifiers in this file are focused automation/test descriptor data for Phase 2.9B schema, descriptor, and state-read mechanics.
- 2.9B must not add a visible door actor, new map, new scenario entry, or final visible door acceptance claim. That work starts in 2.9C.
- Project fixture validation requires a `FixtureResolver` descriptor with capability `existingActor.classTag` for `kind: "existingActor"`.
- Project action validation does not infer capability from `actionName` and has no separate required-action-capability input in 2.9B.
- Project observation required capability comes from `FAINpcVisualObservationDeclaration.Capability` and must appear exactly in the owning `ObservationProvider` descriptor's `Capabilities`.
- Lifecycle cases cover factory creation and current owner availability only. Scenario end, world teardown, and the full stale-owner matrix remain in 2.9D/2.95.

Expected enforcement lanes:
- Parser/schema rows (`P29B-FIXTURE-002`, `P29B-FIXTURE-003`, `P29B-FIXTURE-007`, `P29B-FIXTURE-008`, `P29B-ACTION-002`, `P29B-ACTION-003` literal invalid target refs, `P29B-ACTION-005`, `P29B-OBS-003`) belong in scenario parser/schema tests around `AINpcVisualTestRegistry.cpp`.
- Extension declaration rows (`P29B-FIXTURE-011`, `P29B-ACTION-006`, `P29B-OBS-001`, `P29B-OBS-002`, `P29B-OBS-004`, `P29B-OBS-006`, `P29B-OBS-007`) belong in extension registry/declaration or observation contract tests.
- Runtime startup rows (`P29B-FIXTURE-001`, `P29B-FIXTURE-004`, `P29B-FIXTURE-005`, `P29B-FIXTURE-006`, `P29B-FIXTURE-009`, `P29B-FIXTURE-010`, unresolved `fixture.actor`, `P29B-LIFECYCLE-001`, `P29B-LIFECYCLE-003`) belong in focused runtime automation tests under existing visual test boundaries.
- Step/final rows (`P29B-ACTION-001`, `P29B-ACTION-004`, `P29B-ACTION-007`, `P29B-OBS-005`, `P29B-OBS-008`, `P29B-LIFECYCLE-002`, `P29B-STAGE-001`) belong in focused runtime/automation diagnostics tests.
- Static rows (`P29B-SCRIPT-001`) belong in `scripts/dev/verify-test-system-contract.ps1` execution plus source-scan evidence.

## Cases

### P29B-FIXTURE-001 Valid Existing Actor Fixture

Claim: A project existing-actor fixture can resolve exactly one loaded actor in the current scenario world by exact native class path and non-empty actor tag.
Source: `tasks.md` 3.7; `visual-acceptance-adapters/spec.md` Phase 2.9 project actor fixture; `design.md` Phase 2.9B class lookup.
Stage: `RuntimeStartup`
Action: Run a scenario fixture with `adapterId`, `kind: "existingActor"`, `actorClass: "/Script/<Module>.<ClassName>"`, and a non-empty `actorTag`, with exactly one valid actor in the current world whose `GetClass() == ResolvedClass` and `ActorHasTag(actorTag)`.
Oracle: Fixture startup succeeds and binds only per-run ref `fixture.actor`.
Failure signal: No actor bound, wrong actor bound, subclass accepted, duplicate actor silently selected, or ref name other than `fixture.actor` appears.
Observation point: Runtime diagnostic or fixture result contains available structured fields among stage, test id, adapter category, adapter id, owner module, actor class, actor tag, target ref.
Evidence: Automation/runtime artifact showing success path and no fallback resolver.

### P29B-FIXTURE-002 Reject Invalid Class Path Shapes

Claim: Fixture `actorClass` rejects non-native class references during schema parsing.
Source: `tasks.md` 3.7 and 2.9B runtime收口约束.
Stage: `SchemaParse`
Action: Validate fixture variants using Blueprint generated class path, short class name, soft object path, and object reference.
Oracle: Each variant fails before runtime actor lookup.
Failure signal: Any invalid class shape reaches `RuntimeStartup`, loads a class, or is accepted.
Observation point: Validation diagnostic contains available structured fields among stage, test id, adapter id, field name, `actorClass`.
Evidence: Parser/automation negative rows for each invalid shape.

### P29B-FIXTURE-003 Reject Empty Actor Tag

Claim: `actorTag` is required and must be non-empty.
Source: `tasks.md` 3.7.
Stage: `SchemaParse`
Action: Validate fixture with missing or empty `actorTag`.
Oracle: Schema parse fails.
Failure signal: Empty tag reaches actor enumeration or degenerates into class-only lookup.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter id, field name, actor tag.
Evidence: Parser/automation negative rows.

### P29B-FIXTURE-004 Reject Zero Actor Match

Claim: Runtime startup requires exactly one valid loaded exact-class/tag actor match.
Source: `tasks.md` 3.7.
Stage: `RuntimeStartup`
Action: Run fixture validation with no actor matching both exact class and actor tag.
Oracle: Runtime startup fails.
Failure signal: Startup proceeds, fallback actor is selected, or class/tag is ignored.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter id, actor class, actor tag.
Evidence: Runtime/automation negative row.

### P29B-FIXTURE-005 Reject Multiple Actor Matches

Claim: Runtime startup rejects ambiguous class/tag matches instead of selecting an arbitrary actor.
Source: `tasks.md` 3.7.
Stage: `RuntimeStartup`
Action: Run fixture validation with two valid loaded actors matching exact class and actor tag.
Oracle: Runtime startup fails as ambiguous.
Failure signal: One actor is silently selected.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter id, actor class, actor tag.
Evidence: Runtime/automation negative row.

### P29B-FIXTURE-006 Reject Subclass-Only Match

Claim: Fixture actor lookup uses exact class equality, not subclass matching.
Source: `tasks.md` 3.7; `design.md` Phase 2.9B class lookup.
Stage: `RuntimeStartup`
Action: Provide only an actor whose class is a subclass of the requested native class and has the requested tag.
Oracle: Runtime startup fails.
Failure signal: `IsA`/`IsChildOf` behavior accepts the subclass.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter id, actor class, actor tag.
Evidence: Runtime/automation negative row.

### P29B-FIXTURE-007 Reject Fixture Required Field Failures

Claim: Phase 2.9B existing-actor fixture requires non-empty `adapterId`, `kind: "existingActor"`, native `actorClass`, and non-empty `actorTag`.
Source: `tasks.md` 3.7; `visual-scenario-dsl/spec.md` Phase 2.9B project existing actor fixture schema.
Stage: `SchemaParse`
Action: Validate fixture variants missing `adapterId`, using empty `adapterId`, missing `kind`, using unsupported `kind`, and missing `actorClass`.
Oracle: Each variant fails during schema parsing before extension descriptor lookup or runtime actor enumeration.
Failure signal: Missing or malformed required fields are accepted, defaulted, ignored, or delayed to actor lookup.
Observation point: Validation diagnostic contains available structured fields among stage, test id, field name, adapter id, actor class/tag.
Evidence: Parser/automation negative rows for each required-field variant.

### P29B-FIXTURE-008 Reject Forbidden Fixture Resolver Fields

Claim: Phase 2.9B existing-actor fixture is not a generic actor resolver and rejects every unbudgeted resolver strategy field.
Source: `tasks.md` 3.7; `visual-scenario-dsl/spec.md` Phase 2.9B project existing actor fixture schema.
Stage: `SchemaParse`
Action: Validate fixture variants containing object ref, soft object path, Blueprint generated class path field, component tag, GameplayTag, actor-tag-only resolver shape, cross-map reference, resolver strategy list, or multi-strategy fallback field.
Oracle: Each variant fails during schema parsing; only exact native class plus actor tag remains valid.
Failure signal: Any forbidden field is ignored, accepted, converted into a fallback, or reaches runtime lookup.
Observation point: Validation diagnostic contains available structured fields among stage, test id, adapter id, field name, actor class/tag.
Evidence: Parser/automation negative rows for each forbidden resolver field group.

### P29B-FIXTURE-009 Reject Invalid Or Pending-Kill Actor Match

Claim: Runtime startup requires the exact class/tag actor match to be loaded, valid, and non-pending-kill.
Source: `tasks.md` 3.7; `visual-acceptance-adapters/spec.md` Phase 2.9 project actor fixture.
Stage: `RuntimeStartup`
Action: Run fixture validation where the only exact class/tag actor candidate is invalid, pending kill, or otherwise not a valid loaded actor in the current scenario world.
Oracle: Runtime startup fails instead of binding the candidate to `fixture.actor`.
Failure signal: Invalid or pending-kill actor is bound, dereferenced, or reported as a successful fixture.
Observation point: Runtime diagnostic contains available structured fields among stage, test id, adapter id, actor class, actor tag, target ref.
Evidence: Runtime/automation negative row for invalid or pending-kill actor exclusion.

### P29B-FIXTURE-010 Reject Valid-Shaped Unresolved Native Class

Claim: A syntactically valid `/Script/<Module>.<ClassName>` fixture class path must still resolve to a currently loaded native class; 2.9B has no load fallback.
Source: `tasks.md` 3.7 and 2.9B runtime收口约束; `design.md` Phase 2.9B class lookup.
Stage: `RuntimeStartup`
Action: Run fixture startup with a valid-shaped native class path that is not a currently loaded native class.
Oracle: Runtime startup fails before actor enumeration or fixture binding, without loading the class, mapping to another class, or falling back to a broader resolver.
Failure signal: `StaticLoadClass`/`LoadObject` fallback is used, a missing class is accepted, or actor lookup proceeds with a guessed/substitute class.
Observation point: Runtime diagnostic contains available structured fields among stage, test id, adapter id, actor class, actor tag.
Evidence: Runtime/automation negative row plus source-scan proof that actor class lookup uses loaded native class resolution only.

### P29B-FIXTURE-011 Reject Bad Fixture Adapter Declaration

Claim: `kind: "existingActor"` can only bind a registered `FixtureResolver` descriptor whose `Capabilities` contains `existingActor.classTag`.
Source: `tasks.md` 3.7 and 3.10; `visual-acceptance-adapters/spec.md` adapter capability validation; `phase-2-9b-start-readiness.md` capability validation rule.
Stage: `ExtensionDeclaration`
Action: Validate fixture descriptors for three negative rows: unregistered fixture `adapterId`, `adapterId` registered only as `ActionAdapter` or `ObservationProvider`, and `FixtureResolver` descriptor missing `existingActor.classTag`.
Oracle: Each row fails during extension declaration before runtime actor lookup, factory creation, or fixture binding.
Failure signal: Missing/wrong descriptor is deferred to `RuntimeStartup`, category mismatch is accepted, or the fixture kind bypasses descriptor capability validation.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category, adapter id, capability, target ref.
Evidence: Extension declaration negative rows for unregistered id, wrong category, and missing fixture capability.

### P29B-ACTION-001 Valid Project Action Step

Claim: `project.action.execute` is a fixed step type with shape-only schema, then descriptor/capability validation and action adapter execution.
Source: `tasks.md` 3.8; `visual-scenario-dsl/spec.md` Phase 2.9B fixed project action step.
Stage: `StepExecution`
Action: Execute a scenario step with non-empty `adapterId`, non-empty `actionName`, and `targetRef: "fixture.actor"` after fixture resolution and descriptor validation.
Oracle: Runner invokes the registered `ActionAdapter` via `FAINpcVisualActionExecuteRequest` and records attempt diagnostic; it does not route through latest LLM intent or SmartObject rejection logic.
Failure signal: Runner uses `action.executeLatestIntent`, SmartObject support, test-id branch, or hardcoded door action branch.
Observation point: Step diagnostic contains available structured fields among stage, test id, step index, step type, adapter id, action name, target ref.
Evidence: Automation/runtime action adapter call proof.

### P29B-ACTION-002 Reject Forbidden Project Action Payload Fields

Claim: `project.action.execute` rejects old action fields and unknown fields during schema parsing.
Source: `tasks.md` 3.8; `visual-acceptance-adapters/spec.md` project action payload old fields.
Stage: `SchemaParse`
Action: Validate payloads containing `allowActionRejection`, `actorRef`, latest-intent fields, SmartObject fields, or unknown fields.
Oracle: Schema parse fails for each forbidden field.
Failure signal: Forbidden fields are ignored, accepted, or passed to runtime.
Observation point: Diagnostic contains available structured fields among stage, test id, step index, step type, adapter id, field name.
Evidence: Parser/automation negative rows.

### P29B-ACTION-003 Reject Invalid Target Ref

Claim: `project.action.execute.payload.targetRef` must be `fixture.actor`.
Source: `tasks.md` 3.8 and 2.9B target ref收口约束.
Stage: `SchemaParse` for literal invalid target ref, or `RuntimeStartup` when `fixture.actor` was not bound.
Action: Validate/run variants using `fixture.door`, `fixture.target`, missing target ref, or `fixture.actor` without successful fixture binding.
Oracle: Invalid literal target refs fail before step execution; unresolved `fixture.actor` fails before action adapter call.
Failure signal: Runner invents a ref, guesses by test id, or passes null actor to action adapter.
Observation point: Diagnostic contains available structured fields among stage, test id, step index, adapter id, target ref.
Evidence: Parser/runtime negative rows.

### P29B-ACTION-004 Action Success Cannot Satisfy Final Observation

Claim: Action adapter accepted/success only records attempt diagnostic and cannot write final success observation.
Source: `tasks.md` 3.8; `visual-observation-assertions/spec.md` action success lacks state-read observation.
Stage: `FinalAssertion`
Action: Use a focused automation action adapter that reports accepted/success while no observation provider records `project.door.isOpen == true`.
Oracle: Step execution may pass, but final assertion fails.
Failure signal: Scenario passes from action success or adapter acceptance alone.
Observation point: Runtime result contains action attempt diagnostic and final assertion failure for missing or invalid `project.door.isOpen`.
Evidence: Runtime/automation negative row.

### P29B-ACTION-005 Reject Missing Or Empty Project Action Fields

Claim: `project.action.execute` requires non-empty `adapterId`, non-empty `actionName`, and `targetRef: "fixture.actor"`.
Source: `tasks.md` 3.8; `visual-scenario-dsl/spec.md` Phase 2.9B project action payload malformed.
Stage: `SchemaParse`
Action: Validate `project.action.execute` payload variants missing `adapterId`, using empty `adapterId`, missing `actionName`, using empty `actionName`, and missing `targetRef`.
Oracle: Each variant fails during schema parsing before descriptor validation or step execution.
Failure signal: Required fields are defaulted, inferred from door examples, ignored, or delayed until action adapter call.
Observation point: Validation diagnostic contains available structured fields among stage, test id, step index, step type, field name, adapter id.
Evidence: Parser/automation negative rows for each missing or empty field.

### P29B-ACTION-006 Reject Wrong Action Adapter Category

Claim: `project.action.execute` can only bind a registered `ActionAdapter` descriptor; 2.9B does not validate a separate required action capability.
Source: `tasks.md` 3.8 and 3.10; `visual-acceptance-adapters/spec.md` Phase 2.9B project action step.
Stage: `ExtensionDeclaration`
Action: Negative row: validate a scenario whose `adapterId` exists only as a fixture resolver or observation provider. Positive control row: validate a scenario whose `adapterId` exists as an `ActionAdapter` with non-empty descriptor capabilities.
Oracle: The negative row fails during extension declaration before runtime startup or step execution. The positive control row passes extension declaration and is not rejected by deriving a required capability from `actionName`.
Failure signal: Category mismatch is accepted, a valid `ActionAdapter` is rejected solely because `actionName` is not treated as a capability, or core derives action validity from `actionName` instead of category and adapter execution semantics.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category, adapter id, action name.
Evidence: Descriptor validation negative rows for wrong category and a positive row showing descriptor capabilities are registration metadata, not actionName matcher input.

### P29B-ACTION-007 Unsupported Action Name Is Adapter Execution Failure

Claim: 2.9B does not implement a generic `actionName` to capability matcher; unsupported action names are adapter execution semantics.
Source: `tasks.md` 3.8 and 3.10; `design.md` Phase 2.9B project action rule.
Stage: `StepExecution`
Action: Invoke a valid `ActionAdapter` descriptor with a non-empty but unsupported `actionName`.
Oracle: The action adapter returns rejected/failed with diagnostic; core does not hardcode `Interact`, derive a capability, or silently treat the action as valid.
Failure signal: Core parser/validator accepts or rejects solely by hardcoded action name, or maps `actionName` to a capability without any 2.9B contract input for such mapping.
Observation point: Step diagnostic contains available structured fields among stage, test id, step index, adapter category, adapter id, action name, target ref.
Evidence: Runtime/automation negative row for adapter-reported unsupported action name.

### P29B-OBS-001 Valid Typed Observation Declaration

Claim: A project observation provider can declare `project.door.isOpen` as a boolean state-read observation with source metadata requirements.
Source: `tasks.md` 3.9; `visual-observation-assertions/spec.md` Phase 2.9B typed declaration.
Stage: `ExtensionDeclaration`
Action: Register a focused automation observation provider descriptor with `FAINpcVisualObservationDeclaration` containing `ObservationName: "project.door.isOpen"`, boolean value type, source kind `observation-provider`, sampling method `state-read`, capability, and source object/class metadata requirements.
Oracle: Declaration-aware validation accepts the declaration.
Failure signal: Validation requires the observation to be built-in, treats capability as observation name, or accepts incomplete declaration fields.
Observation point: Validation artifact or diagnostic contains available structured fields among stage, test id, adapter category, adapter id, observation name.
Evidence: Descriptor validation positive row.

### P29B-OBS-002 Undeclared Project Observation Fails Before Startup

Claim: Project observation references are shape-accepted during parse but must be declared before runtime startup.
Source: `tasks.md` 3.9; `design.md` declaration-aware validation.
Stage: `ExtensionDeclaration`
Action: Parse a scenario referencing `project.door.isOpen` without a registered observation provider declaration for that exact observation name.
Oracle: `SchemaParse` accepts shape; `ExtensionDeclaration` fails before `RuntimeStartup`.
Failure signal: Parse rejects solely because it is not in built-in whitelist, or runtime starts with undeclared observation.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category/provider id, observation name.
Evidence: Parser positive-shape row plus declaration negative row.

### P29B-OBS-003 Malformed Project Observation Shape Fails During Parse

Claim: Project observation references must match the minimal `project.<domain>.<name>` shape before declaration-aware validation can run.
Source: `tasks.md` 3.9; `design.md` scenario static parse/declaration validation ownership; `visual-observation-assertions/spec.md` Phase 2.9B project observation reference is parsed before descriptors load.
Stage: `SchemaParse`
Action: Validate scenario assertions referencing malformed project observations such as `project`, `project.`, `project.door`, `project..isOpen`, `.project.door.isOpen`, `project.door.`, or an empty observation string.
Oracle: Schema parse fails before extension declaration lookup.
Failure signal: Malformed project observation shapes are treated as undeclared observations, deferred to runtime startup, or accepted as built-in observations.
Observation point: Diagnostic contains available structured fields among stage, test id, observation name, field name.
Evidence: Parser/automation negative rows for malformed project observation shape.

### P29B-OBS-004 Capability Is Not Observation Name

Claim: Capability `observation.project.door.isOpen` cannot substitute for runtime observation name `project.door.isOpen`.
Source: `tasks.md` 3.9; `visual-observation-assertions/spec.md`.
Stage: `ExtensionDeclaration`
Action: Register a provider with capability only, or a declaration whose observation name is the capability string instead of `project.door.isOpen`, then validate a scenario expecting `project.door.isOpen`.
Oracle: Extension declaration validation fails.
Failure signal: Capability string is accepted as the runtime observation name.
Observation point: Diagnostic contains available structured fields among stage, observation name, adapter id, field name or mismatch.
Evidence: Declaration negative row.

### P29B-OBS-005 Valid State-Read Observation Satisfies Final Assertion

Claim: Final assertion accepts `project.door.isOpen == true` only from observation provider state-read metadata; in 2.9B this is a focused automation state source, not a visible door scenario.
Source: `tasks.md` 3.10; `visual-observation-assertions/spec.md`.
Stage: `FinalAssertion`
Action: During `FinalAssertion`, after step execution and before evaluating the expected value, the runner calls `SampleObservation` with `SourceActor` set to the per-run `fixture.actor` weak actor ref resolved during `RuntimeStartup`; the observation provider samples that test-owned actor/object/world state source under existing automation boundary and returns a typed boolean record named `project.door.isOpen` with source kind `observation-provider`, sampling method `state-read`, adapter/provider id, and source object/class metadata.
Oracle: Final assertion passes.
Failure signal: Final assertion ignores provider observation, requires built-in observation, accepts a non-state-read/fallback/action-attempt record, or adds a new visible door actor/scenario in 2.9B.
Observation point: Runtime observation record and final assertion diagnostic.
Evidence: Runtime/automation positive row.

### P29B-OBS-006 Reject Incomplete Observation Declaration Fields

Claim: Project observation declarations must provide every required typed field, not just a string name.
Source: `tasks.md` 3.9; `visual-observation-assertions/spec.md` Phase 2.9B project observation declaration is typed.
Stage: `ExtensionDeclaration`
Action: Validate observation provider descriptors with `project.door.isOpen` missing or empty `ObservationName`, missing boolean value type, missing source kind, wrong source kind, missing sampling method, wrong sampling method, missing capability, missing source object metadata requirement, or missing source class metadata requirement.
Oracle: Each incomplete or wrong declaration fails during declaration-aware validation.
Failure signal: A declaration is accepted because the observation name exists, because the capability string exists, or because missing metadata is filled by defaults.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category, adapter id, observation name, missing declaration field.
Evidence: Descriptor validation negative rows for each declaration-field group.

### P29B-OBS-007 Reject Wrong Category Or Unsupported Observation Capability

Claim: A project observation reference can only bind a registered `ObservationProvider` descriptor whose typed declaration names that exact observation and whose declaration `Capability` is present in the descriptor `Capabilities`.
Source: `tasks.md` 3.9 and 3.10; `visual-observation-assertions/spec.md` Phase 2.9B project observation declaration is typed.
Stage: `ExtensionDeclaration`
Action: Validate a scenario whose observation provider adapter id exists only as an action adapter or fixture resolver; then validate an observation provider declaration whose `Capability` is missing from the owning descriptor `Capabilities`; then validate a declaration using a different observation name.
Oracle: Extension declaration validation fails before runtime startup.
Failure signal: Wrong adapter category is accepted, capability is treated as the observation name, or undeclared observations slip into runtime.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category, adapter id, observation name, capability.
Evidence: Descriptor validation negative rows for wrong category, declaration capability missing from descriptor capabilities, and observation-name mismatch.

### P29B-OBS-008 Reject Wrong Runtime Observation Metadata

Claim: A declared project observation cannot satisfy final assertion if the runtime record has the right name but wrong source metadata.
Source: `tasks.md` 3.9 and 3.10; `visual-observation-assertions/spec.md` Phase 2.9B project observation declaration is typed; `phase-2-9b-start-readiness.md` 2.9B final assertion boundary.
Stage: `FinalAssertion`
Action: Register a valid declaration for `project.door.isOpen`, then return a runtime observation record with the correct observation name and boolean true value but wrong `SourceKind`, wrong `SamplingMethod`, missing/wrong `AdapterOrProviderId`, missing required source object path, or missing required source class.
Oracle: Final assertion fails; only `observation-provider` + `state-read` + matching provider id + required source object/class metadata can satisfy the final assertion.
Failure signal: Correct name and true value pass despite wrong source kind, wrong sampling method, wrong provider identity, or missing source metadata.
Observation point: Diagnostic contains available structured fields among stage, test id, observation name, source kind, source id/provider id, field name.
Evidence: Runtime/automation negative rows for wrong positive-path observation metadata.

### P29B-LIFECYCLE-001 Fixture Resolver Factory Or Owner Unavailable

Claim: Runtime startup validates fixture resolver factory creation and owner availability before binding `fixture.actor`.
Source: `tasks.md` 3.10; `visual-acceptance-adapters/spec.md` adapter registry lifecycle.
Stage: `RuntimeStartup`
Action: Run fixture startup with a registered fixture resolver descriptor whose factory fails to create an instance, then with its owner module marked unavailable before startup resolution.
Oracle: Runtime startup fails with structured diagnostics and no fixture ref is bound.
Failure signal: Runner proceeds with stale descriptor, uses a null adapter instance, falls back to built-in fixture lookup, or binds `fixture.actor`.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category, adapter id, owner module, target ref.
Evidence: Runtime/automation negative rows for factory failure and owner-unavailable fixture resolver.

### P29B-LIFECYCLE-002 Action Adapter Factory Or Owner Unavailable

Claim: Runtime or step execution validates action adapter factory/owner availability before invoking project action behavior.
Source: `tasks.md` 3.10; `visual-acceptance-adapters/spec.md` adapter registry lifecycle.
Stage: `RuntimeStartup` for factory creation, `StepExecution` for owner loss before call.
Action: Run a scenario with a registered action adapter descriptor whose factory fails during per-run instance creation, then with owner availability lost before `project.action.execute` calls the adapter.
Oracle: The scenario fails at the earliest applicable stage and never executes the project action.
Failure signal: Runner calls a stale adapter, treats missing instance as success, or falls back to latest-intent/SmartObject execution.
Observation point: Diagnostic contains available structured fields among stage, test id, step index, adapter category, adapter id, owner module, target ref.
Evidence: Runtime/automation negative rows for action factory failure and owner-unavailable step execution.

### P29B-LIFECYCLE-003 Observation Provider Factory Or Owner Unavailable At Startup

Claim: Runtime startup validates observation provider factory creation and current owner availability before final assertions can depend on provider observations.
Source: `tasks.md` 3.9 and 3.10; `visual-observation-assertions/spec.md` Phase 2.9B project observation declaration.
Stage: `RuntimeStartup`
Action: Run startup with a registered observation provider descriptor whose declaration passes but whose factory fails; then run startup with the owner module marked unavailable before per-run provider instance creation.
Oracle: Runtime startup fails before final assertion and before entering the provider sampling path.
Failure signal: Runner treats the declaration alone as a runtime observation, creates a provider from an unavailable owner, or lets final assertion pass without startup-created state-read provider data.
Observation point: Diagnostic contains available structured fields among stage, test id, adapter category, adapter id, owner module, observation name.
Evidence: Runtime/automation negative rows for observation provider factory failure and owner-unavailable startup.

### P29B-STAGE-001 Stage Diagnostics Matrix

Claim: Each 2.9B failure is reported at the earliest correct stage with available structured fields.
Source: `tasks.md` 3.10 and 2.9B diagnostics收口约束.
Stage: `SchemaParse`, `ExtensionDeclaration`, `RuntimeStartup`, `StepExecution`, `FinalAssertion`
Action: Produce a diagnostics matrix whose rows are bound to exact case ids: `P29B-FIXTURE-007` or `P29B-OBS-003` for `SchemaParse`, `P29B-FIXTURE-011`, `P29B-OBS-002`, or `P29B-OBS-006` for `ExtensionDeclaration`, `P29B-FIXTURE-004`, `P29B-FIXTURE-009`, or `P29B-FIXTURE-010` for `RuntimeStartup`, `P29B-ACTION-007` or `P29B-LIFECYCLE-002` for `StepExecution`, and `P29B-ACTION-004` for `FinalAssertion`.
Oracle: Each row reports the expected stage, cites the source case id, and includes the available fields among category, adapter id, test id, actor class/tag, target ref, observation name, step index.
Failure signal: Generic `validation failed` only, wrong stage, missing key available fields, or free-form strings used instead of structured diagnostic fields.
Observation point: Step/startup/final diagnostics.
Evidence: Matrix artifact mapping expected stage to actual stage and fields.

### P29B-SCRIPT-001 PowerShell Static Contract Surface

Claim: PowerShell static checks cover the 2.9B static contract surface without owning project-domain behavior.
Source: `phase-2-9b-start-readiness.md` 2.9B static-contract script delta; `tasks.md` 3.9 and 3.10; `design.md` scenario static parse/declaration validation ownership.
Stage: Static contract check.
Action: Inspect/execute static contract checks after implementation.
Oracle: PowerShell source-scan checks the C++ contract surface for `project.action.execute`, `existingActor`, `FAINpcVisualObservationDeclaration`, project observation shape validation ownership, and absence of hardcoded project-domain validity branches in core runner/parser; it may reject known forbidden patterns, but it does not define `project.door.isOpen`, fixture/action/observation project ids, or door behavior as an independent whitelist.
Failure signal: `verify-test-system-contract.ps1` lacks coverage for the stated C++ contract surface, contains a project observation whitelist, accepts/validates door-state business logic itself, or makes project-domain ids authoritative in PowerShell.
Observation point: Static check output and source scan evidence.
Evidence: Static artifact.
