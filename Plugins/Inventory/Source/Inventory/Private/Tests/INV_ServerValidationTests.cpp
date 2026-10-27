#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "UObject/UnrealType.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_ServerValidationTest, "Inventory.Server.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_ServerValidationTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryValidationWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UClass* PickupClass = LoadClass<AActor>(nullptr,
		TEXT("/Inventory/Items/Consumables/BP_INV_Potion_Small_Red.BP_INV_Potion_Small_Red_C"));
	if (!TestNotNull(TEXT("Project pickup class"), PickupClass))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	const FName Bag(TEXT("Bag"));
	const FName Unknown(TEXT("Unknown"));
	auto MakeInventory = [&]()
	{
		APlayerController* Owner = World->SpawnActor<APlayerController>();
		Owner->Possess(World->SpawnActor<APawn>());
		UINV_InventoryComponent* Component = NewObject<UINV_InventoryComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		Component->OwningController = Owner;
		UINV_GridLayoutAsset* Layout = NewObject<UINV_GridLayoutAsset>(Component);
		UINV_GridSlotDefinitionWrapper* Panel = NewObject<UINV_GridSlotDefinitionWrapper>(Layout);
		Panel->SlotDefinition.SlotId = Bag;
		Panel->SlotDefinition.SlotDimension = FIntPoint(4, 3);
		Panel->SlotDefinition.AcceptedCategory = EINV_ItemCategory::All;
		Layout->Slots.Add(Panel);
		Component->GridLayouts.Add(Layout);
		return Component;
	};
	auto AddItem = [&](UINV_InventoryComponent* Component, int32 Count, int32 Index,
		int32 Maximum = 10, bool bConsumable = true)
	{
		FINV_ItemManifest Manifest;
		FindFProperty<FClassProperty>(FINV_ItemManifest::StaticStruct(), TEXT("PickupActorClass"))
			->SetObjectPropertyValue_InContainer(&Manifest, PickupClass);
		FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		Category->GetUnderlyingProperty()->SetIntPropertyValue(Category->ContainerPtrToValuePtr<void>(&Manifest),
			static_cast<int64>(bConsumable ? EINV_ItemCategory::Consumable : EINV_ItemCategory::Equippable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = bConsumable
			? FGameplayTag(GameItems::Consumables::Potions::Red::Small) : FGameplayTag(GameItems::Equipment::Weapons::Knife);
		FINV_GridFragment Grid;
		Grid.SetGridSize(FIntPoint(1, 1));
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(Grid));
		if (Count != INDEX_NONE)
		{
			FINV_StackableFragment Stack;
			Stack.SetStackCount(Count);
			FindFProperty<FIntProperty>(FINV_StackableFragment::StaticStruct(), TEXT("MaxStacks"))
				->SetPropertyValue_InContainer(&Stack, Maximum);
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_StackableFragment>(Stack));
		}
		if (bConsumable)
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_ConsumableFragment>());
		else
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_EquipmentFragment>());
		UINV_InventoryItem* Item = Manifest.Manifest(Component->GetOwner());
		Item->SetContainerId(Bag);
		Item->SetGridIndex(Index);
		Item->SetTotalStackCount(Count == INDEX_NONE ? 0 : Count);
		Component->InventoryList.AddEntry(Item);
		return Item;
	};
	auto CheckItem = [&](UINV_InventoryComponent* Component, UINV_InventoryItem* Item, int32 Count, int32 Index)
	{
		TestTrue(TEXT("Entry membership is preserved"), Component->GetAllInventoryItems().Contains(Item));
		TestEqual(TEXT("Quantity is preserved"), Item->GetTotalStackCount(), Count);
		if (Item->IsStackable())
			TestEqual(TEXT("Fragment agrees with quantity"), Item->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), Count);
		TestEqual(TEXT("Position is preserved"), Item->GetGridIndex(), Index);
		TestEqual(TEXT("Container is preserved"), Item->GetContainerId(), Bag);
		TestFalse(TEXT("Rotation is preserved"), Item->IsRotated());
	};
	UINV_InventoryComponent* Inventory = MakeInventory();
	UINV_InventoryItem* From = AddItem(Inventory, 4, 0);
	UINV_InventoryItem* To = AddItem(Inventory, 9, 1);
	for (int32 Amount : {4, MAX_int32})
	{
		Inventory->Server_MergeStack(From, To, Amount);
		CheckItem(Inventory, From, 4, 0);
		CheckItem(Inventory, To, 9, 1);
	}
	for (int32 Amount : {0, -1, MIN_int32})
	{
		AddExpectedError(TEXT("MergeStack 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
		Inventory->Server_MergeStack(From, To, Amount);
		CheckItem(Inventory, From, 4, 0);
		CheckItem(Inventory, To, 9, 1);
	}
	Inventory->Server_MergeStack(From, From, 4);
	CheckItem(Inventory, From, 4, 0);
	Inventory->Server_MergeStack(From, To, 1);
	CheckItem(Inventory, From, 3, 0);
	CheckItem(Inventory, To, 10, 1);
	Inventory->Server_SwapStackCounts(From, To);
	CheckItem(Inventory, From, 10, 0);
	CheckItem(Inventory, To, 3, 1);
	UINV_InventoryItem* LowerLimit = AddItem(Inventory, 2, 2, 5);
	Inventory->Server_SwapStackCounts(From, LowerLimit);
	CheckItem(Inventory, From, 10, 0);
	CheckItem(Inventory, LowerLimit, 2, 2);
	UINV_InventoryComponent* ForeignInventory = MakeInventory();
	UINV_InventoryItem* Foreign = AddItem(ForeignInventory, 2, 0);
	Inventory->Server_MergeStack(From, Foreign, 1);
	Inventory->Server_UseItem(Foreign, 1);
	Inventory->Server_DropItem(Foreign, 1);
	Inventory->Server_SplitStack(Foreign, Bag, 0, 1, 5);
	Inventory->Server_MoveItem(Foreign, Bag, 0, Bag, 5, false);
	CheckItem(ForeignInventory, Foreign, 2, 0);
	for (int32 Amount : {0, -1, MIN_int32, 11, MAX_int32})
	{
		Inventory->Server_DropItem(From, Amount);
		Inventory->Server_UseItem(From, Amount);
		// Even an invalid quantity with a plausible false source must not rewrite the entry.
		Inventory->Server_SplitStack(From, Bag, 7, Amount, 6);
		CheckItem(Inventory, From, 10, 0);
	}
	Inventory->Server_UseItem(From, 2);
	CheckItem(Inventory, From, 10, 0);
	Inventory->Server_UseItem(From, 1);
	CheckItem(Inventory, From, 9, 0);
	Inventory->Server_DropItem(From, 2);
	CheckItem(Inventory, From, 7, 0);
	Inventory->Server_SplitStack(From, Bag, 7, 2, 6);
	Inventory->Server_SplitStack(From, Unknown, 0, 2, 6);
	CheckItem(Inventory, From, 7, 0);
	Inventory->Server_SplitStack(From, Bag, 0, 2, 6);
	CheckItem(Inventory, From, 5, 0);
	TestEqual(TEXT("Valid split creates one entry"), Inventory->GetAllInventoryItems().Num(), 4);
	Inventory->Server_RegisterItemPlacement(From, Unknown, 4, true);
	Inventory->Server_MoveItem(From, Bag, 0, Unknown, 4, true);
	Inventory->Server_SwapItems(From, Bag, 0, To, Unknown, 1);
	AddExpectedError(TEXT("MoveItems 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
	Inventory->Server_MoveItems({From}, {4}, Unknown, {true});
	Inventory->Server_MoveItems({From, From}, {4, 5}, Bag, {true, false});
	CheckItem(Inventory, From, 5, 0);
	UINV_GridSlotDefinitionWrapper* Panel = Inventory->GridLayouts[0]->Slots[0];
	Panel->SlotDefinition.SlotDimension = FIntPoint(MAX_int32, 2);
	Inventory->Server_MoveItem(From, Bag, 0, Bag, 4, true);
	Panel->SlotDefinition.SlotDimension = FIntPoint(0, 3);
	Inventory->Server_RegisterItemPlacement(From, Bag, 4, true);
	Panel->SlotDefinition.SlotDimension = FIntPoint(4, 3);
	CheckItem(Inventory, From, 5, 0);
	UINV_InventoryItem* Gear = AddItem(Inventory, INDEX_NONE, 7, 10, false);
	Inventory->Server_UseItem(Gear, 1);
	Inventory->Server_DropItem(Gear, 0);
	Inventory->Server_DropItem(Gear, 2);
	CheckItem(Inventory, Gear, 0, 7);
	// Exercise the actual UI request boundary: equipment payloads store zero, while the RPC expects one unit.
	auto DropFromGrid = [&](UINV_InventoryItem* Item, int32 PayloadCount)
	{
		UINV_InventoryGrid* Grid = NewObject<UINV_InventoryGrid>();
		Grid->InventoryComponent = Inventory;
		Grid->ItemPayload = NewObject<UINV_ItemPayload>(Grid);
		Grid->ItemPayload->InitByItem(Item);
		Grid->ItemPayload->SetStackCount(PayloadCount);
		Grid->ItemPayload->SetPreviousGridIndex(Item->GetGridIndex());
		Grid->DropItem();
		TestNull(TEXT("UI drop clears payload"), Grid->ItemPayload.Get());
	};
	DropFromGrid(Gear, 0);
	TestFalse(TEXT("One non-stackable physical unit is removed"), Inventory->GetAllInventoryItems().Contains(Gear));
	TestEqual(TEXT("Equipment internal count stays zero"), Gear->GetTotalStackCount(), 0);
	UINV_InventoryItem* UIStack = AddItem(Inventory, 6, 7);
	DropFromGrid(UIStack, 2);
	CheckItem(Inventory, UIStack, 4, 7);
	UINV_InventoryItem* Single = AddItem(Inventory, 1, 7);
	Inventory->Server_UseItem(Single, 1);
	TestFalse(TEXT("Last consumable unit removes entry"), Inventory->GetAllInventoryItems().Contains(Single));
	Inventory->Server_DropItem(From, 5);
	TestFalse(TEXT("Exact full drop removes entry"), Inventory->GetAllInventoryItems().Contains(From));
	UINV_InventoryComponent* Equipment = MakeInventory();
	const FGameplayTag Weapon = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Weapons"));
	const FGameplayTag Mask = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Masks"));
	UINV_InventoryItem* Equipped = AddItem(Equipment, INDEX_NONE, INDEX_NONE, 10, false);
	Equipped->SetContainerId(NAME_None);
	Equipped->SetEquippedSlotType(Weapon);
	UINV_InventoryItem* Candidate = AddItem(Equipment, INDEX_NONE, 0, 10, false);
	UINV_InventoryItem* Potion = AddItem(Equipment, 3, 1);
	Equipment->Server_EquipSlotClicked(Candidate, Candidate, Weapon);
	Equipment->Server_EquipSlotClicked(Candidate, nullptr, Weapon);
	Equipment->Server_EquipSlotClicked(Candidate, nullptr, Mask);
	Equipment->Server_EquipSlotClicked(Potion, Equipped, Weapon);
	Equipment->Server_EquipSlotClicked(Candidate, Foreign, Weapon);
	CheckItem(Equipment, Candidate, 0, 0);
	CheckItem(Equipment, Potion, 3, 1);
	TestEqual(TEXT("Rejected equipment requests preserve current occupant"), Equipped->GetEquippedSlotType(), Weapon);
	Equipment->Server_DropItem(Equipped, 1);
	TestTrue(TEXT("Direct equipped drop cannot bypass unequip effects"), Equipment->GetAllInventoryItems().Contains(Equipped));
	Equipment->Server_MoveEquippedItem(Candidate, nullptr, Weapon, Mask);
	CheckItem(Equipment, Candidate, 0, 0);
	// The atomic path must reject a replacement when the old equipment has no valid
	// return space, leaving both the slot and the source item untouched.
	Equipment->Server_EquipTransaction(Candidate, Equipped, Weapon, Bag, 1, false);
	TestEqual(TEXT("Rejected atomic replacement preserves candidate location"), Candidate->GetGridIndex(), 0);
	TestTrue(TEXT("Rejected atomic replacement preserves equipped occupant"), Equipped->IsEquipped());
	Equipment->Server_EquipSlotClicked(Candidate, Equipped, Weapon);
	TestEqual(TEXT("Compatible replacement still equips"), Candidate->GetEquippedSlotType(), Weapon);
	TestFalse(TEXT("Compatible replacement clears old equipment tag"), Equipped->IsEquipped());
	UINV_InventoryComponent* FinalMerge = MakeInventory();
	UINV_InventoryItem* LastSource = AddItem(FinalMerge, 4, 0);
	UINV_InventoryItem* FinalTarget = AddItem(FinalMerge, 2, 1);
	AddExpectedError(TEXT("MergeStack 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
	FinalMerge->Server_MergeStack(LastSource, FinalTarget, 5);
	CheckItem(FinalMerge, LastSource, 4, 0);
	CheckItem(FinalMerge, FinalTarget, 2, 1);
	FinalMerge->Server_MergeStack(LastSource, FinalTarget, 4);
	TestFalse(TEXT("Full valid merge removes source"), FinalMerge->GetAllInventoryItems().Contains(LastSource));
	CheckItem(FinalMerge, FinalTarget, 6, 1);
	UINV_InventoryItem* Corrupt = AddItem(FinalMerge, 11, 2);
	FinalMerge->Server_DropItem(Corrupt, 1);
	FinalMerge->Server_UseItem(Corrupt, 1);
	FinalMerge->Server_MergeStack(Corrupt, FinalTarget, 1);
	CheckItem(FinalMerge, Corrupt, 11, 2);
	APlayerController* ForeignInteractor = World->SpawnActor<APlayerController>();
	FINV_InteractionContext InvalidInteractorContext(ForeignInteractor, EINV_InteractionType::Use);
	InvalidInteractorContext.Item = UIStack;
	InvalidInteractorContext.Amount = 1;
	const int64 InvalidInteractorRequest = Inventory->SubmitInventoryOperation(InvalidInteractorContext);
	const FINV_InventoryOperationResult InvalidInteractorResult = Inventory->GetLastOperationResult();
	TestEqual(TEXT("Unified operation returns the interactor request id"), InvalidInteractorResult.RequestId,
		InvalidInteractorRequest);
	TestEqual(TEXT("Unified operation rejects a foreign interactor"), InvalidInteractorResult.Failure,
		EINV_OperationFailure::InvalidInteractor);
	AActor* PickupActor = World->SpawnActor<AActor>();
	UINV_ItemComponent* PickupComponent = NewObject<UINV_ItemComponent>(PickupActor);
	PickupActor->AddInstanceComponent(PickupComponent);
	FINV_InteractionContext InvalidTargetContext(Inventory->GetOwner(), EINV_InteractionType::Pickup);
	InvalidTargetContext.ItemComponent = PickupComponent;
	InvalidTargetContext.TargetActor = World->SpawnActor<AActor>();
	const int64 InvalidTargetRequest = Inventory->SubmitInventoryOperation(InvalidTargetContext);
	const FINV_InventoryOperationResult InvalidTargetResult = Inventory->GetLastOperationResult();
	TestEqual(TEXT("Pickup rejects a mismatched target actor"), InvalidTargetResult.RequestId,
		InvalidTargetRequest);
	TestEqual(TEXT("Pickup reports the invalid target failure"), InvalidTargetResult.Failure,
		EINV_OperationFailure::InvalidTarget);
	FINV_InteractionContext InvalidEquipmentContext(Equipment->GetOwner(), EINV_InteractionType::Equip);
	InvalidEquipmentContext.Item = Candidate;
	const int64 InvalidEquipmentRequest = Equipment->SubmitInventoryOperation(InvalidEquipmentContext);
	const FINV_InventoryOperationResult InvalidEquipmentResult = Equipment->GetLastOperationResult();
	TestEqual(TEXT("Equip reports an invalid equipment slot"), InvalidEquipmentResult.RequestId,
		InvalidEquipmentRequest);
	TestEqual(TEXT("Equip rejects a missing equipment slot tag"), InvalidEquipmentResult.Failure,
		EINV_OperationFailure::EquipmentMismatch);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
