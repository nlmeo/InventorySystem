# CurrentQueryResult 生命周期修复

本次按用户确认的方案修改查询缓存失效时机和提交前身份检查。

## 实现

- 新增 `InvalidateDropQuery()`，同时重置 `ItemDropIndex` 和整个 `FINV_SpaceQueryResult`，清空物品指针、集合、映射与判断标志。
- 每次更新查询前先失效；无有效物品、鼠标在网格外等提前返回不再留下上次结果。计算完成后保留结果供高亮及放置使用。
- 开始拖拽、更换外部拖拽、离开网格、清除 Payload、关闭背包时清理查询。单个格子的 DragLeave 不取消整个拖拽。
- 监听 `UDragDropOperation::OnDragCancelled`，清理与取消物品匹配的手持状态及缓存，防止 Esc 取消后继续生成查询。
- 两个松手入口仍重新读取鼠标位置及当前占用；额外拒绝空 Payload、物品或来源不一致的拖放事件。
- 放置分派前核对 `CurrentQueryResult.CurrentItem` 与当前 Payload 物品一致，且查询允许放置、落点索引有效。
- 网格内部 Payload 和拖放操作的 Payload 可以是不同对象，临时旋转也可以不同；按物品身份和来源校验，避免误拒绝正常旋转。
- 两个 Drop 回调使用作用域保护：中途 `ClearHoverItem()` 不清掉批量重排后续所需的查询和落点；整个回调成功或提前返回时统一失效。

仍由服务器执行既有数量、位置与占用校验。此次客户端缓存修复不代表完整事务回滚已实现。

## 验证

- 本机 UE 5.7：`ProjectXHGEditor Win64 DebugGame -NoUBA` 构建成功。
- 新增 `Inventory.DropQuery.Lifecycle`：验证全部查询字段清空、无 Payload 提前返回、鼠标出界、物品身份不符、落点越界、批量步骤保留查询、两个 Drop 失败退出及引擎取消事件。
- 同时回归 `Inventory.Pickup.AuthoritativeLifecycle` 与 `Inventory.Pickup.RotationPlacement`。
- 自动化报告：`Saved/DropQueryChanges/TestReport/index.json`；运行日志：`Saved/DropQueryChanges/automation.log`。
- 原始网格头文件和实现备份：`Saved/DropQueryChanges/Before/`。

## 手动验收记录与用例

用户于 2026-10-21 反馈“没问题了”，本次修复按整体功能反馈验收通过。以下保留回归用例；用户未逐项提供日志，也未单独确认远端客户端结果。

1. 拖到网格外再移回并松手；连续拖不同物品，确认无残留高亮或错放。
2. 拖动时按 Esc 或 I 关闭背包，再打开检查物品位置和数量。
3. 空格移动、合并、交换、一次多物品重排、按 R 旋转后放下。
4. 装备拖回网格，分别检查允许放置及灰显拒绝。
5. 远端客户端重复关键场景，确认服务器回推后位置、数量和朝向一致。

本次自动化未覆盖真实 Slate 鼠标拖放和远端客户端联机验收，保留原 B 组待办。
