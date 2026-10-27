// Copyright 2025, ProjectXHG. All right reserved.

#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"

void UINV_GridLayoutAsset::AddNewSlot()
{
	UINV_GridSlotDefinitionWrapper* NewSlot = NewObject<UINV_GridSlotDefinitionWrapper>(this);

	if (!Slots.IsEmpty() && Slots.Last())
	{
		const FINV_GridSlotDefinition& LastSlot = Slots.Last()->SlotDefinition;

		// 格子大小是整份布局共用的（LayoutDefinition.TileSize），编辑期摆放新面板时也用它算间距
		NewSlot->SlotDefinition.PositionOffset = FVector2D(
			LastSlot.PositionOffset.X + LastSlot.SlotDimension.X * LayoutDefinition.TileSize + LayoutDefinition.SpaceBetweenElements,
			LastSlot.PositionOffset.Y);
	}

	Slots.Add(NewSlot);
}

void UINV_GridLayoutAsset::RemoveLastSlot()
{
	if (Slots.IsEmpty())
	{
		return;
	}

	Slots.RemoveAt(Slots.Num() - 1);
}

void UINV_GridLayoutAsset::EmptySlots()
{
	Slots.Empty();
}

const FINV_GridSlotDefinition* UINV_GridLayoutAsset::GetSlotDefinition(int32 Index) const
{
	return Slots.IsValidIndex(Index) ? &Slots[Index]->SlotDefinition : nullptr;
}
