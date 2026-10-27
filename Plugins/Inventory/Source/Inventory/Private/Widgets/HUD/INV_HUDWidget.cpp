#include "Widgets/HUD/INV_HUDWidget.h"
#include "InventoryManagement/Utils/INV_InventoryStatics.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Widgets/HUD/INV_TmpInfoMessage.h"
void UINV_HUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UINV_InventoryComponent* InventoryComponent = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());

	if (IsValid(InventoryComponent))
	{
		InventoryComponent->NoRoomInInventory.AddDynamic(this, &ThisClass::OnNoRoom);
	}
}

void UINV_HUDWidget::NativeDestruct()
{
	// 先解绑
	UINV_InventoryComponent* InventoryComponent = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->NoRoomInInventory.RemoveDynamic(this, &ThisClass::OnNoRoom);
	}

	Super::NativeDestruct();
}

void UINV_HUDWidget::OnNoRoom()
{
	if (!IsValid(TmpInfoMessage)) return;
	
	TmpInfoMessage->SetMessage(FText::FromString(TEXT("背包容量不足，请整理后再尝试交互")));
}
