#pragma once

#include "CoreMinimal.h"
#include "Types/INV_InteractionTypes.h"

class UINV_InventoryComponent;

struct INVENTORY_API FINV_CrossContainerSwapPlanner
{
	static bool BuildPlan(UINV_InventoryComponent* SourceInventory,
		UINV_InventoryItem* DraggedItem, const TArray<UINV_InventoryItem*>& CoveredItems,
		TArray<FINV_CrossContainerSwapEntry>& OutEntries);
};
