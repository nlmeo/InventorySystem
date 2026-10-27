#include "Interaction/INV_InventoryInteractionSubsystem.h"

#include "InventoryManagement/Components/INV_InventoryComponent.h"

bool UINV_InventoryInteractionSubsystem::BeginInventorySource(AActor* SourceActor,
	UINV_InventoryComponent* SourceInventory)
{
	if (!IsValid(SourceActor) || !IsValid(SourceInventory)) return false;
	if (SourceInventory->GetOwner() != SourceActor) return false;

	if (ActiveSourceActor == SourceActor && ActiveSourceInventory == SourceInventory) return true;
	EndInventorySource();
	ActiveSourceActor = SourceActor;
	ActiveSourceInventory = SourceInventory;
	OnInventorySourceStarted.Broadcast(SourceActor, SourceInventory);
	return true;
}

void UINV_InventoryInteractionSubsystem::EndInventorySource()
{
	if (!HasActiveInventorySource())
	{
		ActiveSourceActor.Reset();
		ActiveSourceInventory.Reset();
		return;
	}
	ActiveSourceActor.Reset();
	ActiveSourceInventory.Reset();
	OnInventorySourceEnded.Broadcast();
}
