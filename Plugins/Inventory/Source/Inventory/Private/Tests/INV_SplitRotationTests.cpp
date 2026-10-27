#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "Items/Fragments/INV_FragmentTags.h"
#include "UObject/UnrealType.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/GridSlots/INV_GridSlot.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_SplitRotationTest, "Inventory.Split.RotationPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_SplitRotationTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues InitialValues = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventorySplitTestWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &InitialValues);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	const FName Bag(TEXT("Bag"));
	auto MakeInventory = [&](FIntPoint Size)
	{
		AActor* Owner = World->SpawnActor<AActor>();
		UINV_InventoryComponent* Component = NewObject<UINV_InventoryComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		UINV_GridLayoutAsset* Layout = NewObject<UINV_GridLayoutAsset>(Component);
		UINV_GridSlotDefinitionWrapper* Panel = NewObject<UINV_GridSlotDefinitionWrapper>(Layout);
		Panel->SlotDefinition.SlotId = Bag;
		Panel->SlotDefinition.SlotDimension = Size;
		Panel->SlotDefinition.AcceptedCategory = EINV_ItemCategory::All;
		Layout->Slots.Add(Panel);
		Component->GridLayouts.Add(Layout);
		return Component;
	};
	auto AddItem = [&](UINV_InventoryComponent* Component, FIntPoint BaseSize, int32 Index,
		bool bRotated = false, int32 Count = 0)
	{
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
		if (Count > 0)
		{
			FINV_StackableFragment Stack;
			Stack.SetStackCount(Count);
			FindFProperty<FIntProperty>(FINV_StackableFragment::StaticStruct(), TEXT("MaxStacks"))
				->SetPropertyValue_InContainer(&Stack, 10);
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_StackableFragment>(Stack));
		}
		UINV_InventoryItem* Item = Manifest.Manifest(Component->GetOwner());
		Item->SetContainerId(Bag);
		Item->SetGridIndex(Index);
		Item->SetRotated(bRotated);
		Item->SetTotalStackCount(Count);
		Component->InventoryList.AddEntry(Item);
		Component->AddRepSubObj(Item);
		return Item;
	};
	auto MakeGrid = [&](UINV_InventoryComponent* Component)
	{
		UINV_InventoryGrid* Grid = NewObject<UINV_InventoryGrid>();
		const FIntPoint Size = Component->GetGridSize(Bag);
		Grid->Columns = Size.X;
		Grid->Rows = Size.Y;
		Grid->SlotId = Bag;
		Grid->ResolvedCategory = EINV_ItemCategory::Consumable;
		Grid->bGridLayoutResolved = true;
		Grid->InventoryComponent = Component;
		for (int32 Index = 0; Index < Size.X * Size.Y; ++Index)
		{
			UINV_GridSlot* Slot = NewObject<UINV_GridSlot>(Grid);
			Slot->SetTileIndex(Index);
			Grid->GridSlots.Add(Slot);
		}
		for (UINV_InventoryItem* Item : Component->GetAllInventoryItems())
		{
			const FIntPoint Dim = Item->GetItemDimension();
			for (int32 Y = 0; Y < Dim.Y; ++Y)
				for (int32 X = 0; X < Dim.X; ++X)
				{
					UINV_GridSlot* Slot = Grid->GridSlots[Item->GetGridIndex() + Y * Size.X + X];
					Slot->SetInventoryItem(Item);
					Slot->SetUpperLeftIndex(Item->GetGridIndex());
					Slot->SetStackCount(Item->GetTotalStackCount());
					Slot->SetAvailable(false);
				}
		}
		return Grid;
	};
	auto PrepareDrop = [&](UINV_InventoryComponent* Component, UINV_InventoryItem* Dragged,
		int32 TargetIndex, bool bPayloadRotated)
	{
		UINV_InventoryGrid* Grid = MakeGrid(Component);
		Grid->AssignHoverItem(Dragged, Dragged->GetGridIndex(), Dragged->GetGridIndex());
		Grid->ItemPayload->SetRotated(bPayloadRotated);
		Grid->ItemDropIndex = TargetIndex;
		Grid->CurrentQueryResult = Grid->CheckHoverPosition(
			FIntPoint(TargetIndex % Grid->Columns, TargetIndex / Grid->Columns), Grid->ItemPayload->GetGridDimensions());
		return Grid;
	};
	auto CheckSplit = [&](UINV_InventoryComponent* Component, UINV_InventoryItem* Source,
		int32 ExpectedIndex, bool bExpectedRotated, int32 ToIndex = INDEX_NONE)
	{
		const int32 BeforeCount = Component->GetAllInventoryItems().Num();
		const int32 SourceCount = Source->GetTotalStackCount();
		const bool bSourceRotated = Source->IsRotated();
		const int32 SourceIndex = Source->GetGridIndex();
		Component->Server_SplitStack(Source, Bag, SourceIndex, 2, ToIndex);
		const TArray<UINV_InventoryItem*> Items = Component->GetAllInventoryItems();
		TestEqual(TEXT("Split adds exactly one entry"), Items.Num(), BeforeCount + 1);
		TestEqual(TEXT("Source count is reduced"), Source->GetTotalStackCount(), SourceCount - 2);
		TestEqual(TEXT("Source rotation is unchanged"), Source->IsRotated(), bSourceRotated);
		TestEqual(TEXT("Source index is unchanged"), Source->GetGridIndex(), SourceIndex);
		if (Items.Num() != BeforeCount + 1) return;
		UINV_InventoryItem* Created = Items.Last();
		TestEqual(TEXT("New entry has selected index"), Created->GetGridIndex(), ExpectedIndex);
		TestEqual(TEXT("New entry has selected rotation"), Created->IsRotated(), bExpectedRotated);
		TestEqual(TEXT("New entry remains in same container"), Created->GetContainerId(), Bag);
		TestEqual(TEXT("Quantity is conserved"), Source->GetTotalStackCount() + Created->GetTotalStackCount(), SourceCount);
		TestEqual(TEXT("Source fragment count agrees"),
			Source->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), SourceCount - 2);
		TestEqual(TEXT("New fragment count agrees"),
			Created->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 2);
	};

	// Only a vertical 1x2 gap remains; the source stays horizontal and remains an obstacle.
	UINV_InventoryComponent* Rotating = MakeInventory(FIntPoint(3, 2));
	UINV_InventoryItem* Source = AddItem(Rotating, FIntPoint(2, 1), 0, false, 8);
	AddItem(Rotating, FIntPoint(2, 1), 3);
	TestEqual(TEXT("Client searches alternate orientation"), MakeGrid(Rotating)->FindFreeSlotForItem(Source), 2);
	TestFalse(TEXT("Client search does not rotate source"), Source->IsRotated());
	CheckSplit(Rotating, Source, 2, true);
	const int32 FullCount = Rotating->GetAllInventoryItems().Num();
	AddExpectedError(TEXT("里已无空位"), EAutomationExpectedErrorFlags::Contains, 1);
	Rotating->Server_SplitStack(Source, Bag, 0, 2, INDEX_NONE);
	TestEqual(TEXT("Full bag creates no extra entry"), Rotating->GetAllInventoryItems().Num(), FullCount);
	TestEqual(TEXT("Full bag preserves source count"), Source->GetTotalStackCount(), 6);
	TestFalse(TEXT("Failed split preserves source rotation"), Source->IsRotated());
	TestEqual(TEXT("Client rejects full bag"), MakeGrid(Rotating)->FindFreeSlotForItem(Source), INDEX_NONE);

	UINV_InventoryItem* SplitOff = Rotating->GetAllInventoryItems().Last();
	UINV_InventoryGrid* MergeBack = PrepareDrop(Rotating, SplitOff, 0, false);
	TestTrue(TEXT("Rotated split can exactly cover original stack"), MergeBack->CurrentQueryResult.bHasSpace);
	TestTrue(TEXT("Regression enters changed-rotation dispatch"), MergeBack->IsHoverRotationChanged());
	MergeBack->PlaceHoverItemAtDropIndex();
	TestEqual(TEXT("Rotated split merges back instead of exchanging"), Source->GetTotalStackCount(), 8);
	TestFalse(TEXT("Merged split entry is removed"), Rotating->GetAllInventoryItems().Contains(SplitOff));
	TestEqual(TEXT("Merge preserves target position"), Source->GetGridIndex(), 0);
	TestFalse(TEXT("Merge preserves target orientation"), Source->IsRotated());

	// A rotated landing exists at index 0, but current orientation fits later at index 6.
	UINV_InventoryComponent* Priority = MakeInventory(FIntPoint(4, 2));
	UINV_InventoryItem* PrioritySource = AddItem(Priority, FIntPoint(2, 1), 2, false, 8);
	AddItem(Priority, FIntPoint(1, 1), 1);
	AddItem(Priority, FIntPoint(1, 1), 5);
	TestEqual(TEXT("Client exhausts current orientation before rotating"),
		MakeGrid(Priority)->FindFreeSlotForItem(PrioritySource), 6);
	CheckSplit(Priority, PrioritySource, 6, false);

	UINV_InventoryComponent* KeepRotation = MakeInventory(FIntPoint(2, 4));
	UINV_InventoryItem* RotatedSource = AddItem(KeepRotation, FIntPoint(2, 1), 0, true, 8);
	CheckSplit(KeepRotation, RotatedSource, 1, true);

	UINV_InventoryComponent* RotateBack = MakeInventory(FIntPoint(2, 3));
	UINV_InventoryItem* VerticalSource = AddItem(RotateBack, FIntPoint(2, 1), 0, true, 8);
	AddItem(RotateBack, FIntPoint(1, 1), 1);
	AddItem(RotateBack, FIntPoint(1, 1), 3);
	TestEqual(TEXT("Client can rotate back"), MakeGrid(RotateBack)->FindFreeSlotForItem(VerticalSource), 4);
	CheckSplit(RotateBack, VerticalSource, 4, false);

	UINV_InventoryComponent* Explicit = MakeInventory(FIntPoint(3, 2));
	UINV_InventoryItem* ExplicitSource = AddItem(Explicit, FIntPoint(2, 1), 0, false, 8);
	AddItem(Explicit, FIntPoint(2, 1), 3);
	CheckSplit(Explicit, ExplicitSource, 2, true, 2);
	AddExpectedError(TEXT("的拆出品放到"), EAutomationExpectedErrorFlags::Contains, 1);
	Explicit->Server_SplitStack(ExplicitSource, Bag, 0, 2, 0);
	TestEqual(TEXT("Explicit landing cannot overlap source"), ExplicitSource->GetTotalStackCount(), 6);
	TestEqual(TEXT("Rejected explicit landing creates no entry"), Explicit->GetAllInventoryItems().Num(), 3);
	UINV_InventoryItem* VerticalTarget = Explicit->GetAllInventoryItems().Last();
	UINV_InventoryGrid* ReverseMerge = PrepareDrop(Explicit, ExplicitSource, 2, true);
	TestTrue(TEXT("Original stack rotated over split is valid"), ReverseMerge->CurrentQueryResult.bHasSpace);
	ReverseMerge->PlaceHoverItemAtDropIndex();
	TestEqual(TEXT("Reverse direction also merges"), VerticalTarget->GetTotalStackCount(), 8);
	TestFalse(TEXT("Reverse merge removes dragged entry"), Explicit->GetAllInventoryItems().Contains(ExplicitSource));
	TestTrue(TEXT("Reverse merge preserves target orientation"), VerticalTarget->IsRotated());
	TestEqual(TEXT("Reverse merge preserves target position"), VerticalTarget->GetGridIndex(), 2);

	UINV_InventoryComponent* Partial = MakeInventory(FIntPoint(3, 2));
	UINV_InventoryItem* PartialTarget = AddItem(Partial, FIntPoint(2, 1), 0, false, 8);
	UINV_InventoryItem* PartialSource = AddItem(Partial, FIntPoint(2, 1), 2, true, 5);
	UINV_InventoryGrid* PartialMerge = PrepareDrop(Partial, PartialSource, 0, false);
	TestTrue(TEXT("Partial rotated merge query is valid"), PartialMerge->CurrentQueryResult.bHasSpace);
	PartialMerge->PlaceHoverItemAtDropIndex();
	TestEqual(TEXT("Partial merge fills target"), PartialTarget->GetTotalStackCount(), 10);
	TestEqual(TEXT("Partial merge keeps remainder"), PartialSource->GetTotalStackCount(), 3);
	TestTrue(TEXT("Remainder keeps original orientation"), PartialSource->IsRotated());
	TestEqual(TEXT("Remainder keeps original position"), PartialSource->GetGridIndex(), 2);
	TestEqual(TEXT("Partial merge keeps two entries"), Partial->GetAllInventoryItems().Num(), 2);
	PrepareDrop(Partial, PartialSource, 0, false)->PlaceHoverItemAtDropIndex();
	TestEqual(TEXT("Full-target count exchange policy is preserved"), PartialTarget->GetTotalStackCount(), 3);
	TestEqual(TEXT("Full-target count exchange conserves quantity"), PartialSource->GetTotalStackCount(), 10);

	UINV_InventoryComponent* SameRotation = MakeInventory(FIntPoint(3, 2));
	UINV_InventoryItem* SameSource = AddItem(SameRotation, FIntPoint(2, 1), 0, false, 3);
	UINV_InventoryItem* SameTarget = AddItem(SameRotation, FIntPoint(2, 1), 3, false, 4);
	PrepareDrop(SameRotation, SameSource, 3, false)->PlaceHoverItemAtDropIndex();
	TestEqual(TEXT("Unchanged-rotation merge still works"), SameTarget->GetTotalStackCount(), 7);
	TestEqual(TEXT("Unchanged-rotation merge removes source"), SameRotation->GetAllInventoryItems().Num(), 1);

	UINV_InventoryComponent* Inexact = MakeInventory(FIntPoint(4, 4));
	UINV_InventoryItem* InexactSource = AddItem(Inexact, FIntPoint(2, 1), 3, true, 2);
	UINV_InventoryItem* InexactTarget = AddItem(Inexact, FIntPoint(2, 1), 8, false, 6);
	UINV_InventoryGrid* InexactDrop = PrepareDrop(Inexact, InexactSource, 8, true);
	TestFalse(TEXT("Different footprint without alignment remains invalid"), InexactDrop->CurrentQueryResult.bHasSpace);
	TestFalse(TEXT("Partial coverage is not treated as a stack operation"), InexactDrop->TryHandleStackDrop());
	TestEqual(TEXT("Partial coverage does not change target count"), InexactTarget->GetTotalStackCount(), 6);

	// Four free cells exist, but no contiguous two-cell footprint in either orientation.
	UINV_InventoryComponent* Fragmented = MakeInventory(FIntPoint(3, 3));
	UINV_InventoryItem* FragmentedSource = AddItem(Fragmented, FIntPoint(2, 1), 0, false, 8);
	for (int32 Index : {3, 5, 7}) AddItem(Fragmented, FIntPoint(1, 1), Index);
	TestEqual(TEXT("Client rejects fragmented free cells"), MakeGrid(Fragmented)->FindFreeSlotForItem(FragmentedSource), INDEX_NONE);
	AddExpectedError(TEXT("里已无空位"), EAutomationExpectedErrorFlags::Contains, 1);
	Fragmented->Server_SplitStack(FragmentedSource, Bag, 0, 2, INDEX_NONE);
	TestEqual(TEXT("Fragmented space preserves quantity"), FragmentedSource->GetTotalStackCount(), 8);
	TestEqual(TEXT("Fragmented space creates no entry"), Fragmented->GetAllInventoryItems().Num(), 4);

	UINV_InventoryComponent* Square = MakeInventory(FIntPoint(2, 1));
	UINV_InventoryItem* SquareSource = AddItem(Square, FIntPoint(1, 1), 0, false, 8);
	CheckSplit(Square, SquareSource, 1, false);

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
