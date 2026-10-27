#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "INV_SlottedItem.generated.h"

class UTextBlock;
class UImage;
class UINV_InventoryItem;
class UINV_ItemPayload;


//DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSlottedItemClicked, int32, GridIndex, const FPointerEvent&, MouseEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSlottedItemClicked, int32, GridIndex, const FPointerEvent&, MouseEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSlottedItemDoubleClicked, int32, GridIndex);
// 拖拽开始：带上 Payload —— 网格需要拿到「拖拽跟随图元」的引用，
// 否则按 R 旋转时只能转格子高亮，手里那张图不会跟着转。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSlottedItemDragDroppedEvent, int32, GridIndex, UINV_ItemPayload*, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSlottedItemDroppedEvent, int32, GridIndex, UINV_ItemPayload*, Payload);

UCLASS()
class INVENTORY_API UINV_SlottedItem : public UUserWidget
{
	GENERATED_BODY()
public:
	
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	
	
	
	void SetIsStackable(bool bStackable) { bIsStackable = bStackable; }
	bool IsStackable() const { return bIsStackable; }
	UImage* GetImageIcon() const {return Image_Icon; }
	void SetGridIndex(int32 Index) { GridIndex = Index; }
	int32 GetGridIndex() const { return GridIndex; }
	void SetGridDimensions(const FIntPoint& Dimensions) { GridDimensions = Dimensions; }
	FIntPoint GetGridDimensions() const { return GridDimensions; }
	void SetInventoryItem(UINV_InventoryItem* Item);
	UINV_InventoryItem* GetInventoryItem() const { return InventoryItem.Get(); }
	void SetImageBrush(const FSlateBrush& Brush) const;
	/**
	 * 朝向表现：true = 顺时针 90°。
	 *
	 * **只旋转 `Image_Icon` 自身**（pivot 设为中心），**不改变任何布局尺寸**：
	 * 布局尺寸由 `UINV_InventoryGrid::AddSlottedItemToCanvas` 用 `UCanvasPanelSlot` 显式给
	 * 「未旋转尺寸」+ 居中偏移。两层分开 —— 布局按未旋转走保证图案比例，渲染按 90° 走保证铺满占地。
	 *
	 * **不要在这里设 Slot 对齐**：Overlay / SizeBox / Border 这类约束容器会把子控件夹在
	 * 父给的可用空间内，实测 108x162 的图在 108 高的容器里被压成 108x108（玩家看到的「图标被压缩」）。
	 */
	void SetRotationVisual(bool bInRotated);
	void UpdateStackCount(int32 StackCount);
	
	// 子类可覆盖以标记拖拽来源（背包网格 vs 装备槽）
	virtual EINV_ItemDragSource GetDragSource() const { return EINV_ItemDragSource::InventoryGrid; }

	//FSlottedItemClicked OnSlottedItemClicked;
	FSlottedItemClicked OnSlottedItemClicked;
	// 左键双击：装备类 → 快速装备到对应槽，消耗类 → 快速使用
	FSlottedItemDoubleClicked OnSlottedItemDoubleClicked;
	FSlottedItemDragDroppedEvent OnSlottedItemDragStart;
	FSlottedItemDroppedEvent OnSlottedItemDropped;
private:
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Icon;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_StackCount;
	
	int32 GridIndex;
	FIntPoint GridDimensions;
	TWeakObjectPtr<UINV_InventoryItem> InventoryItem;
	bool bIsStackable{false};
};
