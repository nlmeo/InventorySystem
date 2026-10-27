# 库存交互 Subsystem（2026-10-25）

## 本轮实现

新增 `UINV_InventoryInteractionSubsystem`，继承 `ULocalPlayerSubsystem`，每个本地玩家拥有一个独立实例。Subsystem 只保存当前库存来源会话，不拥有库存数据，也不替代服务器权限校验。

当前接口：

- `BeginInventorySource(SourceActor, SourceInventory)`：开始检查一个来源库存。
- `EndInventorySource()`：结束当前来源会话。
- `GetActiveSourceActor()`：取得当前来源 Actor。
- `GetActiveSourceInventory()`：取得当前来源 `UINV_InventoryComponent`。
- `OnInventorySourceStarted` / `OnInventorySourceEnded`：供 UI 组合层订阅。

`AINV_PlayerController` 在容器打开和关闭路径中同步 Subsystem，因此现有容器 UI 行为保持不变，掉落背包、弹挂和尸体后续可以复用同一会话入口。

## 下一步 UI 拆分

将现有 `UINV_ContainerInventoryWidget` 收敛为来源侧 UI，再增加 `UINV_InventorySessionWidget` 作为组合根：左侧复用玩家 `UINV_SpatialInventory`，右侧根据来源类型创建网格型、图片加网格型或其它来源 UI。Subsystem 只提供来源上下文，具体展示由 UI 类或展示配置资产决定。

## 验证

- `ProjectXHGEditor Win64 Development` 编译成功。
- `Inventory.Container.Interaction`：1 成功、0 失败。
