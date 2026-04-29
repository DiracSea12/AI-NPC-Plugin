# AI NPC Plugin - Example Blueprints

本目录用于存放示例蓝图资产。

## 如何创建示例蓝图

请参考项目根目录的 `docs/Blueprint_Quickstart_US-1-T12.md` 文档，按照步骤创建：

1. **BP_ExampleNPC** - 示例 NPC Actor
   - 包含 AINpc Component
   - 配置 API Key
   - 实现对话触发和响应监听

2. **WBP_ExampleDialogueBubble** - 示例对话气泡 Widget
   - 基于 NpcDialogueBubbleWidget
   - 显示打字机效果
   - 绑定到 NPC 的响应事件

## 注意事项

- 蓝图资产文件（.uasset）只能在 UE5 编辑器中创建
- 创建后的文件会自动保存在此目录
- 这些示例展示了纯蓝图实现完整对话流程的方法
