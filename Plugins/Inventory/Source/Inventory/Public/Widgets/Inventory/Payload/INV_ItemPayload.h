#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "INV_ItemPayload.generated.h"

class UINV_InventoryItem;
class UWidget;
class UINV_InventoryGrid;
class UINV_InventoryComponent;

/** 拖拽来源：区分物品是从背包网格还是装备槽拖出来的 */
UENUM()
enum class EINV_ItemDragSource : uint8
{
	None			UMETA(DisplayName = "None"),
	InventoryGrid	UMETA(DisplayName = "Inventory Grid"),
	EquippedSlot	UMETA(DisplayName = "Equipped Slot")
};

/**
 * 拿起物品的持久数据载体（取代原 UINV_HoverItem 的数据职责）。
 * 图标跟随视觉已迁移到 UDragDropOperation::DefaultDragVisual（Ghost），
 * 本对象只负责承载「拿起中物品」的状态，并作为拖拽 Payload 跨容器传递。
 */
UCLASS()
class INVENTORY_API UINV_ItemPayload : public UObject
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UINV_InventoryGrid> SourceGrid;
	TWeakObjectPtr<UINV_InventoryComponent> SourceInventory;
	void InitByItem(UINV_InventoryItem* InventoryItem);
	void Clear();

	FGameplayTag GetItemType() const;
	int32 GetStackCount() const;
	bool IsStackable() const;

	int32 GetPreviousGridIndex() const;
	/** 拖拽中的占地尺寸：**已含朝向**（旋转时 X/Y 互换），高亮/落点/挤压全按这个算。 */
	FIntPoint GetGridDimensions() const;
	UINV_InventoryItem* GetInventoryItem() const;
	EINV_ItemDragSource GetDragSource() const;

	/** 拖拽中的临时朝向。按 R 切换；落位时才写回物品（服务器权威）。 */
	bool IsRotated() const { return bRotated; }
	void SetRotated(bool bInRotated) { bRotated = bInRotated; }
	/** 只有在物品非正方形时才真正翻转；返回是否发生了变化（供调用方决定要不要重算高亮）。 */
	bool ToggleRotated();

	/**
	 * 拖拽跟随图元（UDragDropOperation::DefaultDragVisual）。
	 * 存一份弱引用只为在按 R 时能把跟随图标一起转过去 —— 否则玩家看到的是
	 * 「格子高亮转成 2x3 了，但手里那张图还是 3x2」，操作反馈对不上。
	 */
	void SetDragVisual(UWidget* InVisual);
	UWidget* GetDragVisual() const;

	void SetInventoryItem(UINV_InventoryItem* Item);
	void SetPreviousGridIndex(int32 Index);
	/** 传**未旋转**的原始尺寸（朝向由 bRotated 决定）。 */
	void SetGridDimensions(const FIntPoint& Dimensions);
	void SetStackCount(int32 Count);
	void SetIsStackable(bool bStackable);
	void SetDragSource(EINV_ItemDragSource Source);

private:
	TWeakObjectPtr<UINV_InventoryItem> InventoryItem;

	TWeakObjectPtr<UWidget> DragVisual;

	int32 PreviousGridIndex = INDEX_NONE;

	// 原始（未旋转）尺寸
	FIntPoint GridDimensions{0,0};

	bool bRotated = false;

	int32 StackCount = 0;

	bool bIsStackable = false;

	EINV_ItemDragSource DragSource = EINV_ItemDragSource::None;
};
