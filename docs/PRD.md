# AINpc 插件产品需求文档（PRD）

> 来源：AI_NPC_Plugin_Research.md + AI_NPC_Reference_Analysis.md
> 版本：1.7
> 日期：2026-03-01

---

## 一、产品定位

UE5 即插即用的 AI NPC 插件，通过 LLM 驱动 NPC 的对话、情感、记忆和行为，让游戏开发者零 AI 背景即可创建有"灵魂"的 NPC。

**一句话描述**：配置 JSON Provider + 编辑人设 DataAsset = NPC 能说话、能记事、能动感情、能做动作。

**目标用户**：UE5 游戏开发者（独立开发者 / 小型工作室），C++ 或纯蓝图均可。

**竞品参照**：Personica AI（Fab 商城，$80），我们的差异化在记忆深度、情感完整度、安全防护。

---

## 二、核心用户故事

### US-1：基础对话（Phase 1 MVP）
**作为**游戏开发者，**我希望**拖一个组件到 NPC 上、配置 JSON Provider 和人设，NPC 就能和玩家自然对话，**以便**快速验证 LLM NPC 的可行性。

验收标准：
- [ ] `UAINpcComponent` 挂载到任意 Actor，配置 JSON Provider + `NpcPersonaDataAsset` 后可对话
- [ ] 提供 `AAINpcController` 基类（继承 AAIController）和 `UAINpcComponent` 双入口，开发者可选择任一方式接入
- [ ] 支持 OpenAI Provider（首个），异步非阻塞，GameThread 不卡顿
- [ ] 对话气泡 UI 显示 NPC 回复（`AINpcUI` 可选模块）
- [ ] StateTree 驱动对话状态流转：Idle → WaitingForLLM → Speaking → Cooldown，支持超时回退到 Idle
- [ ] LLM 响应等待期间，NPC 播放过渡动画（至少支持"思考"待机动画）
- [ ] 蓝图可完成全流程：绑定人设、发起对话、监听响应、显示文本；Provider 来源不通过蓝图/Settings/Persona 配置，而由 JSON Provider 配置唯一决定
- [ ] Phase 1 采用非流式请求-响应模式；完整响应到达后以**打字机效果逐字显示**（客户端本地模拟，默认 30 字符/秒，可配置），消除"等待→突然弹出完整文本"的割裂感；对话气泡 UI 接口预留流式扩展点（`OnPartialResponse` 委托），Phase 2 接入 SSE 后无缝切换为真正的流式显示

### US-2a：感知事件与结构化输出（Phase 2）
**作为**游戏开发者，**我希望**NPC 能感知玩家行为并输出结构化响应，**以便**游戏逻辑可以解析 NPC 的意图。

验收标准：
- [ ] `NpcEventSubsystem`（GameInstanceSubsystem）接收宿主广播的事件，载荷采用 `FInstancedStruct`（C++ 灵活）+ 蓝图辅助函数封装
- [ ] LLM 输出结构化 JSON（对话 + 动作意图 + 情感变化）
- [ ] `LLMResponseParser` 四级降级：Function Calling / Tool Use（首选）→ 严格 JSON → 宽松提取 → 纯文本
- [ ] 被攻击/收到礼物等事件触发时，NPC 播放对应过渡动画（受击硬直/端详物品）掩盖 LLM 延迟
- [ ] 玩家语言输入与游戏权威事件在上下文结构中分离：玩家说“我打了你”只算玩家话语，宿主游戏逻辑广播攻击事件才算真实受击

### US-2b：SmartObject 行为执行（Phase 2）
**作为**游戏开发者，**我希望**NPC 能通过 SmartObject 执行具体动作，**以便**NPC 的行为不只是说话。

验收标准：
- [ ] StateTree Task 解析动作意图，通过 SmartObject 执行（坐下、拿杯子等）
- [ ] SmartObject 动态注入：Prompt 中列出 NPC 周围可交互对象（通过 `USmartObjectSubsystem::FindSmartObjects()` 查询）
- [ ] 世界语境注入：支持可选题材、时代、社会规则、禁忌配置；未配置时不强造背景，只注入 NPC 实际可感知的局部环境观察，让 LLM 基于可见事实发挥
- [ ] 自建 SmartObjectBridge：槽位查找/占用/释放/位置获取
- [ ] "裁判"架构：LLM 只建议动作，StateTree 验证合法性后才执行

### US-2c：多 Provider 支持（Phase 2）
**作为**游戏开发者，**我希望**能切换不同的 LLM 服务商，**以便**根据成本和地区选择最合适的方案。

验收标准：
- [ ] 新增 AnthropicProvider + LocalProvider（Ollama）
- [ ] DeepSeek 通过 OpenAI 兼容模式接入（国内可直连），Provider 类型、baseUrl、model、API key、fallback provider 均来自 JSON Provider 配置
- [ ] CustomProvider：开发者可通过 JSON 显式配置/注册 seam 接入实现了 `ILLMProvider` 的自定义 endpoint，不通过 Settings、PersonaDataAsset、环境变量、旧字段、双读兼容或静默迁移扩展来源
- [ ] Provider 能力来自显式 capability 声明并驱动降级（无 JSON Mode 时用 prompt 约束）；不得写成按 model 名称自动探测

### US-2d：流式响应（Phase 2）
**作为**游戏开发者，**我希望**NPC 的回复能逐字显示，**以便**降低玩家的等待感。

验收标准：
- [ ] 自建 SSE Parser 处理流式响应（`data:` 前缀、`[DONE]` 终止、跨包拼接、连接中断重连、心跳 `:` 注释行）
- [ ] 对话气泡支持逐字/逐句显示模式

### US-3：记忆存储与检索（Phase 3a）
**作为**游戏开发者，**我希望**NPC 能记住和玩家的交互历史，**以便**产生"这个 NPC 认识我"的沉浸感。

验收标准：
- [ ] 三层记忆实现：工作记忆（上下文窗口）/ 情景记忆（TArray）/ 长期记忆（SQLite）
- [ ] Stanford 检索公式：Score = α×Recency + β×Importance + γ×Relevance
- [ ] α/β/γ 可在 DataAsset 中配置（不同 NPC 类型不同预设）
- [ ] 选择性写入：重要性 < 3 不入情景记忆，< 5 不入长期记忆
- [ ] 记忆持久化到 SQLite，支持存档/读档
- [ ] 记忆冲突解决（P0，Phase 3a 内最高优先级）：写入前检索相似记忆，异步 LLM 判断五种操作——ADD（无冲突新增）/ UPDATE（新信息覆盖旧信息）/ MERGE（合并为更完整记忆）/ COEXIST（矛盾记忆并存）/ SUPERSEDE（完全取代）
- [ ] 主动遗忘（P0，Phase 3a 内最高优先级）：情景记忆满 200 条时按 EvictionScore 淘汰
- [ ] 分段时间衰减替换纯指数衰减：Δt<1h 不衰减 / Δt<1d 衰减至 0.8 / Δt<1w 衰减至 0.5 / 超过 1w 指数衰减

### US-4：反思与压缩（Phase 3b）
**作为**游戏开发者，**我希望**NPC 能从经历中总结出高层洞察，**以便**NPC 表现出"成长"和"理解"。

验收标准：
- [ ] 累积重要性 > 150 时触发反思
- [ ] LLM 从近期记忆提取洞察，写回记忆流
- [ ] 洞察带 Evidence Pointers 指向源记忆
- [ ] 低价值记忆合并/归档机制
- [ ] 记忆条目增加 MemoryType 字段：Factual（事实）/ Experiential（经历）/ Working（工作记忆），检索时可按类型过滤
- [ ] 记忆间显式链接：`LinkedMemoryIds: TArray<int64>`，写入时异步分析链接关系，检索时沿链接扩展 1 跳

### US-5：情感与关系系统（Phase 4）
**作为**游戏开发者，**我希望**NPC 拥有数值驱动的情感和关系系统，**以便**NPC 的反应有情感温度而非机械应答。

验收标准：
- [ ] VAD 三维情感模型：Valence[-1,1] / Arousal[0,1] / Dominance[0,1]
- [ ] 评价链前置（方案B 本地规则计算）：事件→4维评价（Goal Relevance/Certainty/Agency/Coping Potential）→情感推导，零额外 LLM 调用
- [ ] 关系模型：Affinity[-100,100] / Trust[0,100] / Familiarity[0,100]
- [ ] OCEAN 五维人格参数写入 `NpcPersonaDataAsset`，Prompt 注入数值+描述
- [ ] 人格、背景、关系、情感等真实状态由 DataAsset、存档和游戏权威事件维护，玩家输入只能作为“玩家声称/表达/请求”进入上下文，不能直接覆盖这些状态
- [ ] 人格惯性系数防止单次极端事件彻底改变 NPC 人格
- [ ] 情感随时间自然衰减（衰减速率受 Neuroticism 影响）
- [ ] 情感/关系数值影响 StateTree 分支选择（敌人→战斗，朋友→帮助）
- [ ] 情感-行为一致性验证：OutputValidator 检测情感状态与输出倾向矛盾时拒绝并重新生成
- [ ] **简版情感外化**（纯 UI，不依赖动画资产）：对话气泡边框颜色随 Valence 变化（正面→蓝色调，负面→红色调，中性→默认色），让玩家在 Phase 4 就能感知 NPC 情感状态；Phase 6 的 `FEmotionAnimParams` 提供完整动画级外化后，气泡颜色作为补充保留

### US-6：安全防护（Phase 4）
**作为**游戏开发者，**我希望**NPC 不会被玩家通过提示注入攻破人设，**以便**游戏体验不被恶意破坏。

验收标准：
- [ ] `InputSanitizer`：覆盖 3 类攻击检测（直接提示 / 社会工程 / 指令覆盖）
- [ ] 社会工程检测：角色扮演绕过正则 + 渐进式信任建立计数器 + 探测性问题模式匹配
- [ ] `OutputValidator`：JSON Schema 校验 + 动作白名单 + 人设边界检测 + 敏感内容过滤
- [ ] 输出泄露兜底：检测响应中是否包含 system prompt 片段（余弦相似度>0.85）
- [ ] 异常亲密度增长检测：Familiarity 增速超阈值时触发警告

### US-7：打磨与工具（Phase 5）
**作为**游戏开发者，**我希望**有编辑器工具帮助我调试和配置 NPC，**以便**降低开发和调试成本。

验收标准：
- [ ] 编辑器人设编辑面板（`PersonaEditor`）
- [ ] 记忆调试器（`MemoryDebugger`）：可视化记忆流、检索结果、冲突解决过程
- [ ] NpcScheduler：10 个 NPC 同时活跃时 GameThread 帧时间增量 < 2ms；距离 > 50m 的 NPC LLM 调用频率 ≤ 1次/5秒
- [ ] 测试框架：交互回放（固定 seed/mock 响应）+ 人设一致性评分（OutputValidator 拒绝率统计）
- [ ] 示例项目：至少包含 3 个不同人设的 NPC 演示场景

### US-8：沉浸感增强（Phase 6）
**作为**游戏开发者，**我希望**NPC 拥有自主生活节奏、能主动与玩家互动、NPC 之间能社交，**以便**玩家感受到 NPC 是"活着的角色"而非"被动应答器"。

验收标准：
- [ ] NPC 拥有日程表（`UNpcScheduleDataAsset`），非对话时自主执行日常行为（巡逻/工作/休息/社交），通过 StateTree 扩展驱动
- [ ] 主动交互触发：基于关系/记忆/情感状态条件，NPC 主动向玩家发起对话或动作（招手/警告/赠礼）
- [ ] NPC 间轻量社交：NPC↔NPC 对话通过小模型或模板驱动，结果广播到 NpcEventSubsystem
- [ ] 流式首 Token 优化：利用已有 SSE 能力，首个可见字符延迟 < 500ms
- [ ] 情感外化接口：VAD 状态映射到动画蓝图参数（表情/姿态/语速），项目方可绑定到自己的动画系统

---

## 三、功能需求清单

### LLM 通信层
- FR-1：`ILLMProvider` 接口抽象，支持 OpenAI / Anthropic / DeepSeek（兼容 OpenAI 格式，国内可直连）/ Local（Ollama）/ Custom 五类 Provider；除本地部署模型外，Provider 类型、baseUrl、model、API key、fallback provider 必须以 JSON Provider 配置为唯一来源
- FR-2：统一 Request/Response 结构，Provider 能力由显式 capability 声明驱动请求形态和降级（无 JSON Mode 时用 prompt 约束），不得依赖 model 名称猜测或散落配置来源
- FR-3：异步非阻塞调用，GameThread 零等待
- FR-4：自建 SSE Parser 处理流式响应（`data:` 前缀、`[DONE]` 终止、跨包拼接、连接中断重连、心跳 `:` 注释行、`error` 事件处理）
- FR-5：自动重试（指数退避）+ 超时 + 降级（Phase 1 降级到预设模板响应；Phase 2+ 降级链：JSON 显式配置的本地 SLM/fallback provider（本地部署模型例外）→ 预设模板响应 → 静默失败并通知蓝图；LocalProvider 与降级 SLM 可为同一实例，未配置时跳过直接走模板）
- FR-6：Provider 来源配置以 JSON Provider 配置为唯一权威：Provider 类型、baseUrl、model、API key、fallback provider 不得从 `UAINpcSettings`、`NpcPersonaDataAsset`、环境变量、旧字段、双读兼容或静默迁移读取；本地部署模型除外。`UAINpcSettings` 只承载 timeout/retry/template/concurrency 等非 provider-source 通用运行时参数。
- FR-7：C++/蓝图双通道：静态委托（C++）+ 动态多播委托（蓝图）
- FR-8：提供 `AAINpcController` 基类（继承 AAIController）和 `UAINpcComponent` 双入口；StateTree 由 AIController 持有和 Tick（AIComponentSchema 要求），Component-only 模式下插件自动创建轻量 AIController 并绑定，开发者无需手动处理；两种模式下 `FStateTreeTask_LLMQuery` 统一通过 `UAINpcComponent` 获取 NPC 上下文

### 记忆系统
- FR-9：三层记忆：工作记忆（上下文窗口 ~20 条，随对话 Session 创建/销毁，宿主为 `UAINpcComponent`）/ 情景记忆（TArray ~200 条，随 NPC 实例生命周期，宿主为 `UAINpcComponent`）/ 长期记忆（SQLite 无限，全局共享单个 DB 文件，按 NpcId 分表，由 `UMemorySubsystem` 持有连接）
- FR-10：Stanford 检索公式 Score = α×Recency + β×Importance + γ×Relevance，α/β/γ 可配置；各分量通过 `IRelevanceScorer` 接口计算，项目方可替换默认实现
- FR-11：分段时间衰减：Δt<1h 不衰减 / Δt<1d=0.8 / Δt<1w=0.5 / 超过 1w 指数衰减（替换纯指数衰减）
- FR-12：选择性写入：重要性 < 3 不入情景记忆，< 5 不入长期记忆
- FR-13：记忆冲突解决：写入前检索相似记忆（有 Embedding 用向量检索，无 Embedding 降级为 FTS5 BM25 相似度），Top-K=5 候选，相似度阈值默认 0.75（可配置），异步 LLM 判断 ADD/UPDATE/MERGE/COEXIST/SUPERSEDE（计入**记忆维护池**并发配额，不占用对话池；维护池满时冲突解决降级为 ADD）；LLM 判断失败时降级为 ADD；COEXIST 的矛盾记忆检索时全部返回并标记 `Contradicted` 标签，由 Prompt 层呈现矛盾供 LLM 自行判断；`UAINpcSettings::EnabledConflictOperations`（默认全部启用）允许开发者按需裁剪启用的冲突操作子集（保守配置可仅启用 ADD/UPDATE/COEXIST）
- FR-13A：玩家提及记忆加权——对话中如果玩家输入包含某条记忆的关键词（通过 FTS5 匹配），该记忆的 Importance 自动提升 `PlayerMentionBoost`（默认 +3.0，可通过 `UAINpcSettings` 配置），防止对玩家重要但情感变化小的记忆被淘汰
- FR-14：主动遗忘：EvictionScore = w1×(1-Recency) + w2×(1-Importance) + w3×(1-AccessFrequency)
- FR-15：反思机制：累积重要性 > 150 触发，LLM 提取洞察写回记忆流，洞察带 Evidence Pointers（轻量实现：洞察条目存储 `SourceMemoryIds: TArray<int64>` 指向源记忆，无需额外存储层）
- FR-16：记忆持久化到 SQLite，支持存档/读档，条目带 SchemaVersion 支持迁移
- FR-17：`IEmbeddingProvider` 接口（Phase 3a 随记忆系统引入，FR-13 向量检索的前置依赖），无 Embedding 时降级为 SQLite FTS5 全文搜索
- FR-18：记忆条目 MemoryType 字段：Factual / Experiential / Working，检索时可按类型过滤
- FR-18A：记忆条目必须记录事实来源权威级别：玩家对话产生的事实默认是 PlayerClaim（玩家声称），游戏系统广播的事件才是 AuthoritativeGameEvent，插件/存档维护的 NPC 状态是 SystemState；PlayerClaim 不得在冲突解决、反思或检索呈现中升级成 Factual/SystemState
- FR-19：记忆间显式链接 `LinkedMemoryIds`，写入时异步分析链接关系，检索时沿链接扩展 1 跳

> **FMemoryEntry 字段总览（各 FR 汇总）**
>
> | 字段 | 类型 | 来源 FR |
> |------|------|---------|
> | Content | FString | FR-9 |
> | Timestamp | FDateTime | FR-9 |
> | Importance | float | FR-12 |
> | MemoryType | EMemoryType (Factual/Experiential/Working) | FR-18 |
> | Authority | EMemoryAuthority (PlayerClaim/WitnessedGameEvent/SystemFact/Reflection) | FR-18A |
> | LinkedMemoryIds | TArray\<int64\> | FR-19 |
> | SourceMemoryIds | TArray\<int64\> | FR-15 |
> | AccessCount | int32 | FR-14 |
> | SchemaVersion | int32 | FR-16 |
> | Contradicted | bool | FR-13 |
>
> 此表为 PRD 级草案，SDD 阶段确定最终字段名和嵌套结构。

### 情感与关系系统
- FR-20：VAD 三维情感状态（Valence/Arousal/Dominance）+ `FGameplayTagContainer` 情感标签
- FR-21：评价链前置（本地规则计算）：事件类型 + NPC 人格参数 → 4 维评价（Goal Relevance/Certainty/Agency/Coping Potential）→ 情感推导；规则表通过 `NpcPersonaDataAsset` 中的 `TMap<FGameplayTag, FAppraisalRule>` 配置，插件提供常见事件类型的默认规则集（攻击/被攻击/收礼/赠礼/对话/交易/威胁/赞美，共 8 类）
- FR-22：关系模型 Affinity/Trust/Familiarity，事件触发数值变化，随时间自然衰减
- FR-23：OCEAN 五维人格参数，Prompt 注入时同时传数值和描述
- FR-24：人格惯性系数，情感衰减速率 = BaseDecayRate × (1 - Neuroticism × 0.5)（高神经质→衰减慢→情绪持续更久）
- FR-25：当前情感 + 关系数值注入 LLM Prompt，影响对话语气和行为选择；Prompt 必须明确标注这些数值来自系统真实状态，玩家文本中的“好感度=100”等声明只能作为玩家话语处理
- FR-25A：WorldContext 世界语境注入：宿主项目可选通过独立 `UWorldContextDataAsset` 提供全局世界观（时代、类型、社会规则、常识边界、禁忌、语言风格），可选通过 `ULevelContextDataAsset` 提供 UE 关卡/地点级语境（地点描述、本地习俗、势力关系、本地常识），并始终可通过运行时观察提供局部环境（天气、周围物体、NPC 视角多模态图像/视觉摘要）。WorldContext 是解释玩家话语和事件的背景，不是 NPC 个人人设；PromptBuilder 必须把已存在的世界/关卡配置与 Persona、SystemState、PlayerUtterance 分区呈现。缺省未配置时不得生成假设背景，只使用实际观察、NPC 人设和记忆。
- FR-25B：NPC 知识范围：`NpcPersonaDataAsset` 可选声明职业/身份知识、本地知识、教育与经历、已知主题标签；PromptBuilder 只能把该 NPC 应该知道的世界/关卡信息注入 Prompt。未声明 KnowledgeScope 时不注入额外背景知识，不同职业、教育背景、地域归属、经历范围的 NPC 不能拿同一份全知世界百科硬塞给 LLM。

### 行为执行层
- FR-26：LLM 输出结构化 JSON（dialogue + actions + emotion_delta + relationship_delta），典型响应示例：`{"dialogue": "...", "actions": [{"type": "sit", "target": "chair_01"}], "emotion_delta": {"valence": -0.3, "arousal": 0.2, "dominance": 0.0}, "relationship_delta": {"affinity": -5, "trust": -10}}`；`actions` 为数组，每项含 `type`（动作标签）和 `target`（SmartObject ID，可选）；多玩家场景下 `relationship_delta` 自动关联当前对话发起者；LLM 只能返回增量建议，不能设置绝对状态值，最终是否应用由本地规则、权威事件来源和 OutputValidator 裁决
- FR-27：`LLMResponseParser` 四级降级：Function Calling / Tool Use（首选，Provider 支持时优先使用）→ 严格 JSON Schema 校验（JSON Mode）→ 宽松 JSON 提取（正则匹配 `{...}` 块）→ 纯文本降级（仅提取对话文本，行为使用默认模板）
- FR-28：核心 StateTree 状态定义：Idle → WaitingForLLM → Speaking → Cooldown → Idle，超时（默认 4s，可配置，与 NFR-1 P95 指标对齐——预期约 5% 请求触发超时回退，属正常降级行为）自动回退 Idle；插件提供默认 StateTree 资产，项目方可替换
- FR-29：自定义 StateTree Task（`FStateTreeTask_LLMQuery`、`FStateTreeTask_ExecuteSmartObject`）
- FR-30：SmartObject 动态注入：构建 Prompt 前查询 NPC 周围可交互对象，注入合法动作列表；这些对象同时可作为局部环境观察。未配置 WorldContext/LevelContext 时，SmartObject 和感知观察就是主要世界语境来源。
- FR-31：自建 SmartObjectBridge 模块：槽位查找/占用/释放/位置获取
- FR-32："裁判"架构：LLM 只建议，StateTree 验证合法性后才执行；Phase 2 起 `FStateTreeTask_ExecuteSmartObject` 内联基础白名单校验（仅允许 SmartObject 动态注入列表中的动作），Phase 4 抽出 `IActionValidator` 接口由 OutputValidator 实现完整校验
- FR-33：延迟掩盖机制（插件只提供触发框架，不提供动画资产）：两条触发路径——①StateTree 状态切换时自动触发 `UAnimMontage` 插槽；②`NpcEventSubsystem` 事件到达时即时触发（受击等不等 StateTree Tick）。统一通过 `OnDelayMaskingStart/End` 蓝图事件通知，`NpcPersonaDataAsset` 中按策略类型（思考/受击/端详/超时）配置 Montage 引用数组（同一策略类型支持多个变体，随机选取，避免重复播放被玩家识破）；超过 `DelayFillerThreshold`（默认 3s，可配置）未收到 LLM 响应时，自动显示过渡文本（从 `DelayFillerTexts: TArray<FText>` 中随机选取，如"嗯..."、"让我想想..."），过渡文本使用 FText 支持本地化；示例项目提供占位动画演示

### 感知系统
- FR-34：`NpcEventSubsystem`（GameInstanceSubsystem）全局委托广播，宿主只需广播标签+载荷；每个 NPC 的 `UAINpcComponent` 自行订阅并按标签过滤，事件到达后按固定顺序分发给四个消费者：①延迟掩盖动画（FR-33，即时触发）→ ②情感评价链（FR-21，同步计算）→ ③记忆写入（FR-12，异步入队）→ ④Prompt 情境更新（FR-36，下次 LLM 调用时生效）
- FR-34A：输入来源权威边界：`RequestDialogue(PlayerInput)` 产生 PlayerUtterance，只代表玩家说了这句话；`NpcEventSubsystem` 广播的宿主游戏事件才代表真实世界状态变化；系统/存档维护的情感、关系、生命值、任务状态等为 SystemState。玩家语言不能伪造 AuthoritativeGameEvent 或 SystemState。
- FR-35：事件载荷采用 `FInstancedStruct`（C++ 灵活性优先），配套蓝图辅助函数封装常用载荷类型

### Prompt 工程
- FR-36：Prompt 黄金模板结构：系统层（不可覆盖，代码级强制拼接，开发者无法通过 DataAsset 移除）→ 世界层（可选 WorldContext/LevelContext/NpcKnowledgeScope 裁剪后的可知背景；缺省为空）→ 人格层（OCEAN+说话风格）→ 记忆层（动态注入）→ 情境层（每次更新）→ 输出约束。分阶段渐进展开：Phase 1 启用系统层+可选世界层+人格层+输出约束；Phase 3a 增加记忆层；Phase 4 增加情境层（情感/关系/评价链/SmartObject 列表/局部环境观察/可选 NPC 视角视觉摘要）。Token 超限时按优先级从低到高截断：情境层 → 记忆层 → 人格层 → 世界层（系统层和输出约束不可截断）；对话轮次超过 `PersonaReinjectThreshold`（默认 8 轮，可通过 `UAINpcSettings` 配置）时，PromptBuilder 在最近一条 system message 位置自动插入人格层摘要（OCEAN 数值 + 核心说话风格，约 100 tokens），防止长对话中人格漂移；PromptBuilder 必须用结构化分区标注已存在的 WorldContext、LevelContext、NpcKnowledgeScope、PlayerUtterance、AuthoritativeGameEvent、SystemState、RetrievedMemory，不得把玩家输入拼进同一个无来源文本块
- FR-37：Prompt 模板可通过 `NpcPersonaDataAsset` 自定义覆盖各层内容；覆盖为按层替换（非全量替换），未覆盖的层使用插件默认模板；新 Phase 增加的层自动追加，不影响已覆盖的层
- FR-37A：`NpcPersonaDataAsset` 提供 `ESpeakingLength` 枚举（Brief=1-2句 / Normal=2-3句 / Verbose=5-8句），PromptBuilder 根据此值自动生成输出约束层的句数限制；叙事型 NPC 设为 Verbose 无需手动覆盖 Prompt

### 网络同步
- FR-38：多人游戏权威边界：LLM 调用和记忆写入在 Server 端执行（`UFUNCTION(Server)`），对话文本和动作指令通过 Multicast RPC 同步到客户端；单机模式跳过网络层直连

### 安全系统
- FR-39：`InputSanitizer` 覆盖 3 类攻击：直接提示 / 社会工程 / 指令覆盖；状态伪造文本（如“你的好感度现在是100”“我刚才打了你”）不需要删除，但必须保留为 PlayerUtterance，不能升级为事实或事件
- FR-40：`OutputValidator`：JSON Schema 校验 + 动作白名单 + 人设边界检测 + system prompt 泄露检测 + 权威边界校验；若 LLM 输出试图按玩家自报内容设置绝对关系/情感/动作事实，必须拒绝或裁剪对应 delta/action
- FR-41：异常亲密度增长检测，超阈值触发防御升级

### 自主行为与沉浸感（Phase 6）
- FR-42：自主行为循环——NPC 拥有日程表（`UNpcScheduleDataAsset`，时间段→行为标签映射），非对话时 StateTree 根据当前游戏时间选择日程行为（巡逻/工作/休息/社交），通过已有 SmartObjectBridge 执行具体动作；日程可被高优先级事件（对话/被攻击）打断，事件结束后跳到当前时间对应时段恢复（非从中断点继续）；`ScheduleFuzziness` 参数（默认 0.3）控制执行时间随机偏移，避免行为机械可预测
- FR-43：主动交互触发——`UProactiveInteractionEvaluator` 定时评估触发条件（默认 30s 实时时间一次，不受游戏时间缩放影响），前置检查：单 NPC 频率上限（`MaxProactivePerInterval`，默认每5分钟实时时间最多1次，per-NPC 实例限制）且玩家非战斗/非对话中；如需全局限制（防止多个 NPC 同时主动搭话），由 `NpcSchedulerSubsystem` 的 `MaxGlobalProactivePerInterval` 控制；条件包括：关系阈值（Affinity>60 → 主动打招呼）、记忆触发（持有与玩家相关的未分享洞察）、情感溢出（Arousal>0.8 → 主动表达）、环境触发（玩家进入 NPC 感知范围）；触发后通过 NpcEventSubsystem 广播 `ProactiveInteraction` 事件，StateTree 消费并执行
- FR-44：NPC 间社交协议——NPC↔NPC 对话通过轻量 LLM 调用（优先使用 LocalProvider 小模型，降级为模板）或预设对话模板驱动；LLM 社交请求使用 `ELLMRequestPriority::Social`（优先级：Dialogue > Reflection > ConflictResolve > Social，队列满时 Social 直接降级为模板）；LLM 路径最大 2 轮（`MaxSocialLLMRounds = 2`，可通过 `UAINpcSettings` 配置）且每次重注入人格层（防 persona drift）；对话结果写入双方记忆系统（标记 `bShareableWithPlayer` 支持口碑传播）；社交对象选择：`SocialSearchRadius`（默认 2000cm/20米）内 70% Familiarity 最高者 + 30% 随机（避免固定社交圈），0 个可选 NPC 时跳过社交回退日程，1 个可选 NPC 时 100% 选择；NPC↔NPC 关系数据独立存储于 `npc_relationships` 表；`MaxConcurrentSocial`（默认 2 对）为全局限制，由 NpcSchedulerSubsystem 管控；通过 NpcEventSubsystem 广播，玩家可旁观；社交频率受 NpcScheduler 管控；社交模板台词使用 FText（支持本地化）
  > **设计决策**：当前版本仅支持 1v1 NPC 社交，群组对话（3+ NPC，参考 Inworld Multi-Agent 的 2-5 角色群组对话 + Director Layer）作为 Phase 7+ 扩展方向。
- FR-45：流式首 Token 优化——SSE 流式模式下，PromptBuilder 在流首个 `data:` 到达时立即触发 `OnFirstTokenReceived` 委托，对话气泡即时开始显示；Prompt 结构按缓存友好排列（稳定前缀：系统层+人格层，动态后缀：记忆层+情境层+输出约束层），利用 prefix caching 降低 TTFT（输出约束虽内容稳定但位于动态内容之后，不参与 prefix caching，格式遵从度优先于缓存命中率）；延迟目标：本地模型 P50 < 200ms，云端 API P50 < 500ms；可选连接预热（玩家进入感知范围时预建立 HTTP 连接，`PrewarmConnectionTimeout` 默认 30s 空闲后释放）
- FR-46：情感外化接口——`UAINpcComponent` 暴露 `GetEmotionAnimParams()` 返回经 Lerp 平滑后的 `FEmotionAnimParams`（Valence/Arousal/Dominance 归一化 + 主情感标签 + Intensity），Intensity 公式：`Arousal × max(|Valence|, 0.3)`；`EmotionLerpSpeed`（默认 2.0）控制过渡速率，`MinEmotionDuration`（默认 2s）防止表情闪烁；多人模式下 VAD 变化 > 0.1 时才广播同步，`MinEmotionBroadcastInterval`（默认 0.5s）限制最大广播频率；插件不提供动画资产，只提供数据接口
- FR-47：成本治理（Budget Governor）：在 `NpcSchedulerSubsystem` 中引入三层预算阈值，`per-player`（单玩家会话预算）、`per-npc`（单NPC预算）、`per-shard`（单服预算）；预算不足时按优先级降级（社交模板 > 本地SLM > 云端LLM），并广播预算事件供 UI/日志展示

---

## 四、非功能需求

- NFR-1：LLM 响应延迟 P95 < 4 秒（含网络），超时自动降级到本地 SLM 或预设模板
- NFR-2：GameThread 零阻塞，所有 LLM 调用和记忆写入异步执行
- NFR-3：同帧最大并发 LLM 请求 ≤ 3，采用双池调度：对话池（高优先，默认并发2）+ 记忆维护池（后台，默认并发1），槽位数通过 `UAINpcSettings` 可配置（非 constexpr 硬编码）；远距离 NPC 调用频率降至 1/5（LOD）；Phase 1 起通过简单计数器限流，Phase 5 NpcScheduler 扩展为优先级队列调度；文档应提供不同部署场景的推荐配置（单机/小型多人/大型多人）
- NFR-4：情景记忆上限 200 条/NPC，长期记忆 SQLite 单表 < 10MB/NPC
- NFR-5：插件零**项目代码**依赖；引擎侧仅依赖引擎内置模块与引擎内置插件。文档需同时声明”直接依赖”和”传递依赖”（例如 SmartObjects 传递拉入 GameplayAbilities/TargetingSystem/MassEntity）
- NFR-6：AINpcUI 模块与 Runtime 隔离，Dedicated Server 可编译不含 UMG/Slate；运行时拆为 AINpcCore / AINpcMemory / AINpcImmersion 三个可独立编译的子模块
- NFR-7：支持 UE5.4+（StateTree WeakExecutionContext 依赖）
- NFR-8：C++/蓝图双通道，核心交互流程可纯蓝图完成（绑定人设、发起对话、监听响应、查询关系）；Provider/source 路径仍以 JSON Provider 配置为唯一权威，不提供蓝图/Settings/Persona 侧 API key/provider/model/baseUrl 配置入口
- NFR-9：记忆条目带 SchemaVersion，插件升级时支持存档迁移
- NFR-10：多人游戏支持：LLM 调用和记忆写入在 Server 端执行，对话/动作通过 Multicast RPC 同步
- NFR-11：预算治理可观测：必须提供三层预算实时计数（per-player/per-npc/per-shard）和降级命中统计，支持 CSV 导出

---

## 五、技术约束与依赖

### 引擎依赖
| 模块 | 用途 | 备注 |
|------|------|------|
| Core, CoreUObject, Engine | 基础 | — |
| AIModule | AIController 底层支持（`AAINpcController` 基类） | — |
| GameplayStateTreeModule | AI 专用 StateTree（含 AIComponentSchema） | — |
| SmartObjectsModule | 环境交互 | **硬依赖**，Build.cs 无条件依赖并定义 `WITH_SMARTOBJECTS=1`；会拉入 GameplayAbilities/TargetingSystem/MassEntity 等传递依赖 |
| HTTP | LLM API 调用 + SSE 流式 | — |
| WebSockets | Realtime 类接口（可选） | — |
| Json, JsonUtilities | JSON 解析 | — |
| GameplayTags | 标签系统 | — |
| SQLiteCore | 记忆持久化 | 引擎内置插件，需启用；FTS5 在 UE5.4+ 中默认启用（`SQLiteCore/Build.cs` 硬编码 `SQLITE_ENABLE_FTS5`） |
| UMG, Slate | 对话 UI（AINpcUI 模块） | 仅客户端，与 Runtime 隔离 |

### 依赖分层清单

- 直接依赖：Core, CoreUObject, Engine, AIModule, GameplayStateTreeModule, HTTP, WebSockets(可选), Json, JsonUtilities, GameplayTags, SQLiteCore, SmartObjectsModule
- 传递依赖（由 SmartObjects 引擎模块拉入）：GameplayAbilities, TargetingSystem, MassEntity, WorldConditions, NavigationSystem, PropertyBindingUtils, RHI 等

### 关键技术决策
- **运行时三模块拆分**：AINpcRuntime 拆为 AINpcCore（LLM 通信/行为执行/感知/Prompt/网络/调度）、AINpcMemory（记忆系统/冲突解决/反思）、AINpcImmersion（情感/关系/安全/自主行为/社交），三个模块可独立编译，项目方按需引用；AINpcCore 为必选，AINpcMemory 和 AINpcImmersion 为可选（通过 Build.cs 模块依赖控制）
- StateTree 节点全部是 USTRUCT（F 前缀），非 UCLASS
- 自建 SmartObjectBridge 替代实验版 GameplayInteractions，完全可控
- SSE Parser 自建：引擎 `FHttpRequestStreamDelegate` 仅提供原始字节流
- 感知解耦用 `UGameInstanceSubsystem` 全局委托，不依赖 Lyra 的 GameplayMessageRouter
- 情感数值本地计算，不依赖 Provider

### NpcPersonaDataAsset 字段总览（各 FR 汇总）
| 字段 | 类型 | 来源 FR | Phase |
|------|------|---------|-------|
| PersonaName / Background / SpeakingStyle | FString | US-1 | 1 |
| WorldContextOverride / HomeLevelContext / KnowledgeScope | TSoftObjectPtr\<UWorldContextDataAsset\> / TSoftObjectPtr\<ULevelContextDataAsset\> / FNpcKnowledgeScope | FR-25A/FR-25B/FR-36 | 1 |
| PromptTemplateOverrides | TMap\<EPromptLayer, FString\> | FR-37 | 1 |
| SpeakingLength | ESpeakingLength | FR-37A | 1 |
| DelayMaskingMontages | TMap\<EDelayStrategy, TArray\<TSoftObjectPtr\<UAnimMontage\>\>\> | FR-33 | 1 |
| DelayFillerTexts | TArray\<FText\> | FR-33 | 1 |
| DelayFillerThreshold | float (默认 3.0s) | FR-33 | 1 |
| RetrievalWeights (α/β/γ) | FVector | FR-10 | 3a |
| AppraisalRules | TMap\<FGameplayTag, FAppraisalRule\> | FR-21 | 4 |
| OCEAN | FNpcOceanPersonality (5×float) | FR-23 | 4 |
| PersonalityInertia | float | FR-24 | 4 |
| Schedule | UNpcScheduleDataAsset* | FR-42 | 6 |
| ProactiveConditions | TArray\<FProactiveCondition\> | FR-43 | 6 |
| SocialTemplates | TArray\<FNpcSocialTemplate\> | FR-44 | 6 |

> 此表为 PRD 级草案，SDD 阶段确定最终字段名和嵌套结构。

> Provider 类型、baseUrl、model、API key、fallback provider 不属于 `NpcPersonaDataAsset` 字段；这些 provider-source 字段只能来自 JSON Provider 配置，本地部署模型除外。

---

## 六、非目标（Out of Scope）

- 语音交互（STT/TTS）：预留 `ISTTProvider`/`ITTSProvider` 接口，但不在 Phase 1-6 范围
- 面部动画驱动（Audio2Face 等）：Phase 6 提供情感外化数据接口，但面部动画资产和驱动实现属于扩展方向
- ~~NPC 间自主社交调度~~：已纳入 Phase 6（FR-44）
- 程序性记忆层（MACLA）和图关系层（Mem0-Graph）：P2 长期演进
- RL 训练循环：NPC 行为由 LLM 驱动，不引入 RL 策略
- 多模型各司其职架构（对话/情感/动作分离模型）：P2 长期演进
- 生产环境 API Key 中转网关：插件可通过 JSON Provider 配置指向项目方自建网关；不得把 API Key/provider/model/baseUrl 回填到 Settings、PersonaDataAsset、环境变量、旧字段或静默迁移路径

---

## 七、风险与缓解

| 风险 | 等级 | 缓解方案 |
|------|------|----------|
| LLM 延迟影响体验 | 🔴 高 | 异步调用 + 过渡动画掩盖（思考/受击/端详）+ 超时降级本地 SLM |
| 提示注入攻破人设 | 🔴 高 | InputSanitizer 3 类检测 + OutputValidator 输出过滤双重防线 |
| API Key 泄露（客户端发行） | 🔴 高 | 开发模式直连也必须走 JSON Provider 配置；生产模式 JSON Provider 指向中转网关，客户端仅持短期 token |
| NPC 人设漂移 | 🟡 中 | System Prompt 锚定 + 定期人设回顾注入 + 输出一致性校验 |
| 多 NPC 并发性能 | 🟡 中 | NpcScheduler 优先级队列 + 并发上限 + LOD 降频 |
| LLM 幻觉（不合理行为） | 🟡 中 | 动作白名单 + SmartObject 合法列表约束 + "裁判"架构验证 |
| 云端 API 成本 | 🟡 中 | 本地 SLM fallback + 缓存常见回复 + 调用频率限制 |
| GPU 资源争用（本地 SLM） | 🟡 中 | SLM 推理限制 GPU 占用比例 + CPU fallback |
| 记忆/存档版本兼容 | 🟡 中 | SchemaVersion + 迁移脚本 + 向前兼容读取 |
| LLM 输出语言不一致 | 🟡 中 | System Prompt 强制语言 + 输出校验 + fallback 模板 |
| 云端模型版本漂移 | 🟡 中 | Provider 层版本锁定（指定 model ID）+ 回归测试脚本 + 输出格式校验 |

---

## 八、里程碑

| 轨道 | Phase | 内容 | 交付物 |
|------|-------|------|--------|
| Foundation | Phase 1 MVP | 基础对话 | OpenAI Provider + UAINpcComponent + AAINpcController + StateTree + 对话气泡 UI + 过渡动画（AINpcCore 模块） |
| Foundation | Phase 2 | 感知与行为 | NpcEventSubsystem + 结构化 JSON + SmartObject 执行（可选） + JSON-only 多 Provider（Anthropic/DeepSeek/OpenAI-compatible/Local）+ SSE 流式（AINpcCore 模块） |
| Foundation | Phase 3a | 记忆存储与检索 | 三层记忆 + Stanford 检索（IRelevanceScorer）+ 分段衰减 + 选择性写入 + SQLite 持久化 + 冲突解决 + 主动遗忘 + 玩家提及加权（AINpcMemory 模块） |
| Foundation | Phase 3b | 反思与压缩 | 反思机制 + Evidence Pointers + 记忆合并归档 + MemoryType + LinkedMemoryIds（AINpcMemory 模块） |
| Foundation | Phase 4 | 情感/关系/安全 | VAD 情感 + 评价链 + OCEAN 人格 + 关系模型 + InputSanitizer + OutputValidator + 人格重注入防漂移（AINpcImmersion 模块） |
| Foundation | Phase 5 | 打磨与工具 | PersonaEditor + MemoryDebugger + NpcScheduler + 测试框架 + 示例项目 |
| Immersion Pack（可独立开关） | Phase 6 | 沉浸感增强 | 自主行为循环 + 主动交互触发 + NPC 间社交 + 流式首 Token <500ms + 情感外化接口（AINpcImmersion 模块） |

---

## 九、成功指标

- 开发者从插件安装完成到 NPC 开口说话 < 15 分钟（Phase 1 验收基准，前提：已有可编译的 UE5 项目）
- LLM 响应延迟 P50 < 2s，P95 < 4s（Phase 1 起持续度量）
- 人设一致性：连续 50 轮正常对话（非攻击场景）中 OutputValidator 人设边界拒绝率 < 5%（Phase 4 起自动化统计）
- 提示注入防御：3 类攻击场景测试通过率 > 90%（Phase 4 验收基准）
- 记忆检索准确率：Top-5 召回中包含正确记忆 > 80%（Phase 3a 验收基准）
- 蓝图全流程可用：四个核心流程无需写一行 C++（Phase 1 起持续保证）
- 流式首 Token 延迟 P50 < 500ms（Phase 6 验收基准，SSE 模式下从请求发出到首个可见字符）
- NPC 自主行为覆盖率：配置日程表后，NPC 非对话时间 > 90% 处于日程行为中而非 Idle 站桩（Phase 6 验收基准）
- 玩家体感停顿率：对话等待超过 4s 后主动取消/离开占比 < 10%
- 主动交互接受率：NPC 主动发起后被玩家继续对话/响应的比例 > 40%
- 主动交互打断率：NPC 主动发起后 3 秒内被玩家打断的比例 < 30%
- 预算健康度：`per-player/per-npc/per-shard` 三层预算超限率周均 < 5%

---

## 十、开放问题

1. ~~SQLiteCore 的 FTS5 编译标志在各引擎版本中是否默认启用？~~ **已确认**：经引擎源码验证（`SQLiteCore/Build.cs`），`SQLITE_ENABLE_FTS5` 在 UE5.4+ 中默认启用（与 FTS4、JSON1、RTREE 一同硬编码开启），可直接使用
2. 多人游戏中多个玩家同时与同一 NPC 对话时，采用排队、轮询还是群聊模式？（Phase 1 设计约束：假设单玩家对话，多玩家请求排队处理；FR-38 网络同步按此假设设计）
3. 记忆可见性层级（Private/Shared/Public）的具体划分规则待定
4. 记忆冲突解决的异步后台任务是否需要暴露给开发者手动触发的接口？
5. 语音接口（`ISTTProvider`/`ITTSProvider`）是在 Phase 1 定义空接口占位，还是完全推迟到扩展阶段？
6. 多 NPC 请求批处理优化（合并为单次 LLM 调用）的优先级和实现时机待定
