# 开发环境

## 前置条件

- UE5 源码构建位于 `G:\UE5\UnrealEngine`
- 安装 Visual Studio，并包含 C++ desktop workload

## 项目基线

- 宿主项目：`VerifierHost.uproject`
- 插件根目录：`Plugins/AINpc`
- 主要文档：`docs/PRD.md`、`docs/SDD.md`

## 常用命令

- 构建 editor target：
  `pwsh ./scripts/dev/build-editor.ps1`
- 清理生成产物：
  `pwsh ./scripts/dev/clean-generated.ps1`
- 快速 AI 编码验证：
  `pwsh ./scripts/dev/test-fast.ps1`
- 完整静态验证：
  `pwsh ./scripts/dev/test-static.ps1`
- 完整 EditorContext Automation：
  `pwsh ./scripts/dev/test-editor-context.ps1`
- 局内/可视游戏测试：
  `pwsh ./scripts/dev/test-game.ps1`
- LLM 连通性诊断：
  `pwsh ./scripts/dev/test-llm-connectivity.ps1`
- 完整验证（静态 + EditorContext Automation + 局内/可视游戏测试）：
  `pwsh ./scripts/dev/test-all.ps1`

## 本地 Provider 配置

- 仓库本地真实 provider 配置位于：
  `Config/AINpcLocalProvider.json`
- 该文件是 provider、apiKey、baseUrl、model、effortLevel 的唯一来源。
- 不要重新把 provider 设置引入 UE settings、Persona assets、components、Blueprint nodes、环境变量或 fallback chains。
- 该文件刻意只用于本地，并被 git ignore。
- 示例模板是：
  `Config/AINpcLocalProvider.example.json`
- `scripts/dev/test-llm-connectivity.ps1` 会用该配置发送一次真实的最小 provider 请求，只验证 provider reachability/auth/JSON response。
- LLM 连通性不是 EditorContext Automation，也不是 NPC 局内行为验收。

### Packaged visual QA

- packaged runtime verification 不会自动拿到你工作站上被 ignore 的配置；除非你把它放进 packaged project。
- 本地 packaged visual QA 需要复制：
  `Config/AINpcLocalProvider.json`
  到：
  `TestResults/Packaged/VerifierHostAutomation/Windows/VerifierHostAutomation/Config/AINpcLocalProvider.json`
- 这只用于你本机的本地验收。不要提交 secrets；除非 packaged build 使用了真实本地 provider config，否则不要声称 packaged real-provider verification 通过。

## 测试发现规则

- 静态验证脚本放在 `scripts/dev/verify-*.ps1`；`test-static.ps1` 会自动发现并运行。
- Unreal Automation 测试放在 `Plugins/AINpc/Source/**/Private/Tests/*.cpp`；`test-editor-context.ps1` 会发现以 `AINpc.` 开头的 quoted test paths。
- 局内/可视游戏测试入口是 `scripts/dev/test-game.ps1`。当前 visual scenarios 以 `Config/AINpcVisualScenarios.json` 为单一来源；共享 harness 脚本放在 `scripts/dev/game/`。
- 新增 visual game scenario 时，优先新增/修改 scenario 配置和 prompt/persona 文件，不要为每个功能新增散乱 map/mode/script。
- 局内/可视游戏测试默认使用共享 `/Game/Maps/AINpcTestMap` 和 `AAINpcTestGameMode` harness；如果需要项目自定义 actor 或行为，走 scenario adapter/interface，而不是复制一套 runner。

## 运行时验收标准

- Headless automation、static checks、log replay、`NullRHI`、`UnrealEditor-Cmd` 和 `-unattended` 对诊断有用，但不是玩家能看到、听到、触发或感知的 NPC 行为最终验收。
- 玩家可感知 NPC 功能必须有真实 visual editor/game verification path，才能称为 accepted。
- Mock providers、injected `FLLMResponse` payloads、test bypasses 和 script-only demos 只能算调试工具。除非真实 provider path 和 visible runtime behavior 都验证过，否则必须报告为验收不完整。
- 一轮验证默认只使用一个可见 editor/game 实例。如果确实需要额外实例，启动前必须记录原因。

## AI 编码工作流

Agent 还应阅读 `AGENTS.md`，获取主动行为规则。

### aider

- 仓库配置位于 `.aider.conf.yml`
- 默认验证循环运行：
  `pwsh ./scripts/dev/test-fast.ps1`
- 建议用法：
  `aider --model <your-model>`

Aider 配置始终加载：

- `DEVELOPMENT.md`
- `docs/PRD.md`
- `docs/SDD.md`
- `Plugins/AINpc/AINpc.uplugin`
- `.gitignore`

### Continue

- PR checks 位于 `.continue/checks/`
- 在兼容 coding agent 中用 `/check` 本地运行
- 初始 checks 关注：
  - generated-artifact hygiene
  - Unreal runtime/editor/UI 边界
  - core AI NPC 变更的验证预期

### Context7

- 用途：查询最新第三方文档
- 对 library/framework/API 问题优先使用
- Setup helper：
  `pwsh ./scripts/dev/setup-context7.ps1`
- Health check：
  `pwsh ./scripts/dev/ai-stack-doctor.ps1`

如果存在 `CONTEXT7_API_KEY`，agent 应主动 bootstrap 项目本地 Context7 支持，不要等用户记得这件事。

### Repomix

- 用途：把仓库打包成适合 AI handoff 的 artifact
- 配置：
  `repomix.config.json`
- 构建 review bundle：
  `pwsh ./scripts/dev/pack-context.ps1`
- 输出目录：
  `.artifacts/ai/`

大上下文 review、外部模型 handoff、零上下文分析时，主动使用 Repomix。

零上下文子代理审阅必须无锚定：prompt 只给 bundle/目标文件、任务类型、禁止改文件、通用审查维度和输出格式；禁止塞入主代理结论、怀疑点、上一轮 findings、刚改内容或期望验证的 fix。要验证已知问题时必须标成定向复核，不准冒充零上下文。

### Langfuse

- 用途：runtime LLM observability、prompt/version tracking、evals
- 当前仓库状态：推荐，但还没接入 runtime code
- 原因：插件仍需要明确 trace seam，telemetry 才有价值

当项目开始 shipping live provider/memory quality instrumentation 时，Langfuse 是优先考虑的 observability layer。

## 什么应该进 git

- `Config/`
- `Source/`
- `Plugins/AINpc/Source/`
- `Plugins/AINpc/Config/`
- `Plugins/AINpc/Content/Examples/`
- `docs/`
- `openspec/`

Generated Unreal artifacts、scratch logs 和 one-off probe scripts 被 ignore。

## Phase 2.7 visual DSL 说明

- `Config/AINpcVisualScenarios.json` 是 visual game scenarios 的单一发现源，当前只接受 `schemaVersion: 2`。
- 共享启动/聚合脚本仍位于 `scripts/dev/game/` 和 `scripts/dev/test-game.ps1`；新增场景优先只改 scenario 配置、prompt/persona 文件。
- 新增 prompt/config-only scenario 不得在 runner、script dispatcher 或 adapter registry 里加 test-id-specific branch。
- Phase 2.7 不引入远程服务、数据库、Web 控制台、分布式调度、私有服务或长期运行基础设施。
- 开源复现时，提交 `Config/*.example.json`、prompt/persona 模板和 scenario 配置；本地私有 `Config/AINpcLocalProvider.json`、API key、本机 `G:\UE5\...` 路径只属于维护者本机，不是外部贡献者必须条件。
