# ProjectXHG Inventory 1.0 Candidate

日期：2026-10-27

## 自动化回归

命令：

```text
UnrealEditor-Cmd.exe ProjectXHG.uproject -unattended -nop4 -NullRHI -nosplash -DDC-ForceMemoryCache -ExecCmds="Automation RunTests Inventory;Quit"
```

结果：

- 测试数量：12
- 全部通过
- Exit Code：0

通过的测试包括：

- Inventory.Audit.AuthoritySnapshot
- Inventory.Container.Interaction
- Inventory.Drag.EdgeRotation
- Inventory.Drop.SpawnFailure
- Inventory.DropQuery.Lifecycle
- Inventory.Equipment.DropOutside
- Inventory.Equipment.ProxyStateRecovery
- Inventory.Pickup.AuthoritativeLifecycle
- Inventory.Pickup.RotationPlacement
- Inventory.Server.Validation
- Inventory.Split.RotationPlacement
- Inventory.Transfer.Atomic

## Candidate 状态

- Session 背包架构已冻结。
- 跨容器请求统一由 PlayerController 转发。
- 跨容器整件条目复用原复制子对象。
- 客户端负责跨容器交换规划和高亮。
- 服务器验证客户端批量交换计划。
- 多分类回填、旋转回填和多物品交换已通过测试。
- 失败反馈和拖拽状态清理已通过测试。
- 不再新增功能，只处理阻塞性回归问题。

## 后续限制

1. 继续进行专用服务器手工回归。
2. 检查版本号和最终配置资产。
3. 只修复阻塞 1.0 的问题。
