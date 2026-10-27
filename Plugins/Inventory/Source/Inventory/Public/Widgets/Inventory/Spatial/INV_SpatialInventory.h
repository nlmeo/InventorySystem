#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/Inventory/InventoryBase/INV_InventoryBase.h"
#include "INV_SpatialInventory.generated.h"

class UINV_EquippedSlottedItem;
class UINV_SlottedItem;
class UINV_EquippedGridSlot;
class UINV_ItemDescription;
class UINV_ItemPayload;
class UCanvasPanel;
class UINV_InventoryGrid;
class UWidgetSwitcher;
class UButton;

UCLASS()
class INVENTORY_API UINV_SpatialInventory : public UINV_InventoryBase
{
	GENERATED_BODY()
public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnInitialized() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	
	virtual FINV_SlotAvailabilityResult HasRoomForItem(UINV_ItemComponent* ItemComponent) const override;
	virtual FGameplayTag FindEmptyEquipmentSlot(const FINV_ItemManifest& Manifest) const override;
	virtual void OnItemHovered(UINV_InventoryItem* Item) override;
	virtual void OnItemUnHovered() override;
	virtual bool HasHoverItem() const override;
	virtual UINV_ItemPayload* GetHoverItem() const override;
	virtual float GetTileSize() const override;
	virtual void OnInventoryClosed() override;

	/**
	 * 双击快速装备：把背包里的可装备物品直接装到类型匹配的装备槽上。
	 * 优先找空槽；没有空槽时选中第一个已装备的同类槽，走换装（旧装备回填到物品原格）。
	 * 返回 true 表示已经处理（无论成功与否由内部完成 UI 与 RPC 广播），false 表示该物品不适用。
	 */
	bool TryQuickEquipItem(UINV_InventoryItem* Item);

	/** Finds a valid player-grid destination for source double-click pickup. */
	bool FindQuickTransferDestination(UINV_InventoryItem* Item, FName& OutSlotId,
		int32& OutIndex, bool& bOutRotated) const;
	void ClearDragInteractionState();
private:
	friend class FINV_EquippedDropTest;
	bool HandleBackgroundDrop(UINV_ItemPayload* Payload, bool bOverGridPage);
	
	TArray<TObjectPtr<UINV_EquippedGridSlot>> EquippedGridSlots;

	void HandleInventoryRefreshRequested();
	void RefreshEquipmentSlots();
	UFUNCTION()
	void OnInventoryEntryAdded(UINV_InventoryItem* Item);
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UINV_InventoryGrid> Grid_Equippables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UINV_InventoryGrid> Grid_Consumables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UINV_InventoryGrid> Grid_Craftables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Equippables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Consumables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Craftables;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UINV_ItemDescription> ItemDescriptionClass;
	
	UPROPERTY()
	TObjectPtr<UINV_ItemDescription> ItemDescription;
	
	UINV_ItemDescription* GetItemDescription();
	
	FTimerHandle DescriptionTimer;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DescriptionTimerDelay{0.5f};
	
	UFUNCTION()
	void ShowEquippables();

	UFUNCTION()
	void ShowConsumables();

	UFUNCTION()
	void ShowCraftables();

	UFUNCTION() // 装备槽放下（背包物品→装备，或装备→装备换装）
	void EquippedGridSlotClicked(UINV_EquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTypeTag, UINV_ItemPayload* Payload);
	
	UFUNCTION() // 开始拖拽装备槽里的物品 → 立即切到装备页签
	void OnEquippedSlotDragStarted(UINV_EquippedGridSlot* EquippedGridSlot);

	UFUNCTION() // 双击装备槽里的物品 → 快速卸下到装备网格
	void OnEquippedSlotDoubleClicked(UINV_EquippedGridSlot* EquippedGridSlot);

	// SourceGrid = 实际接收放下的网格（可能是消耗品/材料网格，此时 GridIndex 对装备网格无意义）
	UFUNCTION() // 装备物品拖回背包网格 → 卸下
	void EquippedItemDroppedOnGrid(UINV_InventoryGrid* SourceGrid, UINV_ItemPayload* Payload, int32 GridIndex);

	UFUNCTION() // 装备物品拖到另一个已装备物品上 → 换装
	void EquippedSlottedItemDropped(UINV_EquippedSlottedItem* TargetSlottedItem, UINV_ItemPayload* Payload);
	
	void DisableButton(UButton* Button);
	void SetActiveGrid(UINV_InventoryGrid* Grid, UButton* Button);
	void SetItemDescriptionSizeAndPosition(UINV_ItemDescription* Description, UCanvasPanel* Canvas) const;
	
	UINV_EquippedGridSlot* FindSlotWithEquippedItem(UINV_InventoryItem* EquippedItem) const;

	/** 查找物品当前所在的网格（三个网格中任一），找不到返回 nullptr */
	UINV_InventoryGrid* FindGridContainingItem(const UINV_InventoryItem* Item) const;

	void ClearSlotOfItem(UINV_EquippedGridSlot* EquippedGridSlot, bool bUpdateItemState = true);
	void RemoveEquippedSlottedItem(UINV_EquippedSlottedItem* EquippedSlottedItem);

	/** 槽位校验：物品能否放进该 EquipmentTypeTag 对应的装备槽（非堆叠 / 可装备 / 类型匹配） */
	bool CanEquipItemToSlot(const UINV_InventoryItem* Item, const FGameplayTag& SlotEquipmentTypeTag) const;

	/**
	 * 卸下到装备网格的第一个可用位置（兼容路径）。
	 * 用于「鼠标位置无意义」的场景：拖拽时下方显示的是消耗品/材料网格，
	 * GridIndex 属于那个网格的坐标系，不能拿来在装备网格里定位。
	 * 装备网格放不下时返回 false，且不改动任何状态（物品保持装备）。
	 */
	bool UnloadItemToEquippablesGrid(UINV_EquippedGridSlot* EquippedGridSlot, UINV_InventoryItem* ItemToUnequip);

	/** 把物品显示到槽位上（创建图标 + 占位状态），失败返回 nullptr */
	UINV_EquippedSlottedItem* PlaceItemInSlot(UINV_EquippedGridSlot* EquippedSlot, UINV_InventoryItem* Item,
		bool bUpdateItemState = true);

	/** 清空槽位：移除图标并恢复未占用外观 */
	void ClearEquippedSlot(UINV_EquippedGridSlot* EquippedSlot, bool bUpdateItemState = true);

	// 清理所有网格的悬持/外部拖拽缓存。拖拽结束（成功或失败）都必须调用，
	// 否则残留的 Payload 会在开关背包时被 PutHoverItemBack 再次放进网格，造成物品重复。
	void ResetDragStates();
	void MakeEquippedSlottedItem(UINV_EquippedSlottedItem* EquippedSlottedItem, UINV_EquippedGridSlot* EquippedGridSlot, UINV_InventoryItem* ItemToEquip);
	void BroadcastSlotClickedDelegates(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToUnequip,
		FName ReturnSlotId = NAME_None, int32 ReturnIndex = INDEX_NONE, bool bReturnRotated = false,
		FGameplayTag EquipmentSlotTypeOverride = FGameplayTag()) const;

	/** 装备槽之间移动 / 互换：两件物品都仍在装备状态，只是换了槽位 */
	void BroadcastEquippedItemMoved(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToSwap) const;
	
	TWeakObjectPtr<UINV_InventoryGrid> ActiveGrid;
	
};
