#pragma once

#include "CoreMinimal.h"
#include "Widgets/Inventory/Source/INV_InventorySourceWidget.h"
#include "INV_ContainerInventoryWidget.generated.h"

/** Compatibility source widget for ordinary containers. It owns source-side content only. */
UCLASS(Blueprintable)
class INVENTORY_API UINV_ContainerInventoryWidget : public UINV_InventorySourceWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory|Container")
	void InitializeContainer(AActor* InContainerActor, UINV_InventoryComponent* InContainerInventory);

	UFUNCTION(BlueprintPure, Category = "Inventory|Container")
	bool IsContainerOpen() const { return IsSourceOpen(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Container")
	AActor* GetContainerActor() const { return GetSourceActor(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Container")
	UINV_InventoryComponent* GetContainerInventory() const { return GetSourceInventory(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Container")
	UINV_InventoryGrid* GetContainerGrid() const { return GetSourceGrid(); }

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Container")
	void OnContainerOpened();

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Container")
	void OnContainerClosed();

protected:
	virtual void OnSourceContextReady() override;
	virtual void OnSourceOpened_Implementation() override;
	virtual void OnSourceClosed_Implementation() override;
};
