## ADDED Requirements

### Requirement: 验收系统适合个人开源项目维护
Visual acceptance 演进 MUST 保持单个维护者可理解、可实现、可 review、可回滚，不能依赖只有大团队或企业平台才养得起的基础设施。

#### Scenario: 设计引入新基础设施
- **WHEN** implementation proposal 引入远程服务、数据库、Web 控制台、分布式调度、长期后台 daemon、私有 SaaS、企业内网服务或付费平台绑定
- **THEN** proposal 必须被拒绝，除非该能力被改写为可选 adapter 且 core runner 不依赖它

#### Scenario: 阶段任务无法单人闭环
- **WHEN** Phase 2.7、2.8、2.9 或 2.95 的任务需要跨多个大型子系统同时落地才能验证
- **THEN** 该阶段必须被拆小，直到每个阶段都有可独立运行的 build、static contract 和 visible-game 验收闭环

#### Scenario: 设计增加长期维护面
- **WHEN** change 增加 public API、DSL 字段、adapter category、observation namespace 或 assertion operator
- **THEN** design 必须说明该新增面的 owner、验证方式、失败诊断和外部贡献者如何发现正确用法

### Requirement: 核心插件开箱可用，项目差异走 adapter
Core plugin MUST 提供可运行的内置 fixture、adapter、scenario 示例和本地验证入口；项目特定行为 MUST 通过公开 adapter/interface 接入，而不是写死进 core runtime。

#### Scenario: 外部贡献者克隆项目
- **WHEN** 外部贡献者按公开仓库内容配置 provider 并运行 visual game tests
- **THEN** 内置 scenario 可以通过仓库内脚本、配置和 UE map 运行，不需要私有项目历史、内部服务或某台机器专属路径之外的隐藏依赖

#### Scenario: 项目需要自定义 NPC 行为
- **WHEN** 项目需要 quest、inventory、combat、relationship、schedule、vision、hearing 或 emotion animation 等自定义行为验收
- **THEN** 项目通过 adapter/interface 接入自己的行为和 observation，而不是修改 core runner 或复制 harness

### Requirement: DSL 和 adapter 表面积保持最小
Scenario DSL、adapter API 和 assertion grammar MUST 只包含当前阶段需要实现和验证的最小能力。

#### Scenario: 新 DSL 字段被提出
- **WHEN** 新字段不能被当前阶段的 scenario、contract test 或 visible-game run 使用并验证
- **THEN** 该字段不得加入 schema

#### Scenario: 新 adapter category 被提出
- **WHEN** 新 adapter category 没有当前阶段的内置或示例 scenario 使用
- **THEN** 该 category 不得进入 public API，只能作为未来设计记录

#### Scenario: 新 assertion operator 被提出
- **WHEN** 新 operator 没有明确值类型、缺失值语义、诊断输出和至少一个 scenario 验证
- **THEN** 该 operator 不得加入 assertion grammar

### Requirement: 文档和配置面向外部贡献者
OpenSpec、准则文件、scenario 示例和测试入口说明 MUST 用中文描述项目决策和维护约束，同时保留 OpenSpec 语法关键字与代码/API 标识符的英文原文。

#### Scenario: 文档描述测试入口
- **WHEN** documentation 说明 visual game tests 的添加或运行方式
- **THEN** 它明确说明单一真源、prompt/config-only 适用边界、何时必须写 adapter、以及哪些验证不算最终行为验收

#### Scenario: 文档引入本机路径或私有假设
- **WHEN** documentation 或 config 示例使用本机路径、私有服务、私有 provider key、内部项目历史或隐藏依赖作为必需条件
- **THEN** 该内容必须改为可提交 example/template 或被标注为本地私有配置，不能成为 core workflow 的必需条件

#### Scenario: 文档引用 Unreal Engine 安装路径
- **WHEN** documentation 或 task 需要说明 Unreal Engine、UBT 或 project file 路径
- **THEN** 它必须区分仓库内相对路径、示例路径和本机开发路径；`G:\UE5\...` 这类路径只能作为当前开发机示例或本地配置，不得成为开源贡献者必须复现的硬编码前提
