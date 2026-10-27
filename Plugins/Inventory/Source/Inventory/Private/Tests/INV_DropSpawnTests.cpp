#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/Image.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Items/Fragments/INV_FragmentTags.h"
#include "UObject/UnrealType.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/GridSlots/INV_GridSlot.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_DropSpawnTest, "Inventory.Drop.SpawnFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_DropSpawnTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryDropSpawnWorld"),
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
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APawn* Pawn = World->SpawnActor<APawn>();
	Controller->Possess(Pawn);
	UINV_InventoryComponent* Inventory = NewObject<UINV_InventoryComponent>(Controller);
	Controller->AddInstanceComponent(Inventory);
	Inventory->OwningController = Controller;
	const FName Bag(TEXT("Bag"));
	FClassProperty* PickupProperty = FindFProperty<FClassProperty>(FINV_ItemManifest::StaticStruct(), TEXT("PickupActorClass"));
	auto MakeItem = [&](bool bStackable)
	{
		FINV_ItemManifest Manifest;
		FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		Category->GetUnderlyingProperty()->SetIntPropertyValue(Category->ContainerPtrToValuePtr<void>(&Manifest),
			static_cast<int64>(bStackable ? EINV_ItemCategory::Consumable : EINV_ItemCategory::Equippable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = bStackable
			? FGameplayTag(GameItems::Consumables::Potions::Red::Small) : FGameplayTag(GameItems::Equipment::Weapons::Knife);
		PickupProperty->SetObjectPropertyValue_InContainer(&Manifest, PickupClass);
		FINV_GridFragment GridFragment;
		GridFragment.SetGridSize(FIntPoint(2, 1));
		GridFragment.SetFragmentTag(FragmentTags::GridFragment);
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(GridFragment));
		if (bStackable)
		{
			FINV_StackableFragment Stack;
			Stack.SetStackCount(6);
			FindFProperty<FIntProperty>(FINV_StackableFragment::StaticStruct(), TEXT("MaxStacks"))
				->SetPropertyValue_InContainer(&Stack, 10);
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_StackableFragment>(Stack));
		}
		else Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_EquipmentFragment>());
		UINV_InventoryItem* Item = Manifest.Manifest(Controller);
		Item->SetContainerId(Bag);
		Item->SetGridIndex(0);
		Item->SetRotated(true);
		Item->SetTotalStackCount(bStackable ? 6 : 0);
		Inventory->InventoryList.AddEntry(Item);
		return Item;
	};
	auto ActiveActors = [&]()
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Count;
		return Count;
	};
	auto WorldUnits = [&]()
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) continue;
			if (const UINV_ItemComponent* Component = It->FindComponentByClass<UINV_ItemComponent>())
			{
				const FINV_StackableFragment* Stack = Component->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>();
				Count += Stack ? Stack->GetStackCount() : 1;
			}
		}
		return Count;
	};
	for (bool bStackable : {true, false})
	{
		UINV_InventoryItem* Item = MakeItem(bStackable);
		UINV_InventoryGrid* Grid = NewObject<UINV_InventoryGrid>();
		Grid->InventoryComponent = Inventory;
		Grid->Columns = 4;
		Grid->Rows = 2;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			UINV_GridSlot* Slot = NewObject<UINV_GridSlot>(Grid);
			FindFProperty<FObjectPropertyBase>(UINV_GridSlot::StaticClass(), TEXT("Image_GridSlot"))
				->SetObjectPropertyValue_InContainer(Slot, NewObject<UImage>(Slot));
			Slot->SetTileIndex(Index);
			if (Index == 0 || Index == 4)
			{
				Slot->SetInventoryItem(Item);
				Slot->SetUpperLeftIndex(0);
				Slot->SetAvailable(false);
				Slot->SetStackCount(bStackable ? 6 : 0);
			}
			Grid->GridSlots.Add(Slot);
		}
		auto UIDrop = [&](int32 Count)
		{
			Grid->ItemPayload = NewObject<UINV_ItemPayload>(Grid);
			Grid->ItemPayload->InitByItem(Item);
			Grid->ItemPayload->SetStackCount(Count);
			Grid->ItemPayload->SetPreviousGridIndex(0);
			Grid->DropItem();
			TestNull(TEXT("Drop ends dragging"), Grid->ItemPayload.Get());
		};
		const int32 BeforeActors = ActiveActors();
		const int32 BeforeWorldUnits = WorldUnits();
		auto CheckUnchanged = [&]()
		{
			TestTrue(TEXT("Failed spawn preserves inventory membership"), Inventory->GetAllInventoryItems().Contains(Item));
			TestEqual(TEXT("Failed spawn preserves count"), Item->GetTotalStackCount(), bStackable ? 6 : 0);
			if (bStackable) TestEqual(TEXT("Failed spawn preserves fragment count"), Item->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 6);
			TestEqual(TEXT("Failed UI drop preserves container"), Item->GetContainerId(), Bag);
			TestEqual(TEXT("Failed UI drop preserves origin"), Item->GetGridIndex(), 0);
			TestTrue(TEXT("Failed UI drop preserves rotation"), Item->IsRotated());
			for (int32 Index : {0, 4})
			{
				TestTrue(TEXT("Failed UI drop preserves occupied slots"), Grid->GridSlots[Index]->GetInventoryItem().Get() == Item);
				TestEqual(TEXT("Failed UI drop preserves slot quantity"), Grid->GridSlots[Index]->GetStackCount(), bStackable ? 6 : 0);
			}
			TestEqual(TEXT("Failed drop leaves no orphan world actor"), ActiveActors(), BeforeActors);
			TestEqual(TEXT("Failed drop preserves world quantity"), WorldUnits(), BeforeWorldUnits);
		};
		// Cover partial/full stack requests as well as non-stackable payload zero -> physical unit one.
		for (int32 Requested : {bStackable ? 2 : 0, bStackable ? 6 : 0})
		{
			Inventory->OwningController.Reset();
			UIDrop(Requested);
			CheckUnchanged();
			Inventory->OwningController = Controller;
			Controller->UnPossess();
			UIDrop(Requested);
			CheckUnchanged();
			Controller->Possess(Pawn);
			PickupProperty->SetObjectPropertyValue_InContainer(&Item->GetItemManifestMutable(), nullptr);
			UIDrop(Requested);
			CheckUnchanged();
			PickupProperty->SetObjectPropertyValue_InContainer(&Item->GetItemManifestMutable(), AActor::StaticClass());
			UIDrop(Requested);
			CheckUnchanged();
			PickupProperty->SetObjectPropertyValue_InContainer(&Item->GetItemManifestMutable(), PickupClass);
		}
		if (bStackable)
		{
			UIDrop(2);
			TestEqual(TEXT("Successful partial drop deducts exactly two"), Item->GetTotalStackCount(), 4);
			TestEqual(TEXT("Successful partial drop keeps fragment in sync"), Item->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 4);
			TestEqual(TEXT("Successful partial drop preserves inventory position"), Item->GetGridIndex(), 0);
			TestEqual(TEXT("Successful partial drop preserves inventory container"), Item->GetContainerId(), Bag);
			TestEqual(TEXT("Successful partial drop conserves quantity"), Item->GetTotalStackCount() + WorldUnits() - BeforeWorldUnits, 6);
			UIDrop(4);
		}
		else UIDrop(0);
		TestFalse(TEXT("Successful full drop removes inventory entry"), Inventory->GetAllInventoryItems().Contains(Item));
		TestEqual(TEXT("Full drop conserves physical quantity"), WorldUnits() - BeforeWorldUnits, bStackable ? 6 : 1);
		const int32 AfterWorldUnits = WorldUnits();
		Inventory->Server_DropItem(Item, 1);
		TestEqual(TEXT("Removed entry cannot be dropped twice"), WorldUnits(), AfterWorldUnits);
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
