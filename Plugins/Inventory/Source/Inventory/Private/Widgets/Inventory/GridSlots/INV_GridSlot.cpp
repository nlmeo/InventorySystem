#include "Widgets/Inventory/GridSlots/INV_GridSlot.h"

#include "Blueprint/DragDropOperation.h"
#include "Widgets/ItemPopUp/INV_ItemPopUp.h"
#include "Items/INV_InventoryItem.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Components/Image.h"

void UINV_GridSlot::NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	Super::NativeOnMouseEnter(Geometry, MouseEvent);
	
	GridSlotHovered.Broadcast(TileIndex, MouseEvent);
	
}

void UINV_GridSlot::NativeOnMouseLeave(const FPointerEvent& MouseEvent)
{
	Super::NativeOnMouseLeave(MouseEvent);
	
	GridSlotUnhovered.Broadcast(TileIndex, MouseEvent);
}

bool UINV_GridSlot::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                                 UDragDropOperation* InOperation)
{
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	GridSlotDropped.Broadcast(TileIndex, Payload);
	return true;
}

void UINV_GridSlot::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	if (IsValid(Payload))
	{
		GridSlotDragEnter.Broadcast(TileIndex, Payload);
	}
}

void UINV_GridSlot::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);

	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	if (IsValid(Payload))
	{
		GridSlotDragLeave.Broadcast(TileIndex, Payload);
	}
}

//FReply UINV_GridSlot::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
//{
//	GridSlotClicked.Broadcast(TileIndex, MouseEvent);
//	return FReply::Handled();
//}

void UINV_GridSlot::SetInventoryItem(UINV_InventoryItem* Item)
{
	InventoryItem = Item;
}

void UINV_GridSlot::SetItemPopUp(UINV_ItemPopUp* PopUp)
{
	ItemPopUp = PopUp;
	//ItemPopUp->SetGridIndex(GetTileIndex());
	ItemPopUp->OnNativeDestruct.AddUObject(this, &ThisClass::OnItemPopUpDestruct);
}

UINV_ItemPopUp* UINV_GridSlot::GetItemPopUp() const
{
	return ItemPopUp.Get();
}

void UINV_GridSlot::SetOccupiedTexture()
{
	GridSlotState = EINV_GridSlotState::Occupied;
	Image_GridSlot->SetBrush(Brush_Occupied);
}

void UINV_GridSlot::SetUnoccupiedTexture()
{
	GridSlotState = EINV_GridSlotState::Unoccupied;
	Image_GridSlot->SetBrush(Brush_Unoccupied);
}

void UINV_GridSlot::SetSelectedTexture()
{
	GridSlotState = EINV_GridSlotState::Selected;
	Image_GridSlot->SetBrush(Brush_Selected);
}

void UINV_GridSlot::SetGrayedOutTexture()
{
	GridSlotState = EINV_GridSlotState::GrayedOut;
	Image_GridSlot->SetBrush(Brush_GrayedOut);
}

void UINV_GridSlot::OnItemPopUpDestruct(UUserWidget* Menu)
{
	ItemPopUp.Reset();
}


