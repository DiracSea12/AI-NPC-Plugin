## ADDED Requirements

### Requirement: Visual scenario 使用版本化 DSL
可视化验收系统 MUST 从版本化 scenario DSL 加载 visual game scenario；DSL MUST 声明 metadata、fixture setup、prompt input、execution steps 和 expected observations。

#### Scenario: Scenario 声明必填 DSL 区段
- **WHEN** 加载 visual scenario
- **THEN** 它包含 `schemaVersion`、`testId`、`map`、`timeoutSec`、`storyIds`、`phaseIds`、`fixture`、`persona`、`prompt`、`steps`、`expect` 字段

#### Scenario: Phase 2.8 使用内置 fixture schema
- **WHEN** Phase 2.8 scenario 声明 `fixture`
- **THEN** 它使用 `adapterId: "builtin.characterFixture"` 和 `kind: "character" | "characterWithSmartObject"`，旧 `fixture.type` 或其它未声明 fixture fields validation fail

#### Scenario: 遇到不支持的 schema version
- **WHEN** visual scenario 声明不支持的 `schemaVersion`
- **THEN** 在最终验收开始前 validation fail，并报告 unsupported version 和 test id

#### Scenario: 使用旧布尔驱动字段
- **WHEN** visual scenario 包含旧行为开关，例如旧顶层 `requireStructuredResponse`、`requireActionIntent`、`allowActionRejection` 或 `requiredObservations`
- **THEN** validation fail，而不是静默接受旧 schema

#### Scenario: Phase 2.7 使用未知顶层字段
- **WHEN** visual scenario 包含 v2 schema 未声明的顶层字段
- **THEN** validation fail，并报告 unknown field name 和 test id

### Requirement: Visual scenario step 具有明确 payload contract
Visual scenario DSL MUST 只支持声明过的 step type，且每个 step type MUST 声明 required fields、optional fields 和 payload types。可执行 payload contract MUST 由 C++ scenario schema descriptor/validator 作为单一真源持有；scripts 和 docs 可以引用或校验该 contract，但不得各自硬编码另一套独立 schema 真源。

#### Scenario: 使用 Dialogue start step
- **WHEN** step 声明 `type: "dialogue.start"`
- **THEN** 它声明 actor reference 和 prompt reference，并在调用真实 dialogue path 前解析到 scenario context

#### Scenario: 使用 World event step
- **WHEN** step 声明 `type: "world.event"`
- **THEN** 它声明 event adapter id、event tag 或 event id、adapter 所需 target reference，以及仅 adapter 声明过的 payload fields

#### Scenario: Phase 2.8 使用内置 world event adapter
- **WHEN** Phase 2.8 scenario 声明 `world.event`
- **THEN** payload 使用 `adapterId: "builtin.npcEvent"`、`eventTag` 或 `eventId` 二选一、可选 `targetRef`、可选 `payload`，且其它 event payload fields validation fail

#### Scenario: 使用 Wait step
- **WHEN** step 声明 `type: "wait.until"`
- **THEN** 它声明正数 timeout，并声明至少一个受支持 assertion group，例如 `all`、`any` 或 `anyOf`

#### Scenario: 使用 Action execution step
- **WHEN** step 声明 `type: "action.executeLatestIntent"`
- **THEN** 它声明 action adapter id、actor reference，以及仅支持的 policy fields，例如 rejection handling

#### Scenario: Phase 2.8 使用内置 SmartObject action adapter
- **WHEN** Phase 2.8 scenario 声明 `action.executeLatestIntent`
- **THEN** payload 使用 `adapterId: "builtin.smartObjectAction"`、`actorRef`、`allowActionRejection`，且其它 action payload fields validation fail

#### Scenario: 使用 Observation hold step
- **WHEN** step 声明 `type: "observe.hold"`
- **THEN** 它声明正数 duration 和 condition observation，且 condition 必须在整个 hold window 内保持满足

#### Scenario: 使用未知 step type
- **WHEN** scenario step 声明未知 `type`
- **THEN** runtime execution 前 validation fail，并报告 step index、type 和 test id

#### Scenario: Phase 2.7 使用 malformed step payload
- **WHEN** scenario step 缺少该 step type payload contract 的必填字段，或字段类型不匹配
- **THEN** validation fail，并报告 field name、step index、step type 和 test id

#### Scenario: Phase 2.95 在 step 内使用未知字段
- **WHEN** scenario step 包含该 step type payload contract 未声明的字段
- **THEN** validation fail，并报告 field name、step index、step type 和 test id

#### Scenario: Step timeout 超时
- **WHEN** 带 timeout 的 step 在超时前无法满足条件
- **THEN** visual run fail，并在 result diagnostics 中记录 step index、step type、timeout、wait condition、最后 observation 更新时间、provider state summary、NPC state summary 和 missing observations

### Requirement: Prompt-driven scenario 解析 runtime variables
Visual scenario DSL MUST 允许 prompt 文件使用 scenario 声明的变量，并在 dialogue start 前从 fixture 或 runtime context 解析。

#### Scenario: Prompt variable 从 fixture context 解析
- **WHEN** prompt 引用 `{SmartObjectTargetId}` 之类变量，且 scenario 将它映射到 fixture value
- **THEN** runner 在调用真实 NPC dialogue path 前用解析出的 runtime value 替换 placeholder

#### Scenario: Prompt variable 无法解析
- **WHEN** prompt variable 没有匹配的 scenario variable 或 runtime value
- **THEN** validation 或 startup fail，并报告 unresolved variable name 和 test id

#### Scenario: Prompt 文件不符合 visual harness staging 约定
- **WHEN** scenario 引用的 prompt、persona 或 delay-filler 文件本地存在，但不被 visual harness staging convention 覆盖
- **THEN** static validation 在 runtime acceptance 前 fail

### Requirement: 现有 adapter 覆盖时支持 prompt-only 新增
当行为使用现有 fixture、event、action 和 observation adapters 时，系统 MUST 允许新增 visual behavior scenario 而不改 C++。

#### Scenario: 新 scenario 使用现有 adapters
- **WHEN** 开发者新增 prompt file 和 scenario entry，且只引用现有 adapters、step types 和 observations
- **THEN** static validation 接受该 scenario，game test runner 从 scenario source 自动发现它，不需要编辑 runner

#### Scenario: Prompt-only path 被验证
- **WHEN** 新增 prompt-only scenario 作为本系统的验收证明
- **THEN** review evidence 显示只修改了 scenario source 和 prompt/persona 文件，而 scenario runner、script dispatcher、adapter registry 没有增加 test-id-specific branch

#### Scenario: Scenario 需要现有 adapter 未覆盖的行为
- **WHEN** scenario 引用没有已注册 adapter 可执行或观察的项目行为
- **THEN** validation fail，并报告 missing adapter 或 observation name，而不是把硬编码行为加进 core runner

### Requirement: Phase identifiers 是显式 scenario metadata
Visual scenario DSL MUST 将 phase identifiers 作为 scenario metadata 携带，result artifacts MUST 保留这些 identifiers。

#### Scenario: Scenario 声明 phase ids
- **WHEN** 加载 scenario
- **THEN** `phaseIds` 包含一个或多个非空字符串，例如 `phase2.7`、`phase2.8`、`phase2.9` 或 `phase2.95`

#### Scenario: Runtime result 被写出
- **WHEN** scenario 写出 runtime result artifact
- **THEN** result artifact 包含 scenario 的 `phaseIds`，不得从 task numbering 或 file path 推导 phase number
