#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "INV_EquipmentComponent.generated.h"

struct FINV_ItemManifest;
struct FINV_EquipmentFragment;
class AINV_EquipActor;
class UINV_InventoryItem;
class UINV_InventoryComponent;
class APlayerController;
class USkeletalMeshComponent;
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class INVENTORY_API UINV_EquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UINV_EquipmentComponent();
	void SetOwningSkeletalMesh(USkeletalMeshComponent* OwningMesh);
	void SetIsProxy(bool bProxy) {bIsProxy = bProxy;};
	void InitializeOwner(APlayerController* PlayerController);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend class FINV_ProxyEquipmentTest;
	void RefreshProxyEquipment();
	void ClearProxyEquipment();
	void UnbindInventoryComponent();
	UFUNCTION()
	void OnInventoryItemChanged(UINV_InventoryItem* Item);
	// 每个代理独立记录物品到 Actor 的对应关系，不能与真实角色共享 fragment 内的 Actor 引用。
	TMap<TWeakObjectPtr<UINV_InventoryItem>, TWeakObjectPtr<AINV_EquipActor>> ProxyEquipmentActors;
	
	TWeakObjectPtr<UINV_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<APlayerController> OwningPlayerController;
	TWeakObjectPtr<USkeletalMeshComponent> OwningSkeletalMesh;
	
	UFUNCTION()
	void OnItemEquipped(UINV_InventoryItem* EquippedItem);
	
	UFUNCTION()
	void OnItemUnequipped(UINV_InventoryItem* UnequippedItem);

	void InitInventoryComponent();
	AINV_EquipActor* SpawnEquippedActor(FINV_EquipmentFragment* EquipmentFragment, const FINV_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh);

	UPROPERTY()
	TArray<TObjectPtr<AINV_EquipActor>> EquippedActors;
	
	AINV_EquipActor* FindEquippedActor(const FGameplayTag& EquipmentTypeTag);
	
	void RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag);
	
	void InitPlayerController();
	
	UFUNCTION()
	void OnPossessedPawnChange(APawn* OldPawn, APawn* NewPawn);
	
	bool bIsProxy{false};
};
