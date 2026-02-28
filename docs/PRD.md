# AINpc 插件产品需求文档（PRD）

> 来源：AI_NPC_Plugin_Research.md + AI_NPC_Reference_Analysis.md
> 版本：1.0
> 日期：2026-03-01

---

## 一、产品定位

UE5 即插即用的 AI NPC 插件，通过 LLM 驱动 NPC 的对话、情感、记忆和行为，让游戏开发者零 AI 背景即可创建有"灵魂"的 NPC。

**一句话描述**：配置 API Key + 编辑人设 DataAsset = NPC 能说话、能记事、能动感情、能做动作。

**目标用户**：UE5 游戏开发者（独立开发者 / 小型工作室），C++ 或纯蓝图均可。

**竞品参照**：Personica AI（Fab 商城，$80），我们的差异化在记忆深度、情感完整度、安全防护。

---

## 二、核心用户故事

### US-1：基础对话（Phase 1 MVP）
**作为**游戏开发者，**我希望**拖一个组件到 NPC 上、填入 API Key 和人设，NPC 就能和玩家自然对话，**以便**快速验证 LLM NPC 的可行性。

验收标准：
- [ ] `UAINpcComponent` 挂载到任意 Actor，配置 `NpcPersonaDataAsset` + API Key 后可对话
- [ ] 支持 OpenAI Provider（首个），异步非阻塞，GameThread 不卡顿
- [ ] 对话气泡 UI 显示 NPC 回复（`AINpcUI` 可选模块）
- [ ] 最简 StateTree 驱动对话状态流转（空闲→对话中→结束）
- [ ] 蓝图可完成全流程：配置 Key、发起对话、监听响应、显示文本

### US-2：感知与行为执行（Phase 2）
**作为**游戏开发者，**我希望**NPC 能感知玩家行为（攻击、送礼等）并做出动作反应，**以便**NPC 不只是聊天机器人。

验收标准：
- [ ] `NpcEventSubsystem`（GameInstanceSubsystem）接收宿主广播的事件
- [ ] LLM 输出结构化 JSON（对话 + 动作意图 + 情感变化）
- [ ] `LLMResponseParser` 三级降级：严格 JSON → 宽松提取 → 纯文本
- [ ] StateTree Task 解析动作意图，通过 SmartObject 执行（坐下、拿杯子等）
- [ ] SmartObject 动态注入：Prompt 中列出 NPC 周围可交互对象
- [ ] 新增 Anthropic + LocalProvider（Ollama）
- [ ] 流式响应（SSE Parser 自建）

### US-3：记忆存储与检索（Phase 3a）
**作为**游戏开发者，**我希望**NPC 能记住和玩家的交互历史，**以便**产生"这个 NPC 认识我"的沉浸感。

验收标准：
- [ ] 三层记忆实现：工作记忆（上下文窗口）/ 情景记忆（TArray）/ 长期记忆（SQLite）
- [ ] Stanford 检索公式：Score = α×Recency + β×Importance + γ×Relevance
- [ ] α/β/γ 可在 DataAsset 中配置（不同 NPC 类型不同预设）
- [ ] 选择性写入：重要性 < 3 不入情景记忆，< 5 不入长期记忆
- [ ] 记忆持久化到 SQLite，支持存档/读档
- [ ] 记忆冲突解决（P0）：写入前检索相似记忆，异步 LLM 判断 ADD/UPDATE/MERGE/SUPERSEDE
- [ ] 主动遗忘（P0）：情景记忆满 200 条时按 EvictionScore 淘汰

### US-4：反思与压缩（Phase 3b）
**作为**游戏开发者，**我希望**NPC 能从经历中总结出高层洞察，**以便**NPC 表现出"成长"和"理解"。

验收标准：
- [ ] 累积重要性 > 150 时触发反思
- [ ] LLM 从近期记忆提取洞察，写回记忆流
- [ ] 洞察带 Evidence Pointers 指向源记忆
- [ ] 低价值记忆合并/归档机制

### US-5：情感与关系系统（Phase 4）
**作为**游戏开发者，**我希望**NPC 拥有数值驱动的情感和关系系统，**以便**NPC 的反应有情感温度而非机械应答。

验收标准：
- [ ] VAD 三维情感模型：Valence[-1,1] / Arousal[0,1] / Dominance[0,1]
- [ ] 评价链前置（方案B 本地规则计算）：事件→4维评价（Goal Relevance/Certainty/Agency/Coping Potential）→情感推导，零额外 LLM 调用
- [ ] 关系模型：Affinity[-100,100] / Trust[0,100] / Familiarity[0,100]
- [ ] OCEAN 五维人格参数写入 `NpcPersonaDataAsset`，Prompt 注入数值+描述
- [ ] 人格惯性系数防止单次极端事件彻底改变 NPC 人格
- [ ] 情感随时间自然衰减（衰减速率受 Neuroticism 影响）
- [ ] 情感/关系数值影响 StateTree 分支选择（敌人→战斗，朋友→帮助）
- [ ] 情感-行为一致性验证：OutputValidator 检测情感状态与输出倾向矛盾时拒绝并重新生成

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
- [ ] 性能优化：NpcScheduler 请求队列 + 并发上限 + LOD 降频
- [ ] 测试框架：交互回放（固定 seed/mock 响应）+ 人设一致性评分
- [ ] 示例项目：至少包含 3 个不同人设的 NPC 演示场景

---

## 三、功能需求清单

### LLM 通信层
- FR-1：`ILLMProvider` 接口抽象，支持 OpenAI / Anthropic / Local（Ollama）/ Custom 四类 Provider
- FR-2：统一 Request/Response 结构，Provider 能力自动探测与降级（无 JSON Mode 时用 prompt 约束）
- FR-3：异步非阻塞调用，GameThread 零等待
- FR-4：自建 SSE Parser 处理流式响应（`data:` 前缀、`[DONE]` 终止、跨包拼接）
- FR-5：自动重试（指数退避）+ 超时 + 降级（云端超时→本地 fallback）
- FR-6：API Key 通过项目设置（`AINpcSettings`）或 DataAsset 配置
- FR-7：C++/蓝图双通道：静态委托（C++）+ 动态多播委托（蓝图）

### 记忆系统
- FR-8：三层记忆：工作记忆（上下文窗口 ~20 条）/ 情景记忆（TArray ~200 条）/ 长期记忆（SQLite 无限）
- FR-9：Stanford 检索公式 Score = α×Recency + β×Importance + γ×Relevance，α/β/γ 可配置
- FR-10：选择性写入：重要性 < 3 不入情景记忆，< 5 不入长期记忆
- FR-11：记忆冲突解决：写入前向量检索相似记忆，异步 LLM 判断 ADD/UPDATE/MERGE/SUPERSEDE
- FR-12：主动遗忘：EvictionScore = w1×(1-Recency) + w2×(1-Importance) + w3×(1-AccessFrequency)
- FR-13：反思机制：累积重要性 > 150 触发，LLM 提取洞察写回记忆流
- FR-14：记忆持久化到 SQLite，支持存档/读档，条目带 SchemaVersion 支持迁移
- FR-15：`IEmbeddingProvider` 接口，无 Embedding 时降级为 SQLite FTS5 全文搜索

### 情感与关系系统
- FR-16：VAD 三维情感状态 + `FGameplayTagContainer` 情感标签（Angry/Happy/Fearful 等）
- FR-17：评价链前置（本地规则计算）：事件类型 + NPC 人格参数 → 4 维评价 → 情感推导
- FR-18：关系模型 Affinity/Trust/Familiarity，事件触发数值变化，随时间自然衰减
- FR-19：OCEAN 五维人格参数，Prompt 注入时同时传数值和描述
- FR-20：人格惯性系数，情感衰减速率 = BaseDecayRate × (1 - Neuroticism × 0.5)
- FR-21：当前情感 + 关系数值注入 LLM Prompt，影响对话语气和行为选择

### 行为执行层
- FR-22：LLM 输出结构化 JSON（dialogue + actions + emotion_delta + relationship_delta）
- FR-23：`LLMResponseParser` 三级降级：严格 JSON → 宽松提取 → 纯文本
- FR-24：自定义 StateTree Task（`FStateTreeTask_LLMQuery`、`FStateTreeTask_ExecuteSmartObject`）
- FR-25：SmartObject 动态注入：构建 Prompt 前查询 NPC 周围可交互对象，注入合法动作列表
- FR-26：自建 SmartObjectBridge 模块：槽位查找/占用/释放/位置获取
- FR-27："裁判"架构：LLM 只建议，StateTree 验证合法性后才执行

### 感知系统
- FR-28：`NpcEventSubsystem`（GameInstanceSubsystem）全局委托广播，宿主只需广播标签+载荷
- FR-29：事件载荷采用 `FInstancedStruct` 或 `FGameplayTagContainer + TMap<FName, FString>`

### 安全系统
- FR-30：`InputSanitizer` 覆盖 3 类攻击：直接提示 / 社会工程 / 指令覆盖
- FR-31：`OutputValidator`：JSON Schema 校验 + 动作白名单 + 人设边界检测 + system prompt 泄露检测
- FR-32：异常亲密度增长检测，超阈值触发防御升级

---

## 四、非功能需求

- NFR-1：LLM 响应延迟 P95 < 4 秒（含网络），超时自动降级到本地 SLM 或预设模板
- NFR-2：GameThread 零阻塞，所有 LLM 调用和记忆写入异步执行
- NFR-3：同帧最大并发 LLM 请求 ≤ 3，远距离 NPC 调用频率降至 1/5（LOD）
- NFR-4：情景记忆上限 200 条/NPC，长期记忆 SQLite 单表 < 10MB/NPC
- NFR-5：插件零项目依赖，仅依赖引擎标准模块（Core/HTTP/StateTree/SmartObjects/SQLiteCore/GameplayTags）
- NFR-6：AINpcUI 模块与 Runtime 隔离，Dedicated Server 可编译不含 UMG/Slate
- NFR-7：支持 UE5.4+（StateTree WeakExecutionContext 依赖）
- NFR-8：C++/蓝图双通道，四个核心流程可纯蓝图完成（配置 Key、发起对话、监听响应、查询关系）
- NFR-9：记忆条目带 SchemaVersion，插件升级时支持存档迁移
- NFR-10：多人游戏支持：LLM 调用和记忆写入在 Server 端执行，对话/动作通过 Multicast RPC 同步

---

## 五、技术约束与依赖

### 引擎依赖
| 模块 | 用途 | 备注 |
|------|------|------|
| Core, CoreUObject, Engine | 基础 | — |
| GameplayStateTreeModule | AI 专用 StateTree（含 AIComponentSchema） | — |
| SmartObjectsModule | 环境交互 | 隐式拉入 GameplayAbilities；需在 .uproject 启用 |
| HTTP | LLM API 调用 + SSE 流式 | — |
| WebSockets | Realtime 类接口（可选） | — |
| Json, JsonUtilities | JSON 解析 | — |
| GameplayTags | 标签系统 | — |
| SQLiteCore | 记忆持久化 | 引擎内置插件，需启用；FTS5 需运行时检测 |
| UMG, Slate | 对话 UI（AINpcUI 模块） | 仅客户端，与 Runtime 隔离 |

### 关键技术决策
- StateTree 节点全部是 USTRUCT（F 前缀），非 UCLASS
- 自建 SmartObjectBridge 替代实验版 GameplayInteractions，完全可控
- SSE Parser 自建：引擎 `FHttpRequestStreamDelegate` 仅提供原始字节流
- 感知解耦用 `UGameInstanceSubsystem` 全局委托，不依赖 Lyra 的 GameplayMessageRouter
- 情感数值本地计算，不依赖 Provider

---

## 六、非目标（Out of Scope）

- 语音交互（STT/TTS）：预留 `ISTTProvider`/`ITTSProvider` 接口，但不在 MVP 范围
- 面部动画驱动（Audio2Face 等）：属于扩展方向
- NPC 间自主社交调度：事件总线天然支持，但调度器不在 Phase 1-5
- 程序性记忆层（MACLA）和图关系层（Mem0-Graph）：P2 长期演进
- RL 训练循环：NPC 行为由 LLM 驱动，不引入 RL 策略
- 多模型各司其职架构（对话/情感/动作分离模型）：P2 长期演进
- 生产环境 API Key 中转网关：插件提供接口，网关由项目方自建

---

## 七、风险与缓解

| 风险 | 等级 | 缓解方案 |
|------|------|----------|
| LLM 延迟影响体验 | 🔴 高 | 异步调用 + 过渡动画掩盖（思考/受击/端详）+ 超时降级本地 SLM |
| 提示注入攻破人设 | 🔴 高 | InputSanitizer 3 类检测 + OutputValidator 输出过滤双重防线 |
| API Key 泄露（客户端发行） | 🔴 高 | 开发模式直连；生产模式走中转网关，客户端仅持短期 token |
| NPC 人设漂移 | 🟡 中 | System Prompt 锚定 + 定期人设回顾注入 + 输出一致性校验 |
| 多 NPC 并发性能 | 🟡 中 | NpcScheduler 优先级队列 + 并发上限 + LOD 降频 |
| LLM 幻觉（不合理行为） | 🟡 中 | 动作白名单 + SmartObject 合法列表约束 + "裁判"架构验证 |
| 云端 API 成本 | 🟡 中 | 本地 SLM fallback + 缓存常见回复 + 调用频率限制 |
| GPU 资源争用（本地 SLM） | 🟡 中 | SLM 推理限制 GPU 占用比例 + CPU fallback |
| 记忆/存档版本兼容 | 🟡 中 | SchemaVersion + 迁移脚本 + 向前兼容读取 |
| LLM 输出语言不一致 | 🟡 中 | System Prompt 强制语言 + 输出校验 + fallback 模板 |

---

## 八、里程碑

| Phase | 内容 | 交付物 |
|-------|------|--------|
| Phase 1 MVP | 基础对话 | OpenAI Provider + UAINpcComponent + 最简 StateTree + 对话气泡 UI |
| Phase 2 | 感知与行为 | NpcEventSubsystem + 结构化 JSON 输出 + SmartObject 执行 + Anthropic/Local Provider + SSE 流式 |
| Phase 3a | 记忆存储与检索 | 三层记忆 + Stanford 检索 + 选择性写入 + SQLite 持久化 + 冲突解决 + 主动遗忘 |
| Phase 3b | 反思与压缩 | 反思机制 + Evidence Pointers + 记忆合并归档 |
| Phase 4 | 情感/关系/安全 | VAD 情感 + 评价链 + OCEAN 人格 + 关系模型 + InputSanitizer + OutputValidator |
| Phase 5 | 打磨与工具 | PersonaEditor + MemoryDebugger + NpcScheduler + 测试框架 + 示例项目 |

---

## 九、成功指标

- 开发者从零到 NPC 开口说话 < 15 分钟（Phase 1 MVP 验收基准）
- LLM 响应延迟 P50 < 2s，P95 < 4s
- 人设一致性：连续 50 轮对话中人设破坏率 < 5%
- 提示注入防御：3 类攻击场景测试通过率 > 90%
- 记忆检索准确率：Top-5 召回中包含正确记忆 > 80%
- 蓝图全流程可用：四个核心流程无需写一行 C++

---

## 十、开放问题

1. 最低支持 UE5.4 还是 UE5.1？StateTree WeakExecutionContext 是 5.4+ 特性，降低门槛需要自建异步衔接
2. SQLiteCore 的 FTS5 编译标志在各引擎版本中是否默认启用？需实测确认降级策略触发频率
3. 多人游戏中多个玩家同时与同一 NPC 对话时，采用排队、轮询还是群聊模式？
4. 记忆可见性层级（Private/Shared/Public）的具体划分规则待定
5. 记忆冲突解决的异步后台任务是否需要暴露给开发者手动触发的接口？
6. 是否需要内置 Prompt 模板版本管理，以便开发者在不同 LLM 版本间切换？
7. Phase 4 情感系统和安全系统是否可以并行开发，还是安全系统依赖情感系统的输出？
