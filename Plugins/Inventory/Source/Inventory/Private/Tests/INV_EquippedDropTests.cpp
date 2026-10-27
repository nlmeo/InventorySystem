#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "EquipmentManagement/Components/INV_EquipmentComponent.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "Items/Components/INV_ItemComponent.h"
#include "UObject/UnrealType.h"
#include "Widgets/Inventory/Spatial/INV_SpatialInventory.h"
#include "Widgets/Inventory/GridSlots/INV_EquippedGridSlot.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_EquippedDropTest, "Inventory.Equipment.DropOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_EquippedDropTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryEquippedDropWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UClass* PickupClass = LoadClass<AActor>(nullptr,
		TEXT("/Inventory/Items/Consumables/BP_INV_Potion_Small_Red.BP_INV_Potion_Small_Red_C"));
	if (!TestNotNull(TEXT("Project pickup actor class"), PickupClass))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	World->AddController(Controller);
	APawn* Pawn = World->SpawnActor<APawn>();
	Controller->Possess(Pawn);
	UINV_InventoryComponent* Inventory = NewObject<UINV_InventoryComponent>(Controller);
	Controller->AddInstanceComponent(Inventory);
	Inventory->OwningController = Controller;
	UINV_EquipmentComponent* Equipment = NewObject<UINV_EquipmentComponent>(Controller);
	Controller->AddInstanceComponent(Equipment);
	Equipment->InitializeOwner(Controller);
	const FGameplayTag Weapon = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Weapons"));
	const FGameplayTag Mask = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Masks"));
	FINV_ItemManifest Manifest;
	FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
	Category->GetUnderlyingProperty()->SetIntPropertyValue(Category->ContainerPtrToValuePtr<void>(&Manifest),
		static_cast<int64>(EINV_ItemCategory::Equippable));
	*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
		->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = GameItems::Equipment::Weapons::Knife;
	FClassProperty* PickupProperty = FindFProperty<FClassProperty>(FINV_ItemManifest::StaticStruct(), TEXT("PickupActorClass"));
	PickupProperty->SetObjectPropertyValue_InContainer(&Manifest, PickupClass);
	Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_EquipmentFragment>());
	UINV_InventoryItem* Item = Manifest.Manifest(Controller);
	Item->SetEquippedSlotType(Weapon);
	FINV_EquipmentFragment* Fragment = Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_EquipmentFragment>();
	Fragment->bEquipable = true;
	Inventory->InventoryList.AddEntry(Item);
	auto Pickups = [&]()
	{
		TArray<AActor*> Actors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && It->FindComponentByClass<UINV_ItemComponent>()) Actors.Add(*It);
		}
		return Actors;
	};
	auto CheckUnchanged = [&]()
	{
		TestTrue(TEXT("Rejected drop preserves inventory membership"), Inventory->GetAllInventoryItems().Contains(Item));
		TestEqual(TEXT("Rejected drop preserves equipment slot"), Item->GetEquippedSlotType(), Weapon);
		TestTrue(TEXT("Rejected drop preserves applied equipment state"), Fragment->bEquipable);
		TestEqual(TEXT("Rejected drop creates no pickup"), Pickups().Num(), 0);
	};
	Inventory->Server_DropEquippedItem(Item, Mask);
	CheckUnchanged();
	Inventory->OwningController.Reset();
	Inventory->Server_DropEquippedItem(Item, Weapon);
	CheckUnchanged();
	Inventory->OwningController = Controller;
	Controller->UnPossess();
	Inventory->Server_DropEquippedItem(Item, Weapon);
	CheckUnchanged();
	Controller->Possess(Pawn);
	PickupProperty->SetObjectPropertyValue_InContainer(&Item->GetItemManifestMutable(), nullptr);
	Inventory->Server_DropEquippedItem(Item, Weapon);
	CheckUnchanged();
	// A spawn without an item component must fail cleanly rather than assert or consume the equipment.
	PickupProperty->SetObjectPropertyValue_InContainer(&Item->GetItemManifestMutable(), AActor::StaticClass());
	Inventory->Server_DropEquippedItem(Item, Weapon);
	CheckUnchanged();
	PickupProperty->SetObjectPropertyValue_InContainer(&Item->GetItemManifestMutable(), PickupClass);
	UINV_InventoryItem* Foreign = Manifest.Manifest(Controller);
	Foreign->SetEquippedSlotType(Weapon);
	Inventory->Server_DropEquippedItem(Foreign, Weapon);
	CheckUnchanged();
	// Exercise UI dispatch with the same item reference carried by the real drag operation.
	UINV_SpatialInventory* UI = NewObject<UINV_SpatialInventory>();
	UI->SetOwningPlayer(Controller);
	TestEqual(TEXT("UI resolves the local controller"), UI->GetOwningPlayer(), Controller);
	UINV_EquippedGridSlot* Slot = NewObject<UINV_EquippedGridSlot>(UI);
	Slot->SetInventoryItem(Item);
	*FindFProperty<FStructProperty>(UINV_EquippedGridSlot::StaticClass(), TEXT("EquipmentTypeTag"))
		->ContainerPtrToValuePtr<FGameplayTag>(Slot) = Weapon;
	UI->EquippedGridSlots.Add(Slot);
	UINV_ItemPayload* Payload = NewObject<UINV_ItemPayload>(UI);
	Payload->InitByItem(Item);
	Payload->SetDragSource(EINV_ItemDragSource::EquippedSlot);
	TestTrue(TEXT("Invalid position inside grid page is consumed without world drop"), UI->HandleBackgroundDrop(Payload, true));
	CheckUnchanged();
	TestTrue(TEXT("Equipment source background drop is handled"), UI->HandleBackgroundDrop(Payload, false));
	TestFalse(TEXT("Successful drop removes inventory entry"), Inventory->GetAllInventoryItems().Contains(Item));
	TestFalse(TEXT("Successful drop clears equipment slot"), Item->IsEquipped());
	TestFalse(TEXT("Successful drop runs equipment unequip handler"), Fragment->bEquipable);
	const TArray<AActor*> Spawned = Pickups();
	TestEqual(TEXT("One equipment pickup is created"), Spawned.Num(), 1);
	if (Spawned.Num() == 1)
	{
		const FINV_ItemManifest& Dropped = Spawned[0]->FindComponentByClass<UINV_ItemComponent>()->GetItemManifest();
		TestEqual(TEXT("Pickup preserves equipment type"), Dropped.GetItemType(), Manifest.GetItemType());
		TestFalse(TEXT("Pickup does not retain applied equipment state"), Dropped.GetFragmentOfType<FINV_EquipmentFragment>()->bEquipable);
	}
	Inventory->Server_DropEquippedItem(Item, Weapon);
	UI->HandleBackgroundDrop(Payload, false);
	TestEqual(TEXT("Repeated drop cannot duplicate world item"), Pickups().Num(), 1);
	TestFalse(TEXT("Unrelated background payload is not accepted"), UI->HandleBackgroundDrop(nullptr, false));
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
