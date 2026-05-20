
## ADDED Requirements

### Requirement: Deterministic Gate Verdict Contract
Gate evaluation MUST use process-safe signals first, not free-form text matching.

#### Scenario: Structured pass
- **WHEN** a test exits with code 0 and emits `RALPH_TEST_RESULT=PASS`
- **THEN** the gate marks the test as passed

#### Scenario: Structured fail
- **WHEN** a test emits `RALPH_TEST_RESULT=FAIL`
- **THEN** the gate marks the test as failed regardless of pass-like text

#### Scenario: Non-zero exit
- **WHEN** a test exits non-zero
- **THEN** the gate marks the test as failed regardless of text output

### Requirement: Parallel Testcase Discussion Join
Testcase discussion MUST execute in parallel with development and MUST be joined before gate decision.

#### Scenario: Parallel launch
- **WHEN** an iteration starts and a target story is selected from pre-snapshot
- **THEN** the orchestrator starts testcase-discussion worker concurrently with development execution

#### Scenario: Mandatory join
- **WHEN** development output is produced for the iteration
- **THEN** the orchestrator waits for testcase-discussion result before running story gates

### Requirement: Critical/High Convergence Hard Rule
Critical and high findings from testcase discussion MUST converge within bounded rounds.

#### Scenario: Converged
- **WHEN** discussion rounds finish with zero critical/high open findings
- **THEN** iteration may proceed to verifier/story gates

#### Scenario: Not converged
- **WHEN** bounded rounds are exhausted and critical/high findings remain
- **THEN** iteration fails with explicit blocking summary and evidence artifact path

### Requirement: Review Failure Soft Mode
Review parse/timeout failures MUST be non-blocking by default and MAY be blocking in strict mode.

#### Scenario: Default mode parse failure
- **WHEN** review output cannot be parsed in default mode
- **THEN** the system records warning status and continues

#### Scenario: Strict mode parse failure
- **WHEN** strict blocking mode is enabled and review output cannot be parsed
- **THEN** the system fails the gate

### Requirement: Singleton Runner Isolation
Only one Ralph runner MUST operate per state directory.

#### Scenario: Duplicate start
- **WHEN** a second runner starts while lock is held by a live process
- **THEN** startup fails immediately with lock owner pid

#### Scenario: Stale lock
- **WHEN** lock exists but owner process is not alive
- **THEN** stale lock is recoverable and startup can proceed after cleanup
