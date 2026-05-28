## ADDED Requirements

### Requirement: Scenario runner 通过 adapters 执行行为特定工作
Visual scenario runner MUST 通过 registered adapters 路由 fixture creation、event emission、action execution、character driving 和 observation collection，而不是在 runner 中硬编码 feature 或 project behavior。

#### Scenario: 执行内置 SmartObject action
- **WHEN** scenario step 请求使用内置 SmartObject adapter 执行 latest action intent
- **THEN** runner 调用 registered SmartObject action adapter，且 adapter 外的 runner 不包含 SmartObject-specific execution branches

#### Scenario: 发出内置 event
- **WHEN** scenario step 发出内置 NPC event subsystem event
- **THEN** runner 调用 registered event adapter，且 runner 不硬编码 gift-event 或 scenario-specific event construction

#### Scenario: Phase 2.8 请求 project-specific behavior
- **WHEN** Phase 2.8 scenario 引用 combat、inventory、quest、door、custom perception 或 custom interaction 之类 project-specific behavior
- **THEN** validation fail 并报告缺少 public project adapter support，而不是向 core runner code 添加 project-specific branches

#### Scenario: Phase 2.9 请求 project-specific behavior
- **WHEN** Phase 2.9 或后续 scenario 引用 combat、inventory、quest、door、custom perception 或 custom interaction 之类 project-specific behavior
- **THEN** 该行为通过 registered project adapter 执行，而不是向 core runner code 添加 project-specific branches

### Requirement: Fixture adapters 准备 runtime actors
Visual acceptance system MUST 支持 fixture adapters，用来创建或解析 scenario 所需 actors 和 components。

#### Scenario: 使用内置 character fixture
- **WHEN** scenario 使用内置 character fixture
- **THEN** fixture adapter 创建或解析 NPC actor、NPC component、observer camera，以及 scenario 需要的任何 declared built-in target actors

#### Scenario: Phase 2.8 使用内置 fixture adapter
- **WHEN** Phase 2.8 scenario 使用 fixture adapter
- **THEN** 它只能使用 built-in/internal fixture adapter，并且该 adapter 仍绑定当前 world/scenario/run context

#### Scenario: Phase 2.8 误新增 public adapter surface
- **WHEN** Phase 2.8 implementation 新增 project adapter public interface、public registry、public character driver、public capability declaration、Blueprint-exposed adapter type、runtime module public dependency，或把 internal adapter 类型放进 public project extension surface
- **THEN** static/review contract fail；该 surface 必须移回 internal/private test boundary 或推迟到 Phase 2.9

#### Scenario: Phase 2.9 使用现有 project actor fixture
- **WHEN** Phase 2.9 project scenario 通过 native class path 和 actor tag 声明 existing actor fixture
- **THEN** fixture adapter 从当前 scenario world 解析 exactly one loaded、valid、non-pending-kill actor，并把它绑定为 per-run ref `fixture.actor`
- **THEN** `actorClass` 只接受 `/Script/<Module>.<ClassName>` native class path，class lookup 不通过 load fallback 拉起资源
- **THEN** valid-shaped native class path 如果不能解析为当前已加载 native class，必须在 `RuntimeStartup` 失败且不得进入 actor enumeration
- **THEN** actor match 使用 exact class equality，而不是 subclass、`IsA`、`IsChildOf` 或 Blueprint generated class match
- **THEN** object reference、soft object path、Blueprint generated class path、short class name、actor tag only、subclass match、component tag、GameplayTag、跨 map reference 和多策略 resolver 在 Phase 2.9 被拒绝

### Requirement: Character drivers 抽象项目 NPC classes
Visual acceptance system MUST 通过 character driver interface 与 NPC character 交互，而不是要求每个项目 NPC 都继承插件 test character class。

#### Scenario: 驱动内置 test character
- **WHEN** fixture 使用内置 test NPC character
- **THEN** built-in character driver 向 runner 和 adapters 暴露 movement、target-distance、visible text 和 NPC component access

#### Scenario: Phase 2.8 驱动 built-in character driver
- **WHEN** Phase 2.8 scenario 需要 character driver
- **THEN** runner 使用 internal built-in character driver seam，而不是直接依赖 concrete test actor API

#### Scenario: Phase 2.9 请求 public project character driver
- **WHEN** Phase 2.9 scenario 或实现需要项目为自己的 `ACharacter` subclass 注册 public character driver
- **THEN** 该请求被拒绝或使 Phase 2.9 计划进入重新审查；public character driver 不能在本阶段从 internal/experimental 升为 public API

### Requirement: Project adapters 在 Phase 2.9 起注册时不修改 core runner
Visual acceptance system MUST 在 Phase 2.9 起暴露最小 registry mechanism，供 project modules 注册当前已有示例和验证覆盖的 adapter category；Phase 2.9 只覆盖 fixture resolver、observation provider 和必要 action seam。event seam、character driver、其它 category 和 future domain adapter 不进入 Phase 2.9 public API。

#### Scenario: Phase 2.9B public adapter categories 有窄行为 contract
- **WHEN** project module 注册 Phase 2.9B `FixtureResolver`、`ActionAdapter` 或 `ObservationProvider`
- **THEN** fixture resolver 实现 `FAINpcVisualFixtureResolveRequest` -> `FAINpcVisualFixtureResolveResult`，只解析当前 run/world 的 class+tag existing actor，并返回 per-run ref `fixture.actor`
- **THEN** action adapter 实现 `FAINpcVisualActionExecuteRequest` -> `FAINpcVisualActionExecuteResult`，只使用 action name 和 resolved target actor，并返回 attempt/accepted/rejected diagnostic
- **THEN** observation provider 实现 `FAINpcVisualObservationSampleRequest` -> `FAINpcVisualObservationSampleResult`，在 `FinalAssertion` 只使用当前 run 的 `fixture.actor` 作为 `SourceActor`，按 typed declaration 读取 runtime state，并返回 typed/sourced state-read observation
- **THEN** exact method names, exact signatures, request/result fields and typed declaration fields follow `phase-2-9b-start-readiness.md` 的 `2.9B public API contract table`
- **THEN** 这些接口不得暴露 result writer、mutable observation store、public character driver、Blueprint adapter type 或 UObject-owning adapter interface

#### Scenario: Project adapter 被注册
- **WHEN** project module 使用唯一 adapter id 和 category 注册 adapter
- **THEN** scenarios 可以引用该 adapter id，core runner 按 category 从 registry 解析

#### Scenario: Adapter id 缺失
- **WHEN** scenario 引用未注册的 adapter id
- **THEN** validation fail，并报告 missing adapter id、adapter category 和 test id

#### Scenario: Adapter id 重复
- **WHEN** 两个 adapters 注册同一个 id，无论 category 是否相同
- **THEN** registration 或 validation fail，而不是静默替换其中一个 adapter 或依赖 category 消除 diagnostics 歧义

### Requirement: Adapter registry lifecycle 明确
Adapter registry MUST 在接受 project adapters 前定义 registration timing、unregistration timing、duplicate handling、category lookup，以及 world/scenario scoping；module-level registry 只能保存 adapter descriptor/factory，scenario runtime context 必须持有当前 world/run 的 adapter instance view 和 observation store。

#### Scenario: Module 在 startup 注册 adapter
- **WHEN** project module 在 startup 阶段注册 adapter
- **THEN** registry 记录 adapter id、category、owner module、registration phase，以及适用时的 supported observation 或 step capabilities

#### Scenario: Module 在 shutdown 注销 adapter
- **WHEN** 注册过 adapters 的 module shutdown
- **THEN** 其 adapters 被移除或标记为 unavailable，后续 scenarios 不会解析到 stale adapters

#### Scenario: 存在多个 world 或 PIE-like contexts
- **WHEN** validation 中可能存在不止一个 world 或 game context
- **THEN** adapter lookup 和 scenario context 防止其他 world 或 previous run 的 adapters、fixtures、observations 满足当前 scenario

#### Scenario: 连续运行两个 scenarios
- **WHEN** 两个 visual scenarios 在同一编辑器会话或相邻进程中连续运行
- **THEN** 第二个 scenario 不能解析到第一个 scenario 的 stale adapters、fixture actors、runtime variables 或 observation records

### Requirement: Adapter capabilities 声明用于 validation
Adapters MUST 声明它们支持的 step types、action types、event payload fields、fixture kinds、observation names 或 assertion inputs，使 scenario validation 能在 runtime 前发现 unsupported behavior。

#### Scenario: Scenario 引用 unsupported adapter capability
- **WHEN** scenario 引用某 adapter id，但要求它执行 unsupported action type、event payload、fixture kind 或 observation name
- **THEN** validation fail，并报告 adapter id、unsupported capability 和 test id
- **THEN** Phase 2.9B fixture resolver validation requires descriptor capability `existingActor.classTag` for `kind: "existingActor"` during `ExtensionDeclaration`
- **THEN** Phase 2.9B project action validation 不从 `actionName` 推导 capability，也不新增 scenario required-action-capability 字段；project observation required capability 来自 `FAINpcVisualObservationDeclaration.Capability`

#### Scenario: Adapter capability 可用
- **WHEN** scenario 引用 supported adapter capability
- **THEN** validation 接受该 scenario 部分，且不需要为该具体行为新增 runner branch

### Requirement: Phase 2.9B project action step 是固定窄入口
Visual acceptance system MUST 支持固定 step type `project.action.execute`，用于调用 registered project action adapter；它不得扩展或复用 `action.executeLatestIntent` 的 latest-intent/SmartObject 语义。

#### Scenario: Phase 2.9B project action payload
- **WHEN** scenario step 声明 `type: "project.action.execute"`
- **THEN** payload 只允许 `adapterId`、`actionName` 和 `targetRef: "fixture.actor"` fields；`adapterId` 由 registered descriptor category 验证，`actionName` 的业务支持由 action adapter 执行结果验证，不由 core schema hardcode
- **THEN** runner 通过 `ActionAdapter` category 找到 adapter 并把 resolved target actor 传给 action adapter
- **THEN** core validation 不实现 `actionName` 到 capability 的通用 matcher 或 door-specific matcher；unsupported action name 由 action adapter 在 step execution 诊断中报告
- **THEN** action adapter success/accepted 只能记录 attempt diagnostic，不能写 final-success observation

#### Scenario: Phase 2.9B door example action descriptor
- **WHEN** Phase 2.9B test/example descriptor 声明 door action
- **THEN** descriptor/scenario 可以使用 `adapterId: "project.door.action"`、capability `projectAction.doorInteract` 和 `actionName: "Interact"` 作为 test data
- **THEN** `projectAction.doorInteract` 只作为 descriptor registration metadata；core parser/runner 不以这些具体字符串分支、特判或决定 schema validity

#### Scenario: Phase 2.9B project action payload 使用旧 action fields
- **WHEN** `project.action.execute` payload 包含 latest LLM intent、SmartObject rejection policy、`allowActionRejection`、`actorRef` 或其它未声明字段
- **THEN** `SchemaParse` fail，并报告 test id、step index、adapter id 和 forbidden field

#### Scenario: Phase 2.9B project action target ref 未解析
- **WHEN** `project.action.execute.payload.targetRef` 不是 `fixture.actor`，或 fixture resolver 没有在当前 run 绑定该 ref
- **THEN** validation/runtime fail，并报告 stage、test id、adapter id 和 target ref
