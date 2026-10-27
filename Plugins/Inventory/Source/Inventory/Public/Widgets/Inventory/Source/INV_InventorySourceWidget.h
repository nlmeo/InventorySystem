#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/INV_OperationTypes.h"
#include "INV_InventorySourceWidget.generated.h"

class UINV_InventoryComponent;
class UINV_InventoryGrid;
class UButton;
class UTextBlock;
class UImage;
class UINV_InventoryItem;

/** Generic right-side inventory source panel: container, dropped bag, rig, corpse, storage, etc. */
UCLASS(Blueprintable)
class INVENTORY_API UINV_InventorySourceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory|Source")
	void InitializeSource(AActor* InSourceActor, UINV_InventoryComponent* InSourceInventory);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Source")
	void CloseSource();

	UFUNCTION(BlueprintPure, Category = "Inventory|Source")
	bool IsSourceOpen() const { return bSourceOpen; }

	UFUNCTION(BlueprintPure, Category = "Inventory|Source")
	AActor* GetSourceActor() const { return SourceActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Source")
	UINV_InventoryComponent* GetSourceInventory() const { return SourceInventory.Get(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Source")
	UINV_InventoryGrid* GetSourceGrid() const { return SourceGrid; }

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Source")
	void OnSourceOpened();

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Source")
	void OnSourceClosed();

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Source")
	void OnOperationResult(EINV_OperationFailure Failure);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Source")
	void SetQuickTransferTarget(UINV_InventoryComponent* InTargetInventory);

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void OnCloseButtonClicked();
	UFUNCTION()
	void OnSourceItemDoubleClicked(UINV_InventoryItem* Item, int32 GridIndex);

	virtual void OnSourceContextReady() {}

	/** Must be placed in the source widget Blueprint. Grid geometry is owned by Blueprint layout. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UINV_InventoryGrid> SourceGrid;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SourceTitle;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OperationResultText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SourceImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<UINV_InventoryComponent> SourceInventory;
	TWeakObjectPtr<UINV_InventoryComponent> QuickTransferTarget;
	bool bSourceOpen = false;
};
