# Development Setup

## Prerequisites

- UE5 source build at `G:\UE5\UnrealEngine`
- Visual Studio with C++ desktop workload

## Project Baseline

- Host project: `VerifierHost.uproject`
- Plugin root: `Plugins/AINpc`
- Main docs: `docs/PRD.md`, `docs/SDD.md`

## Common Commands

- Build editor target:
  `pwsh ./scripts/dev/build-editor.ps1`
- Clean generated artifacts:
  `pwsh ./scripts/dev/clean-generated.ps1`
- Fast AI-coding verification:
  `pwsh ./scripts/dev/test-fast.ps1`

## Local Provider Config

- Repo-local real-provider config lives in:
  `Config/AINpcLocalProvider.json`
- This file is intentionally local-only and ignored by git.
- The example template is:
  `Config/AINpcLocalProvider.example.json`

### Packaged visual QA

- Packaged runtime verification does not automatically pick up your workstation's ignored config unless you place it into the packaged project.
- For local packaged visual QA, copy:
  `Config/AINpcLocalProvider.json`
  into:
  `TestResults/Packaged/VerifierHostAutomation/Windows/VerifierHostAutomation/Config/AINpcLocalProvider.json`
- This is only for local acceptance on your own machine. Do not commit secrets, and do not claim packaged real-provider verification unless the packaged build is using a real local provider config.

## Runtime Verification Standard

- Headless automation, static checks, log replay, `NullRHI`, `UnrealEditor-Cmd`, and `-unattended` runs are useful diagnostics, but they are not final acceptance for NPC behavior that a player can see, hear, trigger, or feel in-game.
- Player-perceivable NPC features need a real visual editor/game verification path before they can be called accepted.
- Mock providers, injected `FLLMResponse` payloads, test bypasses, and script-only demos are debugging tools only. Report them as incomplete acceptance unless a real configured provider path and visible runtime behavior have also been verified.
- Use one visual editor/game instance for a verification pass by default. If another instance is needed, record why before launching it.

## AI Coding Workflow

Agents should also read `AGENTS.md` for proactive behavior rules.

### aider

- Repo config lives in `.aider.conf.yml`
- The default verification loop runs:
  `pwsh ./scripts/dev/test-fast.ps1`
- Suggested usage:
  `aider --model <your-model>`

The aider config always loads:

- `DEVELOPMENT.md`
- `docs/PRD.md`
- `docs/SDD.md`
- `Plugins/AINpc/AINpc.uplugin`
- `.gitignore`

### Continue

- PR checks live in `.continue/checks/`
- Run them locally with `/check` in a compatible coding agent
- The initial checks focus on:
  - generated-artifact hygiene
  - Unreal runtime/editor/UI boundaries
  - verification expectations for core AI NPC changes

### Context7

- Purpose: current third-party documentation lookup
- First choice for library/framework/API questions
- Setup helper:
  `pwsh ./scripts/dev/setup-context7.ps1`
- Health check:
  `pwsh ./scripts/dev/ai-stack-doctor.ps1`

If `CONTEXT7_API_KEY` is available, agents should proactively bootstrap project-local Context7 support instead of waiting for the user to remember.

### Repomix

- Purpose: pack the repository into an AI-friendly handoff artifact
- Config:
  `repomix.config.json`
- Build a review bundle:
  `pwsh ./scripts/dev/pack-context.ps1`
- Output directory:
  `.artifacts/ai/`

Use Repomix proactively for large-context review, external model handoff, or zero-context analysis.

### Langfuse

- Purpose: runtime LLM observability, prompt/version tracking, evals
- Current repo status: recommended, but not yet wired into runtime code
- Reason: the plugin still needs a deliberate trace seam before telemetry adds real value

When the project starts shipping live provider/memory quality instrumentation, Langfuse should be the first observability layer considered.

## What belongs in git

- `Config/`
- `Source/`
- `Plugins/AINpc/Source/`
- `Plugins/AINpc/Config/`
- `Plugins/AINpc/Content/Examples/`
- `docs/`
- `openspec/`

Generated Unreal artifacts, scratch logs, and one-off probe scripts are ignored.
