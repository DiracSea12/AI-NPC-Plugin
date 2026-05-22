## 2.7 - 最小 Step DSL 闭环

- [ ] 1.1 将 `Config/AINpcVisualScenarios.json` 升级为 `schemaVersion: 2`，为现有 `us1.dialogue-action` 和 `us2.perception-behavior` 场景声明必填顶层字段：`testId`、`map`、`timeoutSec`、`storyIds`、`phaseIds`、`fixture`、`persona`、`prompt`、`steps`、`expect`。
- [ ] 1.2 更新 `Plugins/AINpc/Source/AINpcCore/Public/Test/AINpcVisualTest.h`，加入最小 v2 scenario 数据结构，明确表示 fixture spec、prompt variables、persona files、step payloads、expectations、observation names、assertion scopes 和 phase ids。
- [ ] 1.3 更新 `Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcVisualTestRegistry.cpp`，只解析 v2 scenario schema，拒绝 legacy fields、unknown top-level fields、unknown step types，并在无效场景中报告 test id 和字段/step 上下文。
- [ ] 1.4 将 `Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcDataDrivenVisualScenarioTest.cpp` 重构为最小 step runner，支持 `dialogue.start`、`world.event`、`wait.until`、`action.executeLatestIntent`、`observe.hold`，并通过最小 internal adapter seam 执行，而不是 feature-specific `requireX`、test-id、event 或 action 分支。
- [ ] 1.5 定义 Phase 2.7 每个 supported step type 的 payload contract，包括 required fields、optional fields、timeout semantics、actor references、prompt references、adapter id fields 和 allowed policy fields。
- [ ] 1.6 新增一个 visual scenario，只新增 prompt/persona/scenario entry 并使用现有 built-in behavior coverage，证明 prompt/config-only test addition path 成立。
- [ ] 1.7 为 prompt/config-only scenario 留下 review evidence：scenario source 和 prompt/persona files 有变化，而 scenario runner、script dispatcher、adapter registry 没有新增 test-id-specific branch。
- [ ] 1.8 仅在必要时更新 `scripts/dev/game/VisualGameHarness.ps1` 和 `scripts/dev/test-game.ps1`，以保留可见 `UnrealEditor.exe -game` 启动、确定性 child run ids、精确 result artifact paths，以及 validate-only-not-final-acceptance reporting。
- [ ] 1.9 更新 `scripts/dev/verify-test-system-contract.ps1`，校验 v2 scenario sections、supported step types、step payload contracts、prompt variable references、referenced config files、staged visual harness file naming、visible acceptance guardrails、deterministic result artifact requirements，并拒绝旧 schema fields。
- [ ] 1.10 更新 `DEVELOPMENT.md`、`AGENTS.md` 等 test-discovery 文档，说明 visual game scenarios 从 `Config/AINpcVisualScenarios.json` 发现，而 shared harness scripts 仍位于 `scripts/dev/game/`。
- [ ] 1.11 增加个人开源维护性检查：Phase 2.7 不引入远程服务、数据库、Web 控制台、分布式调度、私有服务或必须长期运营的新基础设施。
- [ ] 1.12 增加开源可复现 setup 说明：区分可提交 example/template 和本地私有 provider config，不把 `G:\UE5\...` 这类本机路径、私有 key 或隐藏服务当作外部贡献者必需条件。
- [ ] 1.13 使用 `pwsh -NoProfile -File ./scripts/dev/verify-test-system-contract.ps1`、VerifierHostEditor UBT build command，以及 `Config/AINpcLocalProvider.json` 真实 provider 配置下的 `pwsh -NoProfile -File ./scripts/dev/test-game.ps1` 可见游戏运行验证 Phase 2.7。

## 2.8 - Internal Adapter Registry

- [ ] 2.1 引入 visual scenario runtime context，持有 world、test id、run id、fixture references、NPC component、runtime variables、observation store、current step、failure recorder、provider state summary 和 NPC state summary。
- [ ] 2.2 将 built-in fixture preparation 抽成 internal fixture adapters，覆盖 character-only 与 character-with-SmartObject scenarios，同时让 `AAINpcTestGameMode` 只负责 lifecycle、camera、polling 和 result writing。
- [ ] 2.2a 增加 internal built-in character driver seam，使 runner 不直接依赖 concrete test actor API；project character driver 仍留到 Phase 2.9 才公开。
- [ ] 2.3 将 dialogue delegate binding 和 dialogue observation collection 抽成 internal dialogue observation provider，产出 typed/sourced `dialogue.*` observations。
- [ ] 2.4 将 NPC event subsystem emission 抽成 internal event adapter，使 runner 不再硬编码 gift-event 或 scenario-specific event construction。
- [ ] 2.5 将 SmartObject action intent execution 抽成 internal action adapter，使 `action.executeLatestIntent` 调用 adapter，runner 不再拥有 SmartObject execution details。
- [ ] 2.6 增加 observation store，记录 name、value type、value、source kind、适用时的 source object/class 或 subsystem、sampling method、source adapter/provider id、step index、timestamp 或 elapsed time。
- [ ] 2.7 增加小型 assertion evaluator，支持 `all`、`any`、`anyOf`、`equals`、`notEquals`、`greaterThan`、`greaterThanOrEqual`、`lessThan`、`lessThanOrEqual`、`exists`、`notExists`，并在 step-scoped 或显式 global observations 上运行。
- [ ] 2.8 增加 contract checks 或 API 边界，拒绝 adapter 在没有 runtime observation provider 或真实 runtime state source 的情况下标记最终玩家可见成功。
- [ ] 2.9 删除针对具体 test id（例如 `us2.perception-behavior`）的脚本侧和 runtime 侧特殊分支；这些差异必须通过 DSL steps 和 expectations 表达。
- [ ] 2.10 增加 Phase 2.8 负向验证：bad adapter id、duplicate internal adapter id、adapter-faked final observation、stale observation、`notExists` 未采样成功假阳性都必须失败。
- [ ] 2.11 通过重跑 Phase 2.7 scenario suite、新增 internal adapter fixture test、以及 visible `UnrealEditor.exe -game` + `Config/AINpcLocalProvider.json` 真实 provider + runtime observations 验证 Phase 2.8。

## 2.9 - 项目即插即用扩展接口

- [ ] 3.1 在插件 test/visual API surface 下新增最小 public extension interfaces；优先公开当期 scenario 实际使用并验证的 fixture resolver、observation provider 和必要 event/action seam，不得一次性公开没有示例和验证的 adapter category。
- [ ] 3.2 增加 visual extension registry，记录 adapter category、unique adapter id、owner module、registration phase、supported capabilities 和 observation declarations。
- [ ] 3.3 定义 adapter registration/unregistration lifecycle，覆盖 module startup/shutdown、game world lifetime、duplicate ids、missing ids 和 stale adapter cleanup。
- [ ] 3.4 增加 validation，确保 scenario adapter ids 存在、adapter capabilities 匹配请求的 step/action/event/fixture/observation 用法、adapter ids 不重复，并在缺少 project adapter 时让 validate-only 或 startup 带 adapter category 和 test id 失败。
- [ ] 3.5 增加 existing-actor fixture support，支持 project maps 通过 actor tag、class plus tag 或 object reference 使用真实 actor，且不要求项目 NPC class 继承 built-in test character。
- [ ] 3.6 增加 character driver path，让项目 `ACharacter` subclasses 通过 interface 暴露 NPC component access、movement/target state、visible text surfaces 和 distance observations。
- [ ] 3.7 增加一个最小 project-style example adapter scenario，例如 door、inventory、quest、combat 或 custom interaction 行为；最终成功必须从真实 runtime state 观察，不能来自 adapter return value。
- [ ] 3.8 为 public extension interfaces 增加最小中文使用说明，解释外部项目如何注册 adapter、声明 observations、运行 scenario，以及哪些行为仍必须由项目自己提供 observation provider。
- [ ] 3.9 增加 Phase 2.9 负向验证：missing project adapter、duplicate public adapter id、stale adapter after module/world teardown、unsupported adapter capability、project adapter fake success 都必须失败。
- [ ] 3.10 通过 static contract checks、UBT build，以及一个使用 registered non-core adapter 且不修改 scenario runner 的 visible `UnrealEditor.exe -game` + `Config/AINpcLocalProvider.json` 真实 provider scenario，验证 Phase 2.9。

## 2.95 - DSL 收口、诊断和未来覆盖护栏

- [ ] 4.1 强化 schema validation，拒绝 nested unknown fields、unresolved prompt variables、unsupported assertion operators、undeclared observation names、invalid adapter ids、invalid namespace usage、invalid phase id fields 和 invalid observation scopes。
- [ ] 4.2 要求 observation providers 声明可产出的 observation names、value types、source kinds 和 sampling methods，使 `expect` 在 scenario 引用 unknown observations 时能在 runtime 前 fail。
- [ ] 4.3 定义内置 observation namespace 约定：`dialogue.*`、`action.*`、`character.*`、`world.*`、`project.<domain>.*`，避免未来 PRD、vision、hearing scenarios 发明冲突 observation names。
- [ ] 4.4 在 runtime result artifacts 中增加 per-step diagnostics，包括 step index、step type、适用时的 adapter id、start/end time、duration、status、failure reason、redacted step input summary、provider state summary、NPC state summary 和相关 observation snapshot。
- [ ] 4.5 增加 assertion failure diagnostics，显示 wait condition、missing observations、stale observation timestamps、mismatched numeric values、expected groups、actual values 和最后 satisfied step。
- [ ] 4.6 扩展 final acceptance diagnostics，记录来自 `Config/AINpcLocalProvider.json` 唯一配置真源和运行时 provider resolver 实际请求链路的安全 provider identity 和 request evidence：provider type、model、endpoint/base URL summary、适用时的 effort level、request attempt status、可用时的 HTTP status、duration 和 redacted error summary；禁止从脚本参数、环境变量、UE settings、Persona DataAsset 或 fallback 配置链引入 provider 身份。
- [ ] 4.7 扩展 visible-behavior evidence，包含 runtime observation source metadata，以及 before/after actor、component、subsystem、transform 或 project-state evidence；如果 manual observation 是验收的一部分，则包含 screenshot、viewport artifact 或 manual observation notes；log pointer 只能作为辅助诊断，不能单独满足可见行为证据。
- [ ] 4.8 扩展 redaction checks，确保 result diagnostics 和 status messages 不持久化 API keys、authorization headers、raw provider bodies、raw sensitive prompt/response content 或 project secrets。
- [ ] 4.9 增加验收规则，区分 multimodal/hearing payload construction tests 和 final behavior acceptance；玩家可感知最终声明必须要求 visible game run、real provider 和 real runtime observations。
- [ ] 4.10 明确 future vision input、scene description、camera/trace/perception data、sound/hearing stimuli、project perception systems 的 adapter responsibilities，但不把这些 domain 实现在 core runner 里；scene-description 必须绑定 runtime camera/trace/perception source、actor ids、source transform 和 timestamp，不能用手写文本冒充视觉输入。
- [ ] 4.11 增加 contract checks，确保 PowerShell scripts 不包含 NPC business-acceptance logic，只处理 visible launch、deterministic artifacts、schema validation、result aggregation 和 forbidden final-acceptance modes。
- [ ] 4.12 增加 contract 或 documentation checks，禁止 fake perception flags、mocked hearing/vision stimuli、adapter-faked success observations 或 prompt-only claims 被报告为 final NPC behavior acceptance。
- [ ] 4.13 增加个人开源维护性 contract：新增 DSL 字段、adapter category、observation namespace 或 assertion operator 必须有当前阶段使用者、验证用例、失败诊断和外部贡献者说明；否则拒绝加入。
- [ ] 4.14 增加 negative validation matrix，覆盖 unknown field、legacy field、unknown step、malformed step payload、bad adapter id、duplicate adapter id、unresolved prompt var、undeclared observation、unsupported assertion operator、fake provider、headless flag、artifact identity mismatch、adapter-faked final observation、`notExists` 未采样假阳性、scene-description 缺少 runtime source、连续 scenario/world/run stale registry pollution。
- [ ] 4.15 使用 static contract checks、UBT build、visible game scenario runs 和 negative validation matrix 验证 Phase 2.95，证明每个 invalid case 都能带 clear field、step、adapter、provider、artifact 或 observation diagnostics 失败。
