## Why

当前可视化局内验收链路已经有真实可见的 `UnrealEditor.exe -game` harness 和单一场景来源，但运行时测试逻辑仍然硬编码在对话、事件、SmartObject 和特定 US-2 行为周围。这个形状无法支撑 prompt 驱动的 NPC 行为测试、项目自定义 NPC 系统、多模态视觉或听觉感知；继续加分支只会把核心 runner 写成特性垃圾堆。

## What Changes

- 引入版本化 visual scenario DSL，用 `fixture`、`prompt`、`steps`、`expect` 和 adapter 引用描述测试，而不是继续堆 feature-specific `requireX` 布尔开关。
- 用 scenario runner 替换硬编码 visual scenario 执行逻辑；runner 只执行有限 step 并评估 observation/assertion。
- 增加 adapter registry 模型，用于 fixture 准备、世界事件、动作执行、角色驱动和 observation 采集。
- 保留可见游戏最终验收红线：fake provider、mock response、injected response、bypass、headless、`NullRHI`、commandlet、unattended run 都不能被当作最终 NPC 行为验收。
- 暴露项目扩展接口，让下游项目能接入自己的 NPC actor、动作系统、世界事件、感知系统和 observation provider，而不是修改 AI-NPC 核心 runner。
- 增加 Phase 2.95 做 schema 校验、诊断和 DSL 护栏，不占用 Phase 3.0 编号。
- 明确这是个人开源项目的验收系统演进：每个阶段都必须能被单个维护者完成、理解和长期维护，不能设计成只有大团队才养得起的平台。
- **BREAKING**：scenario 格式会直接升级；不提供旧 schema 双读、兼容 fallback、静默迁移或 legacy 字段支持。

## Capabilities

### New Capabilities
- `visual-game-acceptance`：可见游戏启动、确定性 artifact、最终验收护栏、脚本/运行时职责边界。
- `visual-scenario-dsl`：用于 prompt 驱动 visual game acceptance scenario 的版本化 DSL。
- `visual-acceptance-adapters`：fixture、event、action、character、observation 集成的 adapter registry 和扩展接口。
- `visual-observation-assertions`：可视化验收运行中的命名空间 observation、assertion 评估和 result diagnostics。
- `multimodal-perception-acceptance`：未来基于引擎真实视觉和听觉感知输入的验收覆盖模型。
- `personal-oss-maintainability`：个人开源项目可维护性、可完成性、低维护成本和外部贡献者可理解性约束。

### Modified Capabilities
- `repo-cleanup-governance`：visual acceptance 相关变更仍必须遵守真实可见游戏验收，不能只靠 static、mock、bypass 或 headless check 验收。

## Impact

- 运行时测试架构：`Plugins/AINpc/Source/AINpcCore/Public/Test/` 与 `Plugins/AINpc/Source/AINpcCore/Private/Test/`。
- 场景配置：`Config/AINpcVisualScenarios.json` 以及 `Config/` 下 visual harness prompt/persona 文件。
- 可视化游戏启动与聚合脚本：`scripts/dev/` 与 `scripts/dev/game/`。
- 静态测试系统契约：`scripts/dev/verify-test-system-contract.ps1`。
- 未来 public test extension API：供项目模块接入 combat、quest、inventory、door、多模态视觉、听觉、感知或世界事件验收。
