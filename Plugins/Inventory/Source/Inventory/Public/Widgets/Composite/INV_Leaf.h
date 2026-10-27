#pragma once

#include "CoreMinimal.h"
#include "INV_CompositeBase.h"
#include "INV_Leaf.generated.h"

UCLASS()
class INVENTORY_API UINV_Leaf : public UINV_CompositeBase
{
	GENERATED_BODY()
public:
	virtual void ApplyFunction(FuncType Function) override;
};
