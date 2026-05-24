## Context

Phase 2.6 已经建立了 AI-NPC visual acceptance 的单一场景来源和可见游戏 harness，但运行时执行路径仍然围绕一个硬编码 data-driven test class。对话状态、事件触发、SmartObject 执行、动作拒绝处理、US-2 特定验收行为，仍然写在 C++ 控制流和脚本特判里，而不是由场景语义表达。

目标系统必须支持可复用行为路径的 prompt 驱动测试编写，同时承认项目特定的世界事件、动作、感知和 observation 必须通过 adapter 接入。玩家可感知 NPC 行为的最终验收路径仍然是：真实 provider + 真实运行行为 + 可见游戏窗口。static check 和 validate-only run 只能算诊断，不能算最终验收。

本文中的 “visual game acceptance” 指可见游戏窗口里的运行时验收模式，不等于只覆盖视觉感官。听觉和多模态感知也可以通过同一套可见游戏 runtime harness 验收，但感知证据必须来自 hearing/vision/perception adapter 和 observation。

## Goals / Non-Goals

**Goals:**
- 当现有 adapter 已覆盖某类行为时，新增可复用 NPC 行为测试只需要新增或修改 prompt/persona 文本和 visual scenario entry。
- 把场景行为从 feature-specific `requireX` 布尔开关迁移到小型版本化 step DSL，并明确 schema、step payload、adapter 引用、prompt variable 和 assertion grammar。
- 通过 adapter 路由 fixture setup、event injection、action execution、character driving 和 observation collection，让核心 runner 不依赖项目玩法系统。
- Phase 2.9 起提供经过验证的扩展接口，让项目模块可接入 combat、inventory、quest、door、多模态视觉、听觉等系统，而不用改核心 runner。
- 保留严格最终验收规则：可见游戏、真实 provider、无 mock、无 injected response、无 bypass、无 headless final run。
- 最终验收 artifact 必须证明 launch mode、provider identity、runtime observations、step diagnostics 和 forbidden-mode absence，而不是只靠“我说成功了”的 JSON。
- Phase 3.0 编号保留给其他工作；DSL/schema/diagnostic hardening 使用 Phase 2.95。
- 每个阶段都必须适合个人开源项目维护：单人可完成、可 review、可回滚、可解释给外部贡献者，不能把测试系统膨胀成大团队平台工程。

**Non-Goals:**
- 不把 JSON 做成通用编程语言。
- 不支持旧 visual scenario schema 的双读、兼容 fallback、静默迁移或 legacy 字段。
- 不让 PowerShell 脚本理解 NPC 业务行为。
- 不修改 Unreal Engine 源码。
- 不把每个项目行为都塞进 core；项目特定行为属于 project adapter。
- 不用 payload construction、日志或 mock stimuli 冒充多模态或听觉行为验收。
- 不在本 change 中引入需要专职平台团队维护的复杂调度、远程服务、数据库、Web 控制台、分布式测试系统或大型观测平台。

## Decisions

### Decision: 使用小型版本化 Scenario DSL，而不是继续加布尔字段

visual scenario 格式使用明确的顶层字段：`schemaVersion`、`testId`、`map`、`timeoutSec`、`storyIds`、`phaseIds`、`fixture`、`persona`、`prompt`、`steps`、`expect`。首批 step type 有意保持很小：`dialogue.start`、`world.event`、`wait.until`、`action.executeLatestIntent`、`observe.hold`。

每种 step type 都有明确 payload contract，而不是开放对象。DSL/schema contract 的单一真源必须是 C++ 中的 scenario schema descriptor/validator；PowerShell 和文档只能消费或静态核对该 contract，不允许再各自硬编码一套独立 schema：
- `dialogue.start`：actor ref 和 prompt ref。
- `world.event`：event adapter id、event tag/id、adapter 需要的 target ref、可选 payload object。
- `wait.until`：timeout，以及当前 step window 内的 `all`/`any`/`anyOf` observation assertion；除非显式声明，否则不吃旧 step 的 observation。
- `action.executeLatestIntent`：action adapter id、actor ref、可选 rejection policy。
- `observe.hold`：duration 和 condition observation。

Phase 2.8 的初始内置 schema contract 必须显式列出内置 adapter id、capability 和 payload 白名单，而不是让开发 agent 现场发明：fixture 使用 `adapterId: "builtin.characterFixture"` 与 `kind: "character" | "characterWithSmartObject"`；`world.event` 使用 `adapterId: "builtin.npcEvent"`、`eventTag` 或 `eventId` 二选一、可选 `targetRef`、可选 `payload`，capability 为 `event.npcSubsystem.broadcast`；`action.executeLatestIntent` 使用 `adapterId: "builtin.smartObjectAction"`、`actorRef`、`allowActionRejection`，capability 为 `action.smartObject.latestIntent`。迁移后旧 `fixture.type`、隐式 event/action adapter、旧 rejection 字段位置都必须 validation fail，不允许双读、fallback 或静默迁移。

Phase 2.7 就必须拒绝 unknown top-level field、legacy top-level field、unknown step type、缺失必填字段、malformed step payload、unresolved prompt variable、artifact identity mismatch、forbidden final-acceptance mode，以及最小 observation/assertion 运行所需的 bad observation name/operator。Phase 2.95 再扩展到完整 nested unknown-field rejection、完整 observation declaration validation、namespace validation 和 assertion operator validation。

**Rationale:** 现有 boolean model（`requireStructuredResponse`、`requireActionIntent`、`allowActionRejection` 等）无法扩展。每加一个新功能就加一个 flag 和 runner 分支。受限 step DSL 能表达有序行为，同时避免 runner 变成项目特定规则引擎。

**Alternatives considered:**
- 继续加 boolean field：拒绝，因为会制造 `requireX` 组合爆炸。
- 在 JSON 里嵌脚本或表达式：拒绝，因为会造出第二套不可审查的编程语言。

### Decision: Phase 2.7 就引入最小 adapter seam，不能做假 DSL

Phase 2.7 可以让 adapter 保持 internal 且范围有限，但 runner 从第一版 DSL 起就必须通过 adapter-like seam 分发 event、action、fixture 和 observation 行为，并提供最小 observation store 与最小 assertion evaluator，否则 `wait.until` 和 `observe.hold` 只能退化成硬编码轮询。不能先写一个套着 DSL 外壳的 `if (TestId == ...)` 或 `if (ActionType == ...)` runner。

**Rationale:** 仍然由 runner 拥有具体 event 构造和 SmartObject 执行的 DSL，只是给硬编码逻辑涂 JSON 口红。2.7 就建立 internal seam，才能降低 2.8 变成痛苦重写的概率。

**Alternatives considered:**
- 先做 DSL，后抽 adapter：拒绝，因为这就是本 change 要避免的烂摊子。

### Decision: 可见游戏启动和运行时验收不进业务脚本

PowerShell 继续负责启动可见 `UnrealEditor.exe -game`、传递确定性 run id/result path、执行 forbidden final-acceptance mode 检查、校验 result artifact。它不判断门有没有开、任务有没有推进、NPC 有没有听见声音、视觉刺激有没有影响行为。

初始可见入口白名单只允许 `UnrealEditor.exe -game`。任何未来“等价可见入口”必须先显式加入 launch contract 和 static validation，才能用于最终验收。

**Rationale:** 业务验收必须来自 UE 对象和系统的 runtime observations，而不是脚本猜测。这样脚本才能在玩法扩展时保持稳定。

**Alternatives considered:**
- 把更多行为判断放进 PowerShell：拒绝，因为它会把脚本耦合到玩法语义，而且无法安全观察 UE runtime state。

### Decision: 最终验收 artifact 必须带 provider 和可见运行证据

最终 visual game acceptance artifact 必须记录安全的 provider identity 和 runtime evidence。provider 身份字段只能来自 `Config/AINpcLocalProvider.json` 这条唯一真源以及运行时 provider resolver 的实际请求链路，不能从脚本参数、环境变量、UE settings、Persona DataAsset 或临时 fallback 配置链重新引入 provider 身份。artifact 需要记录 provider type、base URL present 或 redacted host、model、effort level（如适用）、endpoint、request attempt/status/duration summary、launch executable、redacted launch args、process id、map、run id、result path、visible-entry validation result、runtime observation evidence。

“可见”不是魔法词。最终验收至少要记录：使用非 hidden 可见 launch path、没有 forbidden headless mode、result 由游戏进程内部写出、验收报告包含观察到的 NPC 行为证据，例如 runtime observations、actor state before/after、可用时的 screenshot/viewport capture，或手动观察时的 manual observation notes。log pointer 只能作为辅助诊断，不能单独满足 visible behavior evidence。

**Rationale:** 没有 provider 和 visibility evidence，fake-provider/fake-runtime claim 可以躲在成功 JSON 后面。

**Alternatives considered:**
- 只信脚本 command line：拒绝，因为 command line 无法证明 provider identity 或 runtime behavior。

### Decision: 先做 adapter registry，再公开项目扩展 API

Phase 2.8 先把内置 dialogue、event、SmartObject、fixture、character driver、observation 逻辑拆成 internal adapters，并保持 API 私有。Phase 2.9 再暴露经过验证的最小 public interfaces 给项目模块；public API 只发布当期 example scenario 实际使用并验证的 adapter category，未被当期示例使用的 character driver 或其它 future category 只能保留 internal/experimental，不得进入 public API。

registry 的推荐落点是 scenario/runtime context 持有的 registry view：模块级 registry 只保存 adapter factory 或 descriptor，实际 adapter instance 和 observation store 必须绑定当前 world/scenario/run。生命周期顺序必须明确：module startup 只注册 descriptor/factory；scenario start 在选定 world 后创建 per-run registry view、adapter instances 和 observation store；terminal result 写出前冻结 observation snapshot；scenario end 释放 per-run view、adapter instances、fixture refs 和 observation store；world teardown 必须使残留 per-run refs 失效；module shutdown 注销 descriptor/factory。禁止用跨 run 的 static mutable adapter instance 或 static observation store。执行前校验 adapter id，同 category duplicate id 直接拒绝，并定义 module startup/shutdown、world teardown 和 scenario end 下的注册/注销或失效时机。不能让另一个 world、PIE instance 或上一次 run 的 stale adapter 影响当前 scenario。

**Rationale:** 内部形状没跑通就发布 API，只会把坏设计冻结成公共债务。internal adapter seam 可以先用现有 US-1/US-2 和一个 prompt-only scenario 验证 runner 架构。

**Alternatives considered:**
- DSL 和 public interface 同步发布：拒绝，第一刀太大，而且容易导出半成品 seam。

### Decision: Observation 是 typed、sourced runtime facts，不是 action adapter 写的成功 flag

Observation 记录至少包含：name、value type、value、source kind、适用时的 source object/class 或 subsystem、timestamp 或 elapsed time、sampling method、step index、source adapter/provider id。命名空间包括 `dialogue.*`、`action.*`、`character.*`、`world.*`、`project.<domain>.*`。

Observation store 支持 simple assertion 的 latest-value read，以及 step 需要“某件事在时间窗口内发生”的 event/history read。缺失值导致 assertion 失败。Boolean observation 只有 present 且 true 才满足，除非 assertion 显式检查 false。Numeric observation 必须声明 comparison operator 和 expected value。除非 scenario 显式要求 global/latest observation，前一个 step 的 stale observation 不能满足当前 step-scoped wait。

Action adapter 可以记录 execution-attempt facts，但最终玩家可见成功 observation 必须来自 observation provider 或真实 runtime callback/state reader。API 必须显式区分这两者，避免 “adapter returned success” 冒充 “world state changed”。

**Rationale:** 因为 adapter 返回成功就写 `door.isOpen = true` 的测试是废纸。验收必须反映玩家能感知到的世界状态。

**Alternatives considered:**
- 让每个 adapter 直接标记所有成功 observation：拒绝，因为这会制造假成功并掩盖破损玩法逻辑。

### Decision: Assertion grammar 是有限且声明式的

assertion grammar 只支持声明过的 operator。Phase 2.8 只实现现有 scenarios 和负向验证实际使用的 operator：`all`、`any`、`anyOf`、`equals`、`exists`，以及为 stale/fake-observation 负向验证必需的最小 `notExists` ready/window 语义。numeric operators 只有在 Phase 2.8 scenario 或负向验证明确使用时才允许进入本阶段，否则留到 Phase 2.95。Operator 必须声明 expected value type 和 missing-value behavior。`notExists` 必须要求 observation provider/source 已就绪且采样窗口已覆盖，否则不能用缺失 observation 当成功证据。Unsupported operator 直接 validation fail。

**Rationale:** Assertion 语义含糊会制造 false positive。有限 operator set 才能让 validation 和 diagnostics 可审查。

**Alternatives considered:**
- Free-form expression：拒绝，因为会重造一套无类型脚本语言。

### Decision: 项目特定行为通过 adapter 接入

Core 只提供 sample harness 和 generic ACharacter/dialogue/SmartObject 行为的 built-in adapters。项目模块注册自己的 custom character class、action system、world event、perception stimulus、inventory、quest、combat、door、多模态视觉或听觉 adapter。Phase 2.9 发布的 public API 必须保持最小：优先公开最必要的 fixture resolver、observation provider 和一个 action/event extension seam；character driver 只有在当期 non-core example scenario 实际需要并通过验证时才公开，否则保持 internal/experimental。

**Rationale:** 插件不可能知道每个项目的 NPC 行为。Core runner 必须保持通用，项目必须拥有自己的世界执行和 observation。

**Alternatives considered:**
- 随需求把项目特定行为加进 core：拒绝，因为一定会长成分支堆并破坏可维护性。

### Decision: 每个阶段必须有个人维护者可承受的闭环

Phase 2.7、2.8、2.9、2.95 都必须各自形成可验证闭环。每阶段都应能被单个维护者在有限时间内理解、实现、review 和回滚。新增能力优先使用本地文件、UE runtime、脚本和 C++ 插件内可维护结构，不引入必须长期运营的外部服务或复杂平台。

**Rationale:** 这是有开源打算的个人项目，不是企业平台团队项目。设计再漂亮，如果个人维护者养不起，就是废设计。

**Alternatives considered:**
- 一次性做完整通用测试平台：拒绝，因为范围过大、维护成本高、上线前就会把项目拖死。

### Decision: Phase 2.95 做 DSL 和 diagnostics 收口，不占 Phase 3.0

2.7/2.8/2.9 之后，Phase 2.95 收紧完整 nested schema validation、unknown-field rejection、adapter ID validation、prompt variable resolution、observation declaration checks、namespace rules、negative fixture tests、扩展 provider evidence checks 和 per-step diagnostics；Phase 2.7 已要求的最小 provider/runtime path evidence 不能推迟。

**Rationale:** DSL 不能变成 typo-tolerant junk drawer。Phase 3.0 已经保留给其他工作，所以 hardening 使用 2.x 编号。

## Risks / Trade-offs

- **Risk: 第一版 DSL 太小，覆盖不了未来行为。** → Mitigation: 先支持有限 steps，但新 domain 必须走 adapter/assertion，而不是加 `requireX` 布尔开关。
- **Risk: Adapter API 过早公开，后面难改。** → Mitigation: Phase 2.8 保持 internal adapters，runner 证明可行后再在 Phase 2.9 发布 public API。
- **Risk: Adapter 伪造 observation 导致测试假过。** → Mitigation: API 层区分 action execution result 和 runtime observation record，并要求最终 observation 带 source metadata。
- **Risk: 大矩阵 visible game acceptance 太慢。** → Mitigation: static/unit/provider check 保留为诊断，但玩家可感知验收 gate 仍然必须可见游戏运行；不能用 headless shortcut 替代。
- **Risk: 多模态和听觉测试被误当 payload-only 测试。** → Mitigation: 区分 payload construction check 和 final acceptance；final acceptance 必须触发真实引擎视觉/听觉输入并观察真实 NPC 行为。
- **Risk: 旧 scenario 文件在 schema 变更时坏掉。** → Mitigation: 这是刻意的。scenario 文件和代码同改；不添加 compatibility fallback。
- **Risk: 开发机缺真实 provider 配置。** → Mitigation: 最终验收报告 BLOCKED 并输出安全 provider identity diagnostics；不降级到 fake provider，也不把 validate-only 当行为验收。
