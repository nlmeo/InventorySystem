// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "INV_Highlightable.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UINV_Highlightable : public UInterface
{
	GENERATED_BODY()
};


class INVENTORY_API IINV_Highlightable
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
	void Highlight();

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
	void UnHighlight();
};
