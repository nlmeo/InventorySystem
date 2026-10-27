#include "Widgets/Inventory/Container/INV_ContainerInventoryWidget.h"

void UINV_ContainerInventoryWidget::InitializeContainer(AActor* InContainerActor,
	UINV_InventoryComponent* InContainerInventory)
{
	InitializeSource(InContainerActor, InContainerInventory);
}

void UINV_ContainerInventoryWidget::OnSourceContextReady()
{
	Super::OnSourceContextReady();
}

void UINV_ContainerInventoryWidget::OnSourceOpened_Implementation()
{
	Super::OnSourceOpened_Implementation();
	OnContainerOpened();
}

void UINV_ContainerInventoryWidget::OnSourceClosed_Implementation()
{
	OnContainerClosed();
	Super::OnSourceClosed_Implementation();
}

void UINV_ContainerInventoryWidget::OnContainerOpened_Implementation()
{
}

void UINV_ContainerInventoryWidget::OnContainerClosed_Implementation()
{
}
