#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "INV_GridSlot.generated.h"

class UINV_ItemPopUp;
class UINV_InventoryItem;
class UINV_ItemPayload;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGridSlotMouseEvent, int32, GridIndex, const FPointerEvent&, MouseEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGirdSlotDropped, int32, GridIndex, UINV_ItemPayload*, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGridSlotDragEvent, int32, GridIndex, UINV_ItemPayload*, Payload);

UENUM(BlueprintType)
enum class EINV_GridSlotState : uint8
{
	Unoccupied,
	Occupied,
	Selected,
	GrayedOut
};

UCLASS()
class INVENTORY_API UINV_GridSlot : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	
	//virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	
	void SetTileIndex(int32 Index) { TileIndex = Index; };
	int32 GetTileIndex() const { return TileIndex; };
	EINV_GridSlotState GetGridSlotState() const { return GridSlotState; };
	TWeakObjectPtr<UINV_InventoryItem> GetInventoryItem() const { return InventoryItem; };
	void SetInventoryItem(UINV_InventoryItem* Item);
	int32 GetStackCount() const { return StackCount; };
	void SetStackCount(int32 Count) { StackCount = Count; };
	int32 GetUpperLeftIndex() const { return UpperLeftIndex; };
	void SetUpperLeftIndex(int32 Index) { UpperLeftIndex = Index; };
	bool IsAvailable() const { return bAvailable; };
	void SetAvailable(bool bIsAvailable) { bAvailable = bIsAvailable; };
	
	void SetItemPopUp(UINV_ItemPopUp* PopUp);
	UINV_ItemPopUp* GetItemPopUp() const;
	
	virtual void SetOccupiedTexture();
	virtual void SetUnoccupiedTexture();
	virtual void SetSelectedTexture();
	virtual void SetGrayedOutTexture();
	
	//FGridSlotMouseEvent GridSlotClicked;
	FGridSlotMouseEvent GridSlotHovered;
	FGridSlotMouseEvent GridSlotUnhovered;
	FGirdSlotDropped GridSlotDropped;
	FGridSlotDragEvent GridSlotDragEnter;
	FGridSlotDragEvent GridSlotDragLeave;
private:
	
	
	bool bAvailable{true};
	int32 StackCount{0};
	int32 TileIndex{INDEX_NONE};
	int32 UpperLeftIndex{INDEX_NONE};
	
	
	TWeakObjectPtr<UINV_InventoryItem> InventoryItem;
	
	TWeakObjectPtr<UINV_ItemPopUp> ItemPopUp;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_GridSlot;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_Unoccupied;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_Occupied;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_Selected;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FSlateBrush Brush_GrayedOut;
	
	EINV_GridSlotState GridSlotState;
	
	UFUNCTION()
	void OnItemPopUpDestruct(UUserWidget* Menu);
};
