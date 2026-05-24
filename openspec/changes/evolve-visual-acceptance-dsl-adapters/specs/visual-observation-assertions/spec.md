## ADDED Requirements

### Requirement: Observations 是 typed、sourced runtime facts
可视化验收系统 MUST 将 observations 存为 typed records，来源必须是真实 runtime callbacks、actors、components、subsystems、provider-chain state、perception sources 或声明过的 observation providers。

#### Scenario: Observation 被记录
- **WHEN** 写入 observation
- **THEN** 它记录 name、value type、value、source kind、适用时的 source object/class 或 subsystem、sampling method、source adapter/provider id、step index、timestamp 或 elapsed time

#### Scenario: 采集内置 dialogue observation
- **WHEN** NPC dialogue session 开始、收到 response、收到 partial response、进入 dialogue state 或结束 delay masking
- **THEN** observation store 从真实 dialogue callbacks 或 component state 记录命名空间 observations，例如 `dialogue.sessionStarted`、`dialogue.responseObserved`、`dialogue.partialResponseObserved`、`dialogue.waitingStateObserved`、`dialogue.delayMaskingEnded`

#### Scenario: 采集内置 character observation
- **WHEN** NPC 朝 action target 移动
- **THEN** observation store 从真实 character 或 driver state 记录 character observations，例如 `character.distanceToActionTarget` 和 `character.reachedActionTarget`

#### Scenario: 采集 project observation
- **WHEN** project observation provider 从项目 actor、component、subsystem、perception system、inventory、quest system、combat system 或 audio/vision system 读取状态
- **THEN** observation store 用真实 runtime state 记录 provider 声明的 `project.<domain>.*` observation names 和 values
- **THEN** final-success gate 允许 `observation-provider` source kind only when the record includes concrete runtime identity/provider plus source object path or source class metadata, and the sampling method represents real state reads rather than action attempts, adapter acceptance, fallback/degraded, or failure-path facts

### Requirement: Observation reads 具有明确时间语义
Assertions MUST 指明它读取 latest observations、step-scoped observations、event-history observations 或 hold-window observations。

#### Scenario: Step-scoped wait 读取 observations
- **WHEN** `wait.until` step 评估 assertion
- **THEN** 除非 assertion 显式使用 global/latest scope，否则该 step 之前记录的 observations 不能满足 wait

#### Scenario: Observation hold 读取 condition
- **WHEN** `observe.hold` step 评估 condition
- **THEN** condition 必须在声明的 hold duration 内持续满足，且使用 hold window 内采样到的 observations

#### Scenario: Observation 已过期
- **WHEN** latest observation value 早于 assertion 声明的窗口或当前 step scope
- **THEN** assertion 将其视为 missing，并在 diagnostics 中报告 stale timestamp

### Requirement: Assertions 以声明式方式评估 observations
可视化验收系统 MUST 通过有限的 declarative assertion grammar 评估 scenario expectations，而不是用 feature-specific runner branches。

#### Scenario: 要求所有 observations
- **WHEN** scenario expectation 声明对 observation names 的 `all` assertion
- **THEN** 只有当每个命名 boolean observation 在 assertion scope 内 present 且 true 时 assertion 才通过

#### Scenario: 允许任一 observation group
- **WHEN** scenario expectation 声明 `any` assertion 或 `anyOf` group
- **THEN** 只要 assertion scope 内至少一个 observation 或 observation group 满足，assertion 就通过

#### Scenario: 使用不支持的 assertion operator
- **WHEN** Phase 2.8c scenario 使用不在当前支持集合内的 assertion operator；当前支持集合为 `all`、`any`、`anyOf`、`equals`、`exists`、`notExists`
- **THEN** runtime execution 前 validation fail

#### Scenario: Numeric comparison 留到后续阶段
- **WHEN** Phase 2.8c scenario 尝试使用 numeric operator 或 free-form operator
- **THEN** validation fail，并把该需求留到后续阶段，而不是预先实现完整比较框架

#### Scenario: notExists 缺少采样证据
- **WHEN** `notExists` assertion 的 observation provider/source 未声明 ready、未运行采样，或 assertion window 未覆盖需要检查的时间范围
- **THEN** assertion fail，而不是把未采样导致的缺失当作成功

#### Scenario: Boolean observation 为 false
- **WHEN** assertion 直接按名称要求一个 boolean observation，且没有显式 comparison
- **THEN** 只有当该 observation present 且 true 时 assertion 才通过

### Requirement: Action adapter 不伪造最终 observations
Action adapters MUST NOT 仅因为 action execution function 被调用，就把玩家可感知行为标记为成功。

#### Scenario: Action execution 开始
- **WHEN** action adapter 接受并开始执行 action intent
- **THEN** 它可以记录 execution-attempt observation，但 final success observations 必须来自执行后的真实状态变化

#### Scenario: Project behavior 在 action acceptance 后失败
- **WHEN** action adapter 接受 action，但 world state 没有达到 expected condition
- **THEN** visual scenario fail，而不是把 adapter acceptance 当作 final success

#### Scenario: Adapter 直接写 final success observation
- **WHEN** action adapter 尝试在没有 observation provider 或 runtime state source 的情况下写 final player-visible success observation
- **THEN** adapter API 或 static contract validation 在 scenario 能报告 final acceptance 前拒绝该 write path

### Requirement: Result diagnostics 定位失败 step 和 observations
Visual acceptance result artifacts MUST 包含足够 diagnostics，以定位失败的 step、adapter、assertion，以及 missing 或 mismatched observation。

#### Scenario: Step 失败
- **WHEN** scenario step 执行失败
- **THEN** result artifact 记录 step index、step type、适用时的 adapter id、failure reason、elapsed time、redacted step input summary、provider state summary、NPC state summary 和相关 observation snapshot

#### Scenario: Assertion 失败
- **WHEN** scenario expectations 未满足
- **THEN** result artifact 记录失败 assertion、missing observations、mismatched actual values、stale observation timestamps、expected groups 和最后 observation snapshot

#### Scenario: 存在敏感 provider 数据
- **WHEN** result diagnostics 包含 provider、prompt、response、command-line 或 failure text
- **THEN** secrets、authorization headers、API keys 和 raw sensitive provider bodies 在写入 artifacts 前被 redacted 或 summarized
