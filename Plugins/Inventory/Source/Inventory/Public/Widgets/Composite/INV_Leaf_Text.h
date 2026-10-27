// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "INV_Leaf.h"
#include "INV_Leaf_Text.generated.h"

class UTextBlock;
/**
 * 
 */
UCLASS()
class INVENTORY_API UINV_Leaf_Text : public UINV_Leaf
{
	GENERATED_BODY()

public:
	void SetText(const FText& Text) const;
	virtual void NativePreConstruct() override;
private:
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_LeafText;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float FontSize{12};
};
