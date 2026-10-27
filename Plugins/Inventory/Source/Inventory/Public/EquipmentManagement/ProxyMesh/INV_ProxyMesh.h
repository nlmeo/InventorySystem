#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "INV_ProxyMesh.generated.h"

class UINV_EquipmentComponent;

UCLASS()
class INVENTORY_API AINV_ProxyMesh : public AActor
{
	GENERATED_BODY()

public:
	AINV_ProxyMesh();

	USkeletalMeshComponent* GetMesh() const{return ProxyMesh;}
	
protected:
	virtual void BeginPlay() override;

private:
	
	// This is the mesh on the PC Character.
	TWeakObjectPtr<USkeletalMeshComponent> SourceMesh;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UINV_EquipmentComponent> EquipmentComponent;
	
	// This is the proxy mesh we will see in the Inventory Menu.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> ProxyMesh;
	
	
	FTimerHandle TimerForNextTick;
	void DelayedInitializeOwner();
	void DelayedInitialization();
};
