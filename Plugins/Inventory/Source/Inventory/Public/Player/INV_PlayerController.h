

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Types/INV_OperationTypes.h"
#include "INV_PlayerController.generated.h"


class UInputMappingContext;
class UInputAction;
class UINV_HUDWidget;
class UINV_InventoryComponent;
class UINV_InventorySourceWidget;
class UINV_InventorySessionWidget;
class UINV_InventoryItem;
class UINV_InventoryInteractionSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FContainerInteractionRequested,
	AActor*, ContainerActor, UINV_InventoryComponent*, ContainerInventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FContainerInteractionClosed);
/**
 * 
 */
UCLASS()
class INVENTORY_API AINV_PlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AINV_PlayerController();
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void ToggleInventory();

	/** 当前视线目标是可交互容器时，由主交互键触发，交给 UI 层创建双栏界面。 */
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Container")
	FContainerInteractionRequested OnContainerInteractionRequested;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Container")
	FContainerInteractionClosed OnContainerInteractionClosed;

	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void CloseContainerInteraction();

	UFUNCTION(BlueprintPure, Category = "Inventory|Container")
	UINV_InventoryComponent* GetActiveContainerInventory() const { return ActiveContainerInventory.Get(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Interaction")
	UINV_InventoryInteractionSubsystem* GetInventoryInteractionSubsystem() const;

	UFUNCTION(Server, Reliable)
	void ServerSetContainerOpen(UINV_InventoryComponent* ContainerInventory, bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void SubmitContainerTransfer(UINV_InventoryComponent* Source, UINV_InventoryComponent* Target,
		UINV_InventoryItem* Item, FName TargetSlot, int32 TargetIndex, int32 Amount,
		bool bRotated, UINV_InventoryItem* MergeTarget,
		const TArray<FINV_CrossContainerSwapEntry>& SwapEntries);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void SubmitContainerQuickTransfer(UINV_InventoryComponent* Source, UINV_InventoryComponent* Target,
		UINV_InventoryItem* Item);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void SubmitContainerOperation(UINV_InventoryComponent* Component, FINV_InteractionContext Context);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void SubmitContainerEquip(UINV_InventoryComponent* Source, UINV_InventoryItem* Item,
		FGameplayTag EquipmentSlotType);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void SubmitContainerDrop(UINV_InventoryComponent* Source, UINV_InventoryItem* Item, int32 Amount);

	UFUNCTION(Server, Reliable)
	void ServerContainerTransfer(UINV_InventoryComponent* Source, UINV_InventoryComponent* Target,
		FINV_InteractionContext Context);

	UFUNCTION(Server, Reliable)
	void ServerContainerOperation(UINV_InventoryComponent* Component, FINV_InteractionContext Context);

	UFUNCTION(Server, Reliable)
	void ServerContainerEquip(UINV_InventoryComponent* Source, UINV_InventoryItem* Item,
		FGameplayTag EquipmentSlotType);

	UFUNCTION(Server, Reliable)
	void ServerContainerDrop(UINV_InventoryComponent* Source, UINV_InventoryItem* Item, int32 Amount);

	UFUNCTION(Client, Reliable)
	void ClientContainerResult(EINV_OperationFailure Failure);

	UFUNCTION(Client, Reliable)
	void ClientContainerRejected();

	/**
	 * 【测试用】#14 验收：构造一次「与组外物品重叠」的非法批量移动，验证 Server_MoveItems 硬拒绝。
	 * PIE 控制台输入：INV_TestMoveItemsOverlap
	 */
	UFUNCTION(Exec)
	void INV_TestMoveItemsOverlap();

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void SetupInputComponent() override;
private:
	void PrimaryInteract();
	void CreateHUDWidget();
	void TraceForItem();
	void RequestContainerInteraction(AActor* ContainerActor);
	void OpenContainerWidget(AActor* ContainerActor, UINV_InventoryComponent* ContainerInventory);

	TWeakObjectPtr<UINV_InventoryComponent> InventoryComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TObjectPtr<UInputMappingContext> DefaultIMC;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TObjectPtr<UInputAction> PrimaryInteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TObjectPtr<UInputAction> ToggleInventoryAction;


	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TSubclassOf<UINV_HUDWidget> HUDWidgetClass;
	
	UPROPERTY()
	TObjectPtr<UINV_HUDWidget> HUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Container")
	TSubclassOf<UINV_InventorySourceWidget> ContainerInventoryWidgetClass;

	UPROPERTY()
	TObjectPtr<UINV_InventorySessionWidget> InventorySessionWidget;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Container")
	TSubclassOf<UINV_InventorySessionWidget> InventorySessionWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	float TraceLength;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TEnumAsByte<ECollisionChannel> ItemTraceChannel;

	TWeakObjectPtr<AActor> ThisActor;
	TWeakObjectPtr<AActor> LastActor;
	TWeakObjectPtr<AActor> FocusedContainer;
	TWeakObjectPtr<AActor> ActiveContainerActor;
	TWeakObjectPtr<UINV_InventoryComponent> ActiveContainerInventory;
	TWeakObjectPtr<UINV_InventoryComponent> ServerActiveContainerInventory;
};
