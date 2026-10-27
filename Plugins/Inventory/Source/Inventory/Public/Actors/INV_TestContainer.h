#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/INV_ContainerInteractable.h"
#include "INV_TestContainer.generated.h"

class UBoxComponent;
class UINV_InventoryComponent;
class UStaticMeshComponent;

/** 用于验证资产网格、F 键交互和跨容器转移的最小容器 Actor。 */
UCLASS(Blueprintable)
class INVENTORY_API AINV_TestContainer : public AActor, public IINV_ContainerInteractable
{
	GENERATED_BODY()

public:
	AINV_TestContainer();

	virtual UINV_InventoryComponent* GetContainerInventory_Implementation() override;
	virtual bool CanInteractWith_Implementation(AActor* Interactor) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<UStaticMeshComponent> ContainerMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<UBoxComponent> InteractionCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<UINV_InventoryComponent> InventoryComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Interaction")
	bool bCanInteract = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Interaction", meta = (ClampMin = "0.0"))
	float InteractionDistance = 250.0f;
};
