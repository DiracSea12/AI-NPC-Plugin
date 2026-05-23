# Custom Action Extension Architecture

本文说明 AI NPC 插件在“项目自定义动作”方向上的宏观设计目标。

这里的重点不是让插件内置尽可能多的动作，而是让项目方能够低成本、低耦合地把自己的玩法动作接入 NPC 决策链路。真实项目里的动作一定无法穷尽，例如采集、挖矿、换弹、开箱、交易、祈祷、演奏、召唤、修理、驾驶、使用项目专属交互物等，都应该由项目侧定义和执行。

插件的职责是提供稳定的动作扩展协议、上下文注入、合法性校验、分发执行和验证要求。

## Design Goal

项目方新增一个动作时，理想体验应接近以下流程：

1. 新建一个动作定义资产或配置项，例如 `Action.Gather`。
2. 填写给 LLM 使用的动作描述，例如“采集附近指定资源点”。
3. 声明目标类型和参数，例如目标 Actor、资源类型、数量、半径、是否需要寻路。
4. 绑定 Blueprint、C++、StateTree、SmartObject 或 Montage 执行器。
5. 插件在运行时只把当前 NPC 当前上下文允许执行的动作注入 prompt。
6. LLM 只输出动作意图，游戏侧验证后再分发给项目动作执行器。

插件核心不应该为了某个项目硬编码 `Gather`、`Craft`、`Trade`、`Attack` 等玩法语义。

## Core Principle

LLM 只建议，游戏侧裁判。

```text
Action Catalog
    |
    v
Available Action Query
    |
    v
Prompt Injection
    |
    v
LLM Action Intent
    |
    v
Validator / Referee
    |
    v
Dispatcher
    |
    v
Project Action Handler
    |
    v
Visible Runtime Behavior
```

LLM 输出不等于执行命令。任何动作在执行前都必须经过游戏侧验证，包括动作是否注册、目标是否合法、参数是否合法、NPC 当前状态是否允许、是否满足冷却和资源条件。

## Proposed Concepts

### Action Definition

动作定义描述一个项目动作对 NPC 系统的公开契约。

建议字段：

- `ActionId`：稳定动作标识，推荐 GameplayTag，例如 `Action.Gather`。
- `PromptDescription`：给 LLM 看的短描述。
- `TargetPolicy`：目标策略，例如无目标、玩家、Actor、SmartObject、位置、GameplayTag 查询结果。
- `MovementPolicy`：移动策略，例如不需要移动、必须完整路径可达、允许 partial path 到最近可达点、仅朝向目标。
- `ParameterSchema`：动作参数约束，例如数量、半径、资源类型、持续时间。
- `AvailabilityRule`：当前上下文是否允许暴露给 LLM。
- `ValidationRule`：LLM 返回后是否允许执行。
- `ExecutorBinding`：执行器绑定方式。
- `VisualVerificationProfile`：玩家可感知动作的真实可视化验证要求。

### Action Catalog

动作目录是项目侧注册动作的入口。

插件可以提供默认目录，但项目应能通过 Project Settings、DataAsset、Subsystem、Blueprint 或 C++ 扩展注册动作。目录只负责声明能力，不负责写死玩法逻辑。

运行时 NPC 不应看到全项目所有动作，而应查询“当前 NPC 当前上下文可用的动作集合”。

### Action Intent

动作意图是 LLM 返回的结构化结果。

建议结构：

```json
{
  "type": "Action.Gather",
  "target": "BerryBush_03",
  "params": {
    "count": 3
  }
}
```

`type` 必须对应已注册动作。`target` 和 `params` 必须符合动作定义的目标策略和参数约束。

### Validator / Referee

裁判层负责把 LLM 输出从“不可信建议”变成“可执行请求”。

它至少需要验证：

- 动作是否存在于当前可用动作列表。
- 目标是否存在、可达、可交互；需要移动的动作必须有 UE Navigation/PathFollowing 可达性结果，不能让 LLM 直接规划路线。
- 参数是否符合 schema。
- NPC 当前状态是否允许执行该动作。
- 项目玩法规则是否允许，例如阵营、权限、冷却、资源、任务阶段。

验证失败时应拒绝执行，并返回结构化失败原因和可恢复性结论。移动类失败至少要能区分目标无效、缺少 NavData、目标不在 NavMesh、无完整路径、partial path 被拒绝、静态碰撞阻挡、动态阻挡、长期无进展、中断和超时；随后可选择触发重新生成、降级对话或安全回退。

LLM 获得的不是“自己判断怎么绕路”的权限，而是运行时写入的 `MovementFailureObservation`：失败原因、Recoverability、剩余重试预算、已验证的替代动作和玩家可见摘要。LLM 只能基于这些事实解释失败、选择替代动作或请求外部状态变化。

### Dispatcher

分发层根据动作定义把合法动作交给具体执行器。

建议支持的执行器类型：

- Blueprint handler：最适合项目方快速接入。
- C++ handler：适合性能敏感或复杂玩法。
- StateTree handler：适合状态编排、可中断流程和 AI 行为树融合。
- SmartObject handler：适合环境交互、槽位占用和对齐。
- Montage handler：适合播放明确动作动画。
- Movement handler：适合移动、跟随、绕行、巡逻等空间动作；它复用 UE Navigation、NavMesh、PathFollowing、NavLink 和项目已有移动组件，不在插件核心里自研通用寻路。

插件核心只依赖统一接口，不依赖具体项目玩法类。

## Example: Gather

项目方要接入“采集”动作时，不应该修改插件核心代码。

可能的动作定义：

```text
ActionId: Action.Gather
PromptDescription: Collect resources from a nearby gatherable object.
TargetPolicy: ActorWithGameplayTag(Resource.Gatherable)
ParameterSchema:
  - resource_tag: GameplayTag, optional
  - count: integer, 1..10
AvailabilityRule:
  - NPC is not in combat
  - nearby gatherable object exists
ExecutorBinding:
  - Blueprint: BP_AINpcGatherActionHandler
MovementPolicy:
  - RequireReachablePath
VisualVerificationProfile:
  - visible movement to target
  - visible gather montage or interaction state
  - resource actor state changes or inventory/event confirmation
```

运行时 prompt 中只应出现当前合法动作，例如：

```text
Available actions:
- Action.Gather(target: BerryBush_03, params: count 1..10): Collect berries from the nearby bush.
```

LLM 可以建议：

```json
{
  "dialogue": "I will gather some berries from that bush.",
  "actions": [
    {
      "type": "Action.Gather",
      "target": "BerryBush_03",
      "params": {
        "count": 3
      }
    }
  ]
}
```

插件随后验证并分发给项目方的采集 handler。真正的采集逻辑、动画、资源变化由项目实现。

## Relationship To SmartObjects

SmartObject 是一种重要执行后端，但不应该成为唯一扩展模型。

适合 SmartObject 的动作：

- 坐下、使用机器、拿取固定物体、进入交互槽位。

不一定适合 SmartObject 的动作：

- 项目技能释放。
- 战斗动作。
- 背包操作。
- 纯动画表演。
- 对玩家或动态 Actor 的追踪动作。
- 多阶段任务脚本。

因此架构应是“Action 系统可以使用 SmartObject handler”，而不是“所有 Action 都必须是 SmartObject”。

## Animation Integration Boundary

项目动画资源应由项目方提供，插件不应该交付一套试图覆盖所有游戏类型的动画库。

项目方负责：

- 自己项目的 `SkeletalMesh`。
- 自己项目的 `AnimBlueprint`。
- 自己项目的 `Animation Montage`、`Animation Sequence` 和动画状态机。
- 具体玩法动作的动画语义，例如采集、换弹、施法、挖矿、演奏、驾驶。
- 动画完成后的玩法结果，例如资源扣减、物品入包、伤害结算、技能冷却或任务状态变化。

插件负责交付动画接入能力：

- 允许动作定义把 `ActionId` 绑定到 Montage、AnimBlueprint 参数、StateTree、SmartObject 或项目自定义 handler。
- 提供通用动画执行 handler，例如 `PlayMontageActionHandler`。
- 提供动作生命周期事件，例如 `Started`、`Completed`、`Failed`、`Interrupted`。
- 在动作被中断、Montage 播放失败、目标丢失或角色组件缺失时返回明确失败结果。
- 允许 Blueprint-first 接入，使项目方不写 C++ 也能绑定动作动画。
- 允许 C++ handler 扩展，满足复杂项目的战斗、技能、交互和任务动作。
- 提供示例资产和 visual harness，用 UE 标准 Mannequin 演示“移动到目标、播放交互动作、完成回调”的完整链路。

因此边界是：插件不交付所有项目动画，但必须交付让项目方把自己的动画稳定挂进 AI 行为链路的系统。

正式能力不应假设项目使用 UE Mannequin。Mannequin 只适合作为示例和自动化验证基线。插件侧应依赖 UE 标准抽象，例如 `ACharacter`、`USkeletalMeshComponent`、`UAnimInstance`、`UAnimMontage`、Blueprint Interface 和统一 action handler 接口。

## Runtime Verification Requirement

只要某个自定义动作能被玩家看到、听到、触发或感知到，就不能只用无头测试、mock 响应、日志回放或脚本注入作为最终验收。

动作扩展系统应要求每个玩家可感知执行器类别提供真实可视化验证方式：

- Movement：观察角色真实移动、到达、朝向和退出；同时覆盖墙/土包/空气墙/动态阻挡导致不可达时的停止、失败事件和玩家可见反馈。
- Montage：观察角色骨骼网格体播放正确动画。
- SmartObject：观察槽位占用、移动、交互状态和释放。
- Blueprint custom handler：观察项目侧可见状态变化。
- Combat / skill：观察目标、命中、效果、冷却或状态变化。

插件可以提供通用 visual harness，但项目动作的最终验收必须覆盖项目自己的 handler。

## Non-Goals

以下内容不应成为插件核心目标：

- 穷尽所有项目动作。
- 在核心模块内硬编码项目玩法。
- 让 LLM 绕过游戏侧校验直接执行动作。
- 把 prompt 约束当成唯一安全机制。
- 用 mock provider 或手工注入响应冒充真实行为验收。
- 在插件核心里重写 UE Navigation 或把 LLM 生成的路线当成可执行路径。

## Documentation Gap

现有 PRD、SDD 和研究文档已经提到结构化 `actions`、SmartObject 动态注入、StateTree 执行、`IActionValidator` 和外部项目可替换局部 Task。

但这些内容目前分散在行为执行、SmartObject、StateTree 和 OutputValidator 章节中，还没有形成一个独立的“项目自定义动作扩展系统”说明。

后续正式设计应把以下内容补成可开发规格：

- 动作定义资产或配置格式。
- 动作目录注册和查询接口。
- 参数 schema。
- 上下文可用动作查询。
- prompt 动态注入格式。
- validator/referee 接口。
- dispatcher/handler 接口。
- Blueprint-first 接入流程。
- C++ 扩展流程。
- 玩家可感知动作的真实可视化测试矩阵。
