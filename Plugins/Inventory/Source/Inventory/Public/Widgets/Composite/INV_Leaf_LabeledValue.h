#pragma once

#include "CoreMinimal.h"
#include "INV_Leaf.h"
#include "INV_Leaf_LabeledValue.generated.h"

class UTextBlock;

UCLASS()
class INVENTORY_API UINV_Leaf_LabeledValue : public UINV_Leaf
{
	GENERATED_BODY()
public:
	virtual void NativePreConstruct() override;
	void SetText_Label(const FText& Text, bool bCollapse) const;
	void SetText_Value(const FText& Text, bool bCollapse) const;
	
private:
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Label;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Value;
	
	UPROPERTY(EditAnywhere, Category="Inventory")
	int32 FontSize_Label{12};
	
	
	UPROPERTY(EditAnywhere, Category="Inventory")
	int32 FontSize_Value{18};
	
};
