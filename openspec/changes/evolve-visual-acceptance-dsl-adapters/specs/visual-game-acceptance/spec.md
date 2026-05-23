## ADDED Requirements

### Requirement: 最终 NPC 行为验收使用可见游戏执行
可视化验收系统 MUST 只把可见游戏执行路径 + 真实 provider 配置 + 真实运行行为视为玩家可感知 NPC 行为的最终验收。

#### Scenario: 启动最终验收运行
- **WHEN** visual game acceptance scenario 用于最终 NPC 行为验证
- **THEN** 它启动非 hidden 的可见 `UnrealEditor.exe -game` 入口，并使用确定性 test id、run id、log path 和 result path

#### Scenario: 提议等价可见入口
- **WHEN** 提议使用 `UnrealEditor.exe -game` 之外的 launch path 作为最终验收
- **THEN** 在 visible-entry allowlist、launch contract、static validation 和 result diagnostics 显式支持该入口前，它被拒绝

#### Scenario: 请求 forbidden final acceptance mode
- **WHEN** 最终 NPC 行为验收尝试使用 `UnrealEditor-Cmd`、`NullRHI`、`-unattended`、fake provider、mock response、injected response、dialogue bypass 或 test-only completion hook
- **THEN** launch 或 validation layer 拒绝该运行，并且不报告最终行为验收通过

#### Scenario: Validate-only run 通过
- **WHEN** validate-only visual harness run 成功，但没有启动真实 runtime behavior path
- **THEN** 结果被报告为 diagnostic validation，而不是最终 NPC 行为验收

### Requirement: 最终验收 artifact 证明 provider 和 runtime path
最终 visual game acceptance artifact MUST 包含安全证据，证明运行使用了配置的真实 provider path，并且 result 由游戏 runtime process 写出。该最小证据从 Phase 2.7 起就是最终验收前提；后续阶段可以丰富 diagnostics，但不得把真实 provider/runtime path 证明推迟到 Phase 2.95。

#### Scenario: Provider request 被尝试
- **WHEN** 最终验收 scenario 发送 provider request
- **THEN** diagnostics 包含 provider type、base URL present 或 redacted host、model、适用时的 effort level、endpoint、request attempt status、可用时的 HTTP status、duration，以及不暴露 API key 或 raw sensitive body 的 redacted error summary

#### Scenario: Provider identity 来源被验证
- **WHEN** final acceptance 记录 provider identity
- **THEN** provider identity 必须来自 `Config/AINpcLocalProvider.json` 唯一配置真源和运行时 provider resolver 的实际请求链路，不得来自脚本参数、环境变量、UE settings、Persona DataAsset 或兼容 fallback 配置链

#### Scenario: Provider 配置缺失
- **WHEN** 真实 provider 配置缺失或不完整
- **THEN** 最终行为验收被报告为 blocked 或 failed，并带安全 provider diagnostics；不得 fallback 到 fake provider 或 validate-only result

#### Scenario: Provider chain 使用 fallback 或 degraded response
- **WHEN** provider chain 使用 template fallback、degraded response、fallback-only content 或其它未完成真实 provider success path 的响应
- **THEN** 该响应可以进入安全 diagnostics，但不得满足 final player-visible success observation，最终 NPC 行为验收必须 reported failed 或 blocked

#### Scenario: Runtime process 写出 result
- **WHEN** visual game scenario 到达 terminal outcome
- **THEN** result artifact 记录 game executable path、process id、map、redacted command summary、run id、test id、result path，以及从游戏进程内部写出的 runtime observations

### Requirement: 脚本不评估 NPC 业务行为
脚本层 MUST 只负责 launch orchestration、deterministic artifacts、schema validation、result aggregation 和 final-acceptance guardrails。

#### Scenario: 脚本聚合 runtime result
- **WHEN** 游戏 runtime 写出 visual result artifact
- **THEN** 脚本校验 schema、identity fields、terminal status、artifact paths、forbidden-mode absence 和 required structural fields，但不自行判断 gameplay-specific success

#### Scenario: 需要业务特定 assertion
- **WHEN** scenario 需要判断门是否打开、NPC 是否听到声音、任务是否推进、物品是否交易、视觉刺激是否改变行为
- **THEN** 该判断由 registered adapters 或 observation providers 产出的 runtime observations 和 assertions 表达，而不是由 PowerShell 业务逻辑表达

### Requirement: Visual game result artifacts 确定且可审查
可视化验收系统 MUST 产出每次运行确定性的 result artifact，可以追溯到具体 scenario、command、runtime observations、diagnostics 和 failures。

#### Scenario: Runtime result 被写出
- **WHEN** visual game scenario 到达 terminal outcome
- **THEN** runtime result 包含 schema version、run id、layer、test id、story ids、phase ids、status、timestamps、command summary、artifacts、diagnostics、observations、step diagnostics 和 failures

#### Scenario: Result artifact identity 无效
- **WHEN** 脚本读取到的 runtime result 的 schema version、layer、test id、run id 或 artifact path 与已启动 scenario 不匹配
- **THEN** 脚本 fail 该 scenario，而不是按 timestamp 或 latest-file 猜另一个 result

### Requirement: 最终验收记录可见行为证据
最终 acceptance artifact MUST 包含可审查的玩家可感知行为证据。

#### Scenario: 行为改变 actor 或 world state
- **WHEN** scenario 声称 NPC 移动、交互、反应、状态变化、感知输入或影响项目状态
- **THEN** result diagnostics 包含带 source metadata 的 runtime observations，以及足以审计该声明的 actor、component、subsystem、transform 或 project-state before/after 证据

#### Scenario: 手动观察是验收证据的一部分
- **WHEN** 运行在可见游戏窗口中被手动观察
- **THEN** report 除 runtime JSON diagnostics 外，还记录 manual observation notes、screenshot、viewport capture 或 screenshot/viewport artifact pointer

#### Scenario: 只有 log pointer
- **WHEN** scenario 只有 success status 和 log pointer，却没有 runtime observation evidence、screenshot/viewport artifact、actor/world state change 或 manual observation note
- **THEN** log pointer 只能作为诊断线索，不能被接受为最终 NPC 行为证据

#### Scenario: 没有可见证据
- **WHEN** scenario 只有 success status，却没有该玩家可感知行为的 runtime observation evidence、screenshot/viewport artifact、actor state change 或 manual observation note
- **THEN** 该运行不能被接受为最终 NPC 行为证据
