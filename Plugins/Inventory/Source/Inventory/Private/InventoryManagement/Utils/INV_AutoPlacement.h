#pragma once

#include "CoreMinimal.h"

// Shared geometry for authoritative pickup placement and its automation tests.
struct FINV_AutoPlacementGrid
{
	FName SlotId;
	FIntPoint Size = FIntPoint::ZeroValue;
	TArray<FIntRect> Occupied;
};

struct FINV_AutoPlacement
{
	FName SlotId = NAME_None;
	int32 Index = INDEX_NONE;
	bool bRotated = false;
};

inline bool INV_FindAutoPlacement(const TArray<FINV_AutoPlacementGrid>& Grids,
	const FIntPoint BaseSize, const bool bCurrentRotation, FINV_AutoPlacement& OutPlacement)
{
	OutPlacement = FINV_AutoPlacement();
	if (BaseSize.X <= 0 || BaseSize.Y <= 0) return false;
	const int32 Orientations = BaseSize.X == BaseSize.Y ? 1 : 2;
	// Exhaust the current direction across all containers before rotating.
	for (int32 Direction = 0; Direction < Orientations; ++Direction)
	{
		const bool bRotated = Direction == 0 ? bCurrentRotation : !bCurrentRotation;
		const FIntPoint Size = bRotated ? FIntPoint(BaseSize.Y, BaseSize.X) : BaseSize;
		for (const FINV_AutoPlacementGrid& Grid : Grids)
		{
			if (Grid.SlotId.IsNone() || Grid.Size.X < Size.X || Grid.Size.Y < Size.Y
				|| static_cast<int64>(Grid.Size.X) * Grid.Size.Y > MAX_int32) continue;
			for (int32 Y = 0; Y <= Grid.Size.Y - Size.Y; ++Y)
			{
				for (int32 X = 0; X <= Grid.Size.X - Size.X; ++X)
				{
					const bool bBlocked = Grid.Occupied.ContainsByPredicate([&](const FIntRect& Rect)
					{
						return X < Rect.Max.X && X + Size.X > Rect.Min.X
							&& Y < Rect.Max.Y && Y + Size.Y > Rect.Min.Y;
					});
					if (bBlocked) continue;
					OutPlacement.SlotId = Grid.SlotId;
					OutPlacement.Index = Y * Grid.Size.X + X;
					OutPlacement.bRotated = bRotated;
					return true;
				}
			}
		}
	}
	return false;
}
