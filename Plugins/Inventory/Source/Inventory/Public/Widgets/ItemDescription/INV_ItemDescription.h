#pragma once

#include "CoreMinimal.h"
#include "Widgets/Composite/INV_Composite.h"
#include "INV_ItemDescription.generated.h"


class USizeBox;

UCLASS()
class INVENTORY_API UINV_ItemDescription : public UINV_Composite
{
	GENERATED_BODY()
	
public:
	
	FVector2D GetBoxSize() const;
	
private:
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SizeBox;
};