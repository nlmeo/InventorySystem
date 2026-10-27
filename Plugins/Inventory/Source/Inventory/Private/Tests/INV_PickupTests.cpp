#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "InventoryManagement/Utils/INV_AutoPlacement.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "Items/Components/INV_ItemComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_PickupGeometryTest, "Inventory.Pickup.RotationPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_PickupGeometryTest::RunTest(const FString& Parameters)
{
	FINV_AutoPlacementGrid Grid;
	Grid.SlotId = TEXT("Bag");
	Grid.Size = FIntPoint(2, 3);
	FINV_AutoPlacement Result;
	TestTrue(TEXT("Only the rotated direction fits"), INV_FindAutoPlacement({Grid}, FIntPoint(3, 2), false, Result));
	TestTrue(TEXT("Rotation is recorded"), Result.bRotated);
	TestEqual(TEXT("Top-left index"), Result.Index, 0);
	FINV_AutoPlacementGrid Second;
	Second.SlotId = TEXT("SecondBag");
	Second.Size = FIntPoint(3, 2);
	TestTrue(TEXT("Search all bags before rotating"), INV_FindAutoPlacement({Grid, Second}, FIntPoint(3, 2), false, Result));
	TestEqual(TEXT("Current direction in second bag wins"), Result.SlotId, Second.SlotId);
	TestFalse(TEXT("No unnecessary rotation"), Result.bRotated);
	TestTrue(TEXT("Already rotated item can rotate back"), INV_FindAutoPlacement({Second}, FIntPoint(3, 2), true, Result));
	TestFalse(TEXT("Opposite orientation selected"), Result.bRotated);
	Grid.Occupied.Emplace(FIntPoint(0, 0), Grid.Size);
	TestFalse(TEXT("Full bag rejects both directions"), INV_FindAutoPlacement({Grid}, FIntPoint(2, 1), false, Result));
	TestEqual(TEXT("Failure leaves no landing"), Result.Index, INDEX_NONE);
	TestFalse(TEXT("Invalid dimensions rejected"), INV_FindAutoPlacement({Second}, FIntPoint(0, 2), false, Result));

	// Independent cell-based oracle over every occupancy pattern of a 3x3 bag.
	Grid.Size = FIntPoint(3, 3);
	for (uint32 Mask = 0; Mask < 512; ++Mask)
	{
		Grid.Occupied.Reset();
		for (int32 Cell = 0; Cell < 9; ++Cell)
		{
			if (Mask & (1u << Cell))
			{
				const FIntPoint Position(Cell % 3, Cell / 3);
				Grid.Occupied.Emplace(Position, Position + FIntPoint(1, 1));
			}
		}
		for (int32 Width = 1; Width <= 4; ++Width)
		{
			for (int32 Height = 1; Height <= 4; ++Height)
			{
				int32 ExpectedIndex = INDEX_NONE;
				bool bExpectedRotation = false;
				for (int32 Direction = 0; Direction < 2 && ExpectedIndex == INDEX_NONE; ++Direction)
				{
					const int32 W = Direction ? Height : Width;
					const int32 H = Direction ? Width : Height;
					for (int32 Cell = 0; Cell < 9; ++Cell)
					{
						const int32 X = Cell % 3, Y = Cell / 3;
						if (X + W > 3 || Y + H > 3) continue;
						bool bFree = true;
						for (int32 Row = Y; Row < Y + H; ++Row)
							for (int32 Col = X; Col < X + W; ++Col)
								if (Mask & (1u << (Row * 3 + Col))) bFree = false;
						if (!bFree) continue;
						ExpectedIndex = Cell;
						bExpectedRotation = Direction != 0;
						break;
					}
				}
				const bool bFound = INV_FindAutoPlacement({Grid}, FIntPoint(Width, Height), false, Result);
				if (bFound != (ExpectedIndex != INDEX_NONE) || Result.Index != ExpectedIndex
					|| (bFound && Result.bRotated != bExpectedRotation))
				{
					AddError(FString::Printf(TEXT("Placement mismatch: mask=%u, size=%dx%d"), Mask, Width, Height));
					return false;
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_PickupLifecycleTest, "Inventory.Pickup.AuthoritativeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_PickupLifecycleTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues InitialValues = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryPickupTestWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &InitialValues);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto MakeManifest = [](FIntPoint Size, bool bEquipment, int32 Count = 0)
	{
		FINV_ItemManifest Manifest;
		FEnumProperty* CategoryProperty = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		CategoryProperty->GetUnderlyingProperty()->SetIntPropertyValue(CategoryProperty->ContainerPtrToValuePtr<void>(&Manifest),
			static_cast<int64>(bEquipment ? EINV_ItemCategory::Equippable : EINV_ItemCategory::Consumable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = bEquipment
			? FGameplayTag(GameItems::Equipment::Weapons::Knife) : FGameplayTag(GameItems::Consumables::Potions::Red::Small);
		FINV_GridFragment GridFragment;
		GridFragment.SetGridSize(Size);
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(GridFragment));
		if (bEquipment)
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_EquipmentFragment>());
		if (Count > 0)
		{
			FINV_StackableFragment Stack;
			Stack.SetStackCount(Count);
			FindFProperty<FIntProperty>(FINV_StackableFragment::StaticStruct(), TEXT("MaxStacks"))->SetPropertyValue_InContainer(&Stack, 10);
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_StackableFragment>(Stack));
		}
		return Manifest;
	};
	auto MakeInventory = [&](FIntPoint Size, EINV_ItemCategory Category)
	{
		AActor* Owner = World->SpawnActor<AActor>();
		UINV_InventoryComponent* Component = NewObject<UINV_InventoryComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		UINV_GridLayoutAsset* Layout = NewObject<UINV_GridLayoutAsset>(Component);
		UINV_GridSlotDefinitionWrapper* Panel = NewObject<UINV_GridSlotDefinitionWrapper>(Layout);
		Panel->SlotDefinition.SlotId = TEXT("Bag");
		Panel->SlotDefinition.SlotDimension = Size;
		Panel->SlotDefinition.AcceptedCategory = Category;
		Layout->Slots.Add(Panel);
		Component->GridLayouts.Add(Layout);
		return Component;
	};
	auto MakePickup = [&](const FINV_ItemManifest& Manifest)
	{
		AActor* Owner = World->SpawnActor<AActor>();
		UINV_ItemComponent* Component = NewObject<UINV_ItemComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		Component->InitItemManifest(Manifest);
		return Component;
	};

	UINV_InventoryComponent* RotatedBag = MakeInventory(FIntPoint(2, 3), EINV_ItemCategory::Consumable);
	UINV_ItemComponent* Pickup = MakePickup(MakeManifest(FIntPoint(3, 2), false));
	RotatedBag->AddPickupOnServer(Pickup, FGameplayTag());
	TestTrue(TEXT("Accepted pickup is removed from world"), Pickup->GetOwner()->IsActorBeingDestroyed());
	TestEqual(TEXT("Rotated pickup creates one entry"), RotatedBag->GetAllInventoryItems().Num(), 1);
	if (RotatedBag->GetAllInventoryItems().Num() == 1)
	{
		TestTrue(TEXT("Authoritative rotation is committed"), RotatedBag->GetAllInventoryItems()[0]->IsRotated());
		TestEqual(TEXT("Authoritative container is committed"), RotatedBag->GetAllInventoryItems()[0]->GetContainerId(), FName(TEXT("Bag")));
	}
	UINV_InventoryComponent* AnyCategoryBag = MakeInventory(FIntPoint(2, 2), EINV_ItemCategory::All);
	UINV_ItemComponent* AnyCategoryPickup = MakePickup(MakeManifest(FIntPoint(1, 1), true));
	AnyCategoryBag->AddPickupOnServer(AnyCategoryPickup, FGameplayTag());
	TestEqual(TEXT("All category accepts an otherwise valid item"), AnyCategoryBag->GetAllInventoryItems().Num(), 1);

	UINV_InventoryComponent* NoCategoryBag = MakeInventory(FIntPoint(2, 2), EINV_ItemCategory::None);
	UINV_ItemComponent* NoCategoryPickup = MakePickup(MakeManifest(FIntPoint(1, 1), false));
	NoCategoryBag->AddPickupOnServer(NoCategoryPickup, FGameplayTag());
	TestEqual(TEXT("None category accepts no items"), NoCategoryBag->GetAllInventoryItems().Num(), 0);
	TestFalse(TEXT("None category leaves rejected item on ground"), NoCategoryPickup->GetOwner()->IsActorBeingDestroyed());

	UINV_ItemComponent* Rejected = MakePickup(MakeManifest(FIntPoint(3, 2), false));
	RotatedBag->AddPickupOnServer(Rejected, FGameplayTag());
	TestEqual(TEXT("Full bag creates no invisible entry"), RotatedBag->GetAllInventoryItems().Num(), 1);
	TestFalse(TEXT("Rejected pickup stays in world"), Rejected->GetOwner()->IsActorBeingDestroyed());

	UINV_InventoryComponent* EquipmentBag = MakeInventory(FIntPoint(1, 1), EINV_ItemCategory::Equippable);
	EquipmentBag->AddPickupOnServer(MakePickup(MakeManifest(FIntPoint(1, 1), true)), FGameplayTag());
	const FGameplayTag WeaponSlot = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Weapons"));
	EquipmentBag->AddPickupOnServer(MakePickup(MakeManifest(FIntPoint(3, 2), true)), WeaponSlot);
	TestEqual(TEXT("Full backpack still permits direct equipment"), EquipmentBag->GetAllInventoryItems().Num(), 2);
	UINV_InventoryItem* Equipped = nullptr;
	for (UINV_InventoryItem* Item : EquipmentBag->GetAllInventoryItems()) if (Item->IsEquipped()) Equipped = Item;
	TestNotNull(TEXT("Exactly one equipped item exists"), Equipped);
	if (Equipped) TestEqual(TEXT("Equipment occupies no grid"), Equipped->GetGridIndex(), INDEX_NONE);
	UINV_ItemComponent* SecondEquipment = MakePickup(MakeManifest(FIntPoint(3, 2), true));
	EquipmentBag->AddPickupOnServer(SecondEquipment, WeaponSlot);
	TestEqual(TEXT("Stale empty-slot hint cannot replace equipment"), EquipmentBag->GetAllInventoryItems().Num(), 2);
	TestFalse(TEXT("Unplaceable second equipment stays on ground"), SecondEquipment->GetOwner()->IsActorBeingDestroyed());
	if (Equipped) TestTrue(TEXT("Original equipment remains equipped"), Equipped->IsEquipped());

	UINV_InventoryComponent* SpareBag = MakeInventory(FIntPoint(3, 3), EINV_ItemCategory::Equippable);
	SpareBag->AddPickupOnServer(MakePickup(MakeManifest(FIntPoint(3, 2), true)), WeaponSlot);
	SpareBag->AddPickupOnServer(MakePickup(MakeManifest(FIntPoint(3, 2), true)), WeaponSlot);
	int32 EquippedCount = 0, StoredCount = 0;
	for (UINV_InventoryItem* Item : SpareBag->GetAllInventoryItems())
	{
		EquippedCount += Item->IsEquipped() ? 1 : 0;
		StoredCount += !Item->GetContainerId().IsNone() ? 1 : 0;
	}
	TestEqual(TEXT("Existing equipment is never replaced"), EquippedCount, 1);
	TestEqual(TEXT("Occupied-slot pickup goes to available bag"), StoredCount, 1);

	UINV_InventoryComponent* Stacks = MakeInventory(FIntPoint(3, 3), EINV_ItemCategory::Consumable);
	UINV_ItemComponent* Bulk = MakePickup(MakeManifest(FIntPoint(2, 1), false, 45));
	Stacks->AddPickupOnServer(Bulk, FGameplayTag());
	int32 Total = 0, RotatedCount = 0;
	for (UINV_InventoryItem* Item : Stacks->GetAllInventoryItems())
	{
		Total += Item->GetTotalStackCount();
		RotatedCount += Item->IsRotated() ? 1 : 0;
		TestTrue(TEXT("No stack exceeds maximum"), Item->GetTotalStackCount() <= 10);
	}
	TestEqual(TEXT("Mixed directions fit four stacks"), Stacks->GetAllInventoryItems().Num(), 4);
	TestEqual(TEXT("Final stack rotates into remaining column"), RotatedCount, 1);
	TestEqual(TEXT("Accepted quantity"), Total, 40);
	TestEqual(TEXT("Unaccepted quantity remains on pickup"), Bulk->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 5);
	Stacks->AddPickupOnServer(Bulk, FGameplayTag());
	TestEqual(TEXT("Retry does not duplicate stacks"), Stacks->GetAllInventoryItems().Num(), 4);
	TestEqual(TEXT("Retry preserves ground remainder"), Bulk->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 5);

	UINV_InventoryComponent* MergeBag = MakeInventory(FIntPoint(1, 1), EINV_ItemCategory::Consumable);
	MergeBag->AddPickupOnServer(MakePickup(MakeManifest(FIntPoint(1, 1), false, 8)), FGameplayTag());
	UINV_ItemComponent* MergePickup = MakePickup(MakeManifest(FIntPoint(1, 1), false, 5));
	MergeBag->AddPickupOnServer(MergePickup, FGameplayTag());
	TestEqual(TEXT("Full bag still fills existing stack"), MergeBag->GetAllInventoryItems()[0]->GetTotalStackCount(), 10);
	TestEqual(TEXT("Partial merge leaves exactly three on ground"), MergePickup->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 3);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
