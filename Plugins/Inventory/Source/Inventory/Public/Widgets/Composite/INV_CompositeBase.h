#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "INV_CompositeBase.generated.h"

UCLASS()
class INVENTORY_API UINV_CompositeBase : public UUserWidget
{
	GENERATED_BODY()
public:
	FGameplayTag GetFragmentTag() const { return FragmentTag; }
	void SetFragmentTag(const FGameplayTag& Tag) { FragmentTag = Tag; }
	
	virtual void Collapse();
	virtual void Expand();
	
	using FuncType = TFunction<void(UINV_CompositeBase*)>;
	virtual void ApplyFunction(FuncType Function) {}
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories="FragmentTags"))
	FGameplayTag FragmentTag;
};
