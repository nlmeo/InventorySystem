#include "InventoryManagement/Transfer/INV_CrossContainerSwapPlanner.h"

#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutTypes.h"
#include "Items/INV_InventoryItem.h"

bool FINV_CrossContainerSwapPlanner::BuildPlan(UINV_InventoryComponent* SourceInventory,
	UINV_InventoryItem* DraggedItem, const TArray<UINV_InventoryItem*>& CoveredItems,
	TArray<FINV_CrossContainerSwapEntry>& OutEntries)
{
	OutEntries.Reset();
	if (!IsValid(SourceInventory) || !IsValid(DraggedItem) || CoveredItems.IsEmpty()) return false;
	TArray<const FINV_GridSlotDefinition*> Panels;
	SourceInventory->GetAllGridPanels(Panels);
	TArray<FName> PlannedSlots;
	TArray<int32> PlannedIndices;
	TArray<FIntPoint> PlannedDimensions;
	auto OverlapsPlanned = [&](FName SlotId, int32 Index, const FIntPoint& Dimensions)
	{
		const FIntPoint Size = SourceInventory->GetGridSize(SlotId);
		const FIntPoint Origin(Index % Size.X, Index / Size.X);
		for (int32 I = 0; I < PlannedSlots.Num(); ++I)
		{
			if (PlannedSlots[I] != SlotId) continue;
			const FIntPoint OtherOrigin(PlannedIndices[I] % Size.X, PlannedIndices[I] / Size.X);
			const FIntPoint OtherDim = PlannedDimensions[I];
			if (Origin.X < OtherOrigin.X + OtherDim.X && OtherOrigin.X < Origin.X + Dimensions.X
				&& Origin.Y < OtherOrigin.Y + OtherDim.Y && OtherOrigin.Y < Origin.Y + Dimensions.Y) return true;
		}
		return false;
	};
	for (UINV_InventoryItem* Covered : CoveredItems)
	{
		if (!IsValid(Covered)) return false;
		bool bFound = false;
		const FIntPoint BaseDim = Covered->GetBaseDimension();
		for (const FINV_GridSlotDefinition* Panel : Panels)
		{
			if (!Panel || Panel->ContainerType != EINV_GridContainerType::Grid
				|| !INV_AcceptsItemCategory(Panel->AcceptedCategory, Covered->GetItemManifest().GetItemCategory())) continue;
			const FIntPoint GridSize = SourceInventory->GetGridSize(Panel->SlotId);
			for (int32 Candidate = 0; Candidate < GridSize.X * GridSize.Y && !bFound; ++Candidate)
			{
				for (int32 Rotation = 0; Rotation < (Covered->CanRotate() ? 2 : 1) && !bFound; ++Rotation)
				{
					const bool bRotated = Rotation == 0 ? Covered->IsRotated() : !Covered->IsRotated();
					const FIntPoint Dim = bRotated ? FIntPoint(BaseDim.Y, BaseDim.X) : BaseDim;
					const FIntPoint Origin(Candidate % GridSize.X, Candidate / GridSize.X);
					if (Origin.X + Dim.X > GridSize.X || Origin.Y + Dim.Y > GridSize.Y
						|| OverlapsPlanned(Panel->SlotId, Candidate, Dim)) continue;
					bool bBlocked = false;
					for (UINV_InventoryItem* Other : SourceInventory->GetAllInventoryItems())
					{
						if (!IsValid(Other) || Other == DraggedItem || Other->GetContainerId() != Panel->SlotId
							|| Other->GetGridIndex() == INDEX_NONE) continue;
						const FIntPoint OtherOrigin(Other->GetGridIndex() % GridSize.X, Other->GetGridIndex() / GridSize.X);
						const FIntPoint OtherDim = Other->GetItemDimension();
						if (Origin.X < OtherOrigin.X + OtherDim.X && OtherOrigin.X < Origin.X + Dim.X
							&& Origin.Y < OtherOrigin.Y + OtherDim.Y && OtherOrigin.Y < Origin.Y + Dim.Y) { bBlocked = true; break; }
					}
					if (bBlocked) continue;
					FINV_CrossContainerSwapEntry& Entry = OutEntries.AddDefaulted_GetRef();
					Entry.Item = Covered;
					Entry.TargetSlotId = Panel->SlotId;
					Entry.TargetIndex = Candidate;
					Entry.bRotated = bRotated;
					PlannedSlots.Add(Panel->SlotId);
					PlannedIndices.Add(Candidate);
					PlannedDimensions.Add(Dim);
					bFound = true;
				}
			}
			if (bFound) break;
		}
		if (!bFound) { OutEntries.Reset(); return false; }
	}
	return OutEntries.Num() == CoveredItems.Num();
}
