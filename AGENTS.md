# AI Agent 指南

本仓库面向具备终端能力的 coding agents，例如 Codex 和 Claude Code。所有 agent 都必须遵守本文件；如果与用户当前明确指令冲突，以用户当前明确指令为准，但红线除外。

## 1. 编码前先想清楚

**不要假设。不要掩盖不确定。必须暴露取舍。**

### 1.0 绝对流程红线：写代码和 OpenSpec 审查

以下规则高于本文件其它普通协作偏好：

- **写代码必须走零上下文子代理 + 四门模式。** 任何非平凡源码、脚本、测试、配置实现或 OpenSpec 实现，主代理不得直接下场写代码；必须先生成/使用 repo pack 或 handoff bundle，再把明确范围、base commit、Complexity Contract、OpenSpec 任务、禁止项和验证要求交给零上下文开发子代理。
- **开发子代理也必须走四门。** 子代理交付前必须自测、自审，并提交 Complexity Ledger、改动文件、验证 artifact；主代理只能做派工、进度探测、diff/证据核验和后续四门审查，不得替子代理补代码。
- **实现后四门必须由独立零上下文子代理给结论。** QA Execution、complexity-gate、architecture-health-gate、code-quality-gate 的正式 PASS/FAIL/REVIEW 结论必须来自独立零上下文子代理；主代理不能自封通过，也不能用自己的判断替代 gate 结论。
- **OpenSpec 审查必须同时过复杂度、架构和冷水审查。** 审 OpenSpec proposal/design/spec/tasks、开发计划、阶段可开工性或任务拆分时，必须先做 Complexity Contract/复杂度门，再做 architecture-health-gate，再做 cold-water-review；这些结论也必须由独立零上下文子代理输出。主代理可以整理证据，但不能自己给“可开发/可开工/可通过”的最终结论。
- **零上下文不是空上下文。** 子代理必须拿到 bundle/manifest SHA、worktree、base commit、OpenSpec change、精确任务范围、相关 spec/design/tasks/case/diff/evidence artifact、禁止改文件/禁止扩张项和输出模板；prompt 禁止塞主代理结论、怀疑点、期望答案、上一轮 findings 或“刚修了什么”。
- **开工审查看能不能开发，不追求逐句完美。** OpenSpec/开发计划审到“开发完成后基本不会出现方向性错误、阻塞性问题，需求能正常完成”的程度即可。会让开发代理猜主方向、猜架构边界、猜验收口径，或可能导致需求无法完成的问题必须拦；不影响开工和主方向的措辞、细枝末节、可在开发/后续 gate 中处理的注意项，只记录为风险或后续检查，不得用来无限打回。禁止把冷水 review 变成文案打磨、句子洁癖或复杂度膨胀机器。
- **违反即停。** 发现主代理直接写代码、OpenSpec 审查未走独立零上下文复杂度/架构/冷水、或四门结论由主代理自判时，必须立刻停止，声明 `PROCESS_VIOLATION`，列出已污染的改动/结论，并回滚到合规流程；不得继续把错流程产物包装成进度。

实现前：
- 明确说出你的假设。不确定就问。
- 存在多种解释时，列出来；不要静默选择。
- 如果有更简单方案，说出来。该反驳时反驳。
- 如果需求不清楚，停下。指出哪里不清楚，再问。

## 2. 简单优先

**用能解决问题的最小代码。不要投机。**

- 不做需求外功能。
- 单次使用的代码不要抽象。
- 不要添加未被要求的“灵活性”或“可配置性”。
- 不要为不可能发生的情况写错误处理。
- 如果你写了 200 行但 50 行能解决，重写。

问自己：资深工程师会不会觉得这东西过度复杂？会的话，简化。

针对 cleanup、去重、结构性重构和其他优化类工作：
- 如果一个边界清晰的批次能真正消除冗余或修正 ownership，比人为拆成极小 patch 更好。
- 语义清晰和长期维护性优先于最小 diff。
- 仍然禁止无关 churn、投机抽象和 scope creep。

## 3. 外科手术式修改

**只碰必须碰的地方。只清理你自己制造的垃圾。**

编辑现有代码时：
- 不要顺手“改进”旁边代码、注释或格式。
- 不要重构没坏的东西。
- 匹配现有风格，即使你个人会用另一种写法。
- 发现无关 dead code，只报告；不要擅自删。

当你的改动制造 orphan：
- 删除你这次改动导致未使用的 imports/variables/functions。
- 不要删除任务前就存在的 dead code，除非用户要求。

测试标准：每一行改动都必须能追溯到用户请求。

## 4. 面向目标执行

**定义成功标准。循环直到验证。**

把任务转成可验证目标：
- “加 validation” → “写 invalid input tests，然后让它们通过”
- “修 bug” → “写复现测试，然后让它通过”
- “重构 X” → “重构前后测试都通过”

多步骤任务先给简短计划：

```text
1. [步骤] -> verify: [检查]
2. [步骤] -> verify: [检查]
3. [步骤] -> verify: [检查]
```

强成功标准能让你独立循环。弱标准（“让它能用”）需要持续澄清。

不要在下一步已经明确时使用“if you want”、“if you approve”、“if you 点头”这类请求许可或条件交接话术。直接说明下一步。

## 5. 个人开源项目规划准则

- 这是一个有开源打算的个人项目。规划需求时，避免企业内网专用假设、隐藏内部服务依赖、私有基础设施、付费服务锁死，以及只能在某一台开发机上成立的工作流。
- 个人开发者精力有限。规划必须优先可维护性、可完成性和长期低维护成本；不要设计只有团队规模足够大才养得起的平台化大系统。
- 优先选择外部贡献者能理解和复现的方案：清晰扩展点、低门槛 setup、明确配置归属、确定性测试、安全默认值，以及不依赖私有项目历史的文档。
- 核心插件应开箱可用；项目特定行为必须放在 public adapter/interface 后面，不能把某个项目的假设写死进 core runtime。
- secrets、provider config、generated artifacts、本机路径都视为不可移植。面向开源的规划必须区分可提交 examples/templates 和被 ignore 的本地凭据/机器设置。
- OpenSpec change 文档默认使用中文编写；保留 OpenSpec 语法要求的英文关键字（如 `Requirement`、`Scenario`、`WHEN`、`THEN`）即可。

## 6. 首次运行行为

在本仓库第一次执行任务，或工具链看起来缺失时，主动运行：

`pwsh ./scripts/dev/ai-stack-doctor.ps1`

不要等用户显式要求。用户不太可能记得每个工具的初始化步骤。

## 7. 主动工具使用

### 当前库/框架文档

当任务依赖第三方 API 的当前行为时，优先使用 Context7，而不是依赖模型记忆。

- 如果 `ctx7` 已可用，直接使用。
- 如果项目本地 Context7 skills 尚未设置，且存在 `CONTEXT7_API_KEY`，运行：
  `pwsh ./scripts/dev/setup-context7.ps1`
- 如果 Context7 不可用，退回官方文档并说明原因。

### 大上下文或零上下文 review

当任务涉及：

- 大范围 code review
- 外部模型 review
- 大重构
- 给另一个 agent handoff context
- 零上下文子代理审阅

主动生成 repo pack：

`pwsh ./scripts/dev/pack-context.ps1`

使用 `.artifacts/ai/` 下最新 artifact 作为 handoff bundle。

零上下文子代理审阅必须无锚定：prompt 只给 bundle/目标文件、任务类型、禁止改文件、通用审查维度和输出格式；禁止塞入主代理结论、怀疑点、上一轮 findings、刚改内容或期望验证的 fix。要验证已知问题时必须标成定向复核，不准冒充零上下文。

### 验证循环

常规源码改动使用：

`pwsh ./scripts/dev/test-fast.ps1`

这是默认低成本循环；除非任务只改文档，否则有意义的代码编辑后应自动运行。

## 8. 边界提醒

- 不要把 generated Unreal artifacts 放进 git。
- 保持 runtime/editor/UI 模块边界。
- 新增覆盖时，优先行为验证，不要迷信 source-text-scanning tests。

## 9. Guardrails

- 不要把 product/business fallback text、JSON fallback payloads 或 user-facing default responses 放进 provider/adapter/HTTP/DB/infrastructure 层。
- Mutable defaults 放进 config、settings、DataAssets 或 dedicated policy/template objects。
- 所有 runtime prompt text、system instructions 和 output-contract wording 必须存在 tracked config/template files 中。C++ 可以组装 prompt 并替换 placeholders，但不得在 provider 或 runtime logic 中硬编码整段 prompt body。
- 大型热点文件只能作为协调 facade，而不是继续扩张的目标。如果文件已经很大或已有多个职责，新增职责前必须先考虑 extraction。
- 向现有 class 加代码前先问：这是 configuration、domain logic、infrastructure logic 还是 orchestration？放到对应层，而不是最近的文件。
- 先 review placement，再 review implementation：先问“这个逻辑是否在正确 layer/class？”，再问“它能不能工作？”
- 编辑 hotspot files 时，优先抽 helper/service/controller，而不是扩展 god class。

## 10. 可选下一层

Langfuse 是未来推荐的 runtime LLM observability、prompt versioning 和 evals 附加层。
它目前刻意没有自动接入插件，因为仓库还没有稳定 trace seam。

## 11. Context7 规则

当用户询问库、框架、SDK、API、CLI 工具或云服务时，使用 `ctx7` CLI 获取最新文档；即使是 React、Next.js、Prisma、Express、Tailwind、Django、Spring Boot 这类常见技术也一样。适用范围包括 API 语法、配置项、版本迁移、库特定调试、安装配置和 CLI 用法。即使你觉得自己知道答案，也要查；训练数据可能已经过时。优先使用 Context7，而不是 Web 搜索。

不要用于：重构、从零写脚本、调试业务逻辑、代码审查或通用编程概念。

### 步骤

1. 解析库：`npx ctx7@latest library <name> "<user's question>"` — 使用官方库名和正确标点，例如 `Next.js`，不要写 `nextjs`；`Customer.io`，不要写 `customerio`；`Three.js`，不要写 `threejs`。
2. 选择最佳匹配（ID 格式：`/org/project`）：按名称精确度、描述相关性、代码片段数量、来源声誉（优先 High/Medium）和 benchmark score（越高越好）判断。如果结果明显不对，换名称或改写查询。
3. 获取文档：`npx ctx7@latest docs <libraryId> "<user's question>"`。
4. 如果答案不满意，用同一命令加 `--research` 再跑一次。它会使用沙盒代理 git-pull 实际源码仓库并结合实时 Web 搜索再综合答案；成本更高。
5. 基于获取到的文档回答。

除非用户直接提供 `/org/project` 格式的 ID，否则 MUST 先调用 `library` 获取有效 ID。查询要使用用户的完整问题；具体、详细的查询比模糊单词效果更好。每个问题不要运行超过 3 条命令。不要在查询中包含敏感信息，例如 API key、password、credential。

版本特定文档使用 `library` 输出里的 `/org/project/version`，例如 `/vercel/next.js/v14.3.0`。

如果命令因为 quota 报错，告诉用户并建议运行 `npx ctx7@latest login`，或设置 `CONTEXT7_API_KEY` 环境变量来提高限额。不要静默退回训练数据。

Context7 CLI 请求应在 Codex 默认 sandbox 外运行。如果 Context7 CLI 因 DNS 或网络错误失败，例如 ENOTFOUND、host resolution failure、fetch failed，不要在 sandbox 内反复重试，应改到 sandbox 外运行。

## 12. 自动化测试系统

- 测试入口统一放在 `scripts/dev/`，手动触发入口统一放在仓库根目录 `.bat` 文件。禁止再把一次性 `test_*.ps1` 垃圾脚本扔到根目录。
- 静态测试入口：`pwsh ./scripts/dev/test-static.ps1` 或双击 `test-static.bat`。
  - 静态检查脚本必须命名为 `scripts/dev/verify-*.ps1`。
  - `test-static.ps1` 会动态发现并运行所有 `verify-*.ps1`；新增/删除静态检查时不允许手改聚合脚本里的测试清单。
- EditorContext Automation 入口：`pwsh ./scripts/dev/test-editor-context.ps1` 或双击 `test-editor-context.bat`。
  - C++ Automation 测试放在 `Plugins/AINpc/Source/**/Private/Tests/*.cpp`。
  - 测试路径字符串必须以 `AINpc.` 开头，runner 会从 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 中动态发现。
  - 这类测试只算编辑器上下文自动化，不准冒充局内 NPC 行为验收。
- 局内 NPC 可视验证入口：`pwsh ./scripts/dev/test-game.ps1` 或双击 `test-game.bat`。
  - 当前 visual scenarios 以 `Config/AINpcVisualScenarios.json` 为单一来源；共享 harness 脚本放在 `scripts/dev/game/`。
  - 默认使用 `/Game/Maps/AINpcTestMap` 和 `AAINpcTestGameMode`。
  - 新增玩家可感知 NPC 功能验收时，扩展现有 `AAINpcTestGameMode` / `AINpcTestMap` harness 或 scenario adapter；不要为每个功能新增散乱 map/mode/script。
  - 局内 runner 必须启动可见 `UnrealEditor.exe -game`，不得使用 `UnrealEditor-Cmd`、`-nullrhi`、`-unattended` 冒充。
- 全量测试入口：`pwsh ./scripts/dev/test-all.ps1` 或双击 `test-all.bat`。
  - 全量测试必须依次执行：静态测试、EditorContext Automation、局内 NPC 可视验证。
  - 即使前一类失败，也要继续运行后续类别，最后汇总失败；否则会隐藏局内问题。
- 快速开发入口：`pwsh ./scripts/dev/test-fast.ps1` 保持为 build + 静态测试，不代表局内验收。

## 13. 运行时验收硬规则

- 绝对禁止把无头测试、静态测试、日志回放、NullRHI、`UnrealEditor-Cmd`、`-unattended` 或任何不可视运行，当作 NPC 行为功能的最终验收。
- 只要某个功能最终能在游戏里被玩家直接看到、听到、触发或感知到，就必须补齐对应的可视化自动化编辑器/游戏真实测试链路；没有这条真实测试链路，不得声称该功能已完成最终验收。
- 验证 NPC 行为时，必须打开可视化编辑器或可视化游戏窗口，实际观察 NPC 行为、对话、状态变化和退出过程后，才允许声称“已验证”或“已跑通”。
- 绝对禁止用假数据、手工注入 `FLLMResponse`、测试 bypass、mock provider、`SetDialogueDispatchBypassForTest(true)`、`HandleRequestCompletedForTest(...)` 之类手段，冒充真实功能验收。
- 演示宿主、测试关卡、脚本注入、假对话链路，只能算调试工具，不能算上线标准，更不能对用户声称“功能已可实际使用”。
- 与 NPC、LLM、对话、记忆、行为相关的功能，最终验收标准必须是“可上线实际使用”：真实配置、真实请求链路、真实运行行为，而不是伪造输入后的演示结果。
- 同一轮可视化验证，默认只允许启动一个用于该验证目的的编辑器/游戏实例；如需额外实例，必须先明确说明原因，避免重复拉起多个窗口误导用户。
- 如果当前只能做到 mock、bypass、无头、脚本注入、离线演示中的任一种，必须明确报告“这还不算验收通过”，不得用模糊措辞包装成完成。

## 14. 已踩坑清单（强制避免复犯）

- **红线**：未经用户明确同意，**绝对禁止**新增、扩展、默认保留任何形式的“旧兼容”逻辑。
  这里的“旧兼容”包括但不限于：
  旧 JSON / 旧字段 / 旧路径 / 旧配置 / 旧行为的兼容读取、兼容回填、兼容兜底、双写双读、静默迁移。
  如果发现现存逻辑里含有旧兼容链，默认目标是识别、汇报、等待用户决定是否删除；不能擅自继续维护、加固或扩大覆盖面。
- **红线**：防御式编程默认禁止泛化扩张，但如果是**确定必要**、且经过验证能**有效提升成绩或稳定性**，可以保留。
  前提是：
  必须有明确问题场景，而不是“以防万一”。
  必须能解释为什么这是主路径需要的防御，而不是兼容性或兜底补丁。
  必须优先选择通用规则，不能写成平台特判、样例特判、过拟合分支。
  如果只是为旧行为、旧字段、旧路径兜底，仍然归类为“兼容性”，必须先经用户同意。
- **红线**：当用户已明确要求代码实现由子代理完成时，主代理**绝对禁止**自己改代码。
- **红线**：如需启动或重启子代理，**只允许**使用当前批准的前沿模型或更新型号，禁止使用过旧型号。
- **红线**：子代理使用 worktree 开发前，主代理必须明确 intended base commit；子代理第一步必须回报 `git rev-parse --short HEAD`，只有与 intended base 完全一致才允许改代码。本地分支 ahead remote 时尤其危险，禁止让子代理默认从旧 `origin/master` 开始开发。
- **红线**：如果当前任务存在 OpenSpec，开发必须**严格按照** OpenSpec 文档执行，不得擅自偏离。
- **红线**：未经用户明确要求，**绝对禁止**干涉或催促子代理工作。
- 允许探测子代理进度和工作情况，但**不可擅自干涉**子代理。
- **最高优先级硬约束**：动手之前必须先审当前修改方案是否存在可维护性不佳、架构不最优、过度工程、职责放错层、重复造轮、逻辑混乱、往屎山发展的趋势等问题。只要存在这类问题，当前方案就不可接受，必须先改方案；没得商量。
- **硬约束**：进行开发时，无论是主代理还是子代理，都需要自测，自我 review 没问题后才可以交付。
- 在交付时，需回顾本次所有开发内容，并回答以下问题：
  - 问题 1：动手前审过的方案是否已经避开可维护性不佳、架构不最优、过度工程、职责放错层、重复造轮、逻辑混乱、往屎山发展等问题？如果没有，为什么还敢交付？
  - 问题 2：当前实现是否已经是与声明范围相称的最简最小改动？若不是，仍可继续收缩什么？（如果是重构、优化、精简等需求，则无需要求修改最小，成品代码最简最小即可。）
  - 问题 3：需求行为是否已经完整实现？若没有，缺口是什么？
  - 问题 4：当前是否仍有 bug、风险点或其他不建议放行的问题？若有，逐条说明。
- **硬约束**：任何其它 review 意见，都必须亲自核实，客观分析，绝对禁止无脑接受。
- **绝对禁止**修改引擎源码来修插件问题。插件问题只能改插件、脚本、导出链、parser、运行时逻辑。
- 如需参考，引擎源码在 `G:\UE5\UnrealEngine`，版本为 5.7。

## Phase 2.7 visual DSL 说明

- `Config/AINpcVisualScenarios.json` 是 visual game scenarios 的单一发现源，当前只接受 `schemaVersion: 2`。
- 共享启动/聚合脚本仍位于 `scripts/dev/game/` 和 `scripts/dev/test-game.ps1`；新增场景优先只改 scenario 配置、prompt/persona 文件。
- 新增 prompt/config-only scenario 不得在 runner、script dispatcher 或 adapter registry 里加 test-id-specific branch。
- Phase 2.7 不引入远程服务、数据库、Web 控制台、分布式调度、私有服务或长期运行基础设施。
- 开源复现时，提交 `Config/*.example.json`、prompt/persona 模板和 scenario 配置；本地私有 `Config/AINpcLocalProvider.json`、API key、本机 `G:\UE5\...` 路径只属于维护者本机，不是外部贡献者必须条件。
