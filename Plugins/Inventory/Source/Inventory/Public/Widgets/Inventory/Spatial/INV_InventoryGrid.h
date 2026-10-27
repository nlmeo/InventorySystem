#pragma once


#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/CanvasPanel.h"
#include "Types/INV_GridTypes.h"
#include "Types/INV_InteractionTypes.h"
#include "INV_InventoryGrid.generated.h"


class UINV_ItemPayload;
class UINV_ItemPopUp;
enum class EINV_GridSlotState : uint8;
struct FGameplayTag;
struct FINV_ImageFragment;
struct FINV_GridFragment;
class UINV_SlottedItem;
struct FINV_ItemManifest;
class UINV_ItemComponent;
class UINV_InventoryComponent;
class UINV_GridSlot;
class UCanvasPanel;
class USizeBox;

class UINV_InventoryGrid;

// 装备来源的物品被放到背包网格时广播（交给 SpatialInventory 处理卸下）
// SourceGrid 是实际接收放下的网格：可能是消耗品/材料网格（此时 GridIndex 属于该网格，对装备网格无意义）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEquippedItemDroppedOnGrid,
	UINV_InventoryGrid*, SourceGrid, UINV_ItemPayload*, Payload, int32, GridIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInventoryGridItemDoubleClicked,
	UINV_InventoryItem*, Item, int32, GridIndex);
UCLASS()
class INVENTORY_API UINV_InventoryGrid : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Runtime container panels bind explicitly; their slot widgets reuse the existing visual classes. */
	bool InitializeForInventory(UINV_InventoryComponent* Component, FName PanelId);
	void UnbindInventory();
	void SetSourceInteractionMode(bool bInSourceMode) { bSourceInteractionMode = bInSourceMode; }
	virtual void NativeDestruct() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float InDeltaTime) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	
	EINV_ItemCategory GetItemCategory() const { return ResolvedCategory; }
	FName GetSlotId() const { return SlotId; }
	FINV_SlotAvailabilityResult HasRoomForItem(const UINV_ItemComponent* ItemComponent);
	
	void ShowCursor();
	void HideCursor();
	void SetOwningCanvas(UCanvasPanel* OwningCanvas);
	void DropItem();
	// 装备槽双击卸下后，新建的背包图标可能收到同一轮双击事件；短暂屏蔽该物品的快速装备。
	void SuppressQuickEquipForItem(UINV_InventoryItem* Item, float DurationSeconds = 0.25f);
	bool HasHoverItem() const;
	UINV_ItemPayload* GetHoverItem() const;
	void ClearHoverItem();
	float GetTileSize() const {return TileSize;};

	// 装备来源物品放到网格时触发（供 SpatialInventory 绑定处理卸下）
	FEquippedItemDroppedOnGrid OnEquippedItemDroppedOnGrid;
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Interaction")
	FInventoryGridItemDoubleClicked OnItemDoubleClicked;

	// 把当前 ItemPayload 里的物品放到指定索引（供卸下/外部放置使用）。
	// bReportToServer=false 时只做 UI：批量挤压场景要先把整组（含本件）收齐再一次性提交，
	// 分两次发会让服务器在中间态看到「邻居还没让位」或「自己还占着原格」而拒绝。
	void PutDownOnIndex(const int32 Index, bool bReportToServer = true, bool bUpdateItemState = true);

	// 判断某索引是否有效且可放置（未被占用），供卸下/外部放置前校验
	bool CanPlaceAtIndex(const int32 Index) const;

	// 判断物品以 Index 为左上角、按 Dimensions 尺寸能否完整放置（多格占用校验）
	bool CanPlaceItemAtIndex(const int32 Index, const FIntPoint& Dimensions) const;

	// 同上，但忽略 ItemToIgnore 自身占用的格子（换装时把被替换的物品放回原格用）
	bool CanPlaceItemAtIndexIgnoring(const int32 Index, const FIntPoint& Dimensions,
	                                 const UINV_InventoryItem* ItemToIgnore) const;

	// 查找物品在网格中的左上角索引（不存在返回 INDEX_NONE）
	int32 FindItemGridIndex(const UINV_InventoryItem* Item) const;

	// 该物品能否在本网格的某个位置完整放下（不实际放置）
	bool HasRoomForItemInstance(const UINV_InventoryItem* Item);

	// 放到第一个可用位置（用于「放置索引无意义」的场景，例如在消耗品页签下卸下装备）。
	// 放不下返回 false，且不做任何改动。
	bool AutoPlaceItem(UINV_InventoryItem* Item);

	// 找第一个能完整放下该物品的空位置（要求整片区域为空），没有则返回 INDEX_NONE。
	// 供拆分使用：拆出来的新堆叠要独立占位，不能叠回源堆叠。
	// 先搜当前朝向，再尝试旋转；不修改源物品，服务器在选中索引复核朝向。
	int32 FindFreeSlotForItem(const UINV_InventoryItem* Item) const;

	// 换装回填专用：查找能放下 Item 的位置，同时忽略 IgnoreItem 占用的格子。
	// 先尝试原始尺寸，再尝试旋转尺寸；不修改物品状态。
	int32 FindFreeSlotForItemIgnoring(const UINV_InventoryItem* Item,
		const UINV_InventoryItem* IgnoreItem, bool& bOutRotated) const;

	// 当前拖拽放置的对齐左上角索引（内部空间查询计算得出，装备拖拽也可用）
	int32 GetItemDropIndex() const { return ItemDropIndex; }

	// 当前空间查询结果（含放置区域内物品集合，供卸下/换装判断）
	const FINV_SpaceQueryResult& GetCurrentQueryResult() const { return CurrentQueryResult; }

	// 清理外部拖拽状态（装备物品放下后调用）
	void ClearExternalDragPayload();
	UFUNCTION()
	void AddItem(UINV_InventoryItem* Item);

	/**
	 * 条目被移除（丢弃清空 / 使用耗尽 / 整份合并）。
	 *
	 * 这类变化不体现在任何**属性**上，所以 RepNotify 不会响 —— 之前全工程没有一处绑定它，
	 * 结果就是「源条目已经没了，图标还在」。这里只安排一次重绘，实际移除交给重建逻辑。
	 */
	UFUNCTION()
	void OnInventoryItemRemoved(UINV_InventoryItem* Item);
	void AssignHoverItem(UINV_InventoryItem* InventoryItem);
	void OnHide();
	void RemoveItemFromGrid(UINV_InventoryItem* InventoryItem, const int32 GridIndex, bool bUpdateItemState = true);

	/**
	 * 按权威数据重铺整个网格（服务器权威模式专用）。
	 *
	 * 与「增量纠正」的区别：这里不做推测，直接清空后按每条目的权威 GridIndex 重画。
	 * 因此它天然覆盖「操作被拒绝」的情况 —— 数据没变，重铺后 UI 就退回了原样。
	 * 放不下的条目宁可**不画**，也不在错的位置铺一个假图标。
	 */
	void RebuildFromInventoryList();
	// 本网格是否接受该分类：与 MatchesCategory 同源，但入参是分类而非物品实例，
	// 供 SpatiallyInventory 在物品尚未转成 InventoryItem 时（拾取预判）判断。
	bool AcceptsCategory(EINV_ItemCategory Category) const;

	// 把落位结果上报给服务器（服务器创建条目时只知道东西存在、不知道它在哪）。
	// public：装备卸下（UnloadItemToEquippablesGrid）等外部路径本地放好物品后，
	// 需要把位置回传服务器，覆盖 Server_EquipSlotClicked 清空的 GridIndex，否则权威重铺后物品消失。
	void ReportPlacementToServer(UINV_InventoryItem* Item, int32 Index);
	int64 SubmitInventoryOperation(FINV_InteractionContext Context);
private:
	bool HandleContainerDrop(UINV_ItemPayload* Payload, int32 GridIndex);
	// ===== 放置流程的内部拆分 =====
	//
	// 所有「把 ItemPayload 放到 ItemDropIndex」的入口最终都走 PlaceHoverItemAtDropIndex()：
	//   OnSlottedItemDropped（网格内图标拖放）/ OnGridSlotDropped（悬持状态落到空格）。
	// 两条入口的差别**只有前置守卫**（前者要求源格仍有物品，后者要求手里有东西且不是原地放下），
	// 所以守卫留在各自回调里，主体只写一份 —— 之前这里是两份近 500 行的复制，
	// 任何落点规则的改动都得同步改两处，漏一处就表现为「换个入口行为就不一样」。

	/**
	 * 放置的唯一入口。
	 *
	 * 完整覆盖同类堆叠时，先统一处理合并、填充或满堆数量互换。
	 * 第一层分派：**放置前后朝向是否改变**（见 IsHoverRotationChanged）
	 *   → PlaceHoverItemWithSameRotation / PlaceHoverItemWithRotationChanged
	 * 第二层分派：两个维度内部各自按「是否与自身重叠」→「目标区里是 0 / 1 / 多件物品」。
	 *
	 * 两个回调（OnSlottedItemDropped / OnGridSlotDropped）只保留各自的前置守卫，都汇到这里。
	 */
	void PlaceHoverItemAtDropIndex();
	// 完整覆盖同类堆叠时，数量操作优先于朝向分派；返回是否已处理该次放置。
	bool TryHandleStackDrop();

	/** 松手时按当前鼠标、Payload 尺寸和网格占用重新查询，避免使用上一帧的空区结果。 */
	bool RefreshDropQuery(UINV_ItemPayload* Payload);

	void InvalidateDropQuery();
	bool IsDropQueryCurrent() const;
	void BindDragCancellation(UINV_ItemPayload* Payload);
	UFUNCTION()
	void OnDragOperationCancelled(UDragDropOperation* Operation);
	friend class FINV_DropQueryLifecycleTest;
	friend class FINV_SplitRotationTest;
	friend class FINV_ServerValidationTest;
	friend class FINV_DropSpawnTest;
	friend class FINV_EdgeRotationTest;

	// 放置中途清除 Payload 时保留查询，整个放置回调退出后统一失效。
	bool bProcessingDrop = false;

	/** 第一层分派的判据：物品当前持久朝向 != Payload 里的拖拽临时朝向（且物品本身可旋转）。 */
	bool IsHoverRotationChanged() const;

	/**
	 * 维度一：放置前后朝向不变。
	 * 位移/对称落点只算一次并全程使用整数坐标；整批校验通过后统一清格、落位和提交。
	 */
	void PlaceHoverItemWithSameRotation();

	/**
	 * 维度二：放置前后朝向改变。
	 * 目标区按**旋转后的新尺寸**覆盖，目标区形状与邻居的推移基准都可能不同。
	 * 新旧区域分离时，将目标区物品通过互逆的 90° 整数映射重排到旧占区。
	 */
	void PlaceHoverItemWithRotationChanged();

	/**
	 * 按旧朝向把悬持物品从原格清干净，然后写回新朝向。
	 * 挤压/交换/落位都必须先调它：顺序反了会「按新尺寸清旧位置」，留下半格残留占用（#18 踩过）。
	 */
	void DetachHoverItemFromSourceGrid();

	/** 无挤压的直接落位：Detach + 落到 ItemDropIndex。 */
	void RelocateHoverItemToDropIndex();

	/**
	 * 批量位移的落点护栏：越界 + 本批内重叠（含与主物品目标区）。
	 *
	 * Items / Positions / Dimensions 一一对应，Positions 是每件的**目标左上角**。
	 * Context 只进日志前缀（"批量交换取消" / "挤压取消"）；
	 * LandingWord 区分两种落点算法（"位移落点" / "对称落点"），便于从日志反推是哪条分支。
	 * MainDim 为 (0,0) 表示主物品无效或落点非法 —— 此时只校验越界，跳过重叠（与原实现一致）。
	 */
	bool ValidateDisplacedLandings(const TCHAR* Context, const TCHAR* LandingWord,
		const TArray<UINV_InventoryItem*>& Items, const TArray<FIntPoint>& Positions,
		const TArray<FIntPoint>& Dimensions, const FIntPoint& MainPos, const FIntPoint& MainDim) const;

	/**
	 * 把整批（被推开的邻居 + 主物品）一次性提交给服务器。
	 *
	 * 必须整批提交：逐条发 MoveItem 会撞上「前一位还没让出位置」的中间态而被判占用冲突。
	 * HoverItem 由调用方在 PutDownOnIndex 清空 Payload 前保留，避免提交时遗漏主物品。
	 * LogSuffix 只影响日志，用于区分位移、挤压和旋转重排分支。
	 */
	void CommitBatchMove(UINV_InventoryItem* HoverItem, TArray<UINV_InventoryItem*>& MovedItems,
		TArray<int32>& MovedIndices, const TCHAR* LogSuffix);

	TWeakObjectPtr<UINV_InventoryComponent> InventoryComponent;
	bool bSourceInteractionMode = false;
	TWeakObjectPtr<UCanvasPanel> OwningCanvasPanel;
	
	void ConstructGrid();

	// 仅按 SlotId 从 GridLayouts 资产解析尺寸和分类；缺少或重复 ID、配置非法均停止构造。
	bool ResolveGridLayoutFromAsset();
	FINV_SlotAvailabilityResult HasRoomForItem(const UINV_InventoryItem* Item, const int32 StackAmountOverride = -1);
	// DimensionsOverride 用于旋转过的物品：manifest 里存的是未旋转尺寸，
	// 不传就以资产尺寸为准（拾取预判这类「还没有物品实例」的场景）。
	FINV_SlotAvailabilityResult HasRoomForItem(const FINV_ItemManifest& Manifest, const int32 StackAmountOverride = -1,
		const FIntPoint& DimensionsOverride = FIntPoint::ZeroValue);
	void AddItemToIndeces(const FINV_SlotAvailabilityResult& Result, UINV_InventoryItem* NewItem);
	bool MatchesCategory(const UINV_InventoryItem* Item) const;

	// 物品是否归属本网格：优先看容器归属（ContainerId），未落位（None）时退回分类判断。
	// 两个同分类网格时，分类判断会把同一件物品放进两个网格；用 ContainerId 就能精确归属，
	// 只让物品落位到的那个网格认领它（对应 #12 塔科夫化的「按容器身份分发」）。
	bool BelongsToThisGrid(const UINV_InventoryItem* Item) const;
	FVector2D GetDrawSize(const FINV_GridFragment* GridFragment) const;
	// 按任意尺寸算绘制尺寸（旋转后 X/Y 互换，需要独立入口；保留上面那个给未旋转场合用）
	FVector2D GetDrawSizeForDims(const FIntPoint& Dimensions, float GridPadding) const;
	void SetSlottedItemImage(const UINV_SlottedItem* SlottedItem, const FINV_GridFragment* GridFragment, const FINV_ImageFragment* ImageFragment) const;
	void AddItemAtIndex(UINV_InventoryItem* Item, const int32 Index, bool bStackable, const int32 StackAmount);
	void RemoveSlottedItemEntriesForItem(const UINV_InventoryItem* Item, int32 KeepIndex = INDEX_NONE);
	UINV_SlottedItem* CreateSlottedItem(UINV_InventoryItem* Item, const int32 Index, const bool bStackable, const int32 StackAmount,
		const FINV_GridFragment* GridFragment, const FINV_ImageFragment* ImageFragment);
	void AddSlottedItemToCanvas(const int32 Index, const FINV_GridFragment* GridFragment, UINV_SlottedItem* SlottedItem,
		const UINV_InventoryItem* Item) const;
	void UpdateGridSlots(UINV_InventoryItem* NewItem, const int32 Index, bool bStackable, const int32 StackAmount,
		bool bUpdateItemState = true);	
	bool IsIndexClaimed(const TSet<int32>& ClaimedIndices, const int32 Index) const;
	bool HasRoomAtIndex(const UINV_GridSlot* GridSlot, 
						const FIntPoint& Dimensions,
						const TSet<int32>& CheckedIndices,
						TSet<int32>& OutTentativelyClaimed,
						const FGameplayTag& ItemType,
						int32 MaxStackSize);
	bool CheckSlotConstraints(const UINV_GridSlot* GridSlot,
							  const UINV_GridSlot* SubGridSlot,
							  const TSet<int32>& CheckedIndices,
							  TSet<int32>& OutTentativelyClaimed,
							  const FGameplayTag& ItemType,
							  int32 MaxStackSize) const;
	FIntPoint GetItemDimensions(const FINV_ItemManifest& Manifest) const;
	bool HasValidItem(const UINV_GridSlot* GridSlot) const;
	bool IsUpperLeftSlot(const UINV_GridSlot* GridSlot, const UINV_GridSlot* SubGridSlot) const;
	bool DoesItemTypeMatch(const UINV_InventoryItem* SubItem, const FGameplayTag& ItemType) const;
	bool IsInGridBounds(const int32 StartIndex, const FIntPoint& ItemDimensions) const;

	// 坐标级越界校验（挤压/位移落点专用）：负坐标或按尺寸展开后越界即 false。
	// 不用 IsInGridBounds —— 它吃的是线性索引，负坐标经 GetIndexFromPosition 回卷后会「看起来合法」。
	bool IsLandingInBounds(const FIntPoint& AfterPos, const FIntPoint& ItemDimensions) const;

	// 两个矩形（左上角 + 尺寸）是否重叠，用于挤压落点的「本批内」冲突校验。
	// 判定与服务器 Server_MoveItems 的半开区间口径一致。
	bool DoRectsOverlap(const FIntPoint& APos, const FIntPoint& ADim,
		const FIntPoint& BPos, const FIntPoint& BDim) const;

	// 当前进行空间查询/高亮所用的 Payload（内部 ItemPayload 或外部拖拽进来的装备物品）
	UINV_ItemPayload* GetActivePayload() const;

	/**
	 * 拖拽中按 R 旋转（#18 第一阶段：手动旋转）。
	 *
	 * 为什么不走输入绑定：拖拽期间 Slate 吃住了鼠标，但键盘焦点不在任何网格上；
	 * 这里用「每帧读一次 PC 的按键状态 + 上升沿」的方式探测，不依赖焦点也不改 InputAction 配置。
	 */
	void TryRotateHoverItem();
	void UpdateDragVisualRotation();
	bool RefreshPlacementPreview(const FINV_TileParameters& Parameters);
	/** 把 Payload 的临时朝向写回物品（只在**确定要落位**之后调用；回滚路径绝不能用） */
	void ApplyPayloadRotationToItem();
	// 是否处于「装备来源」的外部拖拽中
	bool HasExternalDragPayload() const;
	int32 DetermineFillAmountForSlot(const bool bStackable, const int32 MaxStackSize, const int32 AmountToFill, const UINV_GridSlot* GridSlot) const;
	int32 GetStackAmount(const UINV_GridSlot* GridSlot) const;
	bool IsRightClicked(const FPointerEvent& MouseEvent) const;
	bool ISLeftClicked(const FPointerEvent& MouseEvent) const;
	void PickUp(UINV_InventoryItem* ClickedInventoryItem, const int32 GridIndex);
	void AssignHoverItem(UINV_InventoryItem* InventoryItem, const int32 GridIndex, const int32 PreviousGridIndex);
	
	void UpdateTileParameters(const FVector2D& CanvasPosition, const FVector2D& MousePosition);
	FIntPoint CalculateHoveredCoordinates(const FVector2D& CanvasPosition, const FVector2D& MousePosition) const;
	EINV_TileQuadrant CalculateTileQuadrant(const FVector2D& CanvasPosition, const FVector2D& MousePosition) const;
	void OnTileParametersUpdate(const FINV_TileParameters Parameters);
	FIntPoint CalculateStartCoordinate(const FIntPoint& Coordinates, const FIntPoint& Dimensions, EINV_TileQuadrant TileQuadrant) const;
	FINV_SpaceQueryResult CheckHoverPosition(const FIntPoint& Position, const FIntPoint& Dimensions) const;
	bool CursorExitedCanvas(const FVector2D& BoundaryPos,const FVector2D& BoundarySize, const FVector2D& Location);
	void HighlightSlots(const int32 Index, const FIntPoint& Dimensions);
	void UnhighlightSlots(const int32 Index, const FIntPoint& Dimensions);
	void SelectSlots(const int32 Index, const FIntPoint& Dimensions);
	void GrayedOutSlots(const int32 Index, const FIntPoint& Dimensions);
	void ChangeHoverType(const int32 Index, const FIntPoint& Dimensions, EINV_GridSlotState GridSlotState);
	UUserWidget* GetVisibleCursorWidget();
	UUserWidget* GetHiddenCursorWidget();
	bool IsSameStackable(const UINV_InventoryItem* ClickedInventoryItem) const;
	/**
	 * 把悬挂物品与目标物品换位。
	 * ClickedIndex = 目标物品的左上角；HoverTargetIndex = 悬挂物品的落点。
	 * 两者默认相同（1x1 互换就是这样）；当悬挂物品是 2x2、目标只有 1x1 时，
	 * 落点应当是鼠标位置而不是目标物品所在格，否则会「落在角落里」或干脆无变化。
	 */
	void SwapWithHoverItem(UINV_InventoryItem* ClickedInventoryItem, int32 ClickedIndex, int32 HoverTargetIndex = INDEX_NONE);
	bool ShouldSwapStackCounts(const int32 RoomInClickSlot, const int32 HoveredStackCount, const int32 MaxStackSize) const;
	void SwapStackCounts(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index);
	bool ShouldConsumeStackCounts(const int32 RoomInClickSlot, const int32 HoveredStackCount) const;
	void ConsumeHoverItemStacks(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index);
	bool ShouldFillInStack(const int32 RoomInClickSlot, const int32 HoveredStackCount) const;
	void FillInStack(const int32 FillAmount, const int32 Remainder, const int32 Index);
	void CreateItemPopUp(const int32 GridIndex);
	void PutHoverItemBack();
	
	UPROPERTY(EditAnywhere, Category= "Inventory")
	TSubclassOf<UINV_ItemPopUp> ItemPopUpClass;
	
	UPROPERTY()
	TObjectPtr<UINV_ItemPopUp> ItemPopUp;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UUserWidget> VisibleCursorWidgetClass;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UUserWidget> HiddenCursorWidgetClass;
	
	UPROPERTY()
	TObjectPtr<UUserWidget> VisibleCursorWidget;
	
	UPROPERTY()	
	TObjectPtr<UUserWidget> HiddenCursorWidget;
	
	UFUNCTION()
	void AddStacks(const FINV_SlotAvailabilityResult& Result);
	
	// 右键点击 SlottedItem → 打开物品操作菜单（拆分 / 丢弃 / 使用）
	UFUNCTION()
	void OnSlottedItemClicked(int32 GridIndex, const FPointerEvent& MouseEvent);

	// 左键双击 SlottedItem → 装备类快速装备到对应装备槽（已装备则换装），消耗类快速使用
	UFUNCTION()
	void OnSlottedItemDoubleClicked(int32 GridIndex);
	
	UFUNCTION()
	void OnSlottedItemDragStart(int32 GridIndex, UINV_ItemPayload* Payload);
	
	UFUNCTION()
	void OnSlottedItemDropped(int32 GridIndex, UINV_ItemPayload* Payload);
	
	UFUNCTION()
	void OnGridSlotDropped(int32 GridIndex, UINV_ItemPayload* Payload);

	UFUNCTION()
	void OnGridSlotDragEnter(int32 GridIndex, UINV_ItemPayload* Payload);

	UFUNCTION()
	void OnGridSlotDragLeave(int32 GridIndex, UINV_ItemPayload* Payload);

	UFUNCTION()
	void OnGridSlotHovered(int32 GridIndex, const FPointerEvent& MouseEvent);
	
	UFUNCTION()
	void OnGridSlotUnhovered(int32 GridIndex, const FPointerEvent& MouseEvent);
	
	UFUNCTION()
	void OnPopUpMenuSplit(int32 SplitAmount, int32 Index);
	
	UFUNCTION()
	void OnPopUpMenuDrop(int32 Index);
	
	UFUNCTION()
	void OnPopUpMenuConsume(int32 Index);
	
	UFUNCTION()
	void OnItemInventoryMenuToggled(bool bOpen);
	
	/**
	 * 本网格对应的 GridLayout 资产面板 SlotId。
	 * 必填且必须在组件引用的布局资产中唯一；尺寸和接受分类全部从该面板读取。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "Inventory")
	FName SlotId;

	// 仅由资产面板派生，不存在蓝图分类或历史值回退。
	EINV_ItemCategory ResolvedCategory = EINV_ItemCategory::None;
	bool bGridLayoutResolved = false;

	UPROPERTY()
	TArray<TObjectPtr<UINV_GridSlot>> GridSlots;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UINV_GridSlot> GridSlotsClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SizeBox_GridFrame;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UINV_SlottedItem> SlottedItemClass;
	
	UPROPERTY()
	TMap<int32, TObjectPtr<UINV_SlottedItem>> SlottedItems;

	TWeakObjectPtr<UINV_InventoryItem> SuppressedQuickEquipItem;
	double SuppressedQuickEquipUntil = 0.0;
	
	// 运行时资产缓存：不加载或保存蓝图中遗留的尺寸值。
	UPROPERTY(Transient)
	int32 Rows = 0;

	UPROPERTY(Transient)
	int32 Columns = 0;

	UPROPERTY(Transient)
	float TileSize = 0.f;
	
	UPROPERTY()
	TObjectPtr<UINV_ItemPayload> ItemPayload;

	// 从装备槽拖拽进来的外部物品（用于驱动网格空间查询与校验高亮）
	UPROPERTY()
	TObjectPtr<UINV_ItemPayload> ExternalDragPayload;
	/** Slate's drag-operation payload; kept in sync with source auto-rotation. */
	TWeakObjectPtr<UINV_ItemPayload> DragOperationPayload;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FVector2D ItemPopUpOffset;
	
	//check at set on every frame
	FINV_TileParameters TileParameters;
	FINV_TileParameters LastTileParameters;
	
	// Index where an item would be placed if we click on the grid at valid location.
	int32 ItemDropIndex{INDEX_NONE};
	FINV_SpaceQueryResult CurrentQueryResult;
	
	bool bMouseWithinCanvas;
	bool bLastMouseWithinCanvas;

	// 上一帧 R 键是否按下（用于取上升沿：按住不放只翻转一次）
	bool bRotateKeyHeld{false};
	
	int32 LastHighlightedIndex{INDEX_NONE};
	FIntPoint LastHighlightedDimensions{FIntPoint::ZeroValue};
};


