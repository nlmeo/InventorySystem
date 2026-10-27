#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "INV_GridSlot.h"
#include "INV_EquippedGridSlot.generated.h"

class UOverlay;
class UINV_EquippedSlottedItem;
class UINV_ItemPayload;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEquippedGridSlotDropped, UINV_EquippedGridSlot*, GridSlot,
	const FGameplayTag&, EquipmentTypeTag, UINV_ItemPayload*, Payload);

// 装备槽里的物品开始被拖拽（拖拽刚发生、还没放下）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEquippedSlotDragStarted, UINV_EquippedGridSlot*, EquippedGridSlot);

// 双击装备槽里的物品（快速卸下）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEquippedSlotDoubleClicked, UINV_EquippedGridSlot*, EquippedGridSlot);

class UImage;
UCLASS()
class INVENTORY_API UINV_EquippedGridSlot : public UINV_GridSlot
{
	GENERATED_BODY()
public:
	virtual void NativeTick(const FGeometry& Geometry, float InDeltaTime) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	
	UINV_EquippedSlottedItem* OnItemEquipped(UINV_InventoryItem* Item, const FGameplayTag& EquipmentTag, float TileSize);
	void SetEquippedSlottedItem(UINV_EquippedSlottedItem* Item) {EquippedSlottedItem = Item;}

	/**
	 * 装备物品的图标覆盖在槽位上方，拖放时会先命中图标而吃不到槽自身的 Drop。
	 * 所有由 OnItemEquipped 创建出来的图标都会绑定到这里，统一转发成槽位放下事件，
	 * 保证「拖到已装备的槽」也能进入换装 / 互换逻辑。
	 */
	UFUNCTION()
	void HandleEquippedSlottedItemDropped(UINV_EquippedSlottedItem* SlottedItem, UINV_ItemPayload* Payload);

	/**
	 * 装备图标被拖拽时转发到槽位。
	 * 上层据此在拖拽「一开始」就把下方网格切到装备页签——装备的归属网格是装备网格，
	 * 停在消耗品/材料页签时没有可放置的位置，等到放下再切就晚了。
	 */
	UFUNCTION()
	void HandleEquippedSlottedItemDragStart(int32 GridIndex, UINV_ItemPayload* Payload);

	/** 装备图标被双击时转发到槽位（快速卸下） */
	UFUNCTION()
	void HandleEquippedSlottedItemDoubleClick(int32 GridIndex);

	UINV_EquippedSlottedItem* GetEquippedSlottedItem() const {return EquippedSlottedItem;}
	FGameplayTag GetEquipmentTypeTag() const { return EquipmentTypeTag; }
	
	virtual void SetOccupiedTexture() override;
	virtual void SetUnoccupiedTexture() override;
	//virtual void SetSelectedTexture() override;
	//virtual void SetGrayedOutTexture() override;
	
	FEquippedGridSlotDropped OnEquippedGridSlotDropped;
	FEquippedSlotDragStarted OnEquippedSlotDragStarted;
	FEquippedSlotDoubleClicked OnEquippedSlotDoubleClicked;
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories = "GameItems.Equipment"))
	FGameplayTag EquipmentTypeTag;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_GrayedOutIcon;

	UPROPERTY(EditAnywhere,Category = "Inventory")
	TSubclassOf<UINV_EquippedSlottedItem> EquippedSlottedItemClass;
	
	UPROPERTY()
	TObjectPtr<UINV_EquippedSlottedItem> EquippedSlottedItem;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> Overlay_Root;
	
	
};
