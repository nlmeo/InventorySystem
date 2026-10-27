# InventorySession 统一背包入口（2026-10-25）

本轮将 `UINV_InventorySessionWidget` 收敛为玩家库存 UI 的唯一 Viewport 根节点。

## 行为

- InventoryComponent 只创建玩家库存 UI，不再直接 `AddToViewport`。
- 玩家按 `I` 时由 PlayerController 取得 Session，Session 挂载并切换玩家背包子 UI 的可见性。
- 容器交互仍复用同一个 Session，同时显示玩家背包和来源 UI。
- Session 关闭时隐藏玩家背包并调用 `OnInventoryClosed()` 清理拖拽、悬停和描述状态，但不把玩家 UI 重新添加到 Viewport。
- 玩家 UI 只在自身 `NativeOnInitialized()` 阶段绑定 InventoryComponent；已移除 `UINV_SpatialInventory::RebindInventoryForSession()` 及其调用路径。

## 生命周期约束

Session 持有玩家背包 UI 指针并复用同一个实例。重复按 `I` 不会重新创建、重新绑定或重新挂载网格；来源会话关闭后，Session 仍可再次切换为玩家单栏模式。

## 验证

- `ProjectXHGEditor Win64 Development -NoHotReloadFromIDE`：Succeeded。
- UHT 通过。
- PIE 仍需验证：首次按 `I`、重复开关、打开容器后关闭、容器与背包交替打开，以及拖拽中按 `I` 的清理行为。
