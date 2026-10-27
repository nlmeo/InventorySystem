# InventorySession 组合根（2026-10-25）

新增 `UINV_InventorySessionWidget`，用于组合玩家已有的 `UINV_SpatialInventory` 和当前来源侧 `UINV_InventorySourceWidget`。

蓝图可选 BindWidget：`PlayerInventoryHost`、`SourceInventoryHost`、`CloseButton`。没有配置两个 Host 时，C++ 会创建左右两个兜底面板。控制器现在创建 Session Widget，再由 Session Widget 创建来源 Widget；玩家库存复用现有 `InventoryComponent->GetInventoryMenu()`，避免创建第二份 SpatiallyInventory。

当前验证：Editor 编译成功，`Inventory.Container.Interaction` 1 成功、0 失败。
