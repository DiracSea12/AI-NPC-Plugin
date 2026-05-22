## MODIFIED Requirements

### Requirement: Test seams stay narrow and do not replace real acceptance
Cleanup workflow MUST reduce or guard public test seams where safe, while preserving necessary automation coverage and never treating mock、bypass、injected、headless、static、prompt-only、payload-only、adapter-faked 或 provider-identity-unknown checks as final acceptance for player-visible NPC behavior.

#### Scenario: Public ForTest API is reviewed
- **WHEN** runtime-facing public header 暴露 `ForTest` function、counter、bypass 或 test-only hook
- **THEN** workflow 记录它是否能移动到更窄边界、被 guard、为 automation 保持 public、或通过更新 tests 后移除；唯一允许的 move targets 是 private test headers、automation-only modules 或 build/test-only compile guards

#### Scenario: Player-visible behavior is affected
- **WHEN** cleanup 触碰 NPC dialogue、memory、behavior state、UI、prompts、provider output interpretation、perception input、action execution、observation collection，或任何玩家能看到、听到、触发或感知的功能
- **THEN** final acceptance 除 build、static、unit、provider-connectivity、payload-construction、validate-only 或 headless checks 外，还需要真实 visual editor/game verification chain

#### Scenario: Validation harness is replaced or removed
- **WHEN** cleanup 触碰现有或替代 gameplay/editor verification harness 文件
- **THEN** workflow 在删除、接受或忽略任一侧前，记录 old harness、new harness、replacement rationale 和 verification evidence

#### Scenario: Adapter reports success without runtime observation
- **WHEN** visual acceptance adapter 声称某玩家可见行为成功，但没有来自真实 actor、component、subsystem、perception source 或 provider-chain callback 的对应 runtime observation
- **THEN** workflow 拒绝该 adapter result 作为 final acceptance evidence

#### Scenario: Perception payload is verified without behavior observation
- **WHEN** 测试只验证 multimodal vision、scene 或 hearing payload construction
- **THEN** workflow 将其报告为 payload verification，而不是 final player-visible NPC behavior acceptance

#### Scenario: Provider identity is not recorded
- **WHEN** 声称 final NPC behavior acceptance，但 safe diagnostics 缺少 provider type、model、endpoint/base URL summary、request attempt status 和 duration
- **THEN** workflow 拒绝该 run 作为充分 final acceptance evidence

### Requirement: Cleanup verification is proportional to behavior impact
Cleanup implementation MUST run verification that matches the risk and behavior impact of each lane.

#### Scenario: Cleanup is source-only and behavior-preserving
- **WHEN** cleanup task 只移除 generated output、收窄 duplicate declarations 或合并行为等价代码
- **THEN** workflow 运行相关 build 和 fast contract checks，或记录当前 blocker 以及精确 process/command evidence

#### Scenario: Provider or request contract changed
- **WHEN** schema、provider、prompt、multimodal payload、perception payload 或 fallback placement 改变 serialized request bodies
- **THEN** workflow 在 change-local verification artifact directory 中保存 before/after payloads，并在接受 cleanup 前比较它们

#### Scenario: Cleanup changes player-visible behavior
- **WHEN** cleanup task 改变会在游戏里呈现的行为
- **THEN** workflow 在可见窗口中运行真实 visual editor/game verification path，并记录 launch path、provider identity summary、observed NPC behavior、dialogue、state changes、适用时的 perception stimuli、actor 或 world state evidence、exit path、screenshots/viewport artifacts 或 manual observation notes；log pointers 只能作为辅助诊断，不能单独满足 final acceptance evidence；同时记录为什么没有用 headless、mock、bypass、injected-response、payload-only 或 adapter-faked run 作为 final acceptance

#### Scenario: Engine source would be modified
- **WHEN** cleanup task 需要修改 plugin、scripts、config、parser、runtime、docs 或 test harness surfaces 之外的 Unreal Engine source tree 文件
- **THEN** workflow 停止并记录 engine source edits 对该 plugin cleanup 是禁止的
