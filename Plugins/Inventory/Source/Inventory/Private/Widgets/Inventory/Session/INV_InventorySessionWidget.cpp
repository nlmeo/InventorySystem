#include "Widgets/Inventory/Session/INV_InventorySessionWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "InputCoreTypes.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Widgets/Inventory/InventoryBase/INV_InventoryBase.h"
#include "Widgets/Inventory/Source/INV_InventorySourceWidget.h"
#include "Widgets/Inventory/Spatial/INV_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/INV_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Blueprint/DragDropOperation.h"
#include "Player/INV_PlayerController.h"

void UINV_InventorySessionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (CloseButton) CloseButton->OnClicked.AddDynamic(this, &ThisClass::OnCloseButtonClicked);
}

void UINV_InventorySessionWidget::InitializeSession(AActor* InSourceActor,
	UINV_InventoryComponent* InPlayerInventory, UINV_InventoryComponent* InSourceInventory,
	TSubclassOf<UINV_InventorySourceWidget> InSourceWidgetClass)
{
	if (!IsValid(InSourceActor) || !IsValid(InPlayerInventory) || !IsValid(InSourceInventory)
		|| InSourceInventory->GetOwner() != InSourceActor || !InSourceWidgetClass)
	{
		CloseSession();
		return;
	}

	CloseSession();
	InitializePlayerInventory(InPlayerInventory);
	OpenPlayerInventory();
	BuildFallbackLayout();
	SourceActor = InSourceActor;
	PlayerInventory = InPlayerInventory;
	SourceInventory = InSourceInventory;
	bSessionOpen = true;

	SourceWidget = CreateWidget<UINV_InventorySourceWidget>(this, InSourceWidgetClass);
	if (SourceWidget && SourceInventoryHost)
	{
		SourceInventoryHost->SetVisibility(ESlateVisibility::Visible);
		SourceInventoryHost->SetIsEnabled(true);
		SourceInventoryHost->AddChild(SourceWidget);
		SourceWidget->SetQuickTransferTarget(InPlayerInventory);
		SourceWidget->InitializeSource(InSourceActor, InSourceInventory);
	}

	SetVisibility(ESlateVisibility::Visible);
}

void UINV_InventorySessionWidget::InitializePlayerInventory(UINV_InventoryComponent* InPlayerInventory)
{
	if (!IsValid(InPlayerInventory) || !IsValid(InPlayerInventory->GetInventoryMenu())) return;
	BuildFallbackLayout();
	PlayerInventory = InPlayerInventory;
	UINV_InventoryBase* ExistingWidget = InPlayerInventory->GetInventoryMenu();
	const bool bAlreadyAttached = PlayerInventoryWidget == ExistingWidget
		&& ExistingWidget && ExistingWidget->GetParent() == PlayerInventoryHost;
	PlayerInventoryWidget = ExistingWidget;
	if (!PlayerInventoryWidget || !PlayerInventoryHost) return;

	if (!bAlreadyAttached && PlayerInventoryWidget->GetParent() != PlayerInventoryHost)
	{
		PlayerInventoryWidget->RemoveFromParent();
		PlayerInventoryHost->AddChild(PlayerInventoryWidget);
	}
	PlayerInventoryHost->SetVisibility(ESlateVisibility::Visible);
	PlayerInventoryHost->SetIsEnabled(true);
	if (!bAlreadyAttached)
	{
		PlayerInventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
		PlayerInventoryWidget->SetIsEnabled(false);
	}
}

void UINV_InventorySessionWidget::OpenPlayerInventory()
{
	if (!PlayerInventoryWidget) return;
	bPlayerInventoryOpen = true;
	bSessionOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	PlayerInventoryWidget->SetVisibility(ESlateVisibility::Visible);
	PlayerInventoryWidget->SetIsEnabled(true);
}

void UINV_InventorySessionWidget::ClosePlayerInventory()
{
	if (!PlayerInventoryWidget) return;
	PlayerInventoryWidget->OnInventoryClosed();
	PlayerInventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
	PlayerInventoryWidget->SetIsEnabled(false);
	bPlayerInventoryOpen = false;
	if (!SourceWidget || !SourceWidget->IsSourceOpen())
	{
		bSessionOpen = false;
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UINV_InventorySessionWidget::TogglePlayerInventory()
{
	if (bPlayerInventoryOpen) ClosePlayerInventory();
	else OpenPlayerInventory();
}

void UINV_InventorySessionWidget::SetQuickTransferTarget(UINV_InventoryComponent* InTargetInventory)
{
	if (SourceWidget) SourceWidget->SetQuickTransferTarget(InTargetInventory);
}

void UINV_InventorySessionWidget::CloseSession()
{
	if (!bSessionOpen && !PlayerInventoryWidget && !SourceWidget) return;

	bSessionOpen = false;
	if (SourceWidget)
	{
		SourceWidget->CloseSource();
		SourceWidget->RemoveFromParent();
	}
	SourceWidget = nullptr;
	HidePlayerInventoryWidget();
	SourceActor.Reset();
	PlayerInventory.Reset();
	SourceInventory.Reset();
	bPlayerInventoryOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UINV_InventorySessionWidget::ForwardOperationResult(EINV_OperationFailure Failure)
{
	if (PlayerInventoryWidget)
	{
		if (UINV_SpatialInventory* Spatial = Cast<UINV_SpatialInventory>(PlayerInventoryWidget))
		{
			Spatial->ClearDragInteractionState();
		}
	}
	if (SourceWidget) SourceWidget->OnOperationResult(Failure);
}

void UINV_InventorySessionWidget::NativeDestruct()
{
	CloseSession();
	Super::NativeDestruct();
}

FReply UINV_InventorySessionWidget::NativeOnKeyDown(const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer()))
		{
			PC->CloseContainerInteraction();
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UINV_InventorySessionWidget::NativeOnDrop(const FGeometry& InGeometry,
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	if (SourceWidget && Payload && SourceWidget->GetSourceGrid()
		&& SourceWidget->GetSourceGrid()->HasHoverItem()
		&& SourceWidget->GetSourceGrid()->GetHoverItem()->GetInventoryItem() == Payload->GetInventoryItem())
	{
		SourceWidget->GetSourceGrid()->DropItem();
		return true;
	}
	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

void UINV_InventorySessionWidget::OnCloseButtonClicked()
{
	if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer()))
	{
		PC->CloseContainerInteraction();
	}
}

void UINV_InventorySessionWidget::BuildFallbackLayout()
{
	if (PlayerInventoryHost && SourceInventoryHost) return;
	if (!WidgetTree) return;

	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UVerticalBox* PlayerHost = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UVerticalBox* SourceHost = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Root->AddChildToHorizontalBox(PlayerHost);
	Root->AddChildToHorizontalBox(SourceHost);
	FallbackRoot = Root;
	WidgetTree->RootWidget = Root;
	PlayerInventoryHost = PlayerHost;
	SourceInventoryHost = SourceHost;
}

void UINV_InventorySessionWidget::HidePlayerInventoryWidget()
{
	if (!PlayerInventoryWidget) return;
	PlayerInventoryWidget->OnInventoryClosed();
	PlayerInventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
	PlayerInventoryWidget->SetIsEnabled(false);
}
