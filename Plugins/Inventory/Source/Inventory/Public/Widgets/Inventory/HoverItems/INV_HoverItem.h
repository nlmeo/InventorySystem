#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Blueprint/UserWidget.h"
#include "INV_HoverItem.generated.h"

class UINV_InventoryItem;
class UTextBlock;
class UImage;


UCLASS()
class INVENTORY_API UINV_HoverItem : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetImageBrush(const FSlateBrush& Brush) const;
	void UpdateStackCount(const int32 Count);
	
	FGameplayTag GetItemType() const;
	int32 GetStackCount() const { return StackCount; }
	bool IsStackable() const { return bIsStackable; }
	void SetIsStackable(bool bStackable);
	
	int32 GetPreviousGridIndex() const { return PreviousGridIndex; }
	void SetPreviousGridIndex(int32 Index) {PreviousGridIndex = Index;}
	FIntPoint GetGridDimensions() const { return GridDimensions; }
	void SetGridDimensions(const FIntPoint& Dimensions) { GridDimensions = Dimensions; }
	UINV_InventoryItem* GetInventoryItem() const;
	void SetInventoryItem(UINV_InventoryItem* Item);
	
private:
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Icon;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_StackCount;
	
	
	int32 PreviousGridIndex{0};
	FIntPoint GridDimensions;
	TWeakObjectPtr<UINV_InventoryItem> InventoryItem;
	bool bIsStackable{false};
	int32 StackCount{0};
};
