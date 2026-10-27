#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "EquipmentManagement/Components/INV_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/INV_EquipActor.h"
#include "EquipmentManagement/ProxyMesh/INV_ProxyMesh.h"
#include "GameFramework/PlayerController.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "Items/Components/INV_ItemComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_ProxyEquipmentTest, "Inventory.Equipment.ProxyStateRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_ProxyEquipmentTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues InitialValues = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryProxyTestWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &InitialValues);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	UClass* KnifeClass = LoadClass<AINV_EquipActor>(nullptr,
		TEXT("/Inventory/Items/Equippables/Weapons/BP_INV_EquipActor_Knife.BP_INV_EquipActor_Knife_C"));
	UClass* AxeClass = LoadClass<AINV_EquipActor>(nullptr,
		TEXT("/Inventory/Items/Equippables/Weapons/BP_INV_EquipActor_Axe.BP_INV_EquipActor_Axe_C"));
	if (!TestNotNull(TEXT("Knife equipment blueprint"), KnifeClass) || !TestNotNull(TEXT("Axe equipment blueprint"), AxeClass))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	APlayerController* PC = World->SpawnActor<APlayerController>();
	PC->SetAsLocalPlayerController();
	TestTrue(TEXT("Proxy owner is a local controller"), PC->IsLocalController());
	UINV_InventoryComponent* Inventory = NewObject<UINV_InventoryComponent>(PC);
	PC->AddInstanceComponent(Inventory);
	const FGameplayTag WeaponSlot = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Weapons"));
	auto MakePickup = [&](UClass* ActorClass)
	{
		FINV_ItemManifest Manifest;
		FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		Category->GetUnderlyingProperty()->SetIntPropertyValue(Category->ContainerPtrToValuePtr<void>(&Manifest),
			static_cast<int64>(EINV_ItemCategory::Equippable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = GameItems::Equipment::Weapons::Knife;
		FINV_GridFragment Grid;
		Grid.SetGridSize(FIntPoint(1, 1));
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(Grid));
		FINV_EquipmentFragment Equipment;
		FindFProperty<FClassProperty>(FINV_EquipmentFragment::StaticStruct(), TEXT("EquipActorClass"))
			->SetObjectPropertyValue_InContainer(&Equipment, ActorClass);
		*FindFProperty<FStructProperty>(FINV_EquipmentFragment::StaticStruct(), TEXT("EquipmentType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Equipment) = WeaponSlot;
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_EquipmentFragment>(Equipment));
		AActor* PickupActor = World->SpawnActor<AActor>();
		UINV_ItemComponent* Pickup = NewObject<UINV_ItemComponent>(PickupActor);
		PickupActor->AddInstanceComponent(Pickup);
		Pickup->InitItemManifest(Manifest);
		return Pickup;
	};

	// Pick up before the proxy exists, with no inventory widget ever constructed or opened.
	Inventory->AddPickupOnServer(MakePickup(KnifeClass), WeaponSlot);
	TestEqual(TEXT("Direct pickup creates an equipped item"), Inventory->GetAllInventoryItems().Num(), 1);
	TestNull(TEXT("No inventory UI is constructed"), Inventory->GetInventoryMenu());
	TestFalse(TEXT("Inventory UI remains closed"), Inventory->IsMenuOpen());
	UINV_InventoryItem* Knife = Inventory->GetAllInventoryItems()[0];
	TestTrue(TEXT("Pickup records persistent equipment membership"), Knife->IsEquipped());
	AINV_ProxyMesh* Proxy = World->SpawnActor<AINV_ProxyMesh>();
	UINV_EquipmentComponent* Equipment = Proxy->FindComponentByClass<UINV_EquipmentComponent>();
	Equipment->InitializeOwner(PC);
	TestEqual(TEXT("Late proxy recovers missed equipment event"), Equipment->EquippedActors.Num(), 1);
	if (Equipment->EquippedActors.Num() != 1)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	AINV_EquipActor* KnifeVisual = Equipment->EquippedActors[0];
	TestEqual(TEXT("Proxy recovers the correct equipment class"), KnifeVisual->GetClass(), KnifeClass);
	TestTrue(TEXT("Recovered equipment attaches to proxy mesh"), KnifeVisual->GetRootComponent()->GetAttachParent() == Proxy->GetMesh());
	TestFalse(TEXT("Proxy equipment is local-only"), KnifeVisual->GetIsReplicated());
	Equipment->InitializeOwner(PC);
	Inventory->OnInventoryRefreshRequested.Broadcast();
	Inventory->OnItemEquipped.Broadcast(Knife);
	TestEqual(TEXT("Repeated initialization and events do not duplicate visuals"), Equipment->EquippedActors.Num(), 1);
	TestTrue(TEXT("Idempotent refresh keeps existing actor"), Equipment->EquippedActors[0] == KnifeVisual);
	FINV_EquipmentFragment* KnifeFragment = Knife->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_EquipmentFragment>();
	TestFalse(TEXT("Proxy recovery does not apply gameplay equipment modifiers"), KnifeFragment->bEquipable);
	AINV_EquipActor* RealActor = World->SpawnActor<AINV_EquipActor>(KnifeClass);
	KnifeFragment->SetEquippedActor(RealActor);
	Equipment->ClearProxyEquipment();
	Equipment->RefreshProxyEquipment();
	KnifeFragment->DestroyAttachedActor();
	TestTrue(TEXT("Proxy refresh does not overwrite real actor reference"), RealActor->IsActorBeingDestroyed());
	TestFalse(TEXT("Destroying real actor does not destroy proxy visual"), Equipment->EquippedActors[0]->IsActorBeingDestroyed());

	// Lost/null equipment RPC: only replicated membership and inventory refresh arrive later.
	Knife->SetEquippedSlotType(FGameplayTag());
	UINV_InventoryItem* Axe = Inventory->CreateItemEntry(MakePickup(AxeClass), 0);
	Inventory->FlushRefresh();
	TestEqual(TEXT("Unequipped state removes stale proxy equipment"), Equipment->EquippedActors.Num(), 0);
	Axe->SetEquippedSlotType(WeaponSlot);
	Axe->OnRep_EquippedSlotType();
	Inventory->FlushRefresh();
	TestEqual(TEXT("Replicated state recovers missed RPC"), Equipment->EquippedActors.Num(), 1);
	if (Equipment->EquippedActors.Num() == 1)
		TestEqual(TEXT("Replacement uses the newly equipped class"), Equipment->EquippedActors[0]->GetClass(), AxeClass);

	// The skeletal mesh can also become available after the owner and inventory.
	AActor* DelayedOwner = World->SpawnActor<AActor>();
	UINV_EquipmentComponent* Delayed = NewObject<UINV_EquipmentComponent>(DelayedOwner);
	DelayedOwner->AddInstanceComponent(Delayed);
	Delayed->SetIsProxy(true);
	Delayed->InitializeOwner(PC);
	TestEqual(TEXT("Missing mesh defers visual construction"), Delayed->EquippedActors.Num(), 0);
	USkeletalMeshComponent* DelayedMesh = NewObject<USkeletalMeshComponent>(DelayedOwner);
	DelayedOwner->AddInstanceComponent(DelayedMesh);
	DelayedMesh->RegisterComponent();
	Delayed->SetOwningSkeletalMesh(DelayedMesh);
	TestEqual(TEXT("Late mesh triggers equipment recovery"), Delayed->EquippedActors.Num(), 1);

	Inventory->InventoryList.RemoveEntry(Axe);
	Inventory->FlushRefresh();
	TestEqual(TEXT("Item removal clears original proxy"), Equipment->EquippedActors.Num(), 0);
	TestEqual(TEXT("Item removal clears delayed proxy"), Delayed->EquippedActors.Num(), 0);
	TestFalse(TEXT("All recovery works without opening inventory"), Inventory->IsMenuOpen());
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
