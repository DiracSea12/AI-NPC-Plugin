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

## What belongs in git

- `Config/`
- `Source/`
- `Plugins/AINpc/Source/`
- `Plugins/AINpc/Config/`
- `Plugins/AINpc/Content/Examples/`
- `docs/`
- `openspec/`

Generated Unreal artifacts, scratch logs, and one-off probe scripts are ignored.
