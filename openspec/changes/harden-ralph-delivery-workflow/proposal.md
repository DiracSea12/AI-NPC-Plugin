
## Why

Ralph loop repeatedly failed for process reasons instead of true feature defects:
- brittle regex/text matching produced false negatives
- encoding and shell variance broke generated tests
- concurrent runners polluted shared iteration artifacts
- review and verifier parse failures created dead-end blocking
- critical/high findings lacked a deterministic convergence contract

This creates low throughput, poor signal quality, and unstable autonomous delivery.

## What Changes

Define a process-level capability for Ralph delivery orchestration with deterministic gates, bounded convergence, and explicit parallelism.

1. Replace text-fragile gate decisions with structured verdicts (`RALPH_TEST_RESULT`) + exit code.
2. Add normalization standards for cross-shell, encoding, and command portability.
3. Enforce single-instance execution and isolated per-iteration artifacts.
4. Run development and testcase-discussion in parallel, then join at a hard decision point.
5. Require critical/high findings to enter bounded discussion rounds and converge before pass.
6. Make parse/timeout review failures degradable to non-blocking warnings unless strict mode is enabled.

## Capabilities

### New Capabilities
- `ralph-delivery-workflow`: deterministic, convergent, and parallel orchestration for story delivery.

### Modified Capabilities
- `<none>`

## Impact

- Primary: `docs/ralph/ralph.sh` orchestration logic and gate semantics
- Secondary: verifier/review prompt contracts and iteration artifact model
- Ops impact: less false blocking, clearer failure attribution, reproducible iteration outcomes
