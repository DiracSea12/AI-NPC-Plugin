# AI Agent Stack

This repository assumes the primary coding agents are Codex and Claude Code.

The goal of this stack is not "more AI", but better autonomous behavior from the agents already in use.

## What is enabled now

### 1. Context7

Purpose:

- current library and framework documentation
- lower-risk API usage when providers, SDKs, or tooling drift

Why it matters here:

- this project mixes Unreal Engine, HTTP providers, JSON parsing, and external AI APIs
- stale memory is expensive in a plugin codebase because compile/test feedback is slower than in a web app

Project support:

- `scripts/dev/setup-context7.ps1`
- `scripts/dev/ai-stack-doctor.ps1`
- proactive instructions in `AGENTS.md` and `CLAUDE.md`

Recommended agent behavior:

- use Context7 automatically for third-party API/library questions
- bootstrap it locally if `CONTEXT7_API_KEY` is present

### 2. Repomix

Purpose:

- generate an AI-friendly repository bundle for external review or zero-context analysis

Why it matters here:

- the plugin spans runtime, editor, UI, and host-project glue
- large-context reviews are common and expensive to repeat manually

Project support:

- `repomix.config.json`
- `scripts/dev/pack-context.ps1`
- output under `.artifacts/ai/`

Recommended agent behavior:

- run proactively before large review/refactor/handoff tasks

### 3. Fast verification loop

Purpose:

- keep agent edits grounded in real buildable behavior

Project support:

- `scripts/dev/test-fast.ps1`
- `scripts/dev/build-editor.ps1`
- contract checks for BaseURL and SSE behavior

Recommended agent behavior:

- run after meaningful source changes unless the task is docs-only

## What is intentionally deferred

### Langfuse

Langfuse is still recommended for:

- LLM request tracing
- prompt/version tracking
- evals and regression analysis

However, it is not auto-wired yet because the repository does not currently expose a clean runtime trace seam.
Adding observability before that seam exists would create noise before it creates value.

When the plugin starts stabilizing:

- provider abstraction
- prompt lifecycle
- fallback/degradation events
- memory retrieval quality metrics

Langfuse should be added next.
