#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "INV_SlottedItem.h"
#include "INV_EquippedSlottedItem.generated.h"

class UINV_ItemPayload;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEquippedSlottedItemClicked, UINV_EquippedSlottedItem*, SlottedItem);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEquippedSlottedItemDropped, UINV_EquippedSlottedItem*, SlottedItem, UINV_ItemPayload*, Payload);


UCLASS()
class INVENTORY_API UINV_EquippedSlottedItem : public UINV_SlottedItem
{
	GENERATED_BODY()
public:
	virtual EINV_ItemDragSource GetDragSource() const override { return EINV_ItemDragSource::EquippedSlot; }
	//virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	
	void SetEquipmentTypeTag(const FGameplayTag& Tag) { EquipmentTypeTag = Tag; }
	FGameplayTag GetEquipmentTypeTag() const { return EquipmentTypeTag; }
	
	FEquippedSlottedItemClicked OnEquippedSlottedItemClicked;
	FEquippedSlottedItemDropped OnEquippedSlottedItemDropped;
private:
	UPROPERTY()
	FGameplayTag EquipmentTypeTag; 
};
