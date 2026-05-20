Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

For cleanup, de-duplication, structural refactors, and other optimization-oriented work:
- Favor a coherent, well-bounded batch over an artificially tiny patch when that batch removes real redundancy or fixes ownership cleanly.
- Prefer semantic clarity and long-term maintenance over the smallest possible diff.
- Still avoid unrelated churn, speculative abstractions, and scope creep.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" -> "Write tests for invalid inputs, then make them pass"
- "Fix the bug" -> "Write a test that reproduces it, then make it pass"
- "Refactor X" -> "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```text
1. [Step] -> verify: [check]
2. [Step] -> verify: [check]
3. [Step] -> verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

Do not use permission-seeking or conditional handoff phrasing such as "if you want", "if you approve", "if you点头", or similar variants when the next step is already clear. State the next step directly.


# AI Agent Guide

This repository is optimized for terminal-capable coding agents such as Codex and Claude Code.

## First-run behavior

On the first task in this repository, or whenever tooling seems missing, proactively run:

`pwsh ./scripts/dev/ai-stack-doctor.ps1`

Do not wait for the user to explicitly request this. The user is unlikely to remember tool-specific setup steps.

## Proactive tool usage

### Current library/framework docs

When a task depends on current third-party API behavior, prefer Context7 before relying on model memory.

- If `ctx7` is already available, use it directly.
- If project-local Context7 skills are not set up yet and `CONTEXT7_API_KEY` is available, run:
  `pwsh ./scripts/dev/setup-context7.ps1`
- If Context7 is unavailable, fall back to official docs and say so.

### Large-context or zero-context review work

When the task involves:

- broad code review
- external model review
- large refactors
- handing context to another agent

proactively generate a repo pack with:

`pwsh ./scripts/dev/pack-context.ps1`

Use the latest artifact under `.artifacts/ai/` as the handoff bundle.

### Verification loop

For routine source changes, use:

`pwsh ./scripts/dev/test-fast.ps1`

This is the default low-cost loop and should run automatically after meaningful code edits unless the task is docs-only.

## Boundary reminders

- Keep generated Unreal artifacts out of git.
- Keep runtime/editor/UI module boundaries intact.
- Prefer behavioral verification over source-text-scanning tests when adding new coverage.

## Guardrails

- Do not put product/business fallback text, JSON fallback payloads, or user-facing default responses in provider/adapter/HTTP/DB/infrastructure layers.
- Put mutable defaults in config, settings, DataAssets, or dedicated policy/template objects.
- All runtime prompt text, system instructions, and output-contract wording must live in tracked config/template files. C++ may assemble prompts and replace placeholders, but must not hardcode whole prompt bodies in provider or runtime logic.
- Treat large hotspot files as coordination facades, not expansion targets. If a file is already large or already owns multiple concerns, do not add a new concern without first considering extraction.
- Before adding code to an existing class, ask: is this configuration, domain logic, infrastructure logic, or orchestration? Put it in that layer instead of the nearest file.
- Review changes for placement first, implementation second: "is this logic in the right layer/class?" before "does it work?"
- When editing hotspot files, prefer extracting helpers/services/controllers over extending god classes.

## Optional next layer

Langfuse is a recommended future add-on for runtime LLM observability, prompt versioning, and evals.
It is intentionally not auto-wired into the plugin yet because the repository does not currently have a stable trace seam.

<!-- context7 -->
Use the `ctx7` CLI to fetch current documentation whenever the user asks about a library, framework, SDK, API, CLI tool, or cloud service -- even well-known ones like React, Next.js, Prisma, Express, Tailwind, Django, or Spring Boot. This includes API syntax, configuration, version migration, library-specific debugging, setup instructions, and CLI tool usage. Use even when you think you know the answer -- your training data may not reflect recent changes. Prefer this over web search for library docs.

Do not use for: refactoring, writing scripts from scratch, debugging business logic, code review, or general programming concepts.

## Steps

1. Resolve library: `npx ctx7@latest library <name> "<user's question>"` — use the official library name with proper punctuation (e.g., "Next.js" not "nextjs", "Customer.io" not "customerio", "Three.js" not "threejs")
2. Pick the best match (ID format: `/org/project`) by: exact name match, description relevance, code snippet count, source reputation (High/Medium preferred), and benchmark score (higher is better). If results don't look right, try alternate names or queries (e.g., "next.js" not "nextjs", or rephrase the question)
3. Fetch docs: `npx ctx7@latest docs <libraryId> "<user's question>"`
4. If you weren't satisfied with the answer, re-run the same command with `--research`. This retries with sandboxed agents that git-pull the actual source repos plus a live web search, then synthesizes a fresh answer. More costly than the default
5. Answer using the fetched documentation

You MUST call `library` first to get a valid ID unless the user provides one directly in `/org/project` format. Use the user's full question as the query -- specific and detailed queries return better results than vague single words. Do not run more than 3 commands per question. Do not include sensitive information (API keys, passwords, credentials) in queries.

For version-specific docs, use `/org/project/version` from the `library` output (e.g., `/vercel/next.js/v14.3.0`).

If a command fails with a quota error, inform the user and suggest `npx ctx7@latest login` or setting `CONTEXT7_API_KEY` env var for higher limits. Do not silently fall back to training data.
Run Context7 CLI requests outside Codex's default sandbox. If a Context7 CLI command fails with DNS or network errors such as ENOTFOUND, host resolution failures, or fetch failed, rerun it outside the sandbox instead of retrying inside the sandbox.
<!-- context7 -->

## Runtime Verification Hard Rules

- 绝对禁止把无头测试、静态测试、日志回放、NullRHI、`UnrealEditor-Cmd`、`-unattended` 或任何不可视运行，当作 NPC 行为功能的最终验收。
- 只要某个功能最终能在游戏里被玩家直接看到、听到、触发或感知到，就必须补齐对应的可视化自动化编辑器/游戏真实测试链路；没有这条真实测试链路，不得声称该功能已完成最终验收。
- 验证 NPC 行为时，必须打开可视化编辑器或可视化游戏窗口，实际观察 NPC 行为、对话、状态变化和退出过程后，才允许声称“已验证”或“已跑通”。
- 绝对禁止用假数据、手工注入 `FLLMResponse`、测试 bypass、mock provider、`SetDialogueDispatchBypassForTest(true)`、`HandleRequestCompletedForTest(...)` 之类手段，冒充真实功能验收。
- 演示宿主、测试关卡、脚本注入、假对话链路，只能算调试工具，不能算上线标准，更不能对用户声称“功能已可实际使用”。
- 与 NPC、LLM、对话、记忆、行为相关的功能，最终验收标准必须是“可上线实际使用”：真实配置、真实请求链路、真实运行行为，而不是伪造输入后的演示结果。
- 同一轮可视化验证，默认只允许启动一个用于该验证目的的编辑器/游戏实例；如需额外实例，必须先明确说明原因，避免重复拉起多个窗口误导用户。
- 如果当前只能做到 mock、bypass、无头、脚本注入、离线演示中的任一种，必须明确报告“这还不算验收通过”，不得用模糊措辞包装成完成。
## 已踩坑清单（强制避免复犯）

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
- **红线**：如果当前任务存在 OpenSpec，开发必须**严格按照** OpenSpec 文档执行，不得擅自偏离。
- **红线**：未经用户明确要求，**绝对禁止**干涉或催促子代理工作。
- 允许探测子代理进度和工作情况，但**不可擅自干涉**子代理。
- **硬约束**：使用子代理进行开发时，子代理需要需要自测，自我review没问题后才可以交付。
- 在交付时，需回顾本次所有开发内容，并回答以下问题：
- 问题 1：当前实现是否已经是与声明范围相称的最简最小改动？若不是，仍可继续收缩什么？（如果是重构，优化，精简等需求则无需要求修改最小，成品代码最简最小即可。）
- 问题 2：需求行为是否已经完整实现？若没有，缺口是什么？
- 问题 3：当前设计是否仍可更解耦、更优雅？架构是否还能更优？代码质量还能否更高？若可以，受什么约束或为什么这轮没有继续做？如果有往屎山发展的趋势，则该问题优先级大于问题1.
- 问题 4：当前是否仍有 bug、风险点或其他不建议放行的问题？是否有重复造轮，逻辑混乱的情况？若有，逐条说明。
- **硬约束**：任何其它的review意见，都必须亲自核实，客观分析，绝对禁止无脑接受。
- **绝对禁止**修改引擎源码来修插件问题。插件问题只能改插件、脚本、导出链、parser、运行时逻辑。
- 如需参考，引擎源码在G:\UE5\UnrealEngine路径下，版本为5.7
