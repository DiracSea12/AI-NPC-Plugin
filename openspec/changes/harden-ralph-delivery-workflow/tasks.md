
## 1. Deterministic Gate Contract

- [x] 1.1 Enforce structured verdict + exit code as default gate contract.
- [x] 1.2 Keep legacy regex checks behind explicit opt-in flag only.
- [x] 1.3 Add contract tests for marker missing/marker fail/non-zero exit cases.

## 2. Normalization & Compatibility

- [x] 2.1 Add command normalization for PowerShell portability patterns.
- [x] 2.2 Add artifact normalization for JSON and line-ending consistency.
- [x] 2.3 Add regression tests for encoding and quote-style variants.

## 3. Parallel Discussion Workflow

- [x] 3.1 Start testcase-discussion worker in parallel with development at iteration start.
- [x] 3.2 Add mandatory join point before gate execution.
- [x] 3.3 Persist discussion result artifact with convergence and severity summary.

## 4. Critical/High Convergence Policy

- [x] 4.1 Require bounded convergence rounds for critical/high findings.
- [x] 4.2 Fail iteration when convergence not reached.
- [x] 4.3 Record gate notes with explicit blocking counts and evidence files.

## 5. Review Blocking Modes

- [x] 5.1 Implement default soft mode for review parse/timeout failures.
- [x] 5.2 Keep strict mode switch for hard blocking governance.
- [x] 5.3 Add startup logs showing active policy mode.

## 6. Operational Safety

- [x] 6.1 Enforce singleton lock lifecycle and stale-lock recovery.
- [x] 6.2 Add cleanup command for runner/lock state reset.
- [x] 6.3 Document monitoring and restart protocol for long-run iterations.
