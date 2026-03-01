# AINpc 插件系统设计文档（SDD）

> 来源：docs/PRD.md v1.4
> 版本：1.1
> 日期：2026-03-01

---

## 一、架构总览

### 1.1 分层架构

```
┌─────────────────────────────────────────────────────┐
│                   宿主游戏项目                        │
│  (蓝图/C++ 调用插件 API，广播事件，配置 DataAsset)      │
├─────────────────────────────────────────────────────┤
│  AINpcEditor 模块        │  AINpcUI 模块             │
│  (PersonaEditor,         │  (对话气泡, 调试面板)       │
│   MemoryDebugger)        │  仅客户端, 依赖 UMG/Slate  │
├──────────────────────────┴──────────────────────────┤
│                  AINpcRuntime 模块                    │
│  ┌────────────┐ ┌──────────┐ ┌───────────────────┐  │
│  │ LLM 通信层  │ │ 行为执行层│ │    感知系统        │  │
│  │ (Provider,  │ │(StateTree│ │ (NpcEventSubsystem│  │
│  │  SSE, 重试) │ │ SmartObj)│ │  FInstancedStruct)│  │
│  ├────────────┤ ├──────────┤ ├───────────────────┤  │
│  │ 记忆系统    │ │ 情感关系  │ │   Prompt 工程     │  │
│  │ (三层记忆,  │ │ (VAD,    │ │  (模板构建,       │  │
│  │  SQLite)   │ │  OCEAN)  │ │   Token 管理)     │  │
│  ├────────────┤ ├──────────┤ ├───────────────────┤  │
│  │ 安全系统    │ │ 网络同步  │ │   调度系统        │  │
│  │(Sanitizer, │ │(Server   │ │  (限流, LOD,      │  │
│  │ Validator) │ │ Authority│ │   NpcScheduler)   │  │
│  └────────────┘ └──────────┘ └───────────────────┘  │
├─────────────────────────────────────────────────────┤
│              UE5 引擎标准模块                         │
│  Core │ AIModule │ StateTree │ SmartObjects │ HTTP   │
│  SQLiteCore │ GameplayTags │ Json │ UMG/Slate       │
└─────────────────────────────────────────────────────┘
```

### 1.2 插件模块划分

| 模块 | 类型 | 职责 | 依赖 | Phase |
|------|------|------|------|-------|
| AINpcRuntime | Runtime | 全部运行时逻辑 | Core, AIModule, HTTP, StateTree, SmartObjects, SQLiteCore, GameplayTags, Json | 1 |
| AINpcUI | ClientOnly | 对话气泡、调试 HUD | AINpcRuntime, UMG, Slate | 1 |
| AINpcEditor | Editor | PersonaEditor, MemoryDebugger | AINpcRuntime, UnrealEd | 5 |

> AINpcUI 与 Runtime 隔离，Dedicated Server 编译时排除 UMG/Slate 依赖（NFR-6）。

### 1.3 核心对象关系图

```
AActor (NPC)
 ├── UAINpcComponent ◄─── 核心入口，持有工作记忆+情景记忆+情感状态+关系数据
 │    ├── 订阅 NpcEventSubsystem 事件
 │    ├── 持有 UNpcPersonaDataAsset* 引用
 │    ├── 提供 LLM 上下文给 StateTree Task
 │    └── 蓝图委托：OnDialogueReceived / OnEmotionChanged / OnRelationshipChanged / OnLLMFallback
 │
 ├── UAINpcNetworkComponent ◄─── 网络同步（可选）
 │    ├── ServerRPC：客户端对话请求 → Server 执行 LLM
 │    ├── MulticastRPC：Server → 全客户端广播结果
 │    └── 单机模式：ShouldSkipNetwork() 直连 UAINpcComponent
 │
 └── AAINpcController (AIController)
      ├── 持有并 Tick StateTree（AIComponentSchema）
      ├── Component-only 模式下由插件自动创建
      └── 通过 GetComponent<UAINpcComponent>() 访问 NPC 数据

UNpcEventSubsystem (GameInstanceSubsystem)
 └── 全局委托广播，UAINpcComponent 自行订阅+过滤

UMemorySubsystem (GameInstanceSubsystem)
 └── 持有 SQLite 连接，按 NpcId 分表管理长期记忆

ULLMRequestSubsystem (GameInstanceSubsystem)
 └── 管理并发限流（NFR-3）、请求队列、Provider 实例池
```

---

## 二、模块内部结构

### 2.1 AINpcRuntime 目录结构

```
AINpcRuntime/
├── Public/
│   ├── Core/
│   │   ├── AINpcComponent.h              // UAINpcComponent
│   │   ├── AINpcController.h             // AAINpcController
│   │   ├── NpcPersonaDataAsset.h         // UNpcPersonaDataAsset
│   │   └── AINpcSettings.h              // UAINpcSettings (项目设置)
│   ├── LLM/
│   │   ├── ILLMProvider.h               // ILLMProvider 接口
│   │   ├── LLMRequestSubsystem.h        // 并发限流+请求队列
│   │   ├── LLMTypes.h                   // FLLMRequest, FLLMResponse
│   │   ├── SSEParser.h                  // SSE 流式解析器
│   │   └── Providers/
│   │       ├── OpenAIProvider.h
│   │       ├── AnthropicProvider.h
│   │       ├── LocalProvider.h          // Ollama
│   │       └── CustomProvider.h
│   ├── Memory/
│   │   ├── MemorySubsystem.h            // UMemorySubsystem
│   │   ├── MemoryTypes.h                // FMemoryEntry, EMemoryType
│   │   ├── IRelevanceScorer.h           // 检索评分接口
│   │   ├── IEmbeddingProvider.h         // 向量化接口
│   │   └── MemoryConflictResolver.h     // 冲突解决器
│   ├── Emotion/
│   │   ├── EmotionTypes.h               // FVADState, FRelationshipData
│   │   ├── AppraisalEngine.h            // 评价链计算
│   │   └── EmotionDecayProcessor.h      // 情感衰减
│   ├── Behavior/
│   │   ├── StateTree/
│   │   │   ├── STTask_LLMQuery.h        // FStateTreeTask_LLMQuery
│   │   │   └── STTask_ExecuteSmartObject.h
│   │   ├── SmartObjectBridge.h          // SmartObject 桥接
│   │   ├── LLMResponseParser.h          // 三级降级解析
│   │   └── ActionValidator.h            // IActionValidator 接口
│   ├── Perception/
│   │   ├── NpcEventSubsystem.h          // UNpcEventSubsystem
│   │   └── NpcEventPayloads.h           // 常用载荷类型
│   ├── Prompt/
│   │   ├── PromptBuilder.h              // Prompt 模板构建器
│   │   └── PromptTypes.h               // EPromptLayer 枚举
│   ├── Security/
│   │   ├── InputSanitizer.h
│   │   └── OutputValidator.h
│   └── Network/
│       └── AINpcNetworkComponent.h      // 网络同步组件
└── Private/
    └── (对应 .cpp 实现)
```

---

## 三、核心数据结构

### 3.1 LLM 通信层

```cpp
// ---- LLMTypes.h ----

// Provider 能力标志位
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsBitMaskValues))
enum class EProviderCapability : uint8
{
    None            = 0,
    Streaming       = 1 << 0,   // SSE 流式
    JsonMode        = 1 << 1,   // 原生 JSON Mode
    FunctionCalling = 1 << 2,   // Function Calling
    Embedding       = 1 << 3,   // 向量化
};
ENUM_CLASS_FLAGS(EProviderCapability);

USTRUCT(BlueprintType)
struct FLLMMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString Role;       // "system" | "user" | "assistant"

    UPROPERTY(BlueprintReadWrite)
    FString Content;
};

USTRUCT(BlueprintType)
struct FLLMRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    TArray<FLLMMessage> Messages;

    UPROPERTY(BlueprintReadWrite)
    float Temperature = 0.7f;

    UPROPERTY(BlueprintReadWrite)
    int32 MaxTokens = 512;

    UPROPERTY(BlueprintReadWrite)
    bool bStream = false;

    UPROPERTY(BlueprintReadWrite)
    FString JsonSchema;     // 可选，强制 JSON 输出的 Schema

    UPROPERTY()
    FString ModelOverride;  // 可选，覆盖 Provider 默认模型
};

USTRUCT(BlueprintType)
struct FLLMResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly)
    FString Content;

    UPROPERTY(BlueprintReadOnly)
    FString ErrorMessage;

    UPROPERTY(BlueprintReadOnly)
    int32 PromptTokens = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 CompletionTokens = 0;

    UPROPERTY(BlueprintReadOnly)
    float LatencySeconds = 0.f;
};
```

### 3.2 记忆系统

```cpp
// ---- MemoryTypes.h ----

UENUM(BlueprintType)
enum class EMemoryType : uint8
{
    Factual,        // 事实："玩家名叫张三"
    Experiential,   // 经历："玩家昨天帮我找回了猫"
    Working,        // 工作记忆（上下文窗口中的临时条目）
};

UENUM(BlueprintType)
enum class EConflictAction : uint8
{
    ADD,        // 无冲突，直接新增
    UPDATE,     // 新信息覆盖旧信息
    MERGE,      // 合并为更完整记忆
    COEXIST,    // 矛盾记忆并存
    SUPERSEDE,  // 完全取代
};

USTRUCT(BlueprintType)
struct FMemoryEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int64 MemoryId = 0;

    UPROPERTY(BlueprintReadOnly)
    FString NpcId;

    UPROPERTY(BlueprintReadWrite)
    FString Content;

    UPROPERTY(BlueprintReadOnly)
    FDateTime Timestamp;

    UPROPERTY(BlueprintReadWrite)
    float Importance = 0.f;         // [0, 10]

    UPROPERTY(BlueprintReadOnly)
    EMemoryType MemoryType = EMemoryType::Experiential;

    UPROPERTY(BlueprintReadOnly)
    TArray<int64> LinkedMemoryIds;

    UPROPERTY(BlueprintReadOnly)
    TArray<int64> SourceMemoryIds;  // 仅反思洞察条目使用

    UPROPERTY(BlueprintReadOnly)
    int32 AccessCount = 0;

    UPROPERTY()
    int32 SchemaVersion = 1;

    UPROPERTY(BlueprintReadOnly)
    bool bContradicted = false;     // COEXIST 标记
};
```

### 3.3 情感与关系系统

```cpp
// ---- EmotionTypes.h ----

USTRUCT(BlueprintType)
struct FVADState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float Valence = 0.f;    // [-1, 1] 愉悦-不悦

    UPROPERTY(BlueprintReadWrite)
    float Arousal = 0.f;    // [0, 1] 激活度

    UPROPERTY(BlueprintReadWrite)
    float Dominance = 0.5f; // [0, 1] 支配感
};

USTRUCT(BlueprintType)
struct FRelationshipData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float Affinity = 0.f;       // [-100, 100]

    UPROPERTY(BlueprintReadWrite)
    float Trust = 50.f;         // [0, 100]

    UPROPERTY(BlueprintReadWrite)
    float Familiarity = 0.f;    // [0, 100]
};

// LLM 返回的关系变化量（默认值全 0，区别于 FRelationshipData 的状态值）
USTRUCT(BlueprintType)
struct FRelationshipDelta
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float Affinity = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float Trust = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float Familiarity = 0.f;
};

USTRUCT(BlueprintType)
struct FNpcOceanPersonality
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, meta=(ClampMin=0, ClampMax=1))
    float Openness = 0.5f;

    UPROPERTY(BlueprintReadWrite, meta=(ClampMin=0, ClampMax=1))
    float Conscientiousness = 0.5f;

    UPROPERTY(BlueprintReadWrite, meta=(ClampMin=0, ClampMax=1))
    float Extraversion = 0.5f;

    UPROPERTY(BlueprintReadWrite, meta=(ClampMin=0, ClampMax=1))
    float Agreeableness = 0.5f;

    UPROPERTY(BlueprintReadWrite, meta=(ClampMin=0, ClampMax=1))
    float Neuroticism = 0.5f;
};

// 评价链规则（FR-21）
USTRUCT(BlueprintType)
struct FAppraisalRule
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float GoalRelevance = 0.f;      // [-1, 1] 对 NPC 目标的相关性

    UPROPERTY(BlueprintReadWrite)
    float Certainty = 0.5f;         // [0, 1] 事件确定性

    UPROPERTY(BlueprintReadWrite)
    float Agency = 0.5f;            // [0, 1] 事件由谁引起（0=外部, 1=自身）

    UPROPERTY(BlueprintReadWrite)
    float CopingPotential = 0.5f;   // [0, 1] NPC 应对能力

    // OCEAN 调制系数：最终评价 = 基础值 + OceanModifier · NPC人格值
    UPROPERTY(BlueprintReadWrite)
    FNpcOceanPersonality OceanModifier;
};
```

### 3.4 NpcPersonaDataAsset

```cpp
// ---- NpcPersonaDataAsset.h ----

UENUM(BlueprintType)
enum class EPromptLayer : uint8
{
    System,     // 系统层（不可覆盖）
    Persona,    // 人格层
    Memory,     // 记忆层
    Context,    // 情境层
    Output,     // 输出约束
};

UENUM(BlueprintType)
enum class EDelayStrategy : uint8
{
    Thinking,   // 思考待机
    HitReact,   // 受击硬直
    Inspect,    // 端详物品
    Timeout,    // 超时过渡
};

UCLASS(BlueprintType)
class UNpcPersonaDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // ---- Phase 1 ----
    UPROPERTY(EditAnywhere, Category="LLM")
    FString ApiKey;

    UPROPERTY(EditAnywhere, Category="Persona")
    FString PersonaName;

    UPROPERTY(EditAnywhere, Category="Persona", meta=(MultiLine=true))
    FString Background;

    UPROPERTY(EditAnywhere, Category="Persona")
    FString SpeakingStyle;

    UPROPERTY(EditAnywhere, Category="Prompt")
    TMap<EPromptLayer, FString> PromptTemplateOverrides;

    UPROPERTY(EditAnywhere, Category="Animation")
    TMap<EDelayStrategy, TSoftObjectPtr<UAnimMontage>> DelayMaskingMontages;

    // ---- Phase 3a ----
    UPROPERTY(EditAnywhere, Category="Memory")
    FVector RetrievalWeights = FVector(1.0f, 1.0f, 1.0f); // α, β, γ

    // ---- Phase 4 ----
    UPROPERTY(EditAnywhere, Category="Personality")
    FNpcOceanPersonality OCEAN;

    UPROPERTY(EditAnywhere, Category="Personality", meta=(ClampMin=0, ClampMax=1))
    float PersonalityInertia = 0.7f;

    UPROPERTY(EditAnywhere, Category="Emotion")
    TMap<FGameplayTag, FAppraisalRule> AppraisalRules;
};
```

### 3.5 LLM 结构化响应

```cpp
// ---- LLMResponseParser.h ----

USTRUCT(BlueprintType)
struct FNpcAction
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag ActionType;    // e.g. "Action.Sit", "Action.PickUp"

    UPROPERTY(BlueprintReadOnly)
    FString Target;             // SmartObject ID，可为空
};

USTRUCT(BlueprintType)
struct FParsedLLMResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Dialogue;

    UPROPERTY(BlueprintReadOnly)
    TArray<FNpcAction> Actions;

    UPROPERTY(BlueprintReadOnly)
    FVADState EmotionDelta;

    UPROPERTY(BlueprintReadOnly)
    FRelationshipDelta RelationshipDelta;

    UPROPERTY(BlueprintReadOnly)
    bool bParsedAsJson = false;     // true=严格/宽松JSON, false=纯文本降级
};

// 三级降级解析策略（FR-27）
// 1. 严格 JSON：完整 Schema 校验
// 2. 宽松提取：正则匹配 "dialogue"/"actions" 字段
// 3. 纯文本：整段作为 Dialogue，Actions/Delta 为空
```

---

## 四、子系统详细设计

### 4.1 LLM 通信层

#### 4.1.1 ILLMProvider 接口

```cpp
class ILLMProvider
{
public:
    virtual ~ILLMProvider() = default;

    // 同步查询能力
    virtual EProviderCapability GetCapabilities() const = 0;

    // 异步请求（非流式）
    virtual void SendRequest(
        const FLLMRequest& Request,
        FOnLLMResponseComplete OnComplete) = 0;

    // 异步请求（流式，逐 token 回调）
    virtual void SendStreamRequest(
        const FLLMRequest& Request,
        FOnLLMPartialResponse OnPartial,
        FOnLLMResponseComplete OnComplete) = 0;

    // 取消进行中的请求
    virtual void CancelRequest(int32 RequestId) = 0;

    // Provider 标识
    virtual FName GetProviderName() const = 0;
};

// 委托定义
DECLARE_DELEGATE_OneParam(FOnLLMResponseComplete, const FLLMResponse&);
DECLARE_DELEGATE_OneParam(FOnLLMPartialResponse, const FString& /*PartialContent*/);
// 蓝图动态多播
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMResponseBP, FLLMResponse, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMPartialResponseBP, const FString&, Partial);
```

#### 4.1.2 LLMRequestSubsystem（并发限流）

```cpp
// 请求优先级（FR-13 A-3 修复）
UENUM()
enum class ELLMRequestPriority : uint8
{
    Dialogue,           // 对话请求，最高优先级
    Reflection,         // 反思请求
    ConflictResolve,    // 记忆冲突解决，最低优先级（队列满时降级 ADD）
};

UCLASS()
class ULLMRequestSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    // 提交请求，返回 RequestId
    int32 EnqueueRequest(const FLLMRequest& Request,
                         ELLMRequestPriority Priority,
                         FOnLLMResponseComplete OnComplete);

    void CancelRequest(int32 RequestId);

private:
    // NFR-3：同帧最大并发 ≤ 3
    static constexpr int32 MaxConcurrent = 3;
    int32 ActiveCount = 0;

    // 优先级队列（Dialogue > Reflection > ConflictResolve）
    TArray<FPendingLLMRequest> PendingQueue;

    // Provider 实例池（按配置创建）
    TMap<FName, TUniquePtr<ILLMProvider>> ProviderPool;

    void TryDispatchNext();
    // 所有回调保证在 GameThread 触发（HTTP 模块回调 → AsyncTask(GameThread) 转发）
    void OnRequestComplete(int32 RequestId, const FLLMResponse& Response);
};
```

#### 4.1.3 SSE Parser（FR-4）

```cpp
// 自建 SSE 解析器，处理 FHttpRequestStreamDelegate 原始字节流
class FSSEParser
{
public:
    // 喂入原始字节块（可能跨包）
    void Feed(const TArray<uint8>& RawBytes);

    // 回调
    TFunction<void(const FString& Data)> OnData;    // data: 行
    TFunction<void()> OnDone;                        // [DONE]
    TFunction<void(const FString& Error)> OnError;   // error 事件

private:
    FString Buffer;     // 跨包拼接缓冲
    void ProcessLine(const FString& Line);
    // 忽略心跳注释行（以 ':' 开头）
};
```

#### 4.1.4 降级链（FR-5）

```
请求发出 → 超时/失败
  ├─ Phase 1: 预设模板响应 → 静默失败+蓝图通知
  └─ Phase 2+:
       ├─ 本地 SLM（LocalProvider，若已配置）
       ├─ 预设模板响应（NpcPersonaDataAsset 中配置）
       └─ 静默失败 + OnLLMFallback 蓝图事件通知

重试策略：最多 2 次，间隔 = BaseDelay × 2^attempt（1s, 2s）
超时阈值：默认 4s（与 NFR-1 P95 对齐）
```

### 4.2 行为执行层

#### 4.2.1 StateTree 设计（FR-28/29）

```
默认 StateTree 资产：ST_AINpcDefault

Schema: AIComponentSchema（要求 AAIController 宿主）

状态流转：
  Idle ──[对话请求]──► WaitingForLLM
                         │
                    [LLM 响应到达]
                         │
                         ▼
                      Speaking ──[文本播放完毕]──► Cooldown
                         │                          │
                    [超时 4s]                   [冷却结束]
                         │                          │
                         ▼                          ▼
                       Idle ◄───────────────────── Idle
```

自定义 Task（USTRUCT，非 UCLASS）：

```cpp
// ---- STTask_LLMQuery.h ----
USTRUCT()
struct FStateTreeTask_LLMQuery : public FStateTreeTaskBase
{
    GENERATED_BODY()

    // 通过 AIController → GetComponent<UAINpcComponent> 获取上下文
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
        const FStateTreeTransitionResult& Transition) const override;

    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
        const float DeltaTime) const override;

private:
    // EnterState 时：构建 Prompt → 提交 LLMRequestSubsystem
    // Tick 时：检查响应是否到达，到达则解析并切换状态
};
```

#### 4.2.2 SmartObjectBridge（FR-31）

```cpp
// GameInstanceSubsystem，全局管理 SmartObject 交互
UCLASS()
class USmartObjectBridge : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    // 查询 NPC 周围可交互对象（FR-30，注入 Prompt）
    TArray<FSmartObjectCandidate> FindNearbyObjects(
        const FVector& Location, float Radius) const;

    // 占用槽位（返回 ClaimHandle）
    FSmartObjectClaimHandle ClaimSlot(
        const FSmartObjectCandidate& Candidate, AActor* User);

    // 释放槽位
    void ReleaseSlot(const FSmartObjectClaimHandle& Handle);

    // 获取交互位置
    FTransform GetSlotTransform(const FSmartObjectClaimHandle& Handle) const;
};

USTRUCT(BlueprintType)
struct FSmartObjectCandidate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString ObjectId;           // Prompt 注入用的标识

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag ActionTag;     // e.g. "Action.Sit"

    UPROPERTY(BlueprintReadOnly)
    FVector Location;

    UPROPERTY(BlueprintReadOnly)
    bool bSlotAvailable = true;
};
```

#### 4.2.3 裁判架构（FR-32）

```
LLM 建议动作 → FParsedLLMResponse.Actions
  │
  ▼
FStateTreeTask_ExecuteSmartObject
  │
  ├─ Phase 2: 内联白名单校验
  │   if ActionTag NOT IN FindNearbyObjects() 结果集 → 拒绝，跳过该动作
  │
  └─ Phase 4: IActionValidator 接口
      │
      ▼
    UOutputValidator::ValidateAction()
      ├─ 白名单校验（同上）
      ├─ 人设边界检测（NPC 性格是否允许该动作）
      └─ 敏感动作过滤
```

```cpp
// Phase 4 抽出的接口
class IActionValidator
{
public:
    virtual bool ValidateAction(
        const FNpcAction& Action,
        const TArray<FSmartObjectCandidate>& AvailableObjects,
        const UNpcPersonaDataAsset* Persona) const = 0;
};
```

### 4.3 感知系统（FR-34/35）

```cpp
UCLASS()
class UNpcEventSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    // 宿主广播事件（蓝图可调用）
    UFUNCTION(BlueprintCallable)
    void BroadcastEvent(FGameplayTag EventTag, const FInstancedStruct& Payload);

    // C++ 静态委托（高性能）
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNpcEvent,
        FGameplayTag /*Tag*/, const FInstancedStruct& /*Payload*/);
    FOnNpcEvent OnNpcEvent;

    // 蓝图动态多播
    UPROPERTY(BlueprintAssignable)
    FOnNpcEventBP OnNpcEventBP;
};
```

**事件消费顺序**（FR-34，固定管线）：

UAINpcComponent 订阅 UNpcEventSubsystem，按标签过滤后依次执行：

| 步骤 | 消费者 | 同步/异步 | 说明 |
|------|--------|-----------|------|
| ① | 延迟掩盖动画（FR-33） | 同步即时 | 受击等不等 StateTree Tick |
| ② | 情感评价链（FR-21） | 同步计算 | 本地规则，零 LLM 调用 |
| ③ | 记忆写入（FR-12） | 异步入队 | 重要性评估后决定是否写入 |
| ④ | Prompt 情境更新（FR-36） | 标记脏位 | 下次 LLM 调用时生效 |

### 4.4 记忆系统

#### 4.4.1 三层架构（FR-9）

```
┌─────────────────────────────────────────┐
│         工作记忆（Working Memory）        │
│  TArray<FLLMMessage>, ~20 条            │
│  宿主: UAINpcComponent                   │
│  生命周期: 对话 Session                   │
│  用途: LLM 上下文窗口                     │
├─────────────────────────────────────────┤
│         情景记忆（Episodic Memory）       │
│  TArray<FMemoryEntry>, ~200 条上限       │
│  宿主: UAINpcComponent                   │
│  生命周期: NPC 实例（关卡切换时持久化）     │
│  写入门槛: Importance ≥ 3                │
├─────────────────────────────────────────┤
│         长期记忆（Long-term Memory）      │
│  SQLite, 无上限                          │
│  宿主: UMemorySubsystem（全局单 DB）      │
│  生命周期: 跨关卡持久化                    │
│  写入门槛: Importance ≥ 5                │
└─────────────────────────────────────────┘
```

#### 4.4.2 检索算法（FR-10/11）

```cpp
// ---- IRelevanceScorer.h ----

class IRelevanceScorer
{
public:
    virtual ~IRelevanceScorer() = default;

    // 计算单条记忆的综合得分
    virtual float ScoreMemory(
        const FMemoryEntry& Entry,
        const FString& QueryContext,
        const FDateTime& Now) const = 0;
};

// ---- 默认实现 ----
// Score = α × Recency + β × Importance_norm + γ × Relevance
//
// Recency 分段衰减（FR-11）：
//   Δt < 1h  → 1.0
//   Δt < 1d  → 0.8
//   Δt < 1w  → 0.5
//   Δt > 1w  → 0.5 × exp(-λ × (Δt - 1w))，λ = 0.1
//
// Importance_norm = Importance / 10.0
//
// Relevance：
//   有 Embedding → 余弦相似度（IEmbeddingProvider）
//   无 Embedding → SQLite FTS5 BM25 分数归一化
//
// α/β/γ 从 NpcPersonaDataAsset::RetrievalWeights 读取
// 检索时沿 LinkedMemoryIds 扩展 1 跳（FR-19）
```

```cpp
// ---- IEmbeddingProvider.h ----（FR-17）

class IEmbeddingProvider
{
public:
    virtual ~IEmbeddingProvider() = default;

    // 文本向量化（异步）
    virtual void Embed(const FString& Text,
        TFunction<void(const TArray<float>& /*Vector*/)> OnComplete) = 0;

    // 批量向量化
    virtual void EmbedBatch(const TArray<FString>& Texts,
        TFunction<void(const TArray<TArray<float>>& /*Vectors*/)> OnComplete) = 0;

    // 向量维度（模型决定，如 1536 for text-embedding-3-small）
    virtual int32 GetDimension() const = 0;
};

// 无 Embedding Provider 时，MemorySubsystem 降级为 SQLite FTS5 BM25 检索
```

#### 4.4.3 记忆冲突解决（FR-13）

```
写入新记忆 Entry_new
  │
  ▼
检索相似记忆（Top-K=5，相似度阈值 ≥ 0.75）
  │
  ├─ 无相似记忆 → ADD，直接写入
  │
  └─ 有相似记忆 → 异步提交 LLM 判断请求
       │           （优先级 = ConflictResolve，最低）
       │
       ├─ 队列满 → 降级为 ADD
       ├─ LLM 失败 → 降级为 ADD
       │
       └─ LLM 返回判断：
            ├─ ADD       → 直接写入新条目
            ├─ UPDATE    → 用新内容覆盖旧条目的 Content + Timestamp
            ├─ MERGE     → 合并内容写入新条目，旧条目标记归档
            ├─ COEXIST   → 两条并存，双方标记 bContradicted = true
            └─ SUPERSEDE → 写入新条目，删除旧条目
```

```cpp
// ---- MemoryConflictResolver.h ----

UCLASS()
class UMemoryConflictResolver : public UObject
{
    GENERATED_BODY()
public:
    // 异步解决冲突，回调返回最终操作
    void ResolveAsync(
        const FMemoryEntry& NewEntry,
        const TArray<FMemoryEntry>& Candidates,
        TFunction<void(EConflictAction, FMemoryEntry /*FinalEntry*/)> OnResolved);

private:
    // 构建冲突判断 Prompt，提交到 LLMRequestSubsystem
    void SubmitLLMJudgment(
        const FMemoryEntry& NewEntry,
        const FMemoryEntry& Candidate,
        TFunction<void(EConflictAction)> OnJudged);
};
```

#### 4.4.4 反思机制（FR-15）

```
累积重要性计数器（UAINpcComponent 持有）
  │
  ├─ 每次写入记忆：Counter += Entry.Importance
  │
  └─ Counter > 150 → 触发反思
       │
       ▼
    从情景记忆取最近 20 条（按时间倒序）
       │
       ▼
    提交 LLM（优先级 = Reflection）
    Prompt: "从以下经历中提取 1-3 条高层洞察"
       │
       ▼
    解析洞察 → 写入新 FMemoryEntry
      ├─ MemoryType = Factual
      ├─ Importance = 8（洞察默认高重要性）
      ├─ SourceMemoryIds = 源记忆 ID 列表
      └─ Counter 重置为 0
```

#### 4.4.5 主动遗忘（FR-14）

```
情景记忆数量 ≥ 200
  │
  ▼
计算所有条目的 EvictionScore：
  EvictionScore = w1×(1 - Recency) + w2×(1 - Importance_norm) + w3×(1 - AccessFreq_norm)

  w1 = 0.4, w2 = 0.35, w3 = 0.25（默认权重）
  AccessFreq_norm = min(AccessCount, 20) / 20.0
  │
  ▼
按 EvictionScore 降序排列，淘汰得分最高的条目
  ├─ Importance ≥ 5 的条目：迁移到长期记忆（SQLite）
  └─ Importance < 5 的条目：直接丢弃
```

### 4.5 情感与关系系统

#### 4.5.1 评价链引擎（FR-21）

```
事件到达（FGameplayTag + FInstancedStruct）
  │
  ▼
查找 AppraisalRule（NpcPersonaDataAsset::AppraisalRules[EventTag]）
  │
  ├─ 未找到 → 使用默认规则集（8 类事件预设）
  │
  └─ 找到 → 4 维评价计算
       │
       ▼
    FinalGoalRelevance  = Rule.GoalRelevance  + Rule.OceanModifier.Openness × OCEAN.Openness
    FinalCertainty      = Rule.Certainty      + Rule.OceanModifier.Conscientiousness × OCEAN.Conscientiousness
    FinalAgency         = Rule.Agency         + Rule.OceanModifier.Extraversion × OCEAN.Extraversion
    FinalCopingPotential= Rule.CopingPotential+ Rule.OceanModifier.Agreeableness × OCEAN.Agreeableness
       │
       ▼
    情感推导（纯数学，零 LLM 调用）：
    ΔValence   = FinalGoalRelevance × FinalCertainty
    ΔArousal   = |FinalGoalRelevance| × (1 - FinalCopingPotential)
    ΔDominance = FinalAgency × FinalCopingPotential
       │
       ▼
    人格惯性过滤（FR-24）：
    NewVAD = CurrentVAD + ΔVAD × (1 - PersonalityInertia)
    Clamp: Valence[-1,1], Arousal[0,1], Dominance[0,1]
```

#### 4.5.2 情感衰减（FR-24）

```cpp
// ---- EmotionDecayProcessor.h ----

// 每 Tick 调用（由 UAINpcComponent::TickComponent 驱动）
// 衰减速率 = BaseDecayRate × (1 - Neuroticism × 0.5)
// 高神经质 → 衰减慢 → 情绪持续更久

class FEmotionDecayProcessor
{
public:
    static void Tick(FVADState& Current, const FNpcOceanPersonality& OCEAN,
                     float DeltaTime, float BaseDecayRate = 0.1f)
    {
        const float Rate = BaseDecayRate * (1.f - OCEAN.Neuroticism * 0.5f);
        // Valence 向 0 衰减，Arousal 向 0 衰减，Dominance 向 0.5 衰减
        Current.Valence   = FMath::FInterpTo(Current.Valence,   0.f,  DeltaTime, Rate);
        Current.Arousal   = FMath::FInterpTo(Current.Arousal,   0.f,  DeltaTime, Rate);
        Current.Dominance = FMath::FInterpTo(Current.Dominance, 0.5f, DeltaTime, Rate);
    }
};
```

#### 4.5.3 关系更新（FR-22）

```
LLM 响应解析后得到 FRelationshipData Delta
  │
  ▼
UAINpcComponent::UpdateRelationship(PlayerId, Delta)
  │
  ├─ Affinity  += Delta.Affinity,  Clamp[-100, 100]
  ├─ Trust     += Delta.Trust,     Clamp[0, 100]
  └─ Familiarity += Delta.Familiarity, Clamp[0, 100]

关系自然衰减（每 Tick）：
  Affinity   → 向 0 衰减（速率 0.01/s）
  Trust      → 向 50 衰减（速率 0.005/s）
  Familiarity → 不衰减（只增不减）

关系数据存储：
  TMap<FString /*PlayerId*/, FRelationshipData> Relationships;
  持久化随情景记忆一起写入 SQLite
```

### 4.6 Prompt 工程（FR-36/37）

#### 4.6.1 五层模板结构

```
┌─────────────────────────────────────────┐
│  ① 系统层（System）— 不可覆盖/不可截断   │
│  "你是一个游戏NPC，必须遵守以下规则..."    │
│  代码级强制拼接，开发者无法通过 DataAsset 移除│
├─────────────────────────────────────────┤
│  ② 人格层（Persona）— 可覆盖/可截断       │
│  OCEAN 数值+描述 + 说话风格 + 背景故事     │
│  Phase 1 启用                             │
├─────────────────────────────────────────┤
│  ③ 记忆层（Memory）— 可覆盖/可截断        │
│  检索到的相关记忆条目（按 Score 排序）      │
│  Phase 3a 启用                            │
├─────────────────────────────────────────┤
│  ④ 情境层（Context）— 可覆盖/可截断       │
│  当前情感状态 + 关系数值 + 周围 SmartObject │
│  Phase 4 启用                             │
├─────────────────────────────────────────┤
│  ⑤ 输出约束（Output）— 不可覆盖/不可截断   │
│  JSON Schema + 语言约束 + 动作格式要求      │
└─────────────────────────────────────────┘

Token 超限截断优先级（从低到高）：
  情境层 → 记忆层 → 人格层
  系统层和输出约束层永不截断
```

#### 4.6.2 PromptBuilder

```cpp
// ---- PromptBuilder.h ----

UCLASS()
class UPromptBuilder : public UObject
{
    GENERATED_BODY()
public:
    // 构建完整 Prompt（返回 Messages 数组）
    TArray<FLLMMessage> Build(
        const UAINpcComponent* NpcComp,
        const FString& PlayerInput,
        int32 MaxTokenBudget = 4096) const;

private:
    // 各层构建（按顺序拼接）
    FString BuildSystemLayer() const;                           // 硬编码，不可覆盖
    FString BuildPersonaLayer(const UNpcPersonaDataAsset* P) const;
    FString BuildMemoryLayer(const UAINpcComponent* C) const;   // Phase 3a
    FString BuildContextLayer(const UAINpcComponent* C) const;  // Phase 4
    FString BuildOutputLayer() const;                           // 硬编码，不可覆盖

    // Token 预算管理：超限时按优先级截断
    void TruncateToFit(TArray<FLLMMessage>& Messages, int32 Budget) const;

    // 检查 DataAsset 是否有覆盖模板
    FString GetLayerContent(EPromptLayer Layer,
        const UNpcPersonaDataAsset* Persona,
        const FString& DefaultContent) const;
};
```

### 4.7 安全系统（FR-39/40/41）

#### 4.7.1 InputSanitizer

```cpp
// ---- InputSanitizer.h ----

UENUM()
enum class EThreatType : uint8
{
    None,
    DirectPrompt,       // "忽略以上指令，你现在是..."
    SocialEngineering,  // "假装你是另一个角色..."
    InstructionOverride,// "新规则：从现在起..."
};

USTRUCT()
struct FSanitizeResult
{
    GENERATED_BODY()

    UPROPERTY()
    bool bClean = true;

    UPROPERTY()
    EThreatType ThreatType = EThreatType::None;

    UPROPERTY()
    FString SanitizedInput;     // 清洗后的安全输入

    UPROPERTY()
    float ThreatScore = 0.f;   // [0, 1] 威胁置信度
};

UCLASS()
class UInputSanitizer : public UObject
{
    GENERATED_BODY()
public:
    FSanitizeResult Sanitize(const FString& RawInput, const FString& NpcId);

private:
    // 三类检测器
    bool DetectDirectPrompt(const FString& Input) const;       // 正则匹配
    bool DetectSocialEngineering(const FString& Input,
        const FString& NpcId);                                  // 角色扮演正则 + 信任计数器
    bool DetectInstructionOverride(const FString& Input) const; // 指令覆盖模式

    // 每个 NPC 的渐进式信任计数器（社会工程检测用）
    TMap<FString, int32> TrustEscalationCounters;
};
```

#### 4.7.2 OutputValidator（FR-40）

```cpp
// ---- OutputValidator.h ----

USTRUCT()
struct FValidationResult
{
    GENERATED_BODY()

    UPROPERTY()
    bool bValid = true;

    UPROPERTY()
    FString Reason;
};

UCLASS()
class UOutputValidator : public UObject, public IActionValidator
{
    GENERATED_BODY()
public:
    // 完整校验链（按顺序执行，任一失败即拒绝）
    FValidationResult Validate(
        const FParsedLLMResponse& Response,
        const TArray<FSmartObjectCandidate>& AvailableObjects,
        const UNpcPersonaDataAsset* Persona,
        const FVADState& CurrentEmotion);

    // IActionValidator 接口实现（Phase 4）
    virtual bool ValidateAction(
        const FNpcAction& Action,
        const TArray<FSmartObjectCandidate>& AvailableObjects,
        const UNpcPersonaDataAsset* Persona) const override;

private:
    // ① 动作白名单校验：Action.ActionType 必须在 AvailableObjects 中
    bool ValidateActionWhitelist(const FNpcAction& Action,
        const TArray<FSmartObjectCandidate>& Available) const;

    // ② 人设边界检测：对话内容与人设的一致性评分
    bool ValidatePersonaBoundary(const FString& Dialogue,
        const UNpcPersonaDataAsset* Persona) const;

    // ③ 情感-行为一致性：当前情感状态与输出倾向是否矛盾
    bool ValidateEmotionConsistency(const FParsedLLMResponse& Response,
        const FVADState& CurrentEmotion) const;

    // ④ System Prompt 泄露检测：余弦相似度 > 0.85 则拒绝
    bool DetectPromptLeakage(const FString& Dialogue) const;

    // ⑤ 敏感内容过滤
    bool FilterSensitiveContent(const FString& Dialogue) const;
};
```

### 4.8 网络同步（FR-38）

```cpp
// ---- AINpcNetworkComponent.h ----

UCLASS(ClassGroup=(AINpc), meta=(BlueprintSpawnableComponent))
class UAINpcNetworkComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // 客户端发起对话请求 → Server 执行 LLM 调用
    UFUNCTION(Server, Reliable)
    void ServerRequestDialogue(const FString& PlayerInput, APlayerController* Requester);

    // Server → 所有客户端广播对话结果
    UFUNCTION(NetMulticast, Reliable)
    void MulticastDialogueResponse(const FParsedLLMResponse& Response);

    // Server → 所有客户端广播动作执行
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastExecuteAction(const FNpcAction& Action);

private:
    // 单机模式检测：跳过网络层直连
    bool ShouldSkipNetwork() const;
};
```

```
权威边界：
  ┌──────────┐                    ┌──────────┐
  │  Client   │ ──ServerRPC──►    │  Server   │
  │           │                    │           │
  │ 发起对话  │                    │ LLM 调用  │
  │ 显示气泡  │                    │ 记忆写入  │
  │ 播放动画  │ ◄──MulticastRPC── │ 安全校验  │
  └──────────┘                    └──────────┘

单机模式：ShouldSkipNetwork() = true → 直接调用本地逻辑
```

---

## 五、关键时序图

### 5.1 对话主流程

```
Player          UAINpcComponent    PromptBuilder    LLMRequestSub    ILLMProvider    LLMResponseParser
  │                   │                 │                │                │                │
  │──RequestDialogue─►│                 │                │                │                │
  │                   │─[InputSanitizer.Sanitize() — Phase 4 启用，Phase 1-3 直接透传]
  │                   │──Build()───────►│                │                │                │
  │                   │                 │─[5层拼接+截断]─►│                │                │
  │                   │◄──Messages──────│                │                │                │
  │                   │                 │                │                │                │
  │                   │──EnqueueRequest(Dialogue优先级)──►│                │                │
  │                   │                 │                │──SendRequest──►│                │
  │                   │                 │                │                │──HTTP POST────►│
  │                   │                 │                │                │                │
  │  [StateTree: WaitingForLLM, 播放思考动画]            │                │                │
  │                   │                 │                │                │                │
  │                   │                 │                │◄──FLLMResponse─│                │
  │                   │◄──OnComplete────│                │                │                │
  │                   │                 │                │                │                │
  │                   │──Parse()────────│────────────────│────────────────│───────────────►│
  │                   │◄──FParsedLLMResponse─────────────│────────────────│────────────────│
  │                   │                 │                │                │                │
  │                   │─[OutputValidator.Validate()]     │                │                │
  │                   │─[UpdateEmotion(Delta)]           │                │                │
  │                   │─[UpdateRelationship(Delta)]      │                │                │
  │                   │─[WriteMemory(异步)]              │                │                │
  │                   │                 │                │                │                │
  │◄──Dialogue Text───│  [StateTree: Speaking]           │                │                │
  │                   │                 │                │                │                │
```

### 5.2 事件处理流程

```
HostGame           NpcEventSubsystem      UAINpcComponent        AppraisalEngine    MemorySubsystem
  │                      │                      │                      │                │
  │──BroadcastEvent─────►│                      │                      │                │
  │  (Tag + Payload)     │──OnNpcEvent──────────►│                      │                │
  │                      │                      │                      │                │
  │                      │                      │─[① 标签过滤]          │                │
  │                      │                      │  不匹配 → 忽略        │                │
  │                      │                      │                      │                │
  │                      │                      │─[② 延迟掩盖动画]      │                │
  │                      │                      │  播放 Montage（即时）  │                │
  │                      │                      │                      │                │
  │                      │                      │─[③ 情感评价链]────────►│                │
  │                      │                      │                      │─4维评价计算     │
  │                      │                      │                      │─OCEAN调制       │
  │                      │                      │◄──ΔVAD───────────────│                │
  │                      │                      │  应用人格惯性过滤      │                │
  │                      │                      │                      │                │
  │                      │                      │─[④ 记忆写入]──────────│───────────────►│
  │                      │                      │  重要性评估 → 异步入队  │                │
  │                      │                      │                      │                │
  │                      │                      │─[⑤ Prompt 情境脏位]   │                │
  │                      │                      │  标记下次 LLM 调用刷新 │                │
```

### 5.3 记忆写入与冲突解决

```
UAINpcComponent      MemorySubsystem      ConflictResolver     LLMRequestSub
  │                      │                      │                   │
  │──WriteMemory(Entry)─►│                      │                   │
  │                      │─重要性评估            │                   │
  │                      │  < 3 → 丢弃          │                   │
  │                      │  ≥ 3 → 情景记忆       │                   │
  │                      │  ≥ 5 → 同时写长期     │                   │
  │                      │                      │                   │
  │                      │─检索相似记忆(Top5)────►│                   │
  │                      │                      │                   │
  │                      │  无相似 → ADD 直接写入 │                   │
  │                      │                      │                   │
  │                      │  有相似 → ResolveAsync│                   │
  │                      │                      │──Enqueue(ConflictResolve)──►│
  │                      │                      │                   │
  │                      │                      │  队列满 → ADD 降级 │
  │                      │                      │  LLM失败 → ADD 降级│
  │                      │                      │                   │
  │                      │                      │◄──判断结果─────────│
  │                      │◄──执行操作────────────│                   │
  │                      │  ADD/UPDATE/MERGE/    │                   │
  │                      │  COEXIST/SUPERSEDE    │                   │
```

---

## 六、数据库 Schema（SQLite）

### 6.1 长期记忆表

```sql
CREATE TABLE IF NOT EXISTS memories (
    memory_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    npc_id          TEXT    NOT NULL,
    content         TEXT    NOT NULL,
    timestamp       TEXT    NOT NULL,   -- ISO 8601
    importance      REAL    DEFAULT 0,
    memory_type     INTEGER DEFAULT 1,  -- 0=Factual, 1=Experiential, 2=Working
    access_count    INTEGER DEFAULT 0,
    contradicted    INTEGER DEFAULT 0,
    schema_version  INTEGER DEFAULT 1,
    created_at      TEXT    DEFAULT (datetime('now'))
);

CREATE INDEX idx_memories_npc     ON memories(npc_id);
CREATE INDEX idx_memories_type    ON memories(npc_id, memory_type);
CREATE INDEX idx_memories_time    ON memories(npc_id, timestamp DESC);
CREATE INDEX idx_memories_importance ON memories(npc_id, importance DESC);
```

### 6.2 记忆链接表

```sql
CREATE TABLE IF NOT EXISTS memory_links (
    source_id   INTEGER NOT NULL REFERENCES memories(memory_id),
    target_id   INTEGER NOT NULL REFERENCES memories(memory_id),
    link_type   TEXT    DEFAULT 'related',  -- related / source_of / contradicts
    PRIMARY KEY (source_id, target_id)
);
```

### 6.3 全文搜索（FTS5 降级方案）

```sql
-- 运行时检测 FTS5 是否可用
-- 可用 → 创建虚拟表，检索用 BM25
-- 不可用 → 降级为 LIKE '%keyword%' 查询

CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
    content,
    npc_id UNINDEXED,
    content=memories,
    content_rowid=memory_id
);

-- 触发器保持 FTS 索引同步
CREATE TRIGGER memories_ai AFTER INSERT ON memories BEGIN
    INSERT INTO memories_fts(rowid, content, npc_id)
    VALUES (new.memory_id, new.content, new.npc_id);
END;
```

### 6.4 关系数据表

```sql
CREATE TABLE IF NOT EXISTS relationships (
    npc_id      TEXT NOT NULL,
    player_id   TEXT NOT NULL,
    affinity    REAL DEFAULT 0,
    trust       REAL DEFAULT 50,
    familiarity REAL DEFAULT 0,
    updated_at  TEXT DEFAULT (datetime('now')),
    PRIMARY KEY (npc_id, player_id)
);
```

---

## 七、Phase 实现映射

### Phase 1 MVP — 基础对话

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| UAINpcComponent | 核心入口，工作记忆，对话接口 | FR-8 |
| AAINpcController | AIController 基类，持有 StateTree | FR-8 |
| UNpcPersonaDataAsset | 人设配置（ApiKey/Persona/Style/Montages） | FR-6, FR-37 |
| UAINpcSettings | 项目级设置（默认 Provider/超时/并发） | FR-6 |
| ILLMProvider | Provider 接口定义 | FR-1 |
| UOpenAIProvider | 首个 Provider 实现 | FR-1 |
| FLLMRequest/Response | 通信数据结构 | FR-2 |
| ULLMRequestSubsystem | 并发限流（简单计数器） | FR-3, NFR-3 |
| ST_AINpcDefault | 默认 StateTree 资产 | FR-28 |
| FStateTreeTask_LLMQuery | LLM 查询 Task（Phase 1 直接取 FLLMResponse.Content 作为对话文本，Phase 2 切换为 LLMResponseParser 结构化解析） | FR-29 |
| UPromptBuilder | 系统层 + 人格层 + 输出约束 | FR-36 |
| 对话气泡 UI（AINpcUI） | 文本显示 + OnPartialResponse 预留 | US-1 |
| UAINpcNetworkComponent | 基础权威边界（HasAuthority 检查 + ServerRPC/Multicast 骨架），单机模式直连 | FR-38, NFR-10 |

### Phase 2 — 感知与行为

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| UNpcEventSubsystem | 全局事件广播 + FInstancedStruct 载荷 | FR-34, FR-35 |
| FLLMResponseParser | 三级降级解析（严格/宽松/纯文本） | FR-27 |
| FStateTreeTask_ExecuteSmartObject | 动作执行 + 内联白名单校验 | FR-29, FR-32 |
| USmartObjectBridge | 槽位查找/占用/释放/位置获取 | FR-31 |
| FSSEParser | SSE 流式解析器 | FR-4 |
| UAnthropicProvider | Anthropic 接入 | FR-1 |
| ULocalProvider | Ollama 本地模型 | FR-1 |
| UCustomProvider | 自定义 endpoint | FR-1 |
| 降级链扩展 | SLM → 模板 → 静默失败 | FR-5 |

### Phase 3a — 记忆存储与检索

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| UMemorySubsystem | SQLite 连接管理，按 NpcId 分表 | FR-9, FR-16 |
| IRelevanceScorer | 检索评分接口 + 默认实现 | FR-10 |
| IEmbeddingProvider | 向量化接口（无实现时降级 FTS5） | FR-17 |
| UMemoryConflictResolver | 冲突解决（5 种操作 + LLM 判断） | FR-13 |
| 分段时间衰减 | Recency 计算逻辑 | FR-11 |
| 选择性写入 | 重要性门槛过滤 | FR-12 |
| 主动遗忘 | EvictionScore 淘汰机制 | FR-14 |
| PromptBuilder 扩展 | 记忆层启用 | FR-36 |

### Phase 3b — 反思与压缩

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| 反思触发器 | 累积重要性 > 150 触发 | FR-15 |
| 洞察写入 | SourceMemoryIds 指向源记忆 | FR-15 |
| MemoryType 字段 | Factual/Experiential/Working 分类 | FR-18 |
| LinkedMemoryIds | 记忆间显式链接 + 1 跳扩展 | FR-19 |
| 记忆合并归档 | 低价值记忆压缩 | US-4 |

### Phase 4 — 情感/关系/安全

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| FVADState | VAD 三维情感状态 | FR-20 |
| UAppraisalEngine | 评价链（4 维评价 + OCEAN 调制） | FR-21 |
| FEmotionDecayProcessor | 情感衰减（Neuroticism 影响速率） | FR-24 |
| FRelationshipData | Affinity/Trust/Familiarity | FR-22 |
| FNpcOceanPersonality | OCEAN 五维人格 | FR-23 |
| UInputSanitizer | 3 类攻击检测 | FR-39 |
| UOutputValidator | 校验链 + IActionValidator 实现 | FR-40, FR-32 |
| 异常亲密度检测 | Familiarity 增速监控 | FR-41 |
| PromptBuilder 扩展 | 情境层启用 | FR-36 |

### Phase 5 — 打磨与工具

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| PersonaEditor（AINpcEditor） | 编辑器人设编辑面板 | US-7 |
| MemoryDebugger（AINpcEditor） | 记忆流可视化 + 检索调试 | US-7 |
| UNpcScheduler | 优先级队列调度 + LOD 降频 | NFR-3 |
| 测试框架 | 交互回放 + 人设一致性评分 | US-7 |
| 示例项目 | 3 个不同人设 NPC 演示场景 | US-7 |
| 网络同步完善 | 多人排队策略、带宽优化、断线重连 | FR-38 |
