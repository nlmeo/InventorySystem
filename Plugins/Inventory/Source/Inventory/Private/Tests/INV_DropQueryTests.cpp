#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/GridSlots/INV_GridSlot.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Items/INV_InventoryItem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_DropQueryLifecycleTest, "Inventory.DropQuery.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_DropQueryLifecycleTest::RunTest(const FString& Parameters)
{
	UINV_InventoryGrid* Grid = NewObject<UINV_InventoryGrid>();
	UINV_InventoryItem* Item = NewObject<UINV_InventoryItem>();
	UINV_InventoryItem* OtherItem = NewObject<UINV_InventoryItem>();
	Grid->GridSlots.Add(NewObject<UINV_GridSlot>(Grid));
	Grid->Columns = 1;
	Grid->Rows = 1;
	auto SeedQuery = [&]()
	{
		Grid->ItemDropIndex = 0;
		FINV_SpaceQueryResult& Query = Grid->CurrentQueryResult;
		Query.bHasSpace = true;
		Query.bIsOverlap = true;
		Query.CurrentItem = Item;
		Query.OverlapIndices.Add(0);
		Query.ClickedIndices.Add(0);
		Query.OriginIndices.Add(0);
		Query.ValidateIndices.Add(0);
		Query.EmptyIndices.Add(0);
		Query.SpaceItemsMap.Add(Item, 0);
	};
	auto CheckInvalidated = [&](const TCHAR* Context)
	{
		const FINV_SpaceQueryResult& Query = Grid->CurrentQueryResult;
		TestTrue(Context, Grid->ItemDropIndex == INDEX_NONE && !Query.bHasSpace
			&& !Query.bIsOverlap && !Query.CurrentItem.IsValid()
			&& Query.OverlapIndices.IsEmpty() && Query.ClickedIndices.IsEmpty()
			&& Query.OriginIndices.IsEmpty() && Query.ValidateIndices.IsEmpty()
			&& Query.EmptyIndices.IsEmpty() && Query.SpaceItemsMap.IsEmpty());
	};

	SeedQuery();
	Grid->ClearHoverItem();
	CheckInvalidated(TEXT("Cancel clears query even without a payload"));
	SeedQuery();
	Grid->ClearExternalDragPayload();
	CheckInvalidated(TEXT("External cleanup clears query even without a payload"));
	SeedQuery();
	Grid->bMouseWithinCanvas = true;
	Grid->OnTileParametersUpdate(FINV_TileParameters());
	CheckInvalidated(TEXT("No active item cannot retain the previous query"));
	SeedQuery();
	Grid->bMouseWithinCanvas = false;
	Grid->UpdateTileParameters(FVector2D::ZeroVector, FVector2D::ZeroVector);
	CheckInvalidated(TEXT("Outside canvas update clears query"));
	SeedQuery();
	Grid->bMouseWithinCanvas = true;
	TestTrue(TEXT("Cursor exit detected"), Grid->CursorExitedCanvas(
		FVector2D::ZeroVector, FVector2D(100, 100), FVector2D(200, 200)));
	CheckInvalidated(TEXT("Cursor exit clears query"));

	Grid->ItemPayload = NewObject<UINV_ItemPayload>(Grid);
	Grid->ItemPayload->SetInventoryItem(Item);
	Grid->ItemPayload->SetDragSource(EINV_ItemDragSource::InventoryGrid);
	SeedQuery();
	TestTrue(TEXT("Matching item and valid index allow query use"), Grid->IsDropQueryCurrent());
	Grid->CurrentQueryResult.CurrentItem = OtherItem;
	TestFalse(TEXT("Another item's query is rejected"), Grid->IsDropQueryCurrent());
	Grid->PlaceHoverItemAtDropIndex();
	CheckInvalidated(TEXT("Rejected placement discards stale query"));
	SeedQuery();
	Grid->ItemDropIndex = 1;
	TestFalse(TEXT("Out-of-range landing is rejected"), Grid->IsDropQueryCurrent());

	UINV_ItemPayload* Incoming = NewObject<UINV_ItemPayload>(Grid);
	Incoming->SetInventoryItem(OtherItem);
	Incoming->SetDragSource(EINV_ItemDragSource::InventoryGrid);
	SeedQuery();
	TestFalse(TEXT("Drop for another item is rejected"), Grid->RefreshDropQuery(Incoming));
	CheckInvalidated(TEXT("Mismatched drop clears query"));
	SeedQuery();
	TestFalse(TEXT("Missing drop payload is rejected"), Grid->RefreshDropQuery(nullptr));
	CheckInvalidated(TEXT("Missing payload clears query"));

	SeedQuery();
	Grid->bProcessingDrop = true;
	Grid->ClearHoverItem();
	TestTrue(TEXT("Batch step preserves query after clearing its payload"),
		Grid->CurrentQueryResult.bHasSpace && Grid->CurrentQueryResult.SpaceItemsMap.Num() == 1
		&& Grid->ItemDropIndex == 0);
	Grid->ClearExternalDragPayload();
	TestTrue(TEXT("Nested cleanup preserves batch query"), Grid->CurrentQueryResult.bHasSpace);
	Grid->bProcessingDrop = false;
	Grid->InvalidateDropQuery();
	CheckInvalidated(TEXT("End of batch invalidates query"));

	SeedQuery();
	Grid->OnGridSlotDropped(0, nullptr);
	CheckInvalidated(TEXT("Grid drop early return always clears query"));
	TestFalse(TEXT("Grid drop restores processing flag"), Grid->bProcessingDrop);
	SeedQuery();
	Grid->OnSlottedItemDropped(0, nullptr);
	CheckInvalidated(TEXT("Icon drop early return always clears query"));
	TestFalse(TEXT("Icon drop restores processing flag"), Grid->bProcessingDrop);

	UDragDropOperation* Operation = NewObject<UDragDropOperation>();
	UINV_ItemPayload* CancelledPayload = NewObject<UINV_ItemPayload>(Operation);
	CancelledPayload->SetInventoryItem(Item);
	Operation->Payload = CancelledPayload;
	Grid->ItemPayload = NewObject<UINV_ItemPayload>(Grid);
	Grid->ItemPayload->SetInventoryItem(Item);
	Grid->BindDragCancellation(CancelledPayload);
	Grid->BindDragCancellation(CancelledPayload);
	SeedQuery();
	Operation->OnDragCancelled.Broadcast(Operation);
	CheckInvalidated(TEXT("Engine cancellation event clears query"));
	TestFalse(TEXT("Cancelled drag cannot regenerate query on next tick"), Grid->HasHoverItem());
	return true;
}

#endif
