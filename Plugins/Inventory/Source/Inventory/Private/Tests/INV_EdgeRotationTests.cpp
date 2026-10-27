#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/Image.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "Items/Fragments/INV_FragmentTags.h"
#include "UObject/UnrealType.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/GridSlots/INV_GridSlot.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_EdgeRotationTest, "Inventory.Drag.EdgeRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_EdgeRotationTest::RunTest(const FString& Parameters)
{
	auto MakeGrid = [](FIntPoint Size, FIntPoint BaseSize, bool bRotated = false)
	{
		UINV_InventoryGrid* Grid = NewObject<UINV_InventoryGrid>();
		Grid->Columns = Size.X;
		Grid->Rows = Size.Y;
		Grid->TileSize = 40.f;
		Grid->bGridLayoutResolved = true;
		Grid->ResolvedCategory = EINV_ItemCategory::Consumable;
		Grid->bMouseWithinCanvas = true;
		for (int32 Index = 0; Index < Size.X * Size.Y; ++Index)
		{
			UINV_GridSlot* Slot = NewObject<UINV_GridSlot>(Grid);
			Slot->SetTileIndex(Index);
			FindFProperty<FObjectPropertyBase>(UINV_GridSlot::StaticClass(), TEXT("Image_GridSlot"))
				->SetObjectPropertyValue_InContainer(Slot, NewObject<UImage>(Slot));
			Slot->SetUnoccupiedTexture();
			Grid->GridSlots.Add(Slot);
		}
		FINV_ItemManifest Manifest;
		FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		Category->GetUnderlyingProperty()->SetIntPropertyValue(Category->ContainerPtrToValuePtr<void>(&Manifest),
			static_cast<int64>(EINV_ItemCategory::Consumable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = GameItems::Consumables::Potions::Red::Small;
		FINV_GridFragment Fragment;
		Fragment.SetGridSize(BaseSize);
		Fragment.SetFragmentTag(FragmentTags::GridFragment);
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(Fragment));
		UINV_InventoryItem* Item = Manifest.Manifest(Grid);
		Item->SetRotated(bRotated);
		Grid->ExternalDragPayload = NewObject<UINV_ItemPayload>(Grid);
		Grid->ExternalDragPayload->InitByItem(Item);
		Grid->ExternalDragPayload->SetDragSource(EINV_ItemDragSource::EquippedSlot);
		UImage* Ghost = NewObject<UImage>(Grid);
		Ghost->SetRenderTransformAngle(bRotated ? 90.f : 0.f);
		Grid->ExternalDragPayload->SetDragVisual(Ghost);
		return Grid;
	};
	auto Tile = [](int32 X, int32 Y, EINV_TileQuadrant Quadrant = EINV_TileQuadrant::TopLeft)
	{
		FINV_TileParameters Result;
		Result.TileCoordinates = FIntPoint(X, Y);
		Result.TileQuadrant = Quadrant;
		return Result;
	};
	auto BlockCell = [](UINV_InventoryGrid* Grid, int32 Index)
	{
		UINV_GridSlot* Slot = Grid->GridSlots[Index];
		Slot->SetInventoryItem(NewObject<UINV_InventoryItem>(Grid));
		Slot->SetUpperLeftIndex(Index);
		Slot->SetAvailable(false);
	};

	const FINV_TileParameters Corners[] = {
		Tile(0, 0), Tile(1, 0, EINV_TileQuadrant::TopRight),
		Tile(0, 2, EINV_TileQuadrant::BottomLeft), Tile(1, 2, EINV_TileQuadrant::BottomRight)};
	for (const FINV_TileParameters& Corner : Corners)
	{
		UINV_InventoryGrid* Grid = MakeGrid(FIntPoint(2, 3), FIntPoint(3, 2));
		Grid->OnTileParametersUpdate(Corner);
		TestTrue(TEXT("Corner selects rotated footprint"), Grid->GetActivePayload()->IsRotated());
		TestTrue(TEXT("Corner preview allows drop"), Grid->IsDropQueryCurrent());
		TestEqual(TEXT("Corner footprint is clamped into grid"), Grid->ItemDropIndex, 0);
		TestEqual(TEXT("Query contains complete selected footprint"), Grid->CurrentQueryResult.ClickedIndices.Num(), 6);
		for (const UINV_GridSlot* Slot : Grid->GridSlots)
			TestTrue(TEXT("Every footprint cell is highlighted"), Slot->GetGridSlotState() == EINV_GridSlotState::Selected);
		TestEqual(TEXT("Drag visual rotates with preview"), Grid->GetActivePayload()->GetDragVisual()->GetRenderTransform().Angle, 90.f);
		TestFalse(TEXT("Preview does not rotate authoritative item"), Grid->GetActivePayload()->GetInventoryItem()->IsRotated());
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			TestTrue(TEXT("Repeated query and drop refresh stay valid"), Grid->RefreshPlacementPreview(Corner));
			TestTrue(TEXT("Repeated query does not oscillate rotation"), Grid->GetActivePayload()->IsRotated());
			TestEqual(TEXT("Repeated query preserves index"), Grid->ItemDropIndex, 0);
		}
		Grid->bMouseWithinCanvas = false;
		Grid->OnTileParametersUpdate(Corner);
		TestEqual(TEXT("Leaving grid invalidates drop index"), Grid->ItemDropIndex, INDEX_NONE);
		for (const UINV_GridSlot* Slot : Grid->GridSlots)
			TestTrue(TEXT("Leaving grid removes highlights"), Slot->GetGridSlotState() == EINV_GridSlotState::Unoccupied);
	}

	struct FEdgeCase { FIntPoint BaseSize; FINV_TileParameters Hover; int32 Index; };
	const FEdgeCase Edges[] = {
		{FIntPoint(3, 1), Tile(0, 2), 5}, {FIntPoint(3, 1), Tile(4, 2), 9},
		{FIntPoint(1, 3), Tile(2, 0), 1}, {FIntPoint(1, 3), Tile(2, 4), 21}};
	for (const FEdgeCase& Edge : Edges)
	{
		UINV_InventoryGrid* Grid = MakeGrid(FIntPoint(5, 5), Edge.BaseSize);
		TestTrue(TEXT("Edge chooses alternate orientation"), Grid->RefreshPlacementPreview(Edge.Hover));
		TestTrue(TEXT("Edge rotation is recorded"), Grid->GetActivePayload()->IsRotated());
		TestEqual(TEXT("Edge drop index matches mouse-centered rotated footprint"), Grid->ItemDropIndex, Edge.Index);
	}
	UINV_InventoryGrid* Rotated = MakeGrid(FIntPoint(3, 2), FIntPoint(3, 2), true);
	TestTrue(TEXT("Already rotated item can rotate back at corner"), Rotated->RefreshPlacementPreview(Tile(0, 0)));
	TestFalse(TEXT("Rotate-back preview chooses unrotated direction"), Rotated->GetActivePayload()->IsRotated());
	TestEqual(TEXT("Rotate-back updates visual"), Rotated->GetActivePayload()->GetDragVisual()->GetRenderTransform().Angle, 0.f);
	TestTrue(TEXT("Rotate-back leaves stored item unchanged"), Rotated->GetActivePayload()->GetInventoryItem()->IsRotated());

	UINV_InventoryGrid* Internal = MakeGrid(FIntPoint(5, 5), FIntPoint(3, 1));
	Internal->ItemPayload = Internal->ExternalDragPayload;
	Internal->ExternalDragPayload = nullptr;
	Internal->ItemPayload->SetDragSource(EINV_ItemDragSource::InventoryGrid);
	Internal->ItemPayload->SetPreviousGridIndex(0);
	UINV_InventoryItem* Stored = Internal->ItemPayload->GetInventoryItem();
	Stored->SetGridIndex(0);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Internal->GridSlots[Index]->SetInventoryItem(Stored);
		Internal->GridSlots[Index]->SetUpperLeftIndex(0);
		Internal->GridSlots[Index]->SetAvailable(false);
	}
	Internal->OnTileParametersUpdate(Tile(4, 2));
	TestTrue(TEXT("Internal inventory drag also rotates at edge"), Internal->IsDropQueryCurrent() && Internal->ItemPayload->IsRotated());
	TestEqual(TEXT("Internal source footprint retains old dimensions"), Internal->CurrentQueryResult.OriginIndices.Num(), 3);
	TestTrue(TEXT("Old source width is retained while preview rotates"), Internal->CurrentQueryResult.OriginIndices.Contains(2));
	TestFalse(TEXT("Preview does not rewrite stored orientation"), Stored->IsRotated());
	Internal->ClearHoverItem();
	TestFalse(TEXT("Cancelling auto-rotated drag leaves source unchanged"), Stored->IsRotated());
	TestTrue(TEXT("Cancelling leaves original source cells occupied"), Internal->GridSlots[2]->GetInventoryItem().Get() == Stored);
	TestEqual(TEXT("Cancelling discards preview index"), Internal->ItemDropIndex, INDEX_NONE);

	UINV_InventoryGrid* Current = MakeGrid(FIntPoint(5, 5), FIntPoint(3, 1));
	TestTrue(TEXT("Current orientation wins when it fits"), Current->RefreshPlacementPreview(Tile(2, 2)));
	TestFalse(TEXT("Valid orientation is not changed"), Current->GetActivePayload()->IsRotated());
	Current->GetActivePayload()->ToggleRotated();
	Current->UpdateDragVisualRotation();
	TestTrue(TEXT("Manual R orientation remains usable"), Current->RefreshPlacementPreview(Tile(2, 2)));
	TestTrue(TEXT("Manual valid rotation stays preferred"), Current->GetActivePayload()->IsRotated());
	TestFalse(TEXT("Negative X cannot wrap into previous row"), Current->CheckHoverPosition(FIntPoint(-1, 1), FIntPoint(1, 1)).bHasSpace);
	TestFalse(TEXT("Negative Y cannot pass bounds"), Current->CheckHoverPosition(FIntPoint(1, -1), FIntPoint(1, 1)).bHasSpace);

	UINV_InventoryGrid* Blocked = MakeGrid(FIntPoint(5, 5), FIntPoint(3, 1));
	BlockCell(Blocked, 12);
	Blocked->OnTileParametersUpdate(Tile(2, 2));
	TestFalse(TEXT("Both blocked orientations reject placement"), Blocked->CurrentQueryResult.bHasSpace);
	TestFalse(TEXT("Failed alternate trial restores payload rotation"), Blocked->GetActivePayload()->IsRotated());
	TestEqual(TEXT("Failed alternate trial leaves visual unchanged"), Blocked->GetActivePayload()->GetDragVisual()->GetRenderTransform().Angle, 0.f);
	TestEqual(TEXT("Failure stays near mouse instead of searching remote empty cells"), Blocked->ItemDropIndex, 11);
	TestTrue(TEXT("Failure shows grey footprint"), Blocked->GridSlots[11]->GetGridSlotState() == EINV_GridSlotState::GrayedOut);
	UINV_InventoryGrid* Alternate = MakeGrid(FIntPoint(5, 5), FIntPoint(3, 1));
	BlockCell(Alternate, 11);
	TestTrue(TEXT("Blocked current footprint can use free alternate footprint"), Alternate->RefreshPlacementPreview(Tile(2, 2)));
	TestTrue(TEXT("Successful alternate footprint rotates"), Alternate->GetActivePayload()->IsRotated());

	UINV_InventoryGrid* Oversized = MakeGrid(FIntPoint(2, 2), FIntPoint(4, 3));
	TestFalse(TEXT("Neither oversized direction fits"), Oversized->RefreshPlacementPreview(Tile(0, 0)));
	TestEqual(TEXT("Oversized failure has no valid index"), Oversized->ItemDropIndex, INDEX_NONE);
	UINV_InventoryGrid* Square = MakeGrid(FIntPoint(2, 2), FIntPoint(1, 1));
	TestTrue(TEXT("Square item can use corner"), Square->RefreshPlacementPreview(Tile(0, 0)));
	TestFalse(TEXT("Square item does not rotate"), Square->GetActivePayload()->IsRotated());
	TestFalse(TEXT("Uninitialized quadrant invalidates query"), Square->RefreshPlacementPreview(FINV_TileParameters()));
	TestEqual(TEXT("Invalid input discards previous index"), Square->ItemDropIndex, INDEX_NONE);
	UINV_InventoryGrid* WrongCategory = MakeGrid(FIntPoint(2, 3), FIntPoint(3, 2));
	WrongCategory->ResolvedCategory = EINV_ItemCategory::Craftable;
	TestFalse(TEXT("Auto rotation cannot bypass category restriction"), WrongCategory->RefreshPlacementPreview(Tile(0, 0)));
	TestFalse(TEXT("Category rejection restores orientation"), WrongCategory->GetActivePayload()->IsRotated());
	return true;
}

#endif
