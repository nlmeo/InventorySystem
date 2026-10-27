#include "Player/INV_PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Widgets/HUD/INV_HUDWidget.h"
#include "Widgets/Inventory/Container/INV_ContainerInventoryWidget.h"
#include "Widgets/Inventory/Source/INV_InventorySourceWidget.h"
#include "Widgets/Inventory/Session/INV_InventorySessionWidget.h"
#include "Widgets/Inventory/Spatial/INV_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Kismet/GameplayStatics.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Interaction/INV_Highlightable.h"
#include "Interaction/INV_ContainerInteractable.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutTypes.h"
#include "Items/INV_InventoryItem.h"
#include "Interaction/INV_InventoryInteractionSubsystem.h"
AINV_PlayerController::AINV_PlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	TraceLength = 500.0;
	ItemTraceChannel = ECollisionChannel::ECC_GameTraceChannel1;
	ContainerInventoryWidgetClass = UINV_ContainerInventoryWidget::StaticClass();
	InventorySessionWidgetClass = UINV_InventorySessionWidget::StaticClass();
}

void AINV_PlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (ActiveContainerInventory.IsValid() && (!ActiveContainerActor.IsValid()
		|| !INV_ContainerInteraction::CanInteract(ActiveContainerActor.Get(), this)))
	{
		CloseContainerInteraction();
	}
	if (HasAuthority() && ServerActiveContainerInventory.IsValid()
		&& !INV_ContainerInteraction::CanInteract(ServerActiveContainerInventory->GetOwner(), this))
	{
		ServerSetContainerOpen_Implementation(ServerActiveContainerInventory.Get(), false);
		ClientContainerRejected();
	}

	TraceForItem();
}


void AINV_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (DefaultIMC)
	{
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
		if (IsValid(Subsystem))
		{
			Subsystem->AddMappingContext(DefaultIMC, 0);
		}
	}
	
	InventoryComponent = FindComponentByClass<UINV_InventoryComponent>();

	CreateHUDWidget();
}

void AINV_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseContainerInteraction();
	if(DefaultIMC)
	{
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
		if (Subsystem)
		{
			Subsystem->RemoveMappingContext(DefaultIMC);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AINV_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);
	
	EnhancedInputComponent->BindAction(PrimaryInteractAction, ETriggerEvent::Started, this, &AINV_PlayerController::PrimaryInteract);
	EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &AINV_PlayerController::ToggleInventory);
}

void AINV_PlayerController::PrimaryInteract()
{
	if (ActiveContainerInventory.IsValid()) { CloseContainerInteraction(); return; }
	if (!ThisActor.IsValid()) return;

	if (FocusedContainer.IsValid()
		&& FocusedContainer->GetClass()->ImplementsInterface(UINV_ContainerInteractable::StaticClass()))
	{
		if (INV_ContainerInteraction::CanInteract(FocusedContainer.Get(), this))
		{
			RequestContainerInteraction(FocusedContainer.Get());
		}
		return;
	}

	UINV_ItemComponent* ItemComponent = ThisActor->GetComponentByClass<UINV_ItemComponent>();
	if (!IsValid(ItemComponent) || !InventoryComponent.IsValid()) return;

	InventoryComponent->TryAddItem(ItemComponent);


}

void AINV_PlayerController::ToggleInventory()
{
	if (!InventoryComponent.IsValid()) return;
	if (ActiveContainerInventory.IsValid())
	{
		CloseContainerInteraction();
		return;
	}
	if (!IsValid(InventorySessionWidget))
	{
		if (!InventorySessionWidgetClass) return;
		InventorySessionWidget = CreateWidget<UINV_InventorySessionWidget>(this, InventorySessionWidgetClass);
	}
	if (!IsValid(InventorySessionWidget)) return;
	InventorySessionWidget->InitializePlayerInventory(InventoryComponent.Get());
	InventorySessionWidget->TogglePlayerInventory();
	if (!InventorySessionWidget->IsInViewport()) InventorySessionWidget->AddToViewport(20);
	if (InventorySessionWidget->IsPlayerInventoryOpen())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(InventorySessionWidget->TakeWidget());
		SetInputMode(Mode);
		bShowMouseCursor = true;
		if (IsValid(HUDWidget)) HUDWidget->SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		if (IsValid(HUDWidget)) HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void AINV_PlayerController::RequestContainerInteraction(AActor* ContainerActor)
{
	if (!IsValid(ContainerActor)
		|| !ContainerActor->GetClass()->ImplementsInterface(UINV_ContainerInteractable::StaticClass())) return;

	UINV_InventoryComponent* ContainerInventory = INV_ContainerInteraction::GetInventory(ContainerActor);
	if (!IsValid(ContainerInventory) || !INV_ContainerInteraction::CanInteract(ContainerActor, this)) return;

	if (ActiveContainerActor == ContainerActor)
	{
		CloseContainerInteraction();
		return;
	}

	if (ActiveContainerActor.IsValid())
	{
		CloseContainerInteraction();
	}

	ActiveContainerActor = ContainerActor;
	ActiveContainerInventory = ContainerInventory;
	if (UINV_InventoryInteractionSubsystem* InteractionSubsystem = GetInventoryInteractionSubsystem())
	{
		InteractionSubsystem->BeginInventorySource(ContainerActor, ContainerInventory);
	}
	ServerSetContainerOpen(ContainerInventory, true);
	OpenContainerWidget(ContainerActor, ContainerInventory);
	OnContainerInteractionRequested.Broadcast(ContainerActor, ContainerInventory);
}

void AINV_PlayerController::CloseContainerInteraction()
{
	if (!ActiveContainerActor.IsValid() && !ActiveContainerInventory.IsValid()
		&& (!InventorySessionWidget || !InventorySessionWidget->IsSessionOpen())) return;
	UINV_InventoryComponent* ClosingInventory = ActiveContainerInventory.Get();
	ServerSetContainerOpen(ClosingInventory, false);
	if (IsValid(InventorySessionWidget))
	{
		InventorySessionWidget->CloseSession();
	}
	ActiveContainerActor.Reset();
	ActiveContainerInventory.Reset();
	if (UINV_InventoryInteractionSubsystem* InteractionSubsystem = GetInventoryInteractionSubsystem())
	{
		InteractionSubsystem->EndInventorySource();
	}
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	if (IsValid(HUDWidget)) HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	OnContainerInteractionClosed.Broadcast();
}

UINV_InventoryInteractionSubsystem* AINV_PlayerController::GetInventoryInteractionSubsystem() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UINV_InventoryInteractionSubsystem>() : nullptr;
}

void AINV_PlayerController::OpenContainerWidget(AActor* ContainerActor, UINV_InventoryComponent* ContainerInventory)
{
	if (!IsLocalController() || !ContainerInventoryWidgetClass || !InventorySessionWidgetClass
		|| !InventoryComponent.IsValid()) return;

	if (!IsValid(InventorySessionWidget))
	{
		InventorySessionWidget = CreateWidget<UINV_InventorySessionWidget>(this, InventorySessionWidgetClass);
	}
	if (!IsValid(InventorySessionWidget)) return;

	// Initialize before AddToViewport so fallback session/source layouts build their roots first.
	InventorySessionWidget->InitializeSession(ContainerActor, InventoryComponent.Get(),
		ContainerInventory, ContainerInventoryWidgetClass);
	if (!InventorySessionWidget->IsInViewport())
	{
		InventorySessionWidget->AddToViewport(20);
	}
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(InventorySessionWidget->TakeWidget());
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	if (IsValid(HUDWidget)) HUDWidget->SetVisibility(ESlateVisibility::Hidden);
}

void AINV_PlayerController::ServerSetContainerOpen_Implementation(UINV_InventoryComponent* ContainerInventory, bool bOpen)
{
	if (!bOpen)
	{
		if (ServerActiveContainerInventory.IsValid()) ServerActiveContainerInventory->SetContainerOpen(false);
		ServerActiveContainerInventory.Reset();
		return;
	}
	if (!IsValid(ContainerInventory) || !IsValid(ContainerInventory->GetOwner())) { ClientContainerRejected(); return; }
	AActor* ContainerActor = ContainerInventory->GetOwner();
	if (!INV_ContainerInteraction::CanInteract(ContainerActor, this)
		|| INV_ContainerInteraction::GetInventory(ContainerActor) != ContainerInventory)
	{ ClientContainerRejected(); return; }
	if (ServerActiveContainerInventory.IsValid() && ServerActiveContainerInventory != ContainerInventory)
		ServerActiveContainerInventory->SetContainerOpen(false);
	ServerActiveContainerInventory = ContainerInventory;
	ContainerInventory->SetContainerOpen(true);
}

void AINV_PlayerController::ClientContainerRejected_Implementation()
{
	CloseContainerInteraction();
}

void AINV_PlayerController::SubmitContainerTransfer(UINV_InventoryComponent* Source,
	UINV_InventoryComponent* Target, UINV_InventoryItem* Item, FName TargetSlot, int32 TargetIndex,
	int32 Amount, bool bRotated, UINV_InventoryItem* MergeTarget,
	const TArray<FINV_CrossContainerSwapEntry>& SwapEntries)
{
	if (!IsValid(Item)) return;
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Transfer] 客户端发送请求：Item=%s，Source=%s(%p)，Target=%s(%p)，Slot=%s，Index=%d，Rotated=%d。"),
		*GetNameSafe(Item), *GetNameSafe(Source), Source, *GetNameSafe(Target), Target,
		*TargetSlot.ToString(), TargetIndex, bRotated ? 1 : 0);
	FINV_InteractionContext Context(this, EINV_InteractionType::Transfer);
	Context.Item = Item;
	Context.SecondaryItem = MergeTarget;
	Context.SourceSlotId = Item->GetContainerId();
	Context.SourceIndex = Item->GetGridIndex();
	Context.TargetSlotId = TargetSlot;
	Context.TargetIndex = TargetIndex;
	Context.Amount = Amount;
	Context.bRotated = bRotated;
	Context.CrossContainerSwapEntries = SwapEntries;
	ServerContainerTransfer(Source, Target, Context);
}

void AINV_PlayerController::SubmitContainerQuickTransfer(UINV_InventoryComponent* Source,
	UINV_InventoryComponent* Target, UINV_InventoryItem* Item)
{
	if (!IsValid(Source) || !IsValid(Target) || !IsValid(Item) || Source == Target) return;
	const FINV_GridSlotDefinition* TargetPanel =
		Target->FindGridPanelForCategory(Item->GetItemManifest().GetItemCategory());
	if (!TargetPanel)
	{
		ClientContainerResult(EINV_OperationFailure::NoSpace);
		return;
	}
	SubmitContainerTransfer(Source, Target, Item, TargetPanel->SlotId, INDEX_NONE,
		Item->IsStackable() ? Item->GetTotalStackCount() : 1, Item->IsRotated(), nullptr, {});
}

void AINV_PlayerController::SubmitContainerOperation(UINV_InventoryComponent* Component,
	FINV_InteractionContext Context)
{
	if (!IsValid(Component) || Component != ActiveContainerInventory.Get()) return;
	Context.Interactor = this;
	ServerContainerOperation(Component, Context);
}

void AINV_PlayerController::SubmitContainerEquip(UINV_InventoryComponent* Source,
	UINV_InventoryItem* Item, FGameplayTag EquipmentSlotType)
{
	if (!IsValid(Source) || !IsValid(Item) || !EquipmentSlotType.IsValid()
		|| Source != ActiveContainerInventory.Get()) return;
	ServerContainerEquip(Source, Item, EquipmentSlotType);
}

void AINV_PlayerController::SubmitContainerDrop(UINV_InventoryComponent* Source,
	UINV_InventoryItem* Item, int32 Amount)
{
	if (!IsValid(Source) || !IsValid(Item) || Source != ActiveContainerInventory.Get() || Amount <= 0) return;
	ServerContainerDrop(Source, Item, Amount);
}

void AINV_PlayerController::ServerContainerEquip_Implementation(UINV_InventoryComponent* Source,
	UINV_InventoryItem* Item, FGameplayTag EquipmentSlotType)
{
	UINV_InventoryComponent* PlayerInventory = FindComponentByClass<UINV_InventoryComponent>();
	if (!IsValid(Source) || Source != ServerActiveContainerInventory.Get()
		|| !IsValid(PlayerInventory) || !IsValid(Item)
		|| !Source->GetOwner() || !Source->IsContainerOpen()
		|| !INV_ContainerInteraction::CanInteract(Source->GetOwner(), this))
	{
		ClientContainerResult(EINV_OperationFailure::TransferNotAllowed);
		return;
	}
	const EINV_OperationFailure Failure = PlayerInventory->TransferAndEquipFromInventory(
		Source, Item, EquipmentSlotType);
	ClientContainerResult(Failure);
}

void AINV_PlayerController::ServerContainerDrop_Implementation(UINV_InventoryComponent* Source,
	UINV_InventoryItem* Item, int32 Amount)
{
	if (!IsValid(Source) || Source != ServerActiveContainerInventory.Get()
		|| !IsValid(Source->GetOwner()) || !Source->IsContainerOpen()
		|| !INV_ContainerInteraction::CanInteract(Source->GetOwner(), this))
	{
		ClientContainerResult(EINV_OperationFailure::TransferNotAllowed);
		return;
	}
	const EINV_OperationFailure Failure = Source->DropItemFromInteractor(this, Item, Amount);
	ClientContainerResult(Failure);
}

void AINV_PlayerController::ServerContainerOperation_Implementation(
	UINV_InventoryComponent* Component, FINV_InteractionContext Context)
{
	if (!IsValid(Component) || Component != ServerActiveContainerInventory.Get()
		|| !IsValid(Component->GetOwner()) || !Component->IsContainerOpen()
		|| !INV_ContainerInteraction::CanInteract(Component->GetOwner(), this))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory][Container] 容器操作拒绝：组件无效、未打开或已超出交互范围。"));
		ClientContainerResult(EINV_OperationFailure::TransferNotAllowed);
		return;
	}

	Context.Interactor = Component->GetOwner();
	Component->SubmitInventoryOperationFromServer(Context);
	ClientContainerResult(Component->GetLastOperationResult().Failure);
}

void AINV_PlayerController::ServerContainerTransfer_Implementation(UINV_InventoryComponent* Source,
	UINV_InventoryComponent* Target, FINV_InteractionContext Context)
{
	UINV_InventoryComponent* PlayerInventory = FindComponentByClass<UINV_InventoryComponent>();
	UINV_InventoryComponent* Container = ServerActiveContainerInventory.Get();
	if (!IsValid(Source) || !IsValid(Target) || !IsValid(Container) || !IsValid(PlayerInventory)
		|| (Source != PlayerInventory && Source != Container) || (Target != PlayerInventory && Target != Container)
		|| !INV_ContainerInteraction::CanInteract(Container->GetOwner(), this)
		|| !Container->bAllowTransfer || !Container->IsContainerOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory][Transfer] ServerContainerTransfer 前置拒绝：Source=%s，Target=%s，ActiveContainer=%s，Player=%s，Open=%d。"),
			*GetNameSafe(Source), *GetNameSafe(Target), *GetNameSafe(Container),
			*GetNameSafe(PlayerInventory), IsValid(Container) && Container->IsContainerOpen() ? 1 : 0);
		ClientContainerResult(EINV_OperationFailure::TransferNotAllowed); return;
	}
	Context.Interactor = this;
	Context.TargetActor = Target->GetOwner();
	Context.TargetInventoryComponent = Target;
	Context.Type = Source != Target ? EINV_InteractionType::Transfer
		: (Context.SecondaryItem ? EINV_InteractionType::Merge : EINV_InteractionType::Move);
	if (Source != Target && !Target->CanAcceptTransfer(this, Source->GetOwner()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory][Transfer] ServerContainerTransfer 访问策略拒绝：Target=%s，SourceOwner=%s。"),
			*GetNameSafe(Target), *GetNameSafe(Source->GetOwner()));
		ClientContainerResult(EINV_OperationFailure::TransferNotAllowed); return;
	}
	// This function already runs on the server through the player-controller RPC.
	// Do not call the component's client-facing Server RPC again: container actors
	// have no owning player connection and Unreal will discard that nested RPC.
	const int64 RequestId = Source->SubmitInventoryOperationFromServer(Context);
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Transfer] 服务端已提交：RequestId=%lld，Source=%s，Target=%s。"),
		RequestId, *GetNameSafe(Source), *GetNameSafe(Target));
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Transfer] 服务端操作结果：Failure=%d，Source=%s，Target=%s。"),
		static_cast<int32>(Source->GetLastOperationResult().Failure), *GetNameSafe(Source), *GetNameSafe(Target));
	ClientContainerResult(Source->GetLastOperationResult().Failure);
}

void AINV_PlayerController::ClientContainerResult_Implementation(EINV_OperationFailure Failure)
{
	// A transfer mutates two InventoryComponents, but the operation result is
	// delivered through the source component only. Refresh both local views so
	// the Session's SourceGrid and player grids converge on replicated state.
	if (InventoryComponent.IsValid()) InventoryComponent->ScheduleRefresh();
	if (ActiveContainerInventory.IsValid()) ActiveContainerInventory->ScheduleRefresh();
	if (InventorySessionWidget) InventorySessionWidget->ForwardOperationResult(Failure);
}

void AINV_PlayerController::INV_TestMoveItemsOverlap()
{
	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory][Test] 找不到 InventoryComponent，无法测试。"));
		return;
	}
	InventoryComponent->TestMoveOwnerOverlap();
}

void AINV_PlayerController::CreateHUDWidget()
{
	if (!IsLocalController()) return;

	HUDWidget = CreateWidget<UINV_HUDWidget>(this,HUDWidgetClass);
	if (IsValid(HUDWidget))
	{
		HUDWidget->AddToViewport();
	}

}

void AINV_PlayerController::TraceForItem()
{
	if (!GEngine || !IsValid(GEngine->GameViewport) || !GetWorld()) return;
	
	FVector2D ViewportSize;
	GEngine->GameViewport->GetViewportSize(ViewportSize);
	const FVector2d ViewportCenter = ViewportSize / 2.f;
	FVector TraceStart,Forward;

	if (!UGameplayStatics::DeprojectScreenToWorld(this, ViewportCenter, TraceStart, Forward)) return;

	const FVector TraceEnd = TraceStart + Forward * TraceLength;

	FHitResult HitResult;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ItemTraceChannel);

	LastActor = ThisActor;
	ThisActor = HitResult.GetActor();
	FocusedContainer.Reset();
	if (ThisActor.IsValid()
		&& ThisActor->GetClass()->ImplementsInterface(UINV_ContainerInteractable::StaticClass()))
	{
		FocusedContainer = ThisActor;
	}


	if (!ThisActor.IsValid())
	{
		if (IsValid(HUDWidget)) HUDWidget->HidePickupMessage();
	}

	if (LastActor == ThisActor) return;


	if (ThisActor.IsValid())
	{
		UINV_ItemComponent* ItemComponent = ThisActor->FindComponentByClass<UINV_ItemComponent>();
		if (IsValid(ItemComponent))
		{
			if (IsValid(HUDWidget)) HUDWidget->ShowPickupMessage(ItemComponent->GetPickupMessage());
		}
		else if (IsValid(HUDWidget))
		{
			HUDWidget->HidePickupMessage();
		}
	
		//Highlight
		if (UActorComponent* Highlightable = ThisActor->FindComponentByInterface(UINV_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IINV_Highlightable::Execute_Highlight(Highlightable);
		}

		
	}

	if (LastActor.IsValid())
	{
		//Highlight
		if (UActorComponent* Highlightable = LastActor->FindComponentByInterface(UINV_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IINV_Highlightable::Execute_UnHighlight(Highlightable);
		}
	}
}
