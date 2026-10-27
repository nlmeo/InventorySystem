#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "INV_ContainerInteractable.generated.h"

class UINV_InventoryComponent;

UINTERFACE(BlueprintType)
class INVENTORY_API UINV_ContainerInteractable : public UInterface
{
	GENERATED_BODY()
};

/** 可被玩家主交互键打开的容器接口。UI 会由玩家控制器创建，容器只提供库存组件和访问规则。 */
class INVENTORY_API IINV_ContainerInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Container")
	UINV_InventoryComponent* GetContainerInventory();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Container")
	bool CanInteractWith(AActor* Interactor);
};

namespace INV_ContainerInteraction
{
	/** C++ 容器优先走原生实现；蓝图实现则走 Execute 分发。 */
	inline UINV_InventoryComponent* GetInventory(AActor* ContainerActor)
	{
		if (!IsValid(ContainerActor)
			|| !ContainerActor->GetClass()->ImplementsInterface(UINV_ContainerInteractable::StaticClass())) return nullptr;
		if (IINV_ContainerInteractable* Native = Cast<IINV_ContainerInteractable>(ContainerActor))
		{
			return Native->GetContainerInventory_Implementation();
		}
		return IINV_ContainerInteractable::Execute_GetContainerInventory(ContainerActor);
	}

	inline bool CanInteract(AActor* ContainerActor, AActor* Interactor)
	{
		if (!IsValid(ContainerActor)
			|| !ContainerActor->GetClass()->ImplementsInterface(UINV_ContainerInteractable::StaticClass())) return false;
		if (IINV_ContainerInteractable* Native = Cast<IINV_ContainerInteractable>(ContainerActor))
		{
			return Native->CanInteractWith_Implementation(Interactor);
		}
		return IINV_ContainerInteractable::Execute_CanInteractWith(ContainerActor, Interactor);
	}
}
