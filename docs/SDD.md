# AINpc 插件系统设计文档（SDD）

> 来源：docs/PRD.md v1.4
> 版本：1.7
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
> 最低引擎版本：UE5.4+（NFR-7，StateTree WeakExecutionContext 依赖），`.uplugin` 中设置 `"EngineVersion": "5.4.0"`。

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
 └── 持有 SQLite 连接，单表按 NpcId 索引管理长期记忆

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
│   │   │   ├── StateTreeTask_LLMQuery.h        // FStateTreeTask_LLMQuery
│   │   │   └── StateTreeTask_ExecuteSmartObject.h
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

    UPROPERTY(BlueprintReadOnly)
    FGameplayTagContainer EmotionTags; // 由评价链根据 VAD 映射生成（如 Emotion.Angry）
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

// LLM 返回的情感变化量（默认值全 0，区别于 FVADState 的状态值，与 FRelationshipDelta 对称设计）
USTRUCT(BlueprintType)
struct FVADDelta
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    float Valence = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float Arousal = 0.f;

    UPROPERTY(BlueprintReadWrite)
    float Dominance = 0.f;    // 注意：默认 0 而非 FVADState 的 0.5
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

    UPROPERTY(EditAnywhere, Category="LLM")
    TArray<FString> FallbackResponses;  // 降级模板响应池（FR-5），随机选取

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
    FVADDelta EmotionDelta;

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

// UAINpcComponent 蓝图委托（FR-7）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueReceived, const FString&, Dialogue, const TArray<FNpcAction>&, Actions);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEmotionChanged, FVADState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRelationshipChanged, const FString&, PlayerId, FRelationshipData, NewData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMFallback, const FString&, FallbackText);

// ---- UAINpcComponent 蓝图公共 API（NFR-8）----
// 四个核心流程的蓝图入口，确保纯蓝图可完成全流程

UCLASS(ClassGroup=(AINpc), meta=(BlueprintSpawnableComponent))
class UAINpcComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // ---- 蓝图可调用方法 ----

    // 发起对话（核心流程②）
    UFUNCTION(BlueprintCallable, Category="AINpc|Dialogue")
    void RequestDialogue(const FString& PlayerInput);

    // 查询关系数据（核心流程④）
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AINpc|Relationship")
    FRelationshipData GetRelationship(const FString& PlayerId) const;

    // 获取当前情感状态
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="AINpc|Emotion")
    FVADState GetCurrentEmotion() const;

    // ---- 蓝图可绑定委托（核心流程③：监听响应）----

    UPROPERTY(BlueprintAssignable, Category="AINpc|Dialogue")
    FOnDialogueReceived OnDialogueReceived;

    UPROPERTY(BlueprintAssignable, Category="AINpc|Emotion")
    FOnEmotionChanged OnEmotionChanged;

    UPROPERTY(BlueprintAssignable, Category="AINpc|Relationship")
    FOnRelationshipChanged OnRelationshipChanged;

    UPROPERTY(BlueprintAssignable, Category="AINpc|Dialogue")
    FOnLLMFallback OnLLMFallback;

    // ---- 配置（核心流程①：配置 Key）----

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AINpc|Config")
    UNpcPersonaDataAsset* PersonaData;
};
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
    // 同时播放延迟掩盖动画：NpcPersonaDataAsset::DelayMaskingMontages[EDelayStrategy::Thinking]
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

| 步骤 | 消费者 | 同步/异步 | 说明 | Phase |
|------|--------|-----------|------|-------|
| ① | 延迟掩盖动画（FR-33） | 同步即时 | 受击等不等 StateTree Tick | 2 |
| ② | 情感评价链（FR-21） | 同步计算 | 本地规则，零 LLM 调用（Phase 2-3 跳过此步） | 4 |
| ③ | 记忆写入（FR-12） | 异步入队 | 重要性评估后决定是否写入 | 3a |
| ④ | Prompt 情境更新（FR-36） | 标记脏位 | 下次 LLM 调用时生效 | 2 |

### 4.4 记忆系统

#### 4.4.1 三层架构（FR-9）

```
┌─────────────────────────────────────────┐
│         工作记忆（Working Memory）        │
│  TArray<FLLMMessage>, ~20 条            │
│  宿主: UAINpcComponent                   │
│  生命周期: 对话 Session                   │
│  用途: LLM 上下文窗口                     │
│  Session 定义：                           │
│    开始 = 首次 RequestDialogue 调用       │
│    结束 = 超时 5min 无交互 / 玩家离开     │
│           感知范围 / 显式 EndDialogue()   │
│    结束时：摘要写入情景记忆（Phase 3a+）   │
│    多玩家：每 PlayerId 独立 Session       │
├─────────────────────────────────────────┤
│         情景记忆（Episodic Memory）       │
│  TArray<FMemoryEntry>, ~200 条上限       │
│  宿主: UAINpcComponent                   │
│  生命周期: NPC 实例（关卡切换时持久化）     │
│  写入门槛: Importance ≥ 3                │
│  持久化: 共用 memories 表（memory_type=1） │
│  加载: BeginPlay 时按 NpcId+memory_type  │
│        从 SQLite 加载回 TArray            │
│  写回: EndPlay + 定时刷盘（同关系数据） │
├─────────────────────────────────────────┤
│         长期记忆（Long-term Memory）      │
│  SQLite, 无上限                          │
│  宿主: UMemorySubsystem（全局单 DB）      │
│  生命周期: 跨关卡持久化                    │
│  写入门槛: Importance ≥ 5                │
└─────────────────────────────────────────┘
```

#### 4.4.2 记忆写入转换（FR-12）

两条写入路径的 FMemoryEntry 构建规则：

```
路径 A：对话后写入（UAINpcComponent::WriteDialogueMemory）
  │
  ├─ Content = "玩家说：{PlayerInput}，NPC回复：{Dialogue}"
  ├─ MemoryType = Experiential
  ├─ Importance = |EmotionDelta.Valence| × 10（情感变化越大越重要）
  │   Clamp[0, 10]，最低保底 = 2（确保对话至少有基础记录）
  └─ Timestamp = FDateTime::Now()

路径 B：事件触发写入（UAINpcComponent 事件消费步骤③）
  │
  ├─ Content = 事件描述（由 FInstancedStruct 载荷的 ToString() 或蓝图辅助函数格式化）
  ├─ MemoryType = Experiential
  ├─ Importance：
  │   Phase 4+：|FinalGoalRelevance| × 10（评价链步骤②的输出直接喂入）
  │   Phase 3a（评价链未启用）：按事件标签预设映射（攻击=7, 赠礼=5, 对话=3, 其他=4）
  │   Clamp[0, 10]
  └─ Timestamp = FDateTime::Now()

反思产出的洞察：
  ├─ MemoryType = Factual
  ├─ Importance = 8（洞察默认高重要性）
  └─ SourceMemoryIds = 源记忆 ID 列表
```

#### 4.4.3 检索算法（FR-10/11）

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

#### 4.4.4 记忆冲突解决（FR-13）

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

#### 4.4.5 反思机制（FR-15）

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

#### 4.4.6 主动遗忘（FR-14）

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

> **设计决策：FVADState 不持久化**。情感状态为纯运行时数据，NPC 重生/关卡切换后重置为默认值（Valence=0, Arousal=0, Dominance=0.5）。理由：情感是短期状态，自然衰减机制已保证趋向基线；长期情感印象通过记忆系统间接体现（如"上次被攻击"的记忆会在下次对话时通过 Prompt 影响 LLM 输出）。

#### 4.5.3 关系更新（FR-22）

```
LLM 响应解析后得到 FRelationshipDelta
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

关系数据存储与生命周期：
  TMap<FString /*PlayerId*/, FRelationshipData> Relationships;
  独立持久化到 SQLite relationships 表（6.4 节）

  生命周期：
  ├─ 加载（BeginPlay）：UAINpcComponent 向 UMemorySubsystem 注册，
  │   UMemorySubsystem 按 NpcId 从 relationships 表加载已有数据填充 TMap
  ├─ 运行时：LLM 响应 Delta 更新 + Tick 自然衰减
  ├─ 写回（EndPlay）：UAINpcComponent 注销时将 TMap 回写 UMemorySubsystem
  └─ 批量刷盘（三种触发源，任一即写入）：
       ├─ 关卡切换/手动存档（单机/关卡制游戏）
       ├─ 定时自动刷盘（默认 300s，UAINpcSettings 可配置，常驻世界网游依赖此路径）
       └─ Dirty 标记优化：仅写入自上次刷盘后有变更的 NPC，避免无效 IO
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
│  Phase 1：纯自然语言回复 + 语言约束         │
│  Phase 2+：JSON Schema + 动作格式要求       │
└─────────────────────────────────────────┘

Token 超限截断优先级（从低到高）：
  情境层 → 记忆层 → 人格层
  系统层和输出约束层永不截断

情境层 Prompt 注入格式示例（FR-25）：
  "[当前状态]
   情绪：愉悦度 0.6，激活度 0.3，支配感 0.7（整体偏积极放松）
   与对方关系：好感度 45/100，信任度 70/100，熟悉度 30/100
   周围可交互对象：chair_01(坐下), bookshelf_02(查看), door_03(开门)"
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
    // 注意：System 和 Output 层硬编码忽略覆盖，即使 TMap 中存在也不生效
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

    // 渐进式信任计数器（社会工程检测用）
    // Key = PlayerId（按攻击者维度追踪，同一玩家对不同 NPC 的试探累计计数）
    // 衰减：每 10min 无新触发 → 计数器 -1，最低归零
    // 阈值：计数器 ≥ 3 → ThreatScore 提升，≥ 5 → 直接拒绝
    TMap<FString /*PlayerId*/, int32> TrustEscalationCounters;
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

> **Phase 依赖说明**：`FParsedLLMResponse` 结构体在 Phase 1 即定义（仅数据容器），Phase 1 手动填充 `Dialogue` 字段；Phase 2 引入 `LLMResponseParser` 后自动填充全部字段。

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

### 4.9 沉浸感增强（Phase 6，FR-42/43/44/45/46）

#### 4.9.1 自主行为循环（FR-42）

NPC 非对话时不再 Idle 站桩，而是根据日程表自主执行日常行为。

```cpp
// ---- NpcScheduleDataAsset.h ----

USTRUCT(BlueprintType)
struct FScheduleEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StartHour;    // 游戏内小时 [0, 24)

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EndHour;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag BehaviorTag;   // "Schedule.Work" / "Schedule.Patrol" / "Schedule.Rest"

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag SmartObjectTag; // 可选，指定交互目标类型
};

UCLASS(BlueprintType)
class UNpcScheduleDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FScheduleEntry> DailySchedule;

    // 日程模糊度 [0, 1]：实际执行时间 = 计划时间 ± Fuzziness × 随机偏移
    // 0 = 严格按时间执行，1 = 最大偏移（±30分钟游戏时间）
    // 设计理由：纯静态时间槽会让 NPC 行为机械可预测，适度随机偏移增加"活人"感
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "1"))
    float ScheduleFuzziness = 0.3f;

    // 根据当前游戏时间返回应执行的行为标签
    FGameplayTag GetCurrentBehavior(float GameHour) const;
};
```

**StateTree 集成方式：**

```
ST_AINpcDefault（扩展后）:
  Root
  ├── [高优先级] DialogueSubtree     ← 玩家发起对话时激活（已有）
  ├── [高优先级] ReactiveSubtree     ← 被攻击/收礼等事件（已有）
  ├── [中优先级] ProactiveSubtree    ← 主动交互（4.9.2，Phase 6 新增）
  └── [低优先级] ScheduleSubtree     ← 日程行为（Phase 6 新增）
       ├── FStateTreeTask_ScheduleRouter  ← 读取日程表，输出当前 BehaviorTag
       ├── [BehaviorTag == Schedule.Work]  → FindSlot + ClaimSlot + UseSlot
       ├── [BehaviorTag == Schedule.Patrol] → MoveTo 巡逻点序列
       └── [BehaviorTag == Schedule.Rest]  → FindSlot(Chair/Bed) + UseSlot
```

- 高优先级子树激活时自动打断日程行为（StateTree 原生优先级机制）
- 事件结束后 StateTree 自动回落到 ScheduleSubtree 恢复日程
- 无日程表配置时降级为原有 Idle 行为，零破坏性

**日程中断恢复策略：**

- 中断结束后，ScheduleRouter 重新读取当前游戏时间，跳到对应时段（而非从中断点继续）
- 若当前时间已超过被中断时段的 EndHour，直接进入下一时段
- 设计理由：玩家与 NPC 对话 2 小时游戏时间后，NPC 不应"补做"已过去的工作时段，而应自然过渡到当前应做的事

#### 4.9.2 主动交互触发（FR-43）

NPC 不再只被动等待玩家，而是基于内部状态主动发起交互。

```cpp
// ---- ProactiveInteractionEvaluator.h ----

USTRUCT(BlueprintType)
struct FProactiveCondition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    FGameplayTag TriggerTag;    // "Proactive.Greet" / "Proactive.Warn" / "Proactive.ShareInsight"

    UPROPERTY(EditAnywhere)
    float AffinityThreshold = 60.f;     // 好感度门槛（0=不检查）

    UPROPERTY(EditAnywhere)
    float ArousalThreshold = 0.8f;      // 情感激动度门槛（0=不检查）

    UPROPERTY(EditAnywhere)
    bool bRequireUnsharedInsight = false; // 是否需要持有未分享的洞察

    UPROPERTY(EditAnywhere)
    float CooldownSeconds = 300.f;       // 同类触发冷却时间
};
```

**全局频率控制：**

```cpp
// UAINpcComponent 中的全局主动交互限制
UPROPERTY(EditAnywhere, Category = "AINpc|Proactive")
int32 MaxProactivePerInterval = 1;       // 每个时间窗口最多主动交互次数

UPROPERTY(EditAnywhere, Category = "AINpc|Proactive")
float ProactiveIntervalSeconds = 300.f;  // 时间窗口长度（默认5分钟）
```

> 设计理由：单条件冷却只防止同一条件反复触发，但多条件可能在短时间内依次命中。全局上限防止 NPC 变成"话痨"。Proactive Dialogue 评测论文（2508.20973）核心发现：过度主动比不够主动更令人反感。

**评估流程（定时轮询，默认 30s 实时时间）：**

> 时间基准说明：主动交互的所有时间参数（轮询间隔、冷却、频率窗口）均使用**实时时间**，不受游戏时间缩放影响。理由：主动交互面向玩家体验，玩家感知的是实时时间；若使用游戏时间，时间加速场景（如日程系统 1秒=1小时）会导致 NPC 在玩家视角疯狂触发主动交互。

```
FStateTreeEvaluator_ProactiveCheck（StateTree Evaluator，每 30s 实时时间 Tick 一次）
  │
  ├─ 前置检查：
  │   ├─ 全局频率上限：本窗口内已触发次数 < MaxProactivePerInterval
  │   └─ 玩家状态：非战斗中（bPlayerInCombat）且非对话中（bPlayerInDialogue）
  │
  ├─ 检查玩家是否在感知范围内（AIPerception 或距离检测）
  ├─ 遍历 NpcPersonaDataAsset 中配置的 TArray<FProactiveCondition>
  │   ├─ 检查 Affinity >= AffinityThreshold
  │   ├─ 检查 Arousal >= ArousalThreshold
  │   ├─ 检查 bRequireUnsharedInsight → 查询记忆系统是否有未标记"已分享"的洞察
  │   └─ 检查冷却计时器
  │
  ├─ 命中 → 向 NpcEventSubsystem 广播 ProactiveInteraction 事件
  │         StateTree ProactiveSubtree 消费：
  │         ├─ Greet → 转向玩家 + 播放招手动画 + 发起 LLM 对话（Prompt 注入触发原因）
  │         ├─ Warn → 移动到玩家附近 + 播放警告动画 + 发起对话
  │         └─ ShareInsight → 发起对话（Prompt 注入洞察内容）
  │
  └─ 未命中 → 继续日程行为
```

- 主动交互优先级高于日程、低于玩家主动对话
- 冷却机制防止 NPC 反复骚扰玩家
- 未配置任何 FProactiveCondition 时此功能静默关闭

#### 4.9.3 NPC 间社交协议（FR-44）

NPC↔NPC 对话通过轻量路径驱动，避免占用玩家对话的 LLM 配额。

**对话驱动策略（按优先级降级）：**

| 策略 | 条件 | 延迟 | Token 消耗 |
|------|------|------|-----------|
| LocalProvider 小模型 | 已配置 LocalProvider | ~200ms | 本地，零成本 |
| 云端 LLM（低优先级） | 无 LocalProvider，云端可用 | ~1-3s | 计入 NFR-3 配额 |
| 预设对话模板 | 以上均不可用 | ~0ms | 零 |

**对话模板结构：**

```cpp
USTRUCT(BlueprintType)
struct FNpcSocialTemplate
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    FGameplayTag SocialContext;    // "Social.Greeting" / "Social.Gossip" / "Social.Trade"

    UPROPERTY(EditAnywhere)
    TArray<FText> InitiatorLines;  // 发起方台词池（随机选取，FText 支持本地化）

    UPROPERTY(EditAnywhere)
    TArray<FText> ResponderLines;  // 回应方台词池
};
```

**社交流程：**

```
NPC_A 的 ScheduleSubtree 进入 "Schedule.Socialize" 时段
  │
  ├─ NpcScheduler 检查：当前社交 NPC 数量 < MaxConcurrentSocial（默认 2 对，全局限制，由 UNpcSchedulerSubsystem 管控）
  ├─ 选择社交对象：SocialSearchRadius（默认 2000cm/20米）内 NPC 中 Familiarity 最高者
  │
  ├─ 发起对话（按策略降级）
  │   ├─ LLM 路径：精简 Prompt（仅人格层+关系数值+社交上下文，不含完整记忆层）
  │   └─ 模板路径：从 FNpcSocialTemplate 池随机选取
  │
  ├─ 对话结果处理
  │   ├─ 双方记忆系统写入（异步，重要性默认 3-5）
  │   ├─ 双方关系数值微调（Familiarity +1~3）
  │   └─ 通过 NpcEventSubsystem 广播 NpcSocialEvent（玩家可旁观）
  │
  └─ 社交频率受 NpcScheduler 管控，优先级低于玩家对话
```

- 社交对话的 LLM 请求优先级为 `ESocialLow`，队列满时直接降级为模板
- 玩家发起对话时立即打断社交，NPC 转入对话状态
- 社交结果广播后，UI 层可选择性显示旁观气泡（默认不显示，蓝图可开启）

**Persona Drift 防护：**

研究表明 LLM 在 8 轮对话内就会出现显著人格漂移（2402.10962），NPC↔NPC 社交如果使用 LLM 路径，长期运行后两个 NPC 的人格可能互相"污染"。

- LLM 路径社交对话最大轮数限制：2-3 轮（不含模板路径）
- 每次 LLM 社交 Prompt 必须重新注入完整人格层（不依赖上下文延续）
- 社交对话结果经过轻量 OutputValidator 校验（至少检查人设边界词汇）

**社交信息传播：**

- 社交记忆增加 `bShareableWithPlayer` 标志（默认 true）
- 后续与玩家对话时，PromptBuilder 可注入"我听说..."类型的社交记忆
- 实现 NPC 之间的"口碑传播"效果，增强世界活性感知

**社交对象选择优化：**

- 70% 概率选择 Familiarity 最高的附近 NPC
- 30% 概率随机选择附近 NPC（引入新关系建立机会）
- 避免固定社交圈导致的行为单调

#### 4.9.4 流式首 Token 优化（FR-45）

基于 Phase 2 已有的 SSE 流式能力，优化首个可见字符的到达时间。

**关键修改点：SSE Parser 增加首 Token 委托**

```cpp
// FSSEParser 新增委托（Phase 2 已有 OnPartialResponse，此处增加更早的触发点）
DECLARE_DELEGATE_OneParam(FOnFirstTokenReceived, const FString& /* FirstChunk */);

// SSE Parser 处理流程
void FSSEParser::ProcessChunk(const TArray<uint8>& Data)
{
    // ... 现有 SSE 解析逻辑（data: 前缀剥离、跨包拼接）...

    if (!bFirstTokenFired && ParsedContent.Len() > 0)
    {
        bFirstTokenFired = true;
        OnFirstTokenReceived.ExecuteIfBound(ParsedContent);
        // UAINpcComponent 收到后立即触发 StateTree 进入 Speaking 状态
        // 对话气泡开始显示，无需等待完整响应
    }

    OnPartialResponse.ExecuteIfBound(ParsedContent);  // 后续 chunk 继续走已有路径
}
```

**时序优化对比：**

```
非流式（Phase 1）：
  请求发出 ──────────── 完整响应到达 ──── 显示
  |<────── 2-4s 空白等待 ────────>|

流式（Phase 2）：
  请求发出 ── 首chunk到达 ── 后续chunk ── 完成
  |<── ~500ms ──>|<── 逐字显示 ──>|

首Token优化（Phase 6）：
  请求发出 ── 首Token ── Speaking状态 ── 逐字显示 ── 完成
  |<─ <500ms ─>|  ← OnFirstTokenReceived 触发
                   StateTree 立即切换，气泡立即出现
```

- 目标：首 Token 延迟 P50 < 500ms（网络延迟 + Provider 首包时间）
- 非流式模式下此优化不生效，行为与 Phase 1 一致
- `OnFirstTokenReceived` 为蓝图可绑定委托，项目方可自定义首 Token 响应行为

**Prompt Caching 策略：**

NPC 的 Prompt 结构天然适合 prefix caching——系统层+人格层在多次对话间高度稳定，记忆层+情境层是动态的。

```
Prompt 结构（缓存友好排列）：
┌─────────────────────────────┐
│ [系统层] 角色设定、禁止规则  │ ← 稳定前缀（可缓存）
│ [人格层] OCEAN数值、说话风格  │ ← 稳定前缀（可缓存）
│ [输出约束] 格式要求          │ ← 稳定前缀（同Phase内不变）
├─────────────────────────────┤
│ [记忆层] 相关记忆注入        │ ← 动态后缀（每次不同）
│ [情境层] 当前场景、对话历史  │ ← 动态后缀（每次不同）
└─────────────────────────────┘
```

- PromptBuilder 确保稳定内容始终在 Prompt 前部，动态内容在后部
- 利用 OpenAI/Anthropic 的自动 prefix caching（无需额外 API 调用）
- 预期收益：TTFT 降低 13-31%，token 成本降低 45-80%（参考 2601.06007 评测）
- 延迟目标细化：本地模型 P50 < 200ms，云端 API P50 < 500ms

**连接预热（可选增强）：**

- 玩家进入 NPC 感知范围时，预先建立 HTTP 连接（TCP/TLS 握手）
- 减少首次请求的连接建立延迟（约 50-150ms）
- 实现：在 AIPerception 的 OnTargetPerceptionUpdated 中触发预连接
- `PrewarmConnectionTimeout: float = 30.f`：空闲连接超时释放（秒），玩家离开感知范围后计时，超时则关闭连接释放服务端资源

#### 4.9.5 情感外化接口（FR-46）

将 VAD 情感状态映射为动画蓝图可消费的参数结构，插件只提供数据接口，不提供动画资产。

**数据结构：**

```cpp
USTRUCT(BlueprintType)
struct AINPCRUNTIME_API FEmotionAnimParams
{
    GENERATED_BODY()

    // VAD 归一化值（直接透传，动画蓝图可直接用作 BlendSpace 参数）
    UPROPERTY(BlueprintReadOnly)
    float Valence = 0.f;     // [-1, 1]

    UPROPERTY(BlueprintReadOnly)
    float Arousal = 0.f;     // [0, 1]

    UPROPERTY(BlueprintReadOnly)
    float Dominance = 0.5f;  // [0, 1]

    // 主情感标签（ActiveEmotions 中 Arousal 最高的一个）
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag PrimaryEmotion;

    // 情感强度，用于控制表情幅度
    // 公式：Arousal × max(|Valence|, 0.3)
    // 注：|Valence| 设 0.3 下限，确保高 Arousal 低 |Valence| 状态（如惊讶）仍有足够表情幅度
    UPROPERTY(BlueprintReadOnly)
    float Intensity = 0.f;
};
```

**UAINpcComponent 接口：**

```cpp
// 蓝图可调用，返回经过 Lerp 平滑后的情感参数（非瞬时值）
UFUNCTION(BlueprintCallable, Category = "AINpc|Emotion")
FEmotionAnimParams GetEmotionAnimParams() const;

// 情感变化时广播（蓝图可绑定，避免轮询）
UPROPERTY(BlueprintAssignable, Category = "AINpc|Emotion")
FOnEmotionAnimParamsChanged OnEmotionAnimParamsChanged;
```

**情感过渡平滑策略：**

- `GetEmotionAnimParams()` 返回的是经过 `FMath::FInterpTo` 平滑后的值，而非情感系统的瞬时原始值
- `EmotionLerpSpeed: float = 2.0f`（可配置）：控制情感插值速率，值越大过渡越快
- `MinEmotionDuration: float = 2.0f`（可配置）：情感状态最小持续时间，防止表情闪烁
- 多人模式同步策略：VAD 任一维度变化 > 0.1 时才广播 `OnEmotionAnimParamsChanged`，减少网络流量
- `MinEmotionBroadcastInterval: float = 0.5f`：最小广播间隔（秒），即使阈值触发也不会高于 2Hz 广播频率。20 NPC 同时活跃时峰值带宽 ≈ 20×2Hz×28bytes ≈ 1.1KB/s，可接受

**项目方绑定示例（动画蓝图）：**

```
EventGraph:
  OnEmotionAnimParamsChanged → Set Valence/Arousal/Dominance 变量

AnimGraph:
  BlendSpace2D (X=Valence, Y=Arousal) → 表情混合
  Dominance → 姿态权重（高=挺胸抬头，低=缩肩低头）
  Intensity → 动画播放速率缩放（高强度=动作更快/幅度更大）
```

- 插件不提供动画资产，示例项目中提供占位动画演示绑定方式
- 多人模式下 `FEmotionAnimParams` 通过 `UAINpcNetworkComponent` 的 Multicast RPC 同步到客户端
- 未启用情感系统（Phase 1-3）时，`GetEmotionAnimParams()` 返回默认中性值

---

## 五、关键时序图

### 5.1 对话主流程

```
Player          UAINpcComponent    PromptBuilder    LLMRequestSub    ILLMProvider    LLMResponseParser
  │                   │                 │                │                │                │
  │──RequestDialogue─►│                 │                │                │                │
  │                   │─[多人模式：以下全部步骤在 Server 端执行（HasAuthority），见 4.8 节权威边界]
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
  │                   │  ├─ 通过 → 继续                  │                │                │
  │                   │  └─ 拒绝 → 重试 1 次（Prompt 追加约束），仍拒绝 → 降级 FallbackResponses
  │                   │─[UpdateEmotion(Delta)]           │                │                │
  │                   │  └─ Phase 4 启用后生效，Phase 1-3 跳过
  │                   │─[UpdateRelationship(Delta)]      │                │                │
  │                   │  └─ Phase 4 启用后生效，Phase 1-3 跳过
  │                   │─[WriteMemory(异步)]              │                │                │
  │                   │  └─ Phase 3a 启用后生效，Phase 1-2 跳过
  │                   │                 │                │                │                │
  │                   │─[多人模式：Server 通过 MulticastDialogueResponse 广播结果]
  │◄──Dialogue Text───│  [StateTree: Speaking]           │                │                │
  │                   │  [客户端仅负责 UI 显示+动画播放，不执行状态更新]
  │                   │                 │                │                │                │
```

### 5.2 事件处理流程

```
HostGame           NpcEventSubsystem      UAINpcComponent        UAppraisalEngine   UMemorySubsystem
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

> **link_type 与 FMemoryEntry 字段映射**：
> - `related` → `FMemoryEntry::LinkedMemoryIds`（FR-19，检索时沿链接扩展 1 跳）
> - `source_of` → `FMemoryEntry::SourceMemoryIds`（FR-15，反思洞察的证据指针）
> - `contradicts` → `FMemoryEntry::bContradicted = true`（FR-13，COEXIST 标记）
>
> 读取 FMemoryEntry 时从 memory_links 表反查填充对应 TArray 字段。

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

### 6.5 NPC 间关系数据表

NPC 社交（FR-44）需要 NPC↔NPC 的关系数据。独立于 `relationships` 表，避免破坏已有 NPC-玩家关系设计。

```sql
CREATE TABLE IF NOT EXISTS npc_relationships (
    npc_id_a    TEXT NOT NULL,   -- 字母序较小的 NPC ID
    npc_id_b    TEXT NOT NULL,   -- 字母序较大的 NPC ID
    familiarity REAL DEFAULT 0,  -- 熟悉度，社交后 +1~3
    updated_at  TEXT DEFAULT (datetime('now')),
    PRIMARY KEY (npc_id_a, npc_id_b)
);
-- 约束：npc_id_a < npc_id_b，确保同一对 NPC 只有一条记录
-- 查询时：WHERE (npc_id_a = ? AND npc_id_b = ?) OR (npc_id_a = ? AND npc_id_b = ?)
```

> 设计决策：使用字母序约束而非双向存储，避免数据冗余。`UAINpcComponent::GetNpcFamiliarity(FString OtherNpcId)` 内部自动排序后查询。

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
| EmotionTypes.h / FRelationshipDelta | 结构体定义占位（FVADState + FRelationshipDelta），Phase 4 实现行为逻辑 | FR-20 |
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
| UMemorySubsystem | SQLite 连接管理，单表按 NpcId 索引 | FR-9, FR-16 |
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

### Phase 6 — 沉浸感增强

| 类/文件 | 实现内容 | 对应 FR |
|---------|---------|---------|
| UNpcScheduleDataAsset | 日程表配置（时间段→行为标签） | FR-42 |
| FStateTreeTask_ScheduleRouter | 根据游戏时间选择日程行为 | FR-42 |
| UProactiveInteractionEvaluator | 主动交互条件评估（30s 轮询） | FR-43 |
| NPC 间社交管线 | 轻量 LLM/模板对话 + 双方记忆写入 | FR-44 |
| SSE 首 Token 优化 | OnFirstTokenReceived 委托 + UI 即时显示 | FR-45 |
| FEmotionAnimParams | VAD→动画蓝图参数映射 | FR-46 |
