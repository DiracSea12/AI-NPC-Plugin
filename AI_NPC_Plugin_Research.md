# AI NPC 通用插件可行性调研报告

> 调研日期：2026-02-28
> 目标：基于UE5开发通用AI NPC插件，实现LLM驱动的智能NPC交互
> 定位：即插即用的引擎插件，零项目依赖

---

## 一、需求分析

### 核心需求
1. 通过配置API Key即可让NPC接入大模型
2. 玩家可与NPC自然语言对话
3. NPC能感知玩家行为（攻击、送礼等）并做出反应
4. NPC能记住交互历史，影响长期剧情（链式反应）
5. 通用插件，即插即用，不依赖特定项目

### 需求拆解
| 能力 | 技术要求 |
|------|---------|
| 自然对话 | LLM API调用 + 流式响应 |
| 行为感知 | UE5感知系统扩展 |
| 情感反应 | 情感状态机 + LLM情感注入 |
| 行为执行 | StateTree + 结构化指令解析 + SmartObjects |
| 短期记忆 | LLM上下文管理 |
| 长期记忆 | 嵌入式存储 + 向量检索 |
| 链式反应 | 事件系统 + 记忆反思机制 |

---

## 二、技术方案对比

### 传统AI vs 现代AI

| 维度 | StateTree / 行为树 | 强化学习(RL) | 大语言模型(LLM) |
|------|-----------|-------------|----------------|
| 自然对话 | ❌ 不可能 | ❌ 不可能 | ✅ 核心能力 |
| 行为确定性 | ✅ 完全确定 | ⚠️ 训练后确定 | ❌ 不确定 |
| 性能开销 | ✅ 极低 | ✅ 推理低 | ⚠️ 较高 |
| 涌现行为 | ❌ 无 | ⚠️ 有限 | ✅ 强 |
| 情感理解 | ❌ 无 | ❌ 无 | ✅ 强 |
| 记忆能力 | ❌ 无 | ❌ 无 | ⚠️ 需外部系统 |
| 开发效率 | ⚠️ 手动编写 | ❌ 训练耗时 | ✅ prompt迭代快 |
| UE5集成 | ✅ 原生支持 | ⚠️ LearningAgents（实验性） | ⚠️ 需自建 |

### 结论：混合架构是唯一正解

**LLM（大脑）+ StateTree（发号施令）+ SmartObjects（具体执行）+ 记忆系统（海马体）**

这是Inworld AI、NVIDIA ACE、Stanford Generative Agents、PhiloAgents等所有成熟方案的共识。

---

## 三、业界方案调研

### 3.1 商业方案

| 方案 | 核心能力 | 部署方式 | 参考价值 |
|------|---------|---------|---------|
| **Inworld AI** | 多模型编排 → 2025年扩展为通用Agent Runtime平台 | 云端API + SDK | ★★★★★ 架构标杆 |
| **NVIDIA ACE** | 自主角色（感知→规划→行动）+ 本地SLM | 本地RTX + 云端 | ★★★★★ 本地推理方向 |
| **Convai** | 对话 + 环境感知 + 行动执行 | 云端API + UE5 SDK | ★★★★ 感知+行动集成 |
| **Personica AI** | Cognitive NPC Brain 插件（Fab商城） | UE5插件 | ★★★ 即插即用竞品参考 |

**关键洞察：**
- Inworld：2025年从游戏NPC Character Engine扩展为通用Agent Runtime平台（2025.08.13 正式发布 Inworld Runtime，定位"首个面向消费级应用的 AI Runtime"），记忆/情感/多模态编排架构仍为业界标杆；已发布 Unreal AI Runtime SDK（Fab商城+GitHub开源）
- NVIDIA ACE：已发布 ACE Unreal Plugin 2.5 + NVIGI SDK（本地推理），开源 Audio2Face-3D 动画模型，支持本地 SLM 推理，RTX 40系列+ 可本地运行推理管线
- Convai：开源UE5 SDK（GitHub），感知环境并执行游戏内动作，路线图包含流式视觉感知
- Personica AI：Fab 商城上架的 Cognitive NPC Brain 插件，定位与本插件相似，可作为竞品参考
- NVIDIA ACE 商业合作：已宣布与 PUBG、Naraka: Bladepoint 等游戏的 ACE 集成计划（截至2026年初仍在测试阶段，尚未正式上线）

**NVIDIA ACE 具体能力：**
- ACE Unreal Plugin 2.5：面部动画驱动（线性插值、情绪混合、提前打断）
- NVIGI SDK（In-Game Inference）：GPU优化的本地推理管理器，支持 ASR/LLM/Embedding 等 AI 插件
- Audio2Face-3D：开源面部动画模型（2025.09.24 发布于 HuggingFace，Apache 2.0），音频驱动面部混合形状
- 本地 SLM 推理：支持多种开源小语言模型，RTX 40系列+ 可本地运行
- PersonaPlex：2026.01 发布的 7B 全双工语音对话模型（MIT许可），整合 STT→LLM→TTS 管线
- 商业合作：KRAFTON 于 CES 2025 展示基于 ACE 的 Co-Playable Character (CPC) 技术，计划应用于 PUBG IP
- GitHub 开源：https://github.com/NVIDIA/ACE（Apache 2.0，含微服务示例和参考工作流）

### 3.2 UE5 LLM集成插件

| 项目 | 定位 | 特点 |
|------|------|------|
| **Llama-Unreal** (getnamo) | llama.cpp的UE5封装 | 本地推理，零网络依赖 |
| **Frozen-Projects/AI_Cactus** | 本地LLM/VLM/TTS框架 (UE5.6) | 支持移动端边缘设备 |
| **UnrealGenAISupport** | 多模型API统一封装 | 多Provider支持（OpenAI/DeepSeek/Anthropic等） |
| **GladeCore** | 本地AI NPC完整管线 | LLM + STT + TTS 单机设备部署 |
| **Sovahero/UnrealAiConnector** | 极简API集成框架 | 蓝图+C++双支持 (Claude/GPT/Gemini) |
| **life-exe/UnrealOpenAIPlugin** | OpenAI全功能UE5封装 | Chat/Image/Audio/Embedding等全API，蓝图+C++双支持，UE 5.2-5.7 |
| **LLM_Connect** | 纯蓝图LLM集成 | 零代码门槛 |

### 3.3 相关开源项目

| 项目 | Stars | 核心贡献 |
|------|-------|---------|
| **Stanford Generative Agents** | 20,700+ | 记忆流+反思+规划架构（奠基之作） |
| **cognee** | 12,600+ | 轻量Agent记忆系统 |
| **mem0ai/mem0** | 48,200+ | 生产级Agent记忆框架（长期记忆管理） |
| **nickm980/smallville** | 740+ | Generative Agents的游戏化实现 |
| **Interactive-LLM-Powered-NPCs** | 680+ | 通用LLM NPC方案 |
| **getnamo/Llama-Unreal** | 145+ | llama.cpp的UE5封装 |
| **joe-gibbs/local-llms-ue5** | 46 | 本地SLM+TTS NPC管线参考 |
| **PhiloAgents** (课程) | — | LangGraph+RAG+WebSocket完整工程化 |
| **AMD Schola** (论文) | — | RL+行为树混合方案的UE5验证 |

### 3.4 已发售的 LLM NPC 游戏（截至2026年初）

| 游戏 | 核心机制 | 状态 |
|------|---------|------|
| **Vaudeville** | AI驱动侦探推理，NPC自由对话（初期用Inworld，后自研离线引擎） | 已正式发布 |
| **Suck Up!** | 语音/文字说服AI吸血鬼NPC开门 | 已正式发布 |
| **Whispers from the Star（群星低语）** | 与AI宇航员实时语音/文字/视频通话，强调情感连接 | 已发布，Steam特别好评 |
| **AI2U: With You 'Til The End** | AI角色对话+密室逃脱 | Early Access |
| **Where Winds Meet（逆水寒海外版）** | 网易旗舰MMO，NPC自由对话+任务生成；曾因"Solid Snake Method"提示注入漏洞引发安全讨论（PCGamesN/GameRant/Polygon报道） | 已发布（首个大厂LLM NPC上线案例） |

**关键发现：**
- Where Winds Meet 是截至2026年初最大规模的 LLM NPC 商业部署案例，但也暴露了提示注入的现实风险
- 除此之外的案例均为独立游戏/小型工作室，说明该技术仍处于早期采用阶段
- 业界趋势：从纯云端API向本地+云端混合迁移（Vaudeville从Inworld转向自研离线引擎）

---

## 四、推荐架构设计

### 4.1 总体架构：三层分离

```
┌─────────────────────────────────────────────┐
│              游戏项目（宿主）                  │
│  - 继承插件提供的AIController/Component       │
│  - 配置NPC人设、API Key                      │
│  - 注册自定义行为和感知事件                    │
└──────────────────┬──────────────────────────┘
                   │ 插件接口层
┌──────────────────▼──────────────────────────┐
│         AINpc Plugin（通用插件）              │
│                                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐ │
│  │ LLM通信层 │ │ 记忆系统  │ │ 情感/关系系统 │ │
│  │ HTTP/WS  │ │ 三层记忆  │ │ 数值驱动     │ │
│  └────┬─────┘ └────┬─────┘ └──────┬───────┘ │
│       │            │              │          │
│  ┌────▼────────────▼──────────────▼───────┐  │
│  │       决策引擎(Decision Engine)            │  │
│  │  快环: StateTree Tick ~16ms/帧 + 感知事件 50-200ms │  │
│  │  慢环: LLM 纯文本 0.5-2s / 语音管线 1.5-4s    │  │
│  │  LLM输出 → 结构化解析 → 状态机变量/队列     │  │
│  └────────────────┬───────────────────────┘  │
│                   │                          │
│  ┌────────────────▼───────────────────────┐  │
│  │        行为执行层(Action Executor)       │  │
│  │  StateTree Tasks → 移动/动画/对话       │  │
│  │  SmartObjects → 复杂环境交互 (寻路/播放)    │  │
│  └────────────────────────────────────────┘  │
│                                              │
│  ┌────────────────────────────────────────┐  │
│  │        感知收集层(Perception Layer)      │  │
│  │ 利用自定义 GameInstanceSubsystem 解耦事件 │  │
│  └────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

**多人游戏支持策略：**
- LLM 调用和记忆写入必须在 Server 端（Authority）执行
- 对话文本和动作指令通过 Multicast RPC 同步到客户端
- `UAINpcComponent` 需区分 Server-Only 逻辑（LLM调用、记忆写入）和 Client 逻辑（UI显示、动画播放）
- 插件需提供 C/S 权威边界的明确标注（`UPROPERTY(Replicated)` / `UFUNCTION(Server)` 等）
- 单机模式下简化为本地直连，跳过网络层
- 并发对话管理：多个玩家同时与同一 NPC 对话时，需排队或合并策略（优先级队列 / 轮询 / 群聊模式）
- 记忆可见性：NPC 对玩家 A 的私有记忆 vs 所有玩家可感知的公共记忆，需设计记忆可见性层级（Private / Shared / Public）

### 4.2 数据流

```
玩家行为（攻击/对话/送礼，宿主只需向总线广播 GameplayMessage）
  → 插件感知系统（通过自定义 GameInstanceSubsystem 的全局 Delegate 监听）捕获事件
  → 写入记忆系统（带时间戳+重要性评分）
  → 更新情感/关系数值
  → 构建LLM Prompt（人设 + 当前情感 + 相关记忆 + 当前感知）
  → LLM返回结构化JSON（对话文本 + 行为意图 + 情感变化）
  → 解析指令并写入 StateTree 参数
  → StateTree 状态流转执行（说话/寻找SmartObject/攻击/逃跑）
  → 执行结果反馈 → 新事件写入记忆
```

### 4.3 LLM通信层设计

**多Provider统一接口：**
```
ILLMProvider
├── OpenAIProvider     (OpenAI系列模型)
├── AnthropicProvider  (Claude系列模型)
├── DeepSeekProvider   (DeepSeek系列，兼容OpenAI格式，国内可直连)
├── LocalProvider      (llama.cpp / Ollama)
└── CustomProvider     (用户自定义endpoint)
```

**Provider能力声明（各Provider差异抽象）：**

| 能力 | OpenAI | Anthropic | DeepSeek | Local(Ollama) |
|------|--------|-----------|----------|---------------|
| Function Calling | ✅ | ✅ | ✅ | ⚠️ 部分模型 |
| JSON Mode | ✅ | ✅ | ✅ | ⚠️ 部分模型 |
| 流式响应 | ✅ SSE（插件自建解析层） | ✅ SSE（插件自建解析层） | ✅ SSE（插件自建解析层） | ✅ |
| 上下文窗口 | 以当期模型版本为准 | 以当期模型版本为准 | 以当期模型版本为准 | 模型依赖 |
| 国内可直连 | ❌ 需代理 | ❌ 需代理 | ✅ | ✅ |

**关键设计：**
- 统一的Request/Response结构
- Provider能力自动探测与降级（无JSON Mode时用prompt约束）
- 流式响应支持（需自建 SSE Parser：引擎 `FHttpRequestStreamDelegate` 仅提供原始字节流，`data:` / `event:` / `id:` 字段拆分需插件实现）
- 异步非阻塞（GameThread不等待）
- 自动重试 + 超时 + 降级（云端超时→本地fallback）
- API Key通过项目设置或DataAsset配置

### 4.4 记忆系统设计

**三层记忆模型（参考Stanford Generative Agents）：**

| 层级 | 实现 | 生命周期 | 容量 |
|------|------|---------|------|
| 工作记忆 | LLM上下文窗口滑动窗口 | 单次对话 | ~20条 |
| 情景记忆 | TArray + 重要性排序 | 游戏Session | ~200条/NPC |
| 长期记忆 | SQLiteCore(引擎内置插件，需启用) + 内存点积余弦相似度 | 存档持久化 | 无限 |

**Embedding 生成**：通过可替换的 `IEmbeddingProvider` 接口（云端API / 本地模型均可）；无 Embedding 时降级为 SQLite FTS5 全文搜索（引擎内置 sqlite 3.47.1 原生支持，BM25 排序）。注意：FTS5 需要 `SQLITE_ENABLE_FTS5` 编译标志，引擎内置 SQLiteCore 不一定默认启用，插件初始化时应通过 `PRAGMA compile_options` 运行时检测，不可用时降级为 LIKE 模糊匹配。

**记忆条目结构：**
```
FNpcMemoryEntry {
  FString Description;      // "玩家送了我一朵花"
  float Importance;          // 0-10, LLM评估
  FDateTime Timestamp;
  FVector Location;
  FName SubjectId;           // Who — 事件主体（哪个玩家/NPC触发）
  FName ObjectId;            // Whom — 事件对象（对谁做的）
  FString Cause;             // Why — 因果链（可选，反思机制生成）
  FGameplayTagContainer Tags; // "Gift", "Positive", "Player"
  TArray<float> Embedding;   // 向量（可选，用于语义检索）
  int32 SchemaVersion;       // 记忆条目版本号（存档迁移用）
}
```

**检索算法（Stanford公式 — 加权求和）：**
```
Score = α × Recency(时间衰减) + β × Importance(重要性) + γ × Relevance(相关性)
其中 α + β + γ = 1，默认 α=0.3, β=0.3, γ=0.4
```

各分量通过 `IRelevanceScorer` 接口计算，项目方可替换默认实现（如自定义时间衰减曲线或领域特定的相关性评分）。

**选择性写入策略：**
- 重要性 < 3 的事件仅保留在工作记忆，不入情景记忆
- 重要性 < 5 的事件不入长期记忆（SQLite）
- 防止低价值事件污染记忆库，降低检索噪声

**反思机制：**
- 当累积重要性 > 阈值时触发
- LLM从近期记忆提取高层洞察（如"玩家一直在帮助我"）
- 洞察写回记忆流，影响后续决策

### 4.5 情感与关系系统

**情感状态（数值驱动，非LLM自管理）：**
```
FNpcEmotionState {
  float Valence;     // [-1, 1] 正面/负面
  float Arousal;     // [0, 1]  激动程度
  float Dominance;   // [0, 1]  支配感（高=自信掌控，低=顺从无力）
  FGameplayTagContainer ActiveEmotions; // "Angry", "Happy", "Fearful"
}
```

**关系模型：**
```
FNpcRelationship {
  float Affinity;     // [-100, 100] 好感度
  float Trust;        // [0, 100]    信任度
  float Familiarity;  // [0, 100]    熟悉度
}
```

**驱动规则：**
- 事件触发数值变化（被攻击→好感-30, 愤怒+50）
- 数值随时间自然衰减（情绪冷却）
- 当前情感注入LLM prompt（影响对话语气）
- 当前关系影响 StateTree 分支和状态选择（敌人→战斗，朋友→帮助）
- **UE5.7 State Tree Selectors（实验性）**：官方引入 Utility-based 状态选择行为（`FStateTreeConsiderationBase` 评分节点），情感/关系数值可直接作为 Consideration 权重输入，替代硬编码条件分支

### 4.6 行为执行层

**LLM输出格式（结构化JSON）：**
```json
{
  "dialogue": "谢谢你的礼物，我很开心！",
  "actions": [
    {"type": "PlayAnimation", "name": "Happy_React"},
    {"type": "UseSmartObject", "tag": "CoffeeMachine"}
  ],
  "emotion_delta": {"valence": 0.3, "arousal": 0.2},
  "relationship_delta": {"affinity": 15, "trust": 5}
}
```

**输出解析降级策略（`LLMResponseParser` 多级容错）：**
1. 严格 JSON Schema 校验（首选，支持 Function Calling / Tool Use 的 Provider 优先使用）
2. 宽松 JSON 提取（正则匹配 `{...}` 块，容忍多余文本包裹）
3. 纯文本降级（仅提取对话文本，行为使用默认模板，情感/关系不变）

**StateTree 集成：**
- 自定义 StateTree Task 节点 (`FStateTreeTask_LLMQuery`, `FStateTreeTask_ExecuteSmartObject`)（注：StateTree 节点全部是 USTRUCT/F前缀，非 UCLASS）
- 从 Parameter 结构体中读取 LLM 指令队列
- StateTree 负责状态流转：对话→动画→寻找目标SmartObject并交互
- 委托 SmartObject 系统处理复杂的精细寻路和插槽对齐动画，降低大模型空间控制的幻觉。
- **防幻觉：动态注入可用动作列表** — 构建 Prompt 前，通过 `USmartObjectSubsystem::FindSmartObjects()` 查询 NPC 周围可交互的 SmartObject（引擎内置 Octree/HashGrid 空间索引，比手动 Sphere Trace 更高效），将可用标签（如 `Available_Objects: [Chair, Cup, Sword]`）注入 System Prompt，强制 LLM 只能从合法列表中选择动作
- **异步衔接：UE5.4+ StateTree WeakExecutionContext** — `FStateTreeTask_LLMQuery::EnterState()` 保存 `WeakContext = Context.MakeWeakExecutionContext()` 后发起异步 HTTP 请求；HTTP 回调中通过 `StrongContext.SendEvent()` 安全触发 StateTree 状态转换，无需自建指令队列

---

## 五、UE5引擎集成点

### 5.1 插件仅依赖引擎模块（零项目依赖）

```
模块依赖：
  Core, CoreUObject, Engine,
  GameplayStateTreeModule,  // AI专用StateTree组件（含UStateTreeAIComponent + AIComponentSchema）
  StateTreeModule,      // 现代化状态机/行为编排（被GameplayStateTree自动拉入）
  SmartObjectsModule,    // 复杂环境交互目标处理（注意：隐式拉入GameplayAbilities + WorldConditions；需在项目.uproject中启用SmartObjects插件）
  AIModule,              // AIController底层支持
  HTTP,                  // HTTP请求 + SSE流式响应（主要传输方式）
  WebSockets,            // WebSocket通信（可选，用于Realtime类接口）
  Json, JsonUtilities,   // JSON解析
  GameplayTags,          // 标签系统
  SQLiteCore,            // 记忆持久化（引擎内置插件，需启用）

AINpcUI 可选模块额外依赖（与 Runtime 隔离，保证 Dedicated Server 可编译）：
  UMG, Slate             // 对话气泡等UI（客户端专用）
```

### 5.2 关键扩展点利用

| 引擎系统 | 插件利用方式 |
|---------|------------|
| `AAIController` | 提供 `AAINpcController` 基类 |
| `UStateTreeAIComponent` | GameplayStateTree 插件提供的 AI 专用 StateTree 组件（含 AIComponentSchema），插件核心组件 `UAINpcComponent` 基于此构建，用户可挂载到任何已有 AIController 上 |
| `FStateTreeTaskBase` | 自定义LLM调用、状态流转节点（USTRUCT，F前缀）。利用 UE5.4+ 外部 StateTree 资产链接，插件内置通用行为模板（对话/受击反应/送礼反应），项目方只需替换局部 Task 即可复用 |
| `FStateTreeEvaluatorBase` | 后台感知和数值轮询服务（USTRUCT，F前缀） |
| `UGameInstanceSubsystem` | 创建统一的 AI 事件子系统进行全局委托广播，替代非内置的 GameplayMessageRouter 实现零耦合。事件载荷采用 `FInstancedStruct` 或 `FGameplayTagContainer + TMap<FName, FString>` 键值对，宿主只需广播标签+载荷，插件翻译为自然语言入记忆 |
| `USmartObjectComponent` | LLM给意图后，由组件决定具体的交互点和动画播段 |
| 自建 `SmartObjectBridge` 模块 | 轻量桥接层：自定义 StateTree Task 实现槽位查找/占用/释放/位置获取，替代实验版 GameplayInteractions |
| `FHttpModule` | 调用LLM API |
| `IWebSocket` | Realtime类接口（可选），主流LLM streaming走SSE over HTTP |
| `FGameplayTag` | 标记情感、关系、记忆标签 |

**蓝图支持策略：**
- 核心组件（`UAINpcComponent`）暴露 `BlueprintCallable` / `BlueprintAssignable` 接口
- 事件总线 Delegate 使用 `DECLARE_DYNAMIC_MULTICAST_DELEGATE` 保证蓝图可绑定
- NPC 人设 DataAsset 支持蓝图编辑
- 至少保证四个核心流程可纯蓝图完成：配置 API Key、发起对话、监听 NPC 响应、查询关系数值
- StateTree 自定义节点可通过引擎内置 `FStateTreeTaskBlueprintBase` 提供蓝图版本

---

## 六、插件模块结构

```
Plugins/AINpc/
├── AINpc.uplugin
├── Source/
│   ├── AINpcRuntime/          # 运行时模块
│   │   ├── LLM/              # LLM通信层
│   │   │   ├── ILLMProvider.h
│   │   │   ├── OpenAIProvider.cpp
│   │   │   ├── AnthropicProvider.cpp
│   │   │   └── LocalProvider.cpp
│   │   ├── Memory/            # 记忆系统
│   │   │   ├── NpcMemoryComponent.h
│   │   │   ├── NpcMemoryEntry.h
│   │   │   └── NpcMemoryStore.h    # SQLite封装
│   │   ├── Emotion/           # 情感系统
│   │   │   ├── NpcEmotionComponent.h
│   │   │   └── NpcRelationshipComponent.h
│   │   ├── Decision/          # 决策引擎
│   │   │   ├── NpcDecisionEngine.h
│   │   │   └── LLMResponseParser.h
│   │   ├── Action/            # 行为执行
│   │   │   ├── StateTreeTask_LLMQuery.h
│   │   │   ├── StateTreeTask_SmartObjectAction.h
│   │   │   └── StateTreeEvaluator_Perception.h
│   │   ├── SmartObjectBridge/  # 自建桥接层（替代实验版GameplayInteractions）
│   │   │   ├── SmartObjectBridgeContext.h    # 交互执行上下文
│   │   │   ├── StateTreeTask_FindSlot.h      # 查找SmartObject槽位
│   │   │   ├── StateTreeTask_ClaimSlot.h     # 占用/释放槽位
│   │   │   └── StateTreeTask_UseSlot.h       # 在槽位执行交互
│   │   ├── Perception/        # 感知扩展
│   │   │   └── NpcEventSubsystem.h # 基于 GameInstanceSubsystem 的解耦事件总线
│   │   ├── ContentGuard/      # 内容安全（参考 OWASP LLM Top 10 2025）
│   │   │   ├── InputSanitizer.h       # 提示注入防护（输入清洗+系统提示隔离，防 LLM07:2025 系统提示泄露）
│   │   │   └── OutputValidator.h      # JSON Schema校验+动作白名单+人设边界检测+敏感内容过滤
│   │   └── Core/              # 核心类
│   │       ├── AINpcController.h
│   │       ├── AINpcComponent.h   # 一键挂载组件
│   │       ├── AINpcSettings.h    # 项目设置
│   │       └── NpcPersonaDataAsset.h
│   ├── AINpcUI/                 # UI模块（可选，与Runtime隔离，DS可不编译）
│   │   └── NpcDialogueBubble.h
│   └── AINpcEditor/           # 编辑器模块
│       ├── PersonaEditor.h    # 人设编辑器
│       └── MemoryDebugger.h   # 记忆调试面板
└── Content/
    ├── StateTrees/            # 默认状态树
    └── UI/                    # 默认UI资产
```

---

## 七、可行性评估

### 7.1 技术可行性：✅ 完全可行

| 能力 | 引擎支持 | 实现难度 | 说明 |
|------|---------|---------|------|
| LLM API调用 | FHttpModule原生 | ★★ | HTTP+JSON，成熟方案 |
| 流式响应 | FHttpModule 流式回调（需自建 SSE Parser） + WebSocket(可选) | ★★★ | 引擎 HTTP 模块仅提供 `FHttpRequestStreamDelegate` 原始字节流回调，不含 SSE 协议解析；需自建 Parser 处理 `data:` 前缀、`[DONE]` 终止、多行拼接等 |
| StateTree扩展 | FStateTreeTaskBase 继承（USTRUCT） | ★★ | 引擎现代化状态机扩展方式 |
| 感知系统解耦 | UGameInstanceSubsystem | ★ | 引擎内置，通过全局 Multicast Delegate 解耦发包，开销极低 |
| 复杂环境交互 | SmartObjects | ★★★ | 需要预先在地图上配置 |
| 记忆持久化 | SQLiteCore引擎插件 | ★★ | 引擎内置（需启用），无第三方服务依赖 |
| 情感数值系统 | GameplayTags | ★★ | 纯逻辑，无技术障碍 |
| 对话UI | UMG Widget | ★★ | 标准UI开发 |
| 插件化封装 | .uplugin标准 | ★★ | 引擎标准流程 |

### 7.2 风险与挑战

| 风险 | 等级 | 缓解方案 |
|------|------|---------|
| LLM延迟影响游戏体验 | 🔴 高 | 异步调用+预生成+本地SLM降级+过渡状态动画（被击硬直/思考待机）掩盖延迟（见下方延迟掩盖策略表） |
| LLM幻觉（说出不合理的话） | 🟡 中 | 严格prompt约束+输出校验+白名单行为 |
| 云端API成本 | 🟡 中 | 支持本地模型+缓存常见回复+限制调用频率 |
| 记忆膨胀 | 🟢 低 | 重要性衰减+定期清理+容量上限 |
| 多NPC并发性能 | 🟡 中 | NpcScheduler：优先级队列+并发上限+批量合并+冷却间隔；屏幕外/远距NPC降级为本地模板响应 |
| 跨引擎版本兼容 | 🟢 低 | StateTree/SmartObjects API自5.4起趋于成熟（VersionName仍为0.1，EnabledByDefault: false）；已去除实验版GameplayInteractions依赖，自建桥接层完全可控 |
| GPU资源争用（本地SLM与渲染） | 🟡 中 | SLM推理限制GPU占用比例+CPU fallback+帧率优先策略 |
| 模型/SDK许可证合规 | 🟡 中 | 单列分发限制矩阵+仅集成宽松许可模型 |
| 云端模型版本漂移 | 🟡 中 | Provider层版本锁定+回归测试脚本 |
| 评测不可复现 | 🟢 低 | 固定评测脚本+交互回放+指标基线 |
| 多人游戏网络边界 | 🟡 中 | LLM决策/记忆写入须在Server端执行，对话/动作通过RPC同步；插件需明确C/S权威边界 |
| API Key泄露（客户端发行） | 🔴 高 | 开发模式直连（便捷）；生产模式走中转网关（Gateway），客户端仅持短期token |
| 内容安全/提示注入 | 🔴 高 | Where Winds Meet "Solid Snake Method"已证明现实风险；需输入消毒+输出JSON Schema校验+动作白名单+人设边界强化+文本安全过滤+审计日志 |
| LLM输出语言不一致 | 🟡 中 | System Prompt 强制指定语言 + 输出校验 + fallback模板（NPC在中文游戏中突然蹦英文是常见问题） |
| NPC人设漂移 | 🟡 中 | 长对话中NPC逐渐偏离设定人格；需人设边界强化（System Prompt锚定）+ 定期人设回顾注入 + 输出一致性校验 |
| 记忆/存档版本兼容 | 🟡 中 | 插件升级后记忆Schema变更导致旧存档不可读；需记忆条目版本号 + 迁移脚本 + 向前兼容读取 |

**延迟掩盖策略（LLM响应等待期间的用户体验保障）：**

| 触发场景 | 掩盖手段 | 预期掩盖时长 |
|---------|---------|------------|
| 对话发起 | NPC播放"思考"待机动画（挠头/沉吟） | 1-3s |
| 被攻击 | 受击硬直/后退动画（StateTree即时响应） | 0.5-1.5s |
| 收到礼物 | 拾取/端详物品动画 | 1-2s |
| 复杂决策 | NPC自言自语过渡台词（本地模板） | 2-4s |
| 超时降级 | 切换本地SLM或预设回复模板 | >4s fallback |

### 7.3 成本估算

| 方案档次 | 单次交互成本 | 说明 |
|---------|------------|------|
| 轻量云端模型 | ~$0.001-0.005 | 各厂商小参数/低价模型，性价比最高 |
| 标准云端模型 | ~$0.01-0.05 | 取决于上下文长度和模型档次 |
| 本地 SLM (Ollama/llama.cpp) | ~$0（硬件摊销） | 需独显 6GB+ VRAM |
| NVIDIA ACE 本地推理 | ~$0（硬件摊销） | 需 RTX 40系列+ |

**风险提示**：1M DAU × 10次交互/天 × $0.01 = $100K/天。插件应内置调用频率限制、响应缓存、本地SLM降级等成本控制机制。

---

## 八、实现路线图

### Phase 1：核心管线（MVP）
- LLM通信层（OpenAI Provider + 异步HTTP）
- 基础对话组件（文本输入→LLM→文本输出）
- NPC人设DataAsset（名字、性格、背景故事）
- 最简 StateTree（对话流转状态）
- 对话气泡UI

### Phase 2：感知与行为
- 独立感知系统 (基于 `UGameInstanceSubsystem` 的全局事件总线)
- 行为执行层（LLM输出包含意图→引入 SmartObject 执行）
- 多Provider支持（Anthropic、本地Ollama）
- 流式响应（逐字显示对话）

### Phase 3a：记忆存储与检索
- 三层记忆架构实现
- 记忆检索算法（加权求和：时间衰减+重要性+相关性）
- 选择性写入策略（重要性门槛过滤）
- 记忆持久化（SQLite存档集成）

### Phase 3b：反思与压缩
- 反思机制（累积重要性触发高层洞察）
- 记忆压缩（低价值记忆合并/归档）
- 长期记忆语义检索（可选：嵌入向量）

### Phase 4：情感与关系
- 情感状态机（数值驱动）
- 关系系统（好感度/信任度/熟悉度）
- 情感注入LLM prompt
- 情感影响 StateTree 流转树杈选择
- 可选增强：认知评估中间层（情感数值+情境→LLM/规则引擎评估→行为选择，如同样愤怒值面对强敌→退缩、面对弱敌→反击）

### Phase 5：打磨与工具
- 编辑器人设编辑面板
- 记忆调试器（可视化记忆流）
- 性能优化（请求队列、缓存、批处理）
- 测试框架：交互回放系统（固定seed/mock响应）+ NPC行为基线测试（预定义场景自动化验证）+ 人设一致性评分 + 性能基准（LLM延迟P50/P95/P99、记忆检索耗时）
- 示例项目和文档

### 扩展方向（可选）

**NPC 间交互：**
- NpcEventSubsystem 事件总线天然支持 NPC→NPC 事件广播
- 记忆系统 Tags 可标记事件来源为 NPC 而非 Player
- `FNpcRelationship` 可扩展为 NPC↔NPC 关系图
- 后续版本可实现 NPC 自主社交调度器（参考 Stanford Generative Agents 核心亮点）

**语音交互管线：**
- STT（语音转文字）：云端API / 本地 whisper.cpp
- TTS（文字转语音）：云端API / 本地开源方案 / NVIDIA Riva
- 面部动画驱动：NVIDIA Audio2Face-3D（已开源）
- 插件预留 `ISTTProvider` / `ITTSProvider` 接口，与 `ILLMProvider` 同级

---

## 九、结论

### 可行性判定：✅ 完全可行，且时机成熟

1. **技术栈已就绪**：UE5 的 StateTree、SmartObjects、GameInstanceSubsystem 等模块解决了行为解耦的核心痛点。
2. **业界已验证**：Inworld AI、NVIDIA ACE 已证明 LLM + 状态机 + 记忆 的混合架构可行。
3. **开源生态丰富**：Llama-Unreal、PhiloAgents 等项目提供了大量可参考的实现。
4. **通用插件可行**：仅依赖引擎标准插件体系（StateTree/SmartObjects），利用 GameInstanceSubsystem 实现与项目零耦合。

### 核心架构决策

| 决策 | 选择 | 理由 |
|------|------|------|
| AI动作方案 | LLM + StateTree + SmartObject | 自建轻量桥接层替代实验版GameplayInteractions，SO接管物理动画 |
| 解耦方案 | UGameInstanceSubsystem | GameplayMessageRouter 非引擎核心组件（源自Lyra），故采用原生子系统的全局委托解耦 |
| LLM接入 | 多Provider + 本地fallback | 灵活性+可靠性 |
| 记忆方案 | 三层记忆 + Stanford公式 | 学术验证+工程可行 |
| 情感方案 | 数值驱动（非LLM自管理） | 可控+可调试+高性能 |
| 持久化 | SQLiteCore(引擎内置插件，需启用) | 无第三方服务依赖 |
| 集成方式 | UE5插件(.uplugin) | 即插即用 |

### 与纯 BT/RL 方案的本质区别

传统方案只能做到"看起来聪明的预设行为"，而新架构能做到：
- **真正理解**玩家的自由输入
- **涌现性反应**（意料之外但合理的回应）
- **长期记忆**驱动的剧情演化
- **链式反应**（今天的善意→未来的回报）

这就是"AI Native"与"脚本AI"的本质区别。

---

## 十、参考资料

### 学术论文
- [Generative Agents: Interactive Simulacra of Human Behavior](https://github.com/joonspk-research/generative_agents) - Stanford, ⭐20,700+
- [An Appraisal-Based Chain-Of-Emotion Architecture](https://ar5iv.labs.arxiv.org/html/2309.05076) - 情感链架构
- [Combining RL and Behavior Trees (AMD Schola)](https://arxiv.org/html/2510.14154v1) - RL+BT混合方案
- [Driving Generative Agents With Their Personality](https://arxiv.org/html/2402.14879v1) - 人格驱动Agent
- [Mem0: Building Production-Ready AI Agent Memory](https://arxiv.org/abs/2504.19413) - 生产级Agent记忆框架
- [A-MEM: Agentic Memory for LLM Agents](https://arxiv.org/abs/2502.12110) - 自适应记忆结构
- [Memory in the Age of AI Agents](https://arxiv.org/abs/2512.13564) - Agent记忆综述
- [Learning Hierarchical Procedural Memory (MACLA)](https://arxiv.org/abs/2512.18950) - 层级程序性记忆框架
- [Dynamic Personality in LLM Agents](https://aclanthology.org/2025.findings-acl.1185/) - ACL 2025, 人格动态演化
- [Fixed-Persona SLMs for Game NPCs](https://arxiv.org/abs/2511.10277) - 小语言模型固定人格NPC + 运行时可热插拔记忆模块
- [Memory Retrieval in Generative Agents](https://arxiv.org/abs/2401.10069) - 记忆检索机制改进
- [Believable NPCs in Open-World Games (CHI Play 2024)](https://dl.acm.org/doi/10.1145/3665463.3678830) - 开放世界可信NPC玩家研究
- [Evaluating LLM Agent Memory (ICML 2025 Workshop LCFM / ICLR 2026 Poster)](https://arxiv.org/abs/2507.05257) - Agent记忆评测框架
- [Tricking LLM-Powered NPCs (arXiv 2025)](https://arxiv.org/abs/2508.19288) - LLM NPC提示注入攻击研究
- [OmniCharacter: Towards Unified Character Generation (arXiv 2025)](https://arxiv.org/abs/2505.20277) - 统一角色生成框架
- [Open-Theatre: Multi-Agent NPC Interaction (EMNLP 2025)](https://aclanthology.org/2025.emnlp-main.open-theatre) - 多Agent NPC交互框架
- [Exploring Presence with AI NPCs (ACM 2024)](https://dl.acm.org/doi/10.1145/3613905.3650756) - AI NPC沉浸感玩家体验研究

### 商业方案
- [Inworld AI - Agent Runtime平台](https://inworld.ai/blog/introducing-unreal-ai-runtime-sdk)
- [NVIDIA ACE - 自主游戏角色](https://developer.nvidia.com/ace-for-games)
- [NVIDIA ACE GitHub](https://github.com/NVIDIA/ACE) - 开源示例和参考工作流（Apache 2.0）
- [NVIDIA ACE 开源SLM本地部署](https://developer.nvidia.com/blog/nvidia-ace-adds-open-source-qwen3-slm-for-on-device-deployment-in-pc-games/)
- [Convai UE5 SDK](https://github.com/Conv-AI/Convai-UnrealEngine-SDK)
- [Personica AI - Cognitive NPC Brain](https://www.fab.com/listings/personica-ai) - Fab商城竞品插件
- [NVIDIA ACE × PUBG 合作公告](https://nvidianews.nvidia.com/news/nvidia-ace-generative-ai-microservices) - ACE商业合作案例
- [Where Winds Meet "Solid Snake Method" 提示注入事件](https://www.pcgamesn.com/where-winds-meet/ai-npc-exploit) - LLM NPC安全风险实例

### 开源项目
- [getnamo/Llama-Unreal](https://github.com/getnamo/Llama-Unreal) - llama.cpp UE5封装
- [life-exe/UnrealOpenAIPlugin](https://github.com/life-exe/UnrealOpenAIPlugin) - OpenAI全功能UE5封装（Chat/Image/Audio/Embedding等）
- [UnrealGenAISupport](https://github.com/prajwalshettydev/UnrealGenAISupport) - 多模型API封装
- [nickm980/smallville](https://github.com/nickm980/smallville) - Generative Agents游戏版
- [Interactive-LLM-Powered-NPCs](https://github.com/AkshitIreddy/Interactive-LLM-Powered-NPCs)
- [cognee](https://github.com/topoteretes/cognee) - Agent记忆系统
- [PhiloAgents课程](https://www.decodingai.com/p/build-your-gaming-simulation-ai-agent) - 完整工程化教程
- [Megafunk/MassSample](https://github.com/Megafunk/MassSample) - UE5 Mass框架示例（⭐1,100+，含ECS+StateTree实践）
- [mkturkcan/generative-agents](https://github.com/mkturkcan/generative-agents) - Generative Agents Unity实现（⭐990+，跨引擎参考）

### 引擎源码关键路径
- StateTree: `Engine/Plugins/Runtime/StateTree/Source/`
- GameplayStateTree: `Engine/Plugins/Runtime/GameplayStateTree/Source/`
- SmartObjects: `Engine/Plugins/Runtime/SmartObjects/Source/`
- GameplayInteractions: `Engine/Plugins/Runtime/GameplayInteractions/Source/`（实验版，仅参考）
- DB: `Engine/Plugins/Runtime/Database/SQLiteCore/`
- HTTP: `Engine/Source/Runtime/Online/HTTP/`
- WebSockets: `Engine/Source/Runtime/Online/WebSockets/`

### 引擎官方文档
- [State Tree Selectors Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/state-tree-selectors-overview) - UE5.7 Utility-based 状态选择机制
- [NVIDIA ACE Unreal Plugin 2.5](https://docs.nvidia.com/ace/ace-unreal-plugin/2.5/) - 面部动画驱动插件文档
- [Unreal Fest 2023: Building Smart Game AI with StateTree & SmartObjects](https://www.youtube.com/watch?v=LhEHs7GRKzI) - 官方StateTree+SmartObjects实战演讲

### 安全标准
- [OWASP Top 10 for LLM Applications 2025](https://genai.owasp.org/resource/owasp-top-10-for-llm-applications-2025/) - LLM应用安全Top 10（含LLM07:2025 系统提示泄露）

---
