#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "INV_InventoryInteractionSubsystem.generated.h"

class UINV_InventoryComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInventorySourceSessionStarted,
	AActor*, SourceActor, UINV_InventoryComponent*, SourceInventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FInventorySourceSessionEnded);

/**
 * Per-player inventory interaction session.
 * Owns the active source context only; inventory data and server authority remain
 * in InventoryComponent and the player controller RPCs.
 */
UCLASS(BlueprintType)
class INVENTORY_API UINV_InventoryInteractionSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory|Interaction")
	bool BeginInventorySource(AActor* SourceActor, UINV_InventoryComponent* SourceInventory);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Interaction")
	void EndInventorySource();

	UFUNCTION(BlueprintPure, Category = "Inventory|Interaction")
	bool HasActiveInventorySource() const { return ActiveSourceActor.IsValid() && ActiveSourceInventory.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Interaction")
	AActor* GetActiveSourceActor() const { return ActiveSourceActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Interaction")
	UINV_InventoryComponent* GetActiveSourceInventory() const { return ActiveSourceInventory.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Interaction")
	FInventorySourceSessionStarted OnInventorySourceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Interaction")
	FInventorySourceSessionEnded OnInventorySourceEnded;

private:
	TWeakObjectPtr<AActor> ActiveSourceActor;
	TWeakObjectPtr<UINV_InventoryComponent> ActiveSourceInventory;
};
