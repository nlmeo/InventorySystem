#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/INV_OperationTypes.h"
#include "INV_InventorySessionWidget.generated.h"

class UINV_InventoryBase;
class UINV_InventoryComponent;
class UINV_InventorySourceWidget;
class UPanelWidget;
class UButton;

/** Composite inventory session: existing player SpatiallyInventory + one source-side widget. */
UCLASS(Blueprintable)
class INVENTORY_API UINV_InventorySessionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void InitializeSession(AActor* InSourceActor, UINV_InventoryComponent* InPlayerInventory,
		UINV_InventoryComponent* InSourceInventory,
		TSubclassOf<UINV_InventorySourceWidget> InSourceWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void InitializePlayerInventory(UINV_InventoryComponent* InPlayerInventory);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void OpenPlayerInventory();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void ClosePlayerInventory();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void TogglePlayerInventory();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void CloseSession();

	UFUNCTION(BlueprintPure, Category = "Inventory|Session")
	bool IsSessionOpen() const { return bSessionOpen; }
	UFUNCTION(BlueprintPure, Category = "Inventory|Session")
	bool IsPlayerInventoryOpen() const { return bPlayerInventoryOpen; }

	UFUNCTION(BlueprintPure, Category = "Inventory|Session")
	UINV_InventorySourceWidget* GetSourceWidget() const { return SourceWidget; }

	UFUNCTION(BlueprintCallable, Category = "Inventory|Session")
	void ForwardOperationResult(EINV_OperationFailure Failure);
	void SetQuickTransferTarget(UINV_InventoryComponent* InTargetInventory);

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> PlayerInventoryHost;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> SourceInventoryHost;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

private:
	UFUNCTION()
	void OnCloseButtonClicked();

	void BuildFallbackLayout();
	void HidePlayerInventoryWidget();

	UPROPERTY()
	TObjectPtr<UINV_InventoryBase> PlayerInventoryWidget;

	UPROPERTY()
	TObjectPtr<UINV_InventorySourceWidget> SourceWidget;

	UPROPERTY()
	TObjectPtr<UPanelWidget> FallbackRoot;

	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<UINV_InventoryComponent> PlayerInventory;
	TWeakObjectPtr<UINV_InventoryComponent> SourceInventory;
	bool bSessionOpen = false;
	bool bPlayerInventoryOpen = false;
};
