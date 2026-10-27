#include "Widgets/Inventory/SlottedItems/INV_EquippedSlottedItem.h"

#include "Blueprint/DragDropOperation.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

/**
FReply UINV_EquippedSlottedItem::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled().DetectDrag(TakeWidget(),EKeys::LeftMouseButton);
}

void UINV_EquippedSlottedItem::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
}
*/

bool UINV_EquippedSlottedItem::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                                            UDragDropOperation* InOperation)
{
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	OnEquippedSlottedItemDropped.Broadcast(this, Payload);
	return true;
}
