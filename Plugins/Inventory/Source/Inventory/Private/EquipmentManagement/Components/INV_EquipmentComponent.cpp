#include "EquipmentManagement/Components/INV_EquipmentComponent.h"

#include "EquipmentManagement/EquipActor/INV_EquipActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Utils/INV_InventoryStatics.h"
#include "Items/INV_InventoryItem.h"


UINV_EquipmentComponent::UINV_EquipmentComponent()
{

}

void UINV_EquipmentComponent::SetOwningSkeletalMesh(USkeletalMeshComponent* OwningMesh)
{
	OwningSkeletalMesh = OwningMesh;
	RefreshProxyEquipment();
}

void UINV_EquipmentComponent::InitializeOwner(APlayerController* PlayerController)
{
	if (IsValid(PlayerController))
	{
		if (OwningPlayerController.Get() != PlayerController)
		{
			UnbindInventoryComponent();
			ClearProxyEquipment();
		}
		OwningPlayerController = PlayerController;
	}
	InitInventoryComponent();
}

void UINV_EquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	
	InitPlayerController();
}

void UINV_EquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInventoryComponent();
	ClearProxyEquipment();
	if (OwningPlayerController.IsValid())
		OwningPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnPossessedPawnChange);
	Super::EndPlay(EndPlayReason);
}

void UINV_EquipmentComponent::UnbindInventoryComponent()
{
	if (InventoryComponent.IsValid())
	{
		InventoryComponent->OnItemEquipped.RemoveDynamic(this, &ThisClass::OnItemEquipped);
		InventoryComponent->OnItemUnequipped.RemoveDynamic(this, &ThisClass::OnItemUnequipped);
		InventoryComponent->OnItemAdded.RemoveDynamic(this, &ThisClass::OnInventoryItemChanged);
		InventoryComponent->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnInventoryItemChanged);
		InventoryComponent->OnInventoryRefreshRequested.RemoveAll(this);
	}
	InventoryComponent.Reset();
}

void UINV_EquipmentComponent::InitPlayerController()
{
	//它用的是 GetOwner()，即组件的所属 Actor。这条路径成立的前提是：这个 EquipmentComponent 是挂在 PlayerController（或能被 Cast 成 PC 的 Actor）上的。
	if (OwningPlayerController = Cast<APlayerController>(GetOwner()); OwningPlayerController.IsValid())
	{
		// 初始化时序问题，beginplay时还获取不到Pawn可能导致Mesh空指针
		if (ACharacter* OwnerCharacter = Cast<ACharacter>(OwningPlayerController->GetPawn());IsValid(OwnerCharacter))
		{
			OnPossessedPawnChange(nullptr, OwnerCharacter);
		}
		else
		{
			OwningPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossessedPawnChange);
		}
	}
}

void UINV_EquipmentComponent::OnPossessedPawnChange(APawn* OldPawn, APawn* NewPawn)
{
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(OwningPlayerController->GetPawn());IsValid(OwnerCharacter))
	{
		OwningSkeletalMesh = OwnerCharacter->GetMesh();
	}
	InitInventoryComponent();
}


void UINV_EquipmentComponent::InitInventoryComponent()
{
	UINV_InventoryComponent* Resolved = UINV_InventoryStatics::GetInventoryComponent(OwningPlayerController.Get());
	if (InventoryComponent.Get() != Resolved) UnbindInventoryComponent();
	InventoryComponent = Resolved;
	if (!InventoryComponent.IsValid()) return;
	
	if (!InventoryComponent->OnItemEquipped.IsAlreadyBound(this, &ThisClass::OnItemEquipped))
	{
		InventoryComponent->OnItemEquipped.AddDynamic(this, &ThisClass::OnItemEquipped);
	}
	
	if (!InventoryComponent->OnItemUnequipped.IsAlreadyBound(this, &ThisClass::OnItemUnequipped))
	{
		InventoryComponent->OnItemUnequipped.AddDynamic(this, &ThisClass::OnItemUnequipped);
	}
	if (bIsProxy)
	{
		InventoryComponent->OnItemAdded.AddUniqueDynamic(this, &ThisClass::OnInventoryItemChanged);
		InventoryComponent->OnItemRemoved.AddUniqueDynamic(this, &ThisClass::OnInventoryItemChanged);
		InventoryComponent->OnInventoryRefreshRequested.RemoveAll(this);
		InventoryComponent->OnInventoryRefreshRequested.AddUObject(this, &ThisClass::RefreshProxyEquipment);
		// 初始化之前可能已经自动装备；不能只等待下一次装备事件。
		RefreshProxyEquipment();
	}
}

void UINV_EquipmentComponent::OnInventoryItemChanged(UINV_InventoryItem* Item)
{
	// PreReplicatedRemove 广播时条目仍在列表里，下一拍再读取最终状态。
	if (bIsProxy && InventoryComponent.IsValid()) InventoryComponent->ScheduleRefresh();
}

void UINV_EquipmentComponent::ClearProxyEquipment()
{
	for (const auto& Entry : ProxyEquipmentActors)
	{
		if (AINV_EquipActor* Actor = Entry.Value.Get(); IsValid(Actor))
		{
			EquippedActors.Remove(Actor);
			Actor->Destroy();
		}
	}
	ProxyEquipmentActors.Reset();
	EquippedActors.RemoveAll([](const AINV_EquipActor* Actor) { return !IsValid(Actor); });
}

void UINV_EquipmentComponent::RefreshProxyEquipment()
{
	if (!bIsProxy || !OwningPlayerController.IsValid() || !OwningPlayerController->IsLocalController()
		|| !InventoryComponent.IsValid() || !OwningSkeletalMesh.IsValid()) return;
	const TArray<UINV_InventoryItem*> Items = InventoryComponent->GetAllInventoryItems();
	EquippedActors.RemoveAll([](const AINV_EquipActor* Actor) { return !IsValid(Actor); });
	for (auto It = ProxyEquipmentActors.CreateIterator(); It; ++It)
	{
		UINV_InventoryItem* Item = It.Key().Get();
		AINV_EquipActor* Actor = It.Value().Get();
		if (!IsValid(Item) || !Items.Contains(Item) || !Item->IsEquipped() || !IsValid(Actor)
			|| !Actor->GetRootComponent() || Actor->GetRootComponent()->GetAttachParent() != OwningSkeletalMesh.Get())
		{
			EquippedActors.Remove(Actor);
			if (IsValid(Actor)) Actor->Destroy();
			It.RemoveCurrent();
		}
	}
	for (UINV_InventoryItem* Item : Items)
	{
		if (!IsValid(Item) || !Item->IsEquipped() || ProxyEquipmentActors.Contains(Item)) continue;
		FINV_EquipmentFragment* Fragment = Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_EquipmentFragment>();
		if (!Fragment) continue;
		if (AINV_EquipActor* Actor = SpawnEquippedActor(Fragment, Item->GetItemManifest(), OwningSkeletalMesh.Get()); IsValid(Actor))
		{
			Actor->SetReplicates(false);
			EquippedActors.Add(Actor);
			ProxyEquipmentActors.Add(Item, Actor);
		}
	}
}

AINV_EquipActor* UINV_EquipmentComponent::SpawnEquippedActor(FINV_EquipmentFragment* EquipmentFragment,
	const FINV_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh)
{
	AINV_EquipActor* SpawnedEquipActor = EquipmentFragment->SpawnAttachedActor(AttachMesh);
	if (!IsValid(SpawnedEquipActor)) return nullptr;
	SpawnedEquipActor->SetEquipmentType(EquipmentFragment->GetEquipmentType());
	SpawnedEquipActor->SetOwner(GetOwner());
	if (!bIsProxy) EquipmentFragment->SetEquippedActor(SpawnedEquipActor);
	return SpawnedEquipActor;
}

AINV_EquipActor* UINV_EquipmentComponent::FindEquippedActor(const FGameplayTag& EquipmentTypeTag)
{
	auto FoundActor = EquippedActors.FindByPredicate([EquipmentTypeTag](const AINV_EquipActor* EquippedActor)
	{
		return EquippedActor->GetEquipmentType().MatchesTagExact(EquipmentTypeTag);
	});
	
	return FoundActor ? *FoundActor : nullptr;
}

void UINV_EquipmentComponent::RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag)
{
	if (AINV_EquipActor* EquippedActor = FindEquippedActor(EquipmentTypeTag); IsValid(EquippedActor))
	{
		EquippedActors.Remove(EquippedActor);
		EquippedActor->Destroy();
	}
}



void UINV_EquipmentComponent::OnItemEquipped(UINV_InventoryItem* EquippedItem)
{
	if (bIsProxy)
	{
		RefreshProxyEquipment();
		return;
	}
	if (!IsValid(EquippedItem)) return;
	
	FINV_ItemManifest& ItemManifest = EquippedItem->GetItemManifestMutable();
	FINV_EquipmentFragment* EquipmentFragment = ItemManifest.GetFragmentOfTypeMutable<FINV_EquipmentFragment>();
	if (!EquipmentFragment) return;
	
	if (!OwningPlayerController.IsValid() || !OwningPlayerController->HasAuthority()) return;
	EquipmentFragment->OnEquip(OwningPlayerController.Get());
	
	if (!OwningSkeletalMesh.IsValid()) return;
	AINV_EquipActor* SpawnedEquipActor = SpawnEquippedActor(EquipmentFragment, ItemManifest, OwningSkeletalMesh.Get());
	if (IsValid(SpawnedEquipActor)) EquippedActors.Add(SpawnedEquipActor);
}

void UINV_EquipmentComponent::OnItemUnequipped(UINV_InventoryItem* UnequippedItem)
{
	if (bIsProxy)
	{
		RefreshProxyEquipment();
		return;
	}
	if (!IsValid(UnequippedItem)) return;
	
	FINV_ItemManifest& ItemManifest = UnequippedItem->GetItemManifestMutable();
	FINV_EquipmentFragment* EquipmentFragment = ItemManifest.GetFragmentOfTypeMutable<FINV_EquipmentFragment>();
	if (!EquipmentFragment) return;
	
	if (!OwningPlayerController.IsValid() || !OwningPlayerController->HasAuthority()) return;
	EquipmentFragment->OnUnequip(OwningPlayerController.Get());
	RemoveEquippedActor(EquipmentFragment->GetEquipmentType());
}


