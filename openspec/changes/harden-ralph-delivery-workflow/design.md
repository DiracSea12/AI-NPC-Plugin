
## Context

Current Ralph behavior mixes product defects with workflow defects. Most repeated failures were caused by non-deterministic process behavior (encoding, regex dependence, parser fragility, and multi-run interference).

## Goals / Non-Goals

**Goals:**
- deterministic pass/fail decision contract
- explicit and bounded convergence for critical/high findings
- true parallelism: development and testcase discussion
- robust operation under shell and encoding variance
- auditable iteration state transitions

**Non-Goals:**
- changing product-level acceptance criteria in story content
- replacing existing verifier/review agents
- introducing unbounded retries or hidden auto-fixes

## Decisions

1. **Structured Verdict First**
   - Gate verdict uses non-zero exit OR structured marker (`RALPH_TEST_RESULT=PASS|FAIL`).
   - Legacy pass/fail regex is opt-in only.

2. **Process Normalization Layer**
   - Normalize PowerShell command patterns and line endings before execution.
   - Normalize machine-readable JSON artifacts before parsing.

3. **Singleton Orchestration**
   - One active Ralph loop per state dir via lock directory.
   - Fail fast on duplicate runner.

4. **Parallel Discussion Join Point (Hard Process Rule)**
   - Start testcase discussion worker at iteration start using pre-snapshot target story.
   - Main development continues concurrently.
   - Before gates, orchestrator must join worker result.

5. **Critical/High Convergence Contract**
   - If discussion result has any critical/high open findings, iteration fails.
   - Convergence rounds are bounded (default 3).

6. **Soft Review Failure Mode**
   - Review parse/timeout failures degrade to warning in default mode.
   - Strict mode can re-enable hard blocking.

## Risks / Trade-offs

- More agent calls per iteration increase token/time cost.
- Hard convergence on critical/high may reduce short-term pass rate.
- Soft review mode may let some medium quality issues pass unless tracked by governance metrics.
