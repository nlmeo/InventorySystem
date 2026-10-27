#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "INV_HUDWidget.generated.h"


class UINV_TmpInfoMessage;

UCLASS()
class INVENTORY_API UINV_HUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:

	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void ShowPickupMessage(const FString& Message);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void HidePickupMessage();
private:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UINV_TmpInfoMessage> TmpInfoMessage;

	UFUNCTION()
	void OnNoRoom();
};
