#include "Widgets/Inventory/Source/INV_InventorySourceWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "InputCoreTypes.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/INV_InventoryItem.h"
#include "InventoryManagement/Layout/INV_GridLayoutTypes.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Player/INV_PlayerController.h"
#include "Blueprint/DragDropOperation.h"

namespace
{
	FName FirstGridSlot(UINV_InventoryComponent* Component)
	{
		if (!IsValid(Component)) return NAME_None;
		TArray<const FINV_GridSlotDefinition*> Panels;
		Component->GetAllGridPanels(Panels);
		for (const FINV_GridSlotDefinition* Panel : Panels)
		{
			if (Panel && Panel->ContainerType == EINV_GridContainerType::Grid) return Panel->SlotId;
		}
		return NAME_None;
	}
}

void UINV_InventorySourceWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (CloseButton) CloseButton->OnClicked.AddDynamic(this, &ThisClass::OnCloseButtonClicked);
}

void UINV_InventorySourceWidget::InitializeSource(AActor* InSourceActor,
	UINV_InventoryComponent* InSourceInventory)
{
	if (!IsValid(InSourceActor) || !IsValid(InSourceInventory)
		|| InSourceInventory->GetOwner() != InSourceActor)
	{
		CloseSource();
		return;
	}

	SourceActor = InSourceActor;
	SourceInventory = InSourceInventory;
	bSourceOpen = true;
	if (!SourceGrid)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory][SourceWidget] %s 未绑定 SourceGrid；来源网格必须在蓝图中使用 BindWidget 配置，已取消打开。"),
			*GetNameSafe(this));
		CloseSource();
		return;
	}

	const FName SourceSlot = FirstGridSlot(InSourceInventory);
	if (SourceSlot.IsNone())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory][SourceWidget] %s 的来源库存没有有效 Grid 面板；请检查 GridLayoutAsset 和 SlotId，已取消打开。"),
			*GetNameSafe(this));
		CloseSource();
		return;
	}
	SourceGrid->InitializeForInventory(InSourceInventory, SourceSlot);
	SourceGrid->SetSourceInteractionMode(true);
	SourceGrid->OnItemDoubleClicked.RemoveDynamic(this, &ThisClass::OnSourceItemDoubleClicked);
	SourceGrid->OnItemDoubleClicked.AddDynamic(this, &ThisClass::OnSourceItemDoubleClicked);
	SourceGrid->SetVisibility(ESlateVisibility::Visible);
	SourceGrid->SetIsEnabled(true);

	SetVisibility(ESlateVisibility::Visible);
	OnSourceContextReady();
	OnSourceOpened();
}

void UINV_InventorySourceWidget::CloseSource()
{
	if (!bSourceOpen && !SourceActor.IsValid() && !SourceInventory.IsValid()) return;

	bSourceOpen = false;
	OnSourceClosed();
	if (SourceGrid) { SourceGrid->SetSourceInteractionMode(false); SourceGrid->UnbindInventory(); }
	if (SourceGrid) SourceGrid->OnItemDoubleClicked.RemoveDynamic(this, &ThisClass::OnSourceItemDoubleClicked);
	SourceActor.Reset();
	SourceInventory.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UINV_InventorySourceWidget::SetQuickTransferTarget(UINV_InventoryComponent* InTargetInventory)
{
	QuickTransferTarget = InTargetInventory;
}

void UINV_InventorySourceWidget::OnSourceItemDoubleClicked(UINV_InventoryItem* Item, int32 GridIndex)
{
	if (!IsValid(Item) || !QuickTransferTarget.IsValid()) return;
	if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer()))
	{
		PC->SubmitContainerQuickTransfer(SourceInventory.Get(), QuickTransferTarget.Get(), Item);
	}
}

void UINV_InventorySourceWidget::NativeDestruct()
{
	CloseSource();
	Super::NativeDestruct();
}

FReply UINV_InventorySourceWidget::NativeOnKeyDown(const FGeometry& InGeometry,
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

bool UINV_InventorySourceWidget::NativeOnDrop(const FGeometry& InGeometry,
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	if (!IsValid(Payload) || !IsValid(SourceGrid)
		|| !SourceGrid->HasHoverItem()
		|| SourceGrid->GetHoverItem()->GetInventoryItem() != Payload->GetInventoryItem())
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	// A drop that bubbles out of the source grid is a world drop. Reuse the
	// source grid's authoritative Drop path so container items are not removed
	// locally before the server accepts the spawn.
	SourceGrid->DropItem();
	return true;
}

void UINV_InventorySourceWidget::OnCloseButtonClicked()
{
	if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer()))
	{
		PC->CloseContainerInteraction();
	}
}

void UINV_InventorySourceWidget::OnSourceOpened_Implementation()
{
	if (SourceTitle) SourceTitle->SetText(FText::FromString(GetNameSafe(SourceActor.Get())));
	if (OperationResultText) OperationResultText->SetText(FText::GetEmpty());
}

void UINV_InventorySourceWidget::OnSourceClosed_Implementation()
{
}

void UINV_InventorySourceWidget::OnOperationResult_Implementation(EINV_OperationFailure Failure)
{
	if (SourceGrid)
	{
		SourceGrid->ClearExternalDragPayload();
		SourceGrid->ClearHoverItem();
	}
	if (!OperationResultText) return;
	const TCHAR* Message = TEXT("操作失败");
	switch (Failure)
	{
	case EINV_OperationFailure::None: Message = TEXT("操作成功"); break;
	case EINV_OperationFailure::InvalidRequest: Message = TEXT("请求无效"); break;
	case EINV_OperationFailure::InvalidRequestId: Message = TEXT("请求已失效"); break;
	case EINV_OperationFailure::RequestExpired: Message = TEXT("请求已过期"); break;
	case EINV_OperationFailure::InvalidInteractor: Message = TEXT("交互者无效"); break;
	case EINV_OperationFailure::InvalidTarget: Message = TEXT("目标无效"); break;
	case EINV_OperationFailure::NoSpace: Message = TEXT("空间不足"); break;
	case EINV_OperationFailure::InvalidLocation: Message = TEXT("位置无效"); break;
	case EINV_OperationFailure::TransferNotAllowed: Message = TEXT("来源拒绝转移"); break;
	case EINV_OperationFailure::UnsupportedOperation: Message = TEXT("不支持此操作"); break;
	case EINV_OperationFailure::InvalidItem: Message = TEXT("物品无效"); break;
	case EINV_OperationFailure::InvalidQuantity: Message = TEXT("数量无效"); break;
	case EINV_OperationFailure::IncompatibleItem: Message = TEXT("物品类型不匹配"); break;
	case EINV_OperationFailure::EquipmentMismatch: Message = TEXT("装备槽不匹配"); break;
	case EINV_OperationFailure::Occupied: Message = TEXT("目标位置已占用"); break;
	case EINV_OperationFailure::SpawnFailed: Message = TEXT("生成物品失败"); break;
	default: break;
	}
	OperationResultText->SetText(FText::FromString(Message));
}

