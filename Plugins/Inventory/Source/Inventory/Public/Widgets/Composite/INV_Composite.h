#pragma once

#include "CoreMinimal.h"
#include "INV_CompositeBase.h"
#include "INV_Composite.generated.h"

UCLASS()
class INVENTORY_API UINV_Composite : public UINV_CompositeBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void ApplyFunction(FuncType Function) override;
	virtual void Collapse() override;
private:
	UPROPERTY()
	TArray<TObjectPtr<UINV_CompositeBase>> Children;
};
