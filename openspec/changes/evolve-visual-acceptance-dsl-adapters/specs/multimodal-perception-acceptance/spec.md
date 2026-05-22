## ADDED Requirements

### Requirement: 多模态视觉验收使用引擎支撑的视觉输入
可见游戏验收系统 MUST 通过引擎支撑的 visual 或 scene inputs 支持未来 multimodal vision scenarios，而不是依赖 prompt-only claims 或 mocked payloads。

#### Scenario: 测试 actor visibility
- **WHEN** scenario 验证 NPC 对看见 actor、object 或 scene condition 作出反应
- **THEN** scenario 使用 fixture、perception、camera、trace、scene-description 或 project vision adapter，在运行中的 UE world 里创建并观察 visual input

#### Scenario: Scene description 被用作视觉输入
- **WHEN** scenario 使用 scene-description adapter 作为视觉输入来源
- **THEN** scene description 必须绑定 runtime camera、trace、perception source、actor ids、source transform 和 timestamp，不能只是 prompt 中手写的一段“NPC 看见了 X”文本

#### Scenario: Vision context 被发送给 provider
- **WHEN** scenario 向 provider chain 发送 visual context 或 image-derived context
- **THEN** result diagnostics 区分 payload-construction checks 和 final visible-game behavior acceptance

#### Scenario: NPC 对 visual input 作出反应
- **WHEN** final acceptance 声称 NPC 响应了视觉感知
- **THEN** visible game run 记录 visual stimulus evidence、provider request evidence、NPC reaction evidence，以及该视觉输入导致的 world-state 或 actor-state observation

### Requirement: 听觉验收使用引擎支撑的 sound 或 hearing stimuli
可见游戏验收系统 MUST 通过真实 UE sound、AI perception、event 或 project audio stimulus adapters 支持未来 hearing scenarios，而不是 mocked hearing flags。

#### Scenario: 发出 sound stimulus
- **WHEN** scenario 验证 NPC 听到声音
- **THEN** scenario 在 visible game run 中通过 registered adapter 发出真实 engine-backed 或 project-backed sound/hearing stimulus

#### Scenario: 采集 hearing observation
- **WHEN** NPC 或 project perception system 收到 hearing stimulus
- **THEN** observation provider 从真实 perception component、subsystem 或 project state 记录 declared hearing observation，并带 source 和 timestamp metadata

#### Scenario: NPC 对 hearing input 作出反应
- **WHEN** final acceptance 声称 NPC 响应了听觉
- **THEN** visible game run 记录 sound stimulus evidence、provider request evidence（如果涉及 provider reasoning）、NPC reaction evidence，以及听觉输入导致的 world-state 或 actor-state observation

### Requirement: Perception adapters 区分 input capture 和 behavior acceptance
系统 MUST 区分 perception input capture checks 和最终 NPC behavior acceptance。

#### Scenario: 构造 perception payload
- **WHEN** 测试只验证 visual、scene 或 audio context 被采集或序列化
- **THEN** 结果报告为 input/payload verification，而不是最终 NPC behavior acceptance

#### Scenario: Perception 驱动 NPC behavior
- **WHEN** scenario 声称接受依赖 visual 或 hearing perception 的 NPC behavior
- **THEN** scenario 必须通过 visible game acceptance path 运行，使用真实 provider，并产生 resulting behavior 的 runtime observations

#### Scenario: 使用 fake perception flag
- **WHEN** scenario 或 adapter 在没有 engine-backed stimulus、project-backed stimulus、perception component state、scene/camera/trace source 或 declared project observation provider 的情况下标记 visual/hearing perception observation
- **THEN** 该 run 被拒绝作为最终 perception behavior acceptance evidence

### Requirement: 未来 PRD behavior domains 使用 adapters 和 observations
产品设计文档中的 NPC 能力，包括 perception、memory、emotion、relationship、proactive interaction、autonomous behavior 和 social behavior，MUST 通过 adapter-backed execution 和 observation 接受，而不是 core-runner feature branches。

#### Scenario: 新 PRD feature 已有 adapters
- **WHEN** 新 feature scenario 使用已经注册的 adapters 和 observations
- **THEN** 测试可以通过 prompt 和 scenario changes 添加，而不修改 core runner

#### Scenario: 新 PRD feature 需要 project-specific integration
- **WHEN** 新 feature 需要 quest、inventory、combat、relationship、schedule 或 emotion animation 等 project-specific system
- **THEN** 在 scenario 能声称 final behavior acceptance 前，必须提供 project adapter 和 observation provider

#### Scenario: 新 PRD feature 直接加到 core runner
- **WHEN** 实现把 project-specific PRD behavior branch 加到 core runner，而不是使用 adapter 和 observation provider
- **THEN** 该实现不通过本 change 的 architecture review
