#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Items/INV_InventoryItem.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "InventoryManagement/Utils/INV_AutoPlacement.h"
#include "Net/UnrealNetwork.h"
#include "Widgets/Inventory/InventoryBase/INV_InventoryBase.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
namespace
{
	bool HasValidStack(const UINV_InventoryItem* Item)
	{
		const FINV_StackableFragment* Stack = Item->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>();
		return Stack && Stack->GetMaxStackSize() > 0 && Item->GetTotalStackCount() > 0
			&& Item->GetTotalStackCount() <= Stack->GetMaxStackSize();
	}
}
UINV_InventoryComponent::UINV_InventoryComponent() : InventoryList(this)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;
	bInventoryMenuOpen = false;
}
namespace
{
	bool SameInventoryFrame(const FINV_InventoryStateFrame& A, const FINV_InventoryStateFrame& B)
	{
		if (A.Items.Num() != B.Items.Num()) return false;
		for (int32 Index = 0; Index < A.Items.Num(); ++Index)
		{
			const FINV_ItemStateSnapshot& L = A.Items[Index];
			const FINV_ItemStateSnapshot& R = B.Items[Index];
			if (L.Item != R.Item || L.ContainerId != R.ContainerId || L.GridIndex != R.GridIndex
				|| L.StackCount != R.StackCount || L.bRotated != R.bRotated
				|| L.EquippedSlotType != R.EquippedSlotType) return false;
		}
		return true;
	}
}
int64 UINV_InventoryComponent::SubmitInventoryOperation(FINV_InteractionContext Context)
{
	const int64 RequestId = NextClientRequestId++;
	PendingOperations.Add(RequestId, Context.Type);
	if (GetOwner() && GetOwner()->HasAuthority()) Server_SubmitInventoryOperation_Implementation(RequestId, Context);
	else Server_SubmitInventoryOperation(RequestId, Context);
	return RequestId;
}
int64 UINV_InventoryComponent::SubmitInventoryOperationFromServer(FINV_InteractionContext Context)
{
	const int64 RequestId = NextServerRequestId++;
	PendingOperations.Add(RequestId, Context.Type);
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Operation] 服务器内部提交：Component=%s(%p)，RequestId=%lld，Type=%d。"),
		*GetNameSafe(this), this, RequestId, static_cast<int32>(Context.Type));
	ProcessInventoryOperation(RequestId, Context);
	return RequestId;
}
bool UINV_InventoryComponent::IsInventoryRequestPending(int64 RequestId) const
{
	return PendingOperations.Contains(RequestId);
}
FINV_InventoryStateFrame UINV_InventoryComponent::CaptureInventoryStateFrame() const
{
	FINV_InventoryStateFrame Frame;
	Frame.Revision = AuthorityRevision;
	Frame.CompletionSequence = CompletionSequence;
	for (UINV_InventoryItem* Item : InventoryList.GetAllItems())
	{
		if (!IsValid(Item)) continue;
		FINV_ItemStateSnapshot& State = Frame.Items.AddDefaulted_GetRef();
		State.Item = Item;
		State.ContainerId = Item->GetContainerId();
		State.GridIndex = Item->GetGridIndex();
		State.StackCount = Item->GetTotalStackCount();
		State.bRotated = Item->IsRotated();
		State.EquippedSlotType = Item->GetEquippedSlotType();
	}
	return Frame;
}
void UINV_InventoryComponent::Server_SubmitInventoryOperation_Implementation(int64 RequestId, const FINV_InteractionContext& Context)
{
	ProcessInventoryOperation(RequestId, Context);
}
void UINV_InventoryComponent::ProcessInventoryOperation(int64 RequestId, const FINV_InteractionContext& Context)
{
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Operation] 开始处理：Component=%s(%p)，RequestId=%lld，Type=%d，Authority=%d。"),
		*GetNameSafe(this), this, RequestId, static_cast<int32>(Context.Type),
		GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
	if (const FINV_InventoryOperationResult* Existing = CompletedOperations.Find(RequestId))
	{
		FINV_InventoryOperationResult Replay = *Existing;
		Replay.bReplayed = true;
		DeliverOperationResult(Replay, LastOperationStateFrame);
		return;
	}
	FINV_InventoryStateFrame Before = CaptureInventoryStateFrame();
	EINV_OperationFailure Failure = EINV_OperationFailure::None;
	if (Context.Interactor && GetOwner())
	{
		bool bValidInteractor = Context.Interactor == GetOwner();
		if (!bValidInteractor)
		{
			if (const APawn* PawnOwner = Cast<APawn>(GetOwner()))
			{
				bValidInteractor = Context.Interactor == PawnOwner->GetController();
			}
		}
		if (!bValidInteractor && Context.Type == EINV_InteractionType::Transfer
			&& Context.TargetInventoryComponent
			&& Context.TargetInventoryComponent->GetOwner() == Context.Interactor)
		{
			bValidInteractor = true;
		}
		if (!bValidInteractor)
		{
			Failure = EINV_OperationFailure::InvalidInteractor;
		}
	}
	if (Failure == EINV_OperationFailure::None && Context.Type == EINV_InteractionType::Pickup)
	{
		if (!IsValid(Context.ItemComponent))
		{
			Failure = EINV_OperationFailure::InvalidItem;
		}
		else if (IsValid(Context.TargetActor) && Context.ItemComponent->GetOwner() != Context.TargetActor)
		{
			Failure = EINV_OperationFailure::InvalidTarget;
		}
	}
	if (Failure == EINV_OperationFailure::None)
	{
	switch (Context.Type)
	{
	case EINV_InteractionType::Pickup:
		if (!IsValid(Context.ItemComponent)) Failure = EINV_OperationFailure::InvalidItem;
		else Server_PickupItem_Implementation(Context.ItemComponent, Context.EmptyEquipmentSlot);
		break;
	case EINV_InteractionType::Transfer:
		if (!IsValid(Context.TargetInventoryComponent)
			|| Context.TargetInventoryComponent == this
			|| !Context.TargetInventoryComponent->GetOwner()
			|| !Context.TargetInventoryComponent->GetOwner()->HasAuthority())
		{
			Failure = EINV_OperationFailure::InvalidTarget;
		}
		else if (!IsValid(Context.TargetActor)
			|| Context.TargetActor != Context.TargetInventoryComponent->GetOwner())
		{
			Failure = EINV_OperationFailure::InvalidTarget;
		}
		else if (!Context.TargetInventoryComponent->CanAcceptTransfer(Context.Interactor, GetOwner()))
		{
			Failure = EINV_OperationFailure::TransferNotAllowed;
		}
		else if (!IsValidInventoryItem(Context.Item))
		{
			Failure = EINV_OperationFailure::InvalidItem;
		}
		else if (Context.Amount <= 0)
		{
			Failure = EINV_OperationFailure::InvalidQuantity;
		}
		else
		{
			Failure = TransferItemToInventory(Context, Context.TargetInventoryComponent);
		}
		break;
	case EINV_InteractionType::Equip:
		if (!IsValidInventoryItem(Context.Item))
		{
			Failure = EINV_OperationFailure::InvalidItem;
		}
		else if (!Context.EquipmentSlotTag.IsValid())
		{
			Failure = EINV_OperationFailure::EquipmentMismatch;
		}
		else
		{
			Server_EquipTransaction_Implementation(Context.Item, Context.SecondaryItem,
				Context.EquipmentSlotTag, Context.ReturnSlotId, Context.TargetIndex, Context.bReturnRotated);
		}
		break;
	case EINV_InteractionType::Unequip:
		if (!IsValidInventoryItem(Context.Item))
		{
			Failure = EINV_OperationFailure::InvalidItem;
		}
		else if (!Context.EquipmentSlotTag.IsValid())
		{
			Failure = EINV_OperationFailure::EquipmentMismatch;
		}
		else
		{
			Server_EquipTransaction_Implementation(nullptr, Context.Item,
				Context.EquipmentSlotTag, Context.ReturnSlotId, Context.TargetIndex, Context.bReturnRotated);
		}
		break;
	case EINV_InteractionType::Rotate:
		if (!IsValidInventoryItem(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.SourceSlotId.IsNone() || Context.SourceIndex == INDEX_NONE
			|| Context.TargetSlotId.IsNone() || Context.TargetIndex == INDEX_NONE)
			Failure = EINV_OperationFailure::InvalidLocation;
		else Server_MoveItem_Implementation(Context.Item, Context.SourceSlotId, Context.SourceIndex,
			Context.TargetSlotId, Context.TargetIndex, Context.bRotated);
		break;
	case EINV_InteractionType::Drop:
		if (!IsValid(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.Amount <= 0) Failure = EINV_OperationFailure::InvalidQuantity;
		else if (!IsValidInventoryItem(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else Server_DropItem_Implementation(Context.Item, Context.Amount);
		break;
	case EINV_InteractionType::DropEquipped:
		if (!IsValid(Context.Item) || !IsValidInventoryItem(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else if (!Context.EquipmentSlotTag.IsValid()) Failure = EINV_OperationFailure::EquipmentMismatch;
		else Server_DropEquippedItem_Implementation(Context.Item, Context.EquipmentSlotTag);
		break;
	case EINV_InteractionType::Use:
		if (!IsValid(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.Amount <= 0) Failure = EINV_OperationFailure::InvalidQuantity;
		else if (!IsValidInventoryItem(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else Server_UseItem_Implementation(Context.Item, Context.Amount);
		break;
	case EINV_InteractionType::Move:
		if (!IsValidInventoryItem(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.TargetSlotId.IsNone() || Context.TargetIndex == INDEX_NONE)
			Failure = EINV_OperationFailure::InvalidLocation;
		else Server_MoveItem_Implementation(Context.Item, Context.SourceSlotId, Context.SourceIndex,
			Context.TargetSlotId, Context.TargetIndex, Context.bRotated);
		break;
	case EINV_InteractionType::BatchMove:
		if (Context.BatchItems.IsEmpty() || Context.BatchItems.Num() != Context.BatchTargetIndices.Num()
			|| Context.BatchItems.Num() != Context.BatchRotatedStates.Num() || Context.TargetSlotId.IsNone())
		{
			Failure = EINV_OperationFailure::InvalidRequest;
		}
		else
		{
			for (UINV_InventoryItem* Item : Context.BatchItems)
			{
				if (!IsValidInventoryItem(Item))
				{
					Failure = EINV_OperationFailure::InvalidItem;
					break;
				}
			}
			if (Failure == EINV_OperationFailure::None)
			{
				Server_MoveItems_Implementation(Context.BatchItems, Context.BatchTargetIndices,
					Context.TargetSlotId, Context.BatchRotatedStates);
			}
		}
		break;
	case EINV_InteractionType::Swap:
		if (!IsValidInventoryItem(Context.Item) || !IsValidInventoryItem(Context.SecondaryItem))
			Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.SourceSlotId.IsNone() || Context.TargetSlotId.IsNone()
			|| Context.SourceIndex == INDEX_NONE || Context.TargetIndex == INDEX_NONE)
			Failure = EINV_OperationFailure::InvalidLocation;
		else Server_SwapItems_Implementation(Context.Item, Context.SourceSlotId, Context.SourceIndex,
			Context.SecondaryItem, Context.TargetSlotId, Context.TargetIndex);
		break;
	case EINV_InteractionType::SwapEquipped:
		if (!IsValidInventoryItem(Context.Item))
		{
			Failure = EINV_OperationFailure::InvalidItem;
		}
		else if (!Context.EquipmentSlotTag.IsValid() || !Context.SourceEquipmentSlotTag.IsValid())
		{
			Failure = EINV_OperationFailure::EquipmentMismatch;
		}
		else
		{
			Server_MoveEquippedItem_Implementation(Context.Item, Context.SecondaryItem,
				Context.EquipmentSlotTag, Context.SourceEquipmentSlotTag);
		}
		break;
	case EINV_InteractionType::SwapStackCounts:
		if (!IsValidInventoryItem(Context.Item) || !IsValidInventoryItem(Context.SecondaryItem))
			Failure = EINV_OperationFailure::InvalidItem;
		else Server_SwapStackCounts_Implementation(Context.Item, Context.SecondaryItem);
		break;
	case EINV_InteractionType::Split:
		if (!IsValidInventoryItem(Context.Item)) Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.Amount <= 0) Failure = EINV_OperationFailure::InvalidQuantity;
		else if (Context.SourceSlotId.IsNone() || Context.SourceIndex == INDEX_NONE)
			Failure = EINV_OperationFailure::InvalidLocation;
		else Server_SplitStack_Implementation(Context.Item, Context.SourceSlotId, Context.SourceIndex,
			Context.Amount, Context.TargetIndex);
		break;
	case EINV_InteractionType::Merge:
		if (!IsValidInventoryItem(Context.Item) || !IsValidInventoryItem(Context.SecondaryItem))
			Failure = EINV_OperationFailure::InvalidItem;
		else if (Context.Amount <= 0) Failure = EINV_OperationFailure::InvalidQuantity;
		else Server_MergeStack_Implementation(Context.Item, Context.SecondaryItem, Context.Amount);
		break;
	default:
		Failure = EINV_OperationFailure::UnsupportedOperation;
		break;
	}
	}
	FINV_InventoryStateFrame After = CaptureInventoryStateFrame();
	const bool bChanged = !SameInventoryFrame(Before, After);
	const bool bExpectedNoChange = Context.Type == EINV_InteractionType::Move
		&& Context.SourceSlotId == Context.TargetSlotId
		&& Context.SourceIndex == Context.TargetIndex;
	if (Failure == EINV_OperationFailure::None && !bChanged && !bExpectedNoChange)
	{
		Failure = EINV_OperationFailure::InvalidLocation;
	}
	if (bChanged) ++AuthorityRevision;
	++CompletionSequence;
	After.Revision = AuthorityRevision;
	After.CompletionSequence = CompletionSequence;
	FINV_InventoryOperationResult Result;
	Result.RequestId = RequestId;
	Result.Type = Context.Type;
	Result.Failure = Failure;
	Result.Status = Failure == EINV_OperationFailure::None ? (bChanged ? EINV_OperationStatus::Succeeded : EINV_OperationStatus::NoChange) : EINV_OperationStatus::Rejected;
	Result.Revision = AuthorityRevision;
	Result.CompletionSequence = CompletionSequence;
	Result.bReplayed = false;
	CompletedOperations.Add(RequestId, Result);
	LastOperationResult = Result;
	LastOperationStateFrame = After;
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Operation] 处理完成：Component=%s(%p)，RequestId=%lld，Failure=%d，Changed=%d，Items=%d，Revision=%lld。"),
		*GetNameSafe(this), this, RequestId, static_cast<int32>(Failure), bChanged ? 1 : 0,
		After.Items.Num(), AuthorityRevision);
	DeliverOperationResult(Result, After);
}
void UINV_InventoryComponent::DeliverOperationResult(const FINV_InventoryOperationResult& Result, const FINV_InventoryStateFrame& Frame)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (GetNetMode() == NM_Standalone)
		{
			Client_InventoryOperationCompleted_Implementation(Result, Frame);
		}
		else
		{
			Client_InventoryOperationCompleted(Result, Frame);
		}
	}
	else
	{
		Client_InventoryOperationCompleted(Result, Frame);
	}
}
void UINV_InventoryComponent::Client_InventoryOperationCompleted_Implementation(const FINV_InventoryOperationResult& Result, const FINV_InventoryStateFrame& Frame)
{
	PendingOperations.Remove(Result.RequestId);
	LastOperationResult = Result;
	LastOperationStateFrame = Frame;
	Client_RefreshInventoryFromServer(Frame.Items, Frame.Revision, Frame.CompletionSequence);
	OnInventoryOperationCompleted.Broadcast(Result);
}
void UINV_InventoryComponent::TryAddItem(UINV_ItemComponent* ItemComponent)
{
	if (!IsValid(ItemComponent)) return;
	const FGameplayTag EmptySlot = IsValid(InventoryMenu)
		? InventoryMenu->FindEmptyEquipmentSlot(ItemComponent->GetItemManifest()) : FGameplayTag();
	FINV_InteractionContext Context(GetOwner(), EINV_InteractionType::Pickup);
	Context.ItemComponent = ItemComponent;
	Context.TargetActor = ItemComponent->GetOwner();
	Context.EmptyEquipmentSlot = EmptySlot;
	SubmitInventoryOperation(Context);
}
UINV_InventoryItem* UINV_InventoryComponent::CreateItemEntry(UINV_ItemComponent* ItemComponent,
	const int32 AmountThisEntry, FName InSlotId, int32 Index, bool bRotated, FGameplayTag EquipmentSlotType)
{
	if (!IsValid(ItemComponent)) return nullptr;
	UINV_InventoryItem* NewItem = InventoryList.AddEntry(ItemComponent);
	if (!IsValid(NewItem)) return nullptr;
	NewItem->SetTotalStackCount(AmountThisEntry);
	NewItem->SetContainerId(InSlotId);
	NewItem->SetGridIndex(Index);
	NewItem->SetRotated(bRotated);
	NewItem->SetEquippedSlotType(EquipmentSlotType);
	if (FINV_StackableFragment* EntryStackable =
		NewItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_StackableFragment>())
	{
		EntryStackable->SetStackCount(AmountThisEntry);
	}
	if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone)
	{
		OnItemAdded.Broadcast(NewItem);
	}
	return NewItem;
}
UINV_InventoryItem* UINV_InventoryComponent::CreateItemEntryFromItem(UINV_InventoryItem* SourceItem,
	const int32 AmountThisEntry, const int32 GridIndex, const FName SlotId, const bool bRotated)
{
	if (!IsValid(SourceItem)) return nullptr;
	FINV_ItemManifest ManifestCopy = SourceItem->GetItemManifest();
	if (FINV_EquipmentFragment* Equipment =
		ManifestCopy.GetFragmentOfTypeMutable<FINV_EquipmentFragment>())
	{
		Equipment->bEquipable = false;
		Equipment->SetEquippedActor(nullptr);
	}
	UINV_InventoryItem* NewItem = ManifestCopy.Manifest(GetOwner());
	if (!IsValid(NewItem)) return nullptr;
	NewItem->SetTotalStackCount(AmountThisEntry);
	if (FINV_StackableFragment* EntryStackable =
		NewItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_StackableFragment>())
	{
		EntryStackable->SetStackCount(AmountThisEntry);
	}
	NewItem->SetContainerId(SlotId);
	NewItem->SetGridIndex(GridIndex);
	NewItem->SetRotated(bRotated);
	InventoryList.AddEntry(NewItem);
	AddRepSubObj(NewItem);
	if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone)
	{
		OnItemAdded.Broadcast(NewItem);
	}
	return NewItem;
}
bool UINV_InventoryComponent::RehostInventoryItem(UINV_InventoryItem* Item,
	UINV_InventoryComponent* SourceInventory, UINV_InventoryComponent* TargetInventory,
	FName TargetSlot, int32 TargetIndex)
{
	if (!IsValid(Item) || !IsValid(SourceInventory) || !IsValid(TargetInventory)
		|| SourceInventory == TargetInventory || !SourceInventory->IsValidInventoryItem(Item)
		|| !TargetInventory->GetOwner() || !TargetInventory->GetOwner()->HasAuthority()) return false;
	AActor* SourceOwner = SourceInventory->GetOwner();
	AActor* TargetOwner = TargetInventory->GetOwner();
	if (!SourceOwner || !TargetOwner) return false;
	SourceInventory->RemoveRepSubObj(Item);
	SourceInventory->InventoryList.RemoveEntry(Item);
	if (!Item->Rename(nullptr, TargetOwner, REN_DontCreateRedirectors | REN_NonTransactional))
	{
		SourceInventory->InventoryList.AddEntry(Item);
		SourceInventory->AddRepSubObj(Item);
		return false;
	}
	Item->SetOwningInventory(TargetInventory);
	Item->SetContainerId(TargetIndex == INDEX_NONE ? NAME_None : TargetSlot);
	Item->SetGridIndex(TargetIndex);
	TargetInventory->InventoryList.AddEntry(Item);
	TargetInventory->AddRepSubObj(Item);
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Transfer] 复制子对象换宿主：Item=%s，Source=%s，Target=%s，Slot=%s，Index=%d。"),
		*GetNameSafe(Item), *GetNameSafe(SourceInventory), *GetNameSafe(TargetInventory),
		*TargetSlot.ToString(), TargetIndex);
	return true;
}

void UINV_InventoryComponent::Server_PickupItem_Implementation(UINV_ItemComponent* ItemComponent,
	FGameplayTag EmptyEquipmentSlot)
{
	AddPickupOnServer(ItemComponent, EmptyEquipmentSlot);
}
void UINV_InventoryComponent::Client_PickupResult_Implementation(int32 AddedCount, int32 RemainingCount)
{
	if (AddedCount == 0) NoRoomInInventory.Broadcast();
	ScheduleRefresh();
}
void UINV_InventoryComponent::Server_AddNewItem_Implementation(UINV_ItemComponent* ItemComponent,
	int32 StackCount, int32 Remainder)
{
	AddPickupOnServer(ItemComponent, FGameplayTag());
}
void UINV_InventoryComponent::Server_AddStacksToItem_Implementation(UINV_ItemComponent* ItemComponent,
	int32 StackCount, int32 Remainder)
{
	AddPickupOnServer(ItemComponent, FGameplayTag());
}
bool UINV_InventoryComponent::CanAutoEquipPickup(const FINV_ItemManifest& Manifest, FGameplayTag SlotType) const
{
	if (!SlotType.IsValid() || Manifest.GetItemCategory() != EINV_ItemCategory::Equippable
		|| Manifest.GetFragmentOfType<FINV_StackableFragment>()
		|| !Manifest.GetFragmentOfType<FINV_EquipmentFragment>()
		|| !Manifest.GetItemType().MatchesTag(SlotType)) return false;
	for (const UINV_InventoryItem* Item : InventoryList.GetAllItems())
	{
		if (Item->IsEquipped() && (Item->GetEquippedSlotType().MatchesTag(SlotType)
			|| SlotType.MatchesTag(Item->GetEquippedSlotType()))) return false;
	}
	return true;
}
bool UINV_InventoryComponent::FindPickupPlacement(const FINV_ItemManifest& Manifest,
	FName& OutSlotId, int32& OutIndex, bool& bOutRotated) const
{
	OutSlotId = NAME_None;
	OutIndex = INDEX_NONE;
	bOutRotated = false;
	const FINV_GridFragment* Fragment = Manifest.GetFragmentOfType<FINV_GridFragment>();
	if (!Fragment || Fragment->GetGridSize().X <= 0 || Fragment->GetGridSize().Y <= 0) return false;
	TArray<const FINV_GridSlotDefinition*> Panels;
	GetAllGridPanels(Panels);
	TArray<FINV_AutoPlacementGrid> Grids;
	for (const FINV_GridSlotDefinition* Panel : Panels)
	{
		if (!Panel || Panel->SlotId.IsNone() || Panel->ContainerType != EINV_GridContainerType::Grid
			|| !INV_AcceptsItemCategory(Panel->AcceptedCategory, Manifest.GetItemCategory())
			|| Panel->SlotDimension.X <= 0 || Panel->SlotDimension.Y <= 0
			|| static_cast<int64>(Panel->SlotDimension.X) * Panel->SlotDimension.Y > MAX_int32) continue;
		int32 Matches = 0;
		for (const FINV_GridSlotDefinition* Other : Panels)
		{
			if (Other && Other->SlotId == Panel->SlotId) ++Matches;
		}
		const UINV_GridLayoutAsset* Layout = nullptr;
		GetGridPanel(Panel->SlotId, &Layout);
		if (Matches != 1 || !IsValid(Layout) || !FMath::IsFinite(Layout->GetTileSize())
			|| Layout->GetTileSize() <= 0.f
			|| !FMath::IsFinite(Panel->SlotDimension.X * Layout->GetTileSize())
			|| !FMath::IsFinite(Panel->SlotDimension.Y * Layout->GetTileSize())) continue;
		FINV_AutoPlacementGrid& Grid = Grids.AddDefaulted_GetRef();
		Grid.SlotId = Panel->SlotId;
		Grid.Size = Panel->SlotDimension;
		for (const UINV_InventoryItem* Item : InventoryList.GetAllItems())
		{
			if (Item->GetContainerId() != Grid.SlotId || Item->GetGridIndex() == INDEX_NONE || Item->IsEquipped()) continue;
			const int32 Index = Item->GetGridIndex();
			const FIntPoint Dimensions = Item->GetItemDimension();
			const FIntPoint Position(Index % Grid.Size.X, Index / Grid.Size.X);
			Grid.Occupied.Emplace(Position, Position + Dimensions);
		}
	}
	FINV_AutoPlacement Placement;
	if (!INV_FindAutoPlacement(Grids, Fragment->GetGridSize(), false, Placement)) return false;
	OutSlotId = Placement.SlotId;
	OutIndex = Placement.Index;
	bOutRotated = Placement.bRotated;
	return true;
}
void UINV_InventoryComponent::AddPickupOnServer(UINV_ItemComponent* ItemComponent, FGameplayTag EmptyEquipmentSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(ItemComponent)
		|| !IsValid(ItemComponent->GetOwner()) || ItemComponent->GetOwner()->IsActorBeingDestroyed()) return;
	const FINV_ItemManifest Manifest = ItemComponent->GetItemManifest();
	const FINV_GridFragment* GridFragment = Manifest.GetFragmentOfType<FINV_GridFragment>();
	if (!GridFragment || GridFragment->GetGridSize().X <= 0 || GridFragment->GetGridSize().Y <= 0)
	{
		Client_PickupResult(0, 1);
		return;
	}
	const FINV_StackableFragment* Stackable = Manifest.GetFragmentOfType<FINV_StackableFragment>();
	const int32 Requested = Stackable ? Stackable->GetStackCount() : 1;
	const int32 MaxStack = Stackable ? Stackable->GetMaxStackSize() : 1;
	if (Requested <= 0 || MaxStack <= 0) return;
	int32 Remaining = Requested;
	if (CanAutoEquipPickup(Manifest, EmptyEquipmentSlot))
	{
		if (UINV_InventoryItem* Item = CreateItemEntry(ItemComponent, 0, NAME_None, INDEX_NONE, false, EmptyEquipmentSlot))
		{
			Multicast_EquipSlotClicked(Item, nullptr);
			Remaining = 0;
			UE_LOG(LogTemp, Log, TEXT("[Inventory][Server] 拾取自动装备：%s，槽=%s。"),
				*GetNameSafe(Item), *EmptyEquipmentSlot.ToString());
		}
	}
	else
	{
		if (Stackable)
		{
			for (UINV_InventoryItem* Target : InventoryList.GetAllItems())
			{
				if (Remaining == 0) break;
				if (Target->IsEquipped() || Target->GetContainerId().IsNone() || Target->GetGridIndex() == INDEX_NONE
					|| !HasValidStack(Target) || Target->GetItemManifest().GetItemType() != Manifest.GetItemType()) continue;
				const FINV_StackableFragment* TargetStack = Target->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>();
				const int32 Amount = FMath::Min(Remaining, FMath::Max(0, TargetStack->GetMaxStackSize() - Target->GetTotalStackCount()));
				if (Amount == 0) continue;
				SetItemStackCount(Target, Target->GetTotalStackCount() + Amount);
				Remaining -= Amount;
			}
		}
		while (Remaining > 0)
		{
			FName SlotId;
			int32 Index;
			bool bRotated;
			if (!FindPickupPlacement(Manifest, SlotId, Index, bRotated)) break;
			const int32 Amount = FMath::Min(Remaining, MaxStack);
			if (!CreateItemEntry(ItemComponent, Stackable ? Amount : 0, SlotId, Index, bRotated)) break;
			Remaining -= Amount;
			UE_LOG(LogTemp, Log, TEXT("[Inventory][Server] 拾取落位：容器=%s，索引=%d，旋转=%d，数量=%d。"),
				*SlotId.ToString(), Index, bRotated, Amount);
		}
	}
	if (Remaining == 0)
	{
		ItemComponent->PickedUp();
	}
	else if (Stackable && Remaining < Requested)
	{
		FINV_ItemManifest Leftover = Manifest;
		Leftover.GetFragmentOfTypeMutable<FINV_StackableFragment>()->SetStackCount(Remaining);
		ItemComponent->InitItemManifest(Leftover);
	}
	Client_PickupResult(Requested - Remaining, Remaining);
	RequestLocalRefreshIfAuthority();
}
void UINV_InventoryComponent::Server_DropItem_Implementation(UINV_InventoryItem* Item, int32 StackCount)
{
	if (!IsValidInventoryItem(Item)) return;
	if (Item->IsEquipped() || (Item->IsStackable()
		? (!HasValidStack(Item) || StackCount <= 0 || StackCount > Item->GetTotalStackCount())
		: StackCount != 1)) return;
	if (!SpawnDroppedItem(Item, StackCount)) return;
	const int32 NewStackCount = Item->GetTotalStackCount() - StackCount;
	if (NewStackCount <= 0)
	{
		SetItemGridIndex(Item, NAME_None, INDEX_NONE);
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		SetItemStackCount(Item, NewStackCount);
	}
	RequestLocalRefreshIfAuthority();
}
void UINV_InventoryComponent::Server_DropEquippedItem_Implementation(UINV_InventoryItem* Item,
	FGameplayTag ExpectedSlotType)
{
	if (!IsValidInventoryItem(Item) || !ExpectedSlotType.IsValid() || !Item->IsEquipped()
		|| Item->GetEquippedSlotType() != ExpectedSlotType || Item->IsStackable()
		|| Item->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
		|| !Item->GetItemManifest().GetItemType().MatchesTag(ExpectedSlotType)
		|| !Item->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>()
		|| Item->GetGridIndex() != INDEX_NONE || !Item->GetContainerId().IsNone()) return;
	if (!SpawnDroppedItem(Item, 1)) return;
	Item->SetEquippedSlotType(FGameplayTag());
	Multicast_EquipSlotClicked(nullptr, Item);
	SetItemGridIndex(Item, NAME_None, INDEX_NONE);
	InventoryList.RemoveEntry(Item);
	RequestLocalRefreshIfAuthority();
}
bool UINV_InventoryComponent::SpawnDroppedItem(UINV_InventoryItem* Item, int32 StackCount, APlayerController* SpawnController)
{
	APlayerController* DropController = IsValid(SpawnController) ? SpawnController : OwningController.Get();
	if (!IsValid(Item) || !IsValid(DropController)) return false;
	const APawn* Player = DropController->GetPawn();
	if (!IsValid(Player)) return false;
	FVector RotatedForward = Player->GetActorForwardVector();
	RotatedForward = RotatedForward.RotateAngleAxis(FMath::FRandRange(DropSpawnAngleMin, DropSpawnAngleMax), FVector::UpVector);
	FVector SpawnLocation = Player->GetActorLocation() + RotatedForward * FMath::FRandRange(DropSpawnDistanceMin, DropSpawnDistanceMax);
	SpawnLocation.Z -= RelativeSpawnElevation;
	const FRotator SpawnRotation = FRotator::ZeroRotator;
	FINV_ItemManifest ItemManifest = Item->GetItemManifest();
	if (FINV_EquipmentFragment* Equipment = ItemManifest.GetFragmentOfTypeMutable<FINV_EquipmentFragment>())
	{
		Equipment->SetEquippedActor(nullptr);
		Equipment->bEquipable = false;
	}
	FINV_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FINV_StackableFragment>();
	if (!StackableFragment)
	{
		return ItemManifest.SpawnPickupActor(this, SpawnLocation, SpawnRotation);
	}
	StackableFragment->SetStackCount(StackCount);
	return ItemManifest.SpawnPickupActor(this, SpawnLocation, SpawnRotation);
}
EINV_OperationFailure UINV_InventoryComponent::DropItemFromInteractor(APlayerController* Interactor,
	UINV_InventoryItem* Item, int32 StackCount)
{
	if (!IsValidInventoryItem(Item)) return EINV_OperationFailure::InvalidItem;
	if (Item->IsEquipped() || (Item->IsStackable()
		? (!HasValidStack(Item) || StackCount <= 0 || StackCount > Item->GetTotalStackCount())
		: StackCount != 1)) return EINV_OperationFailure::InvalidQuantity;
	if (!IsValid(Interactor)) return EINV_OperationFailure::InvalidInteractor;
	if (!SpawnDroppedItem(Item, StackCount, Interactor)) return EINV_OperationFailure::InvalidLocation;
	const int32 NewStackCount = Item->GetTotalStackCount() - StackCount;
	if (NewStackCount <= 0)
	{
		SetItemGridIndex(Item, NAME_None, INDEX_NONE);
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		SetItemStackCount(Item, NewStackCount);
	}
	RequestLocalRefreshIfAuthority();
	return EINV_OperationFailure::None;
}
void UINV_InventoryComponent::Server_UseItem_Implementation(UINV_InventoryItem* Item, int32 StackCount)
{
	if (!IsValidInventoryItem(Item)) return;
	FINV_ConsumableFragment* ConsumableFragment =
		Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_ConsumableFragment>();
	if (StackCount != 1 || !Item->IsConsumable() || !ConsumableFragment || Item->IsEquipped()
		|| (Item->IsStackable() && !HasValidStack(Item))) return;
	const int32 NewStackCount = Item->GetTotalStackCount() - StackCount;
	if (NewStackCount > 0)
	{
		SetItemStackCount(Item, NewStackCount);
	}
	else
	{
		SetItemGridIndex(Item, NAME_None, INDEX_NONE);
		InventoryList.RemoveEntry(Item);
	}
	ConsumableFragment->OnConsume(OwningController.Get());
}
void UINV_InventoryComponent::Server_EquipSlotClicked_Implementation(UINV_InventoryItem* ItemToEquip,
	UINV_InventoryItem* ItemToUnequip, FGameplayTag EquipmentSlotType)
{
	if (ItemToEquip && ItemToEquip == ItemToUnequip) return;
	if (IsValid(ItemToEquip) && (!IsValidInventoryItem(ItemToEquip) || !EquipmentSlotType.IsValid()
		|| ItemToEquip->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
		|| ItemToEquip->IsStackable() || !ItemToEquip->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>()
		|| !ItemToEquip->GetItemManifest().GetItemType().MatchesTag(EquipmentSlotType))) return;
	if (IsValid(ItemToUnequip) && (!IsValidInventoryItem(ItemToUnequip) || ItemToUnequip->IsStackable()
		|| ItemToUnequip->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
		|| !ItemToUnequip->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>())) return;
	if (IsValid(ItemToEquip))
	{
		for (const UINV_InventoryItem* Other : InventoryList.GetAllItems())
		{
			if (Other == ItemToEquip || Other == ItemToUnequip || !Other->IsEquipped()) continue;
			if (Other->GetEquippedSlotType().MatchesTag(EquipmentSlotType)
				|| EquipmentSlotType.MatchesTag(Other->GetEquippedSlotType())) return;
		}
	}
	if (IsValidInventoryItem(ItemToEquip))
	{
		SetItemGridIndex(ItemToEquip, NAME_None, INDEX_NONE);
		ItemToEquip->SetEquippedSlotType(EquipmentSlotType);
		ItemToEquip->SetRotated(false);
		const FIntPoint BaseDim = ItemToEquip->GetBaseDimension();
		UE_LOG(LogTemp, Log, TEXT("[Inventory][Server] 装备朝向归一：%s，旋转=0，尺寸=%dx%d。"),
			*GetNameSafe(ItemToEquip), BaseDim.X, BaseDim.Y);
	}
	if (IsValid(ItemToUnequip))
	{
		SetItemGridIndex(ItemToUnequip, NAME_None, INDEX_NONE);
		ItemToUnequip->SetEquippedSlotType(FGameplayTag());
	}
	Multicast_EquipSlotClicked(ItemToEquip, ItemToUnequip);
}
void UINV_InventoryComponent::Server_EquipTransaction_Implementation(UINV_InventoryItem* ItemToEquip,
	UINV_InventoryItem* ItemToUnequip, FGameplayTag EquipmentSlotType, FName ReturnSlotId,
	int32 ReturnIndex, bool bReturnRotated)
{
	if (ItemToEquip && ItemToEquip == ItemToUnequip) return;
	if (!EquipmentSlotType.IsValid()) return;
	if (ItemToEquip)
	{
		if (!IsValidInventoryItem(ItemToEquip) || ItemToEquip->IsEquipped()
			|| ItemToEquip->IsStackable()
			|| ItemToEquip->GetContainerId().IsNone() || ItemToEquip->GetGridIndex() == INDEX_NONE
			|| ItemToEquip->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
			|| !ItemToEquip->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>()
			|| !ItemToEquip->GetItemManifest().GetItemType().MatchesTag(EquipmentSlotType)) return;
	}
	if (ItemToUnequip)
	{
		const FGameplayTag CurrentEquippedSlot = IsValid(ItemToUnequip)
			? ItemToUnequip->GetEquippedSlotType()
			: FGameplayTag();
		const bool bEquipmentSlotMatches = CurrentEquippedSlot.IsValid()
			&& (CurrentEquippedSlot.MatchesTag(EquipmentSlotType)
				|| EquipmentSlotType.MatchesTag(CurrentEquippedSlot));
		if (!IsValidInventoryItem(ItemToUnequip) || !ItemToUnequip->IsEquipped()
			|| !bEquipmentSlotMatches
			|| ItemToUnequip->IsStackable()
			|| ItemToUnequip->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
			|| !ItemToUnequip->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] EquipTransaction 卸下校验拒绝：物品=%s，当前槽=%s，请求槽=%s，已装备=%d。"),
				*GetNameSafe(ItemToUnequip), *CurrentEquippedSlot.ToString(), *EquipmentSlotType.ToString(),
				IsValid(ItemToUnequip) && ItemToUnequip->IsEquipped() ? 1 : 0);
			return;
		}
		if (ReturnSlotId.IsNone()) return;
		if (ReturnIndex == INDEX_NONE)
		{
			bool bServerReturnRotated = bReturnRotated;
			ReturnIndex = FindFirstFreeGridIndex(ReturnSlotId, ItemToUnequip, bServerReturnRotated);
			if (ReturnIndex == INDEX_NONE) return;
			bReturnRotated = bServerReturnRotated;
		}
		const FIntPoint ReturnDim = bReturnRotated && ItemToUnequip->CanRotate()
			? FIntPoint(ItemToUnequip->GetBaseDimension().Y, ItemToUnequip->GetBaseDimension().X)
			: ItemToUnequip->GetBaseDimension();
		if (!CanItemFitAt(ReturnSlotId, ReturnIndex, ItemToUnequip, ItemToEquip, nullptr,
			nullptr, true, ReturnDim)) return;
	}
	else if (!ReturnSlotId.IsNone() || ReturnIndex != INDEX_NONE)
	{
		return;
	}
	for (const UINV_InventoryItem* Other : InventoryList.GetAllItems())
	{
		if (Other == ItemToEquip || Other == ItemToUnequip || !Other->IsEquipped()) continue;
		const FGameplayTag Slot = Other->GetEquippedSlotType();
		if (Slot.MatchesTag(EquipmentSlotType) || EquipmentSlotType.MatchesTag(Slot)) return;
	}
	if (ItemToEquip)
	{
		SetItemGridIndex(ItemToEquip, NAME_None, INDEX_NONE);
		ItemToEquip->SetEquippedSlotType(EquipmentSlotType);
		ItemToEquip->SetRotated(false);
	}
	if (ItemToUnequip)
	{
		SetItemGridIndex(ItemToUnequip, ReturnSlotId, ReturnIndex);
		ItemToUnequip->SetEquippedSlotType(FGameplayTag());
		ItemToUnequip->SetRotated(bReturnRotated && ItemToUnequip->CanRotate());
	}
	Multicast_EquipSlotClicked(ItemToEquip, ItemToUnequip);
}
void UINV_InventoryComponent::Multicast_EquipSlotClicked_Implementation(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToUnequip)
{
	BroadcastEquipmentEvent(ItemToUnequip, false);
	BroadcastEquipmentEvent(ItemToEquip, true);
}
void UINV_InventoryComponent::BroadcastEquipmentEvent(UINV_InventoryItem* Item, bool bEquip)
{
	if (!IsValid(Item)) return;
	if (bEquip)
	{
		KnownReplicatedEquippedItems.Add(Item);
		OnItemEquipped.Broadcast(Item);
	}
	else
	{
		KnownReplicatedEquippedItems.Remove(Item);
		OnItemUnequipped.Broadcast(Item);
	}
}
void UINV_InventoryComponent::Server_MoveEquippedItem_Implementation(UINV_InventoryItem* ItemToEquip,
	UINV_InventoryItem* ItemToSwap, FGameplayTag TargetSlotType, FGameplayTag SourceSlotType)
{
	if (!IsValidInventoryItem(ItemToEquip)) return;
	if (IsValid(ItemToSwap) && (ItemToSwap == ItemToEquip || !IsValidInventoryItem(ItemToSwap))) return;
	for (const UINV_InventoryItem* Item : {ItemToEquip, ItemToSwap})
	{
		if (IsValid(Item) && (Item->IsStackable()
			|| Item->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
			|| !Item->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>())) return;
	}
	if (!SourceSlotType.IsValid() || !TargetSlotType.IsValid()
		|| !ItemToEquip->GetItemManifest().GetItemType().MatchesTag(TargetSlotType)
		|| (IsValid(ItemToSwap) && !ItemToSwap->GetItemManifest().GetItemType().MatchesTag(SourceSlotType))) return;
	for (const UINV_InventoryItem* Other : InventoryList.GetAllItems())
	{
		if (Other == ItemToEquip || Other == ItemToSwap || !Other->IsEquipped()) continue;
		const FGameplayTag Slot = Other->GetEquippedSlotType();
		if (Slot.MatchesTag(TargetSlotType) || TargetSlotType.MatchesTag(Slot)
			|| (IsValid(ItemToSwap) && (Slot.MatchesTag(SourceSlotType) || SourceSlotType.MatchesTag(Slot)))) return;
	}
	SetItemGridIndex(ItemToEquip, NAME_None, INDEX_NONE);
	ItemToEquip->SetEquippedSlotType(TargetSlotType);
	ItemToEquip->SetRotated(false);
	if (IsValid(ItemToSwap))
	{
		SetItemGridIndex(ItemToSwap, NAME_None, INDEX_NONE);
		ItemToSwap->SetEquippedSlotType(SourceSlotType);
		ItemToSwap->SetRotated(false);
	}
	Multicast_MoveEquippedItem(ItemToEquip, ItemToSwap);
}
void UINV_InventoryComponent::Multicast_MoveEquippedItem_Implementation(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToSwap)
{
	BroadcastEquipmentEvent(ItemToEquip, false);
	BroadcastEquipmentEvent(ItemToSwap, false);
	BroadcastEquipmentEvent(ItemToEquip, true);
	BroadcastEquipmentEvent(ItemToSwap, true);
}
bool UINV_InventoryComponent::IsValidInventoryItem(UINV_InventoryItem* Item) const
{
	if (!IsValid(Item)) return false;
	return InventoryList.GetAllItems().Contains(Item);
}
const FINV_GridSlotDefinition* UINV_InventoryComponent::GetGridPanel(FName InSlotId,
	const UINV_GridLayoutAsset** OutLayout) const
{
	if (OutLayout) *OutLayout = nullptr;
	if (InSlotId.IsNone()) return nullptr;
	for (const TObjectPtr<UINV_GridLayoutAsset>& Layout : GridLayouts)
	{
		if (!IsValid(Layout)) continue;
		for (int32 Index = 0; Index < Layout->GetSlotCount(); ++Index)
		{
			const FINV_GridSlotDefinition* Definition = Layout->GetSlotDefinition(Index);
			if (Definition && Definition->SlotId == InSlotId)
			{
				if (OutLayout) *OutLayout = Layout;
				return Definition;
			}
		}
	}
	return nullptr;
}
const FINV_GridSlotDefinition* UINV_InventoryComponent::FindGridPanelForCategory(EINV_ItemCategory Category,
	const UINV_GridLayoutAsset** OutLayout) const
{
	if (OutLayout) *OutLayout = nullptr;
	if (Category == EINV_ItemCategory::None) return nullptr;
	for (const bool bExactMatch : { true, false })
	{
		for (const TObjectPtr<UINV_GridLayoutAsset>& Layout : GridLayouts)
		{
			if (!IsValid(Layout)) continue;
			for (int32 Index = 0; Index < Layout->GetSlotCount(); ++Index)
			{
				const FINV_GridSlotDefinition* Definition = Layout->GetSlotDefinition(Index);
				const bool bMatches = bExactMatch
					? Definition && Definition->AcceptedCategory == Category
					: Definition && Definition->AcceptedCategory == EINV_ItemCategory::All;
				if (bMatches)
				{
					if (OutLayout) *OutLayout = Layout;
					return Definition;
				}
			}
		}
	}
	return nullptr;
}
void UINV_InventoryComponent::GetAllGridPanels(TArray<const FINV_GridSlotDefinition*>& OutPanels) const
{
	OutPanels.Reset();
	for (const TObjectPtr<UINV_GridLayoutAsset>& Layout : GridLayouts)
	{
		if (!IsValid(Layout)) continue;
		for (int32 Index = 0; Index < Layout->GetSlotCount(); ++Index)
		{
			if (const FINV_GridSlotDefinition* Definition = Layout->GetSlotDefinition(Index))
			{
				OutPanels.Add(Definition);
			}
		}
	}
}
void UINV_InventoryComponent::ValidateGridLayouts() const
{
	TSet<FName> SeenSlotIds;
	bool bAnyInvalidDimension = false;
	float TileSizeBaseline = 0.f;
	for (const TObjectPtr<UINV_GridLayoutAsset>& Layout : GridLayouts)
	{
		if (!IsValid(Layout))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inventory] GridLayouts 里存在空引用，已跳过。"));
			continue;
		}
		const float LayoutTileSize = Layout->GetTileSize();
		if (LayoutTileSize <= 0.f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] 布局资产 %s 的 TileSize=%.1f 非法，该资产下的网格会退化（格宽为 0）。"),
				*GetNameSafe(Layout), LayoutTileSize);
		}
		else if (TileSizeBaseline <= 0.f)
		{
			TileSizeBaseline = LayoutTileSize;
		}
		else if (!FMath::IsNearlyEqual(LayoutTileSize, TileSizeBaseline))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] 布局资产 %s 的 TileSize=%.1f 与其它资产的 %.1f 不一致，跨页签物品大小会跳变，建议统一。"),
				*GetNameSafe(Layout), LayoutTileSize, TileSizeBaseline);
		}
		for (int32 Index = 0; Index < Layout->GetSlotCount(); ++Index)
		{
			const FINV_GridSlotDefinition* Definition = Layout->GetSlotDefinition(Index);
			if (!Definition) continue;
			if (Definition->SlotId.IsNone())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Inventory] 布局资产 %s 的第 %d 个面板没有配 SlotId，将无法被任何网格引用。"),
					*GetNameSafe(Layout), Index);
				continue;
			}
			if (SeenSlotIds.Contains(Definition->SlotId))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Inventory] 布局资产 %s 的面板 SlotId '%s' 与前面的资产重复，只会用到第一个。"),
					*GetNameSafe(Layout), *Definition->SlotId.ToString());
			}
			else
			{
				SeenSlotIds.Add(Definition->SlotId);
			}
			if (Definition->SlotDimension.X <= 0 || Definition->SlotDimension.Y <= 0)
			{
				bAnyInvalidDimension = true;
			}
		}
	}
	if (bAnyInvalidDimension)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] 存在 SlotDimension 非法的面板，服务器将拒绝向这些面板放置物品。"));
	}
}
FIntPoint UINV_InventoryComponent::GetGridSize(FName InSlotId) const
{
	const FINV_GridSlotDefinition* Definition = GetGridPanel(InSlotId);
	if (!Definition) return FIntPoint::ZeroValue;
	if (Definition->SlotDimension.X <= 0 || Definition->SlotDimension.Y <= 0) return FIntPoint::ZeroValue;
	return Definition->SlotDimension;
}
bool UINV_InventoryComponent::IsValidGridIndex(FName InSlotId, int32 Index) const
{
	if (Index < 0) return false;
	const FINV_GridSlotDefinition* Panel = GetGridPanel(InSlotId);
	if (!Panel || Panel->ContainerType != EINV_GridContainerType::Grid) return false;
	const FIntPoint Size = GetGridSize(InSlotId);
	const int64 Capacity = static_cast<int64>(Size.X) * Size.Y;
	return Size.X > 0 && Size.Y > 0 && Capacity <= MAX_int32 && Index < Capacity;
}
bool UINV_InventoryComponent::IsValidGridIndexForItem(int32 Index, const UINV_InventoryItem* Item) const
{
	if (!IsValid(Item)) return false;
	const FINV_GridSlotDefinition* Definition = FindGridPanelForCategory(Item->GetItemManifest().GetItemCategory());
	if (!Definition) return Index >= 0;
	if (Definition->SlotDimension.X <= 0 || Definition->SlotDimension.Y <= 0) return Index >= 0;
	return Index < Definition->SlotDimension.X * Definition->SlotDimension.Y;
}
FName UINV_InventoryComponent::ResolveItemSlotId(const UINV_InventoryItem* Item) const
{
	if (!IsValid(Item)) return NAME_None;
	if (!Item->GetContainerId().IsNone()) return Item->GetContainerId();
	const FINV_GridSlotDefinition* Definition = FindGridPanelForCategory(Item->GetItemManifest().GetItemCategory());
	return Definition ? Definition->SlotId : NAME_None;
}
bool UINV_InventoryComponent::IsItemInContainer(const UINV_InventoryItem* Item, FName SlotId) const
{
	if (!IsValid(Item)) return false;
	if (SlotId.IsNone()) return true;
	return Item->GetContainerId() == SlotId;
}
FName UINV_InventoryComponent::ResolveContainerSlotId(const UINV_InventoryItem* Item, FName InSlotId) const
{
	if (!InSlotId.IsNone()) return InSlotId;
	return ResolveItemSlotId(Item);
}
void UINV_InventoryComponent::SetItemStackCount(UINV_InventoryItem* Item, int32 NewCount)
{
	if (!IsValid(Item)) return;
	Item->SetTotalStackCount(NewCount);
	if (FINV_StackableFragment* Stackable =
		Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_StackableFragment>())
	{
		Stackable->SetStackCount(NewCount);
	}
}
bool UINV_InventoryComponent::CanItemFitAt(FName SlotId, int32 Index, const UINV_InventoryItem* Item,
	const UINV_InventoryItem* IgnoreA, const UINV_InventoryItem* IgnoreB, UINV_InventoryItem** OutBlocking,
	bool bIgnoreSelf, TOptional<FIntPoint> DimensionsOverride) const
{
	if (OutBlocking) *OutBlocking = nullptr;
	if (!IsValid(Item) || !IsValidGridIndex(SlotId, Index)) return false;
	const FINV_GridSlotDefinition* Panel = GetGridPanel(SlotId);
	if (!Panel || !INV_AcceptsItemCategory(Panel->AcceptedCategory, Item->GetItemManifest().GetItemCategory())) return false;
	const FIntPoint GridSize = GetGridSize(SlotId);
	const FIntPoint Origin(Index % GridSize.X, Index / GridSize.X);
	const FIntPoint Dim = DimensionsOverride.Get(Item->GetItemDimension());
	if (Dim.X <= 0 || Dim.Y <= 0 || Dim.X > GridSize.X - Origin.X || Dim.Y > GridSize.Y - Origin.Y) return false;
	for (UINV_InventoryItem* Other : InventoryList.GetAllItems())
	{
		if (!IsValid(Other)) continue;
		if (Other == Item && bIgnoreSelf) continue;
		if (Other == IgnoreA || Other == IgnoreB) continue;
		if (!IsItemInContainer(Other, SlotId)) continue;
		const int32 OtherIndex = Other->GetGridIndex();
		if (OtherIndex == INDEX_NONE) continue;   
		const FIntPoint OtherCoord(OtherIndex % GridSize.X, OtherIndex / GridSize.X);
		const FIntPoint OtherDim = Other->GetItemDimension();
		const bool bOverlap =
			Origin.X < OtherCoord.X + OtherDim.X && OtherCoord.X < Origin.X + Dim.X &&
			Origin.Y < OtherCoord.Y + OtherDim.Y && OtherCoord.Y < Origin.Y + Dim.Y;
		if (bOverlap)
		{
			if (OutBlocking) *OutBlocking = Other;
			return false;
		}
	}
	return true;
}
void UINV_InventoryComponent::RequestAuthoritativeRefresh()
{
	if (!bServerAuthoritativeUI) return;
	AActor* Owner = GetOwner();
	if (!Owner) return;
	if (Owner->HasAuthority())
	{
		ScheduleRefresh();
		return;
	}
	Server_RequestAuthoritativeRefresh();
}
void UINV_InventoryComponent::RequestLocalRefreshIfAuthority()
{
	if (!bServerAuthoritativeUI) return;
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority()) return;
	ScheduleRefresh();
}
void UINV_InventoryComponent::Server_RequestAuthoritativeRefresh_Implementation()
{
	TArray<FINV_ItemStateSnapshot> States;
	for (UINV_InventoryItem* Item : InventoryList.GetAllItems())
	{
		if (!IsValid(Item)) continue;
		FINV_ItemStateSnapshot& State = States.AddDefaulted_GetRef();
		State.Item = Item;
		State.ContainerId = Item->GetContainerId();
		State.GridIndex = Item->GetGridIndex();
		State.StackCount = Item->GetTotalStackCount();
		State.bRotated = Item->IsRotated();
		State.EquippedSlotType = Item->GetEquippedSlotType();
	}
	Client_RefreshInventoryFromServer(States, AuthorityRevision, CompletionSequence);
}
void UINV_InventoryComponent::Client_RefreshInventoryFromServer_Implementation(
	const TArray<FINV_ItemStateSnapshot>& States, int64 Revision, int64 ProcessedSequence)
{
	if (Revision > 0 && (Revision < LastRefreshRevision
		|| (Revision == LastRefreshRevision && ProcessedSequence < LastRefreshCompletionSequence))) return;
	LastRefreshRevision = FMath::Max(LastRefreshRevision, Revision);
	if (Revision == LastRefreshRevision) LastRefreshCompletionSequence = FMath::Max(LastRefreshCompletionSequence, ProcessedSequence);
	int32 CorrectedCount = 0;
	for (const FINV_ItemStateSnapshot& State : States)
	{
		UINV_InventoryItem* Item = State.Item.Get();
		if (!IsValidInventoryItem(Item)) continue;
		if (Item->GetContainerId() != State.ContainerId || Item->GetGridIndex() != State.GridIndex
			|| Item->GetTotalStackCount() != State.StackCount || Item->IsRotated() != State.bRotated
			|| Item->GetEquippedSlotType() != State.EquippedSlotType)
		{
			++CorrectedCount;
		}
		SetItemGridIndex(Item, State.ContainerId, State.GridIndex);
		SetItemStackCount(Item, State.StackCount);
		Item->SetRotated(State.bRotated);
		Item->SetEquippedSlotType(State.EquippedSlotType);
	}
	UE_LOG(LogTemp, Log, TEXT("[Inventory] 权威快照刷新：收到=%d，纠正=%d。"), States.Num(), CorrectedCount);
	ScheduleRefresh();
}
void UINV_InventoryComponent::Client_RefreshInventoryFromServer_Implementation(
	const TArray<FINV_ItemStateSnapshot>& States)
{
	if (LastRefreshRevision > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] 忽略无版本权威快照：已有 Revision=%lld，条目数=%d。"),
			LastRefreshRevision, States.Num());
		return;
	}
	Client_RefreshInventoryFromServer_Implementation(States, 0, 0);
}
void UINV_InventoryComponent::NotifyItemReplicatedChange(UINV_InventoryItem* Item)
{
	if (!IsValid(Item)) return;
	if (!bServerAuthoritativeUI) return;
	ScheduleRefresh();
	if (!bEquipmentSyncScheduled)
	{
		if (UWorld* World = GetWorld())
		{
			bEquipmentSyncScheduled = true;
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UINV_InventoryComponent::SyncReplicatedEquipmentEvents));
		}
		else
		{
			SyncReplicatedEquipmentEvents();
		}
	}
}
void UINV_InventoryComponent::SyncReplicatedEquipmentEvents()
{
	bEquipmentSyncScheduled = false;
	TSet<TObjectPtr<UINV_InventoryItem>> CurrentEquipped;
	for (UINV_InventoryItem* Item : InventoryList.GetAllItems())
	{
		if (IsValid(Item) && Item->IsEquipped()) CurrentEquipped.Add(Item);
	}
	for (const TObjectPtr<UINV_InventoryItem>& Known : KnownReplicatedEquippedItems)
	{
		if (IsValid(Known) && !CurrentEquipped.Contains(Known))
		{
			OnItemUnequipped.Broadcast(Known);
		}
	}
	for (const TObjectPtr<UINV_InventoryItem>& Current : CurrentEquipped)
	{
		if (IsValid(Current) && !KnownReplicatedEquippedItems.Contains(Current))
		{
			OnItemEquipped.Broadcast(Current);
		}
	}
	KnownReplicatedEquippedItems = MoveTemp(CurrentEquipped);
}
void UINV_InventoryComponent::ScheduleRefresh()
{
	if (bRefreshScheduled) return;
	if (UWorld* World = GetWorld())
	{
		bRefreshScheduled = true;
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UINV_InventoryComponent::FlushRefresh));
		return;
	}
	OnInventoryRefreshRequested.Broadcast();
}
void UINV_InventoryComponent::FlushRefresh()
{
	bRefreshScheduled = false;
	OnInventoryRefreshRequested.Broadcast();
}
int32 UINV_InventoryComponent::FindFirstFreeGridIndex(FName SlotId, const UINV_InventoryItem* Item,
	bool& bOutRotated, const UINV_InventoryItem* IgnoreItem) const
{
	bOutRotated = IsValid(Item) && Item->IsRotated();
	if (!IsValid(Item)) return INDEX_NONE;
	const FIntPoint Size = GetGridSize(SlotId);
	if (Size.X <= 0 || Size.Y <= 0) return INDEX_NONE;
	const int32 Total = Size.X * Size.Y;
	const FIntPoint CurrentDimensions = Item->GetItemDimension();
	if (CurrentDimensions.X <= 0 || CurrentDimensions.Y <= 0) return INDEX_NONE;
	for (int32 Direction = 0; Direction < (Item->CanRotate() ? 2 : 1); ++Direction)
	{
		const FIntPoint Dimensions = Direction == 0 ? CurrentDimensions
			: FIntPoint(CurrentDimensions.Y, CurrentDimensions.X);
		for (int32 Index = 0; Index < Total; ++Index)
		{
			if (CanItemFitAt(SlotId, Index, Item, IgnoreItem, nullptr, nullptr, false, Dimensions))
			{
				bOutRotated = Direction == 0 ? Item->IsRotated() : !Item->IsRotated();
				return Index;
			}
		}
	}
	return INDEX_NONE;
}
void UINV_InventoryComponent::SetItemGridIndex(UINV_InventoryItem* Item, FName SlotId, int32 NewIndex)
{
	if (!IsValid(Item)) return;
	Item->SetContainerId(NewIndex == INDEX_NONE ? NAME_None : SlotId);
	Item->SetGridIndex(NewIndex);
	if (NewIndex != INDEX_NONE) Item->SetEquippedSlotType(FGameplayTag());
}
EINV_OperationFailure UINV_InventoryComponent::TransferItemToInventory(
	const FINV_InteractionContext& Context, UINV_InventoryComponent* TargetInventory)
{
	if (!IsValid(TargetInventory) || TargetInventory == this || !TargetInventory->GetOwner()
		|| !TargetInventory->GetOwner()->HasAuthority())
	{
		return EINV_OperationFailure::InvalidTarget;
	}
	UINV_InventoryItem* SourceItem = Context.Item;
	if (!IsValidInventoryItem(SourceItem)) return EINV_OperationFailure::InvalidItem;
	const bool bEquippedSource = SourceItem->IsEquipped();
	const FName SourceSlot = ResolveContainerSlotId(SourceItem, Context.SourceSlotId);
	if ((!bEquippedSource && (SourceSlot.IsNone() || SourceItem->GetContainerId() != SourceSlot
		|| SourceItem->GetGridIndex() != Context.SourceIndex
		|| !IsValidGridIndex(SourceSlot, Context.SourceIndex)))
		|| (bEquippedSource && (!Context.SourceSlotId.IsNone() || Context.SourceIndex != INDEX_NONE)))
	{
		return EINV_OperationFailure::InvalidLocation;
	}
	if (Context.TargetSlotId.IsNone()) return EINV_OperationFailure::InvalidLocation;
	const FName TargetSlot = Context.TargetSlotId;
	const FINV_GridSlotDefinition* TargetPanel = TargetInventory->GetGridPanel(TargetSlot);
	if (!TargetPanel || !INV_AcceptsItemCategory(TargetPanel->AcceptedCategory,
		SourceItem->GetItemManifest().GetItemCategory()))
	{
		return EINV_OperationFailure::InvalidLocation;
	}
	const int32 SourceCount = SourceItem->IsStackable() ? SourceItem->GetTotalStackCount() : 1;
	if (Context.Amount > SourceCount) return EINV_OperationFailure::InvalidQuantity;
	if (SourceItem->IsStackable() && !HasValidStack(SourceItem)) return EINV_OperationFailure::InvalidItem;
	UINV_InventoryItem* TargetItem = Context.SecondaryItem;
	if (IsValid(TargetItem))
	{
		if (!TargetInventory->IsValidInventoryItem(TargetItem) || TargetItem == SourceItem)
			return EINV_OperationFailure::InvalidItem;
		if (TargetItem->IsEquipped() || TargetItem->GetContainerId() != TargetSlot)
			return EINV_OperationFailure::InvalidLocation;
		if (Context.TargetIndex == INDEX_NONE || !TargetInventory->IsValidGridIndex(TargetSlot, Context.TargetIndex))
			return EINV_OperationFailure::InvalidLocation;
		const bool bMerge = TargetItem->IsStackable() && SourceItem->IsStackable()
			&& SourceItem->GetItemManifest().GetItemType().MatchesTagExact(TargetItem->GetItemManifest().GetItemType());
		if (!bMerge)
		{
			if (TargetInventory != this && Context.CrossContainerSwapEntries.IsEmpty())
				return EINV_OperationFailure::InvalidRequest;
			return SwapItemsBetweenInventories(Context, TargetInventory, TargetItem);
		}
		const FINV_StackableFragment* TargetStack =
			TargetItem->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>();
		if (!TargetStack || TargetItem->GetTotalStackCount() > TargetStack->GetMaxStackSize()
			|| Context.Amount > TargetStack->GetMaxStackSize() - TargetItem->GetTotalStackCount())
			return EINV_OperationFailure::NoSpace;
		const int32 SourceRemaining = SourceItem->GetTotalStackCount() - Context.Amount;
		if (SourceRemaining == 0)
		{
			SetItemGridIndex(SourceItem, NAME_None, INDEX_NONE);
			RemoveRepSubObj(SourceItem);
			InventoryList.RemoveEntry(SourceItem);
		}
		else
		{
			SetItemStackCount(SourceItem, SourceRemaining);
		}
		SetItemStackCount(TargetItem, TargetItem->GetTotalStackCount() + Context.Amount);
		TargetInventory->AuthorityRevision++;
		TargetInventory->ScheduleRefresh();
		return EINV_OperationFailure::None;
	}
	if (Context.TargetIndex != INDEX_NONE && !TargetInventory->IsValidGridIndex(TargetSlot, Context.TargetIndex))
		return EINV_OperationFailure::InvalidLocation;
	int32 TargetIndex = Context.TargetIndex;
	bool bTargetRotated = SourceItem->IsRotated();
	if (TargetIndex == INDEX_NONE)
	{
		TargetIndex = TargetInventory->FindFirstFreeGridIndex(TargetSlot, SourceItem, bTargetRotated);
		if (TargetIndex == INDEX_NONE) return EINV_OperationFailure::NoSpace;
	}
	else
	{
		bTargetRotated = SourceItem->CanRotate() && Context.bRotated;
		const FIntPoint BaseDimensions = SourceItem->GetBaseDimension();
		const FIntPoint TargetDimensions = bTargetRotated
			? FIntPoint(BaseDimensions.Y, BaseDimensions.X) : BaseDimensions;
		if (!TargetInventory->CanItemFitAt(TargetSlot, TargetIndex, SourceItem, nullptr, nullptr,
			nullptr, true, TargetDimensions))
		{
			const FIntPoint TargetGridSize = TargetInventory->GetGridSize(TargetSlot);
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory][Transfer] 目标落点无空间：Item=%s，Target=%s，Slot=%s，Index=%d，Grid=%dx%d，Dim=%dx%d，Rotated=%d。"),
				*GetNameSafe(SourceItem), *GetNameSafe(TargetInventory), *TargetSlot.ToString(),
				TargetIndex, TargetGridSize.X, TargetGridSize.Y, TargetDimensions.X, TargetDimensions.Y,
				bTargetRotated ? 1 : 0);
			return EINV_OperationFailure::NoSpace;
		}
	}
	const bool bMoveWholeEntry = !SourceItem->IsStackable()
		|| Context.Amount >= SourceItem->GetTotalStackCount();
	if (bMoveWholeEntry)
	{
		if (bEquippedSource)
		{
			SourceItem->SetEquippedSlotType(FGameplayTag());
			Multicast_EquipSlotClicked(nullptr, SourceItem);
		}
		if (!RehostInventoryItem(SourceItem, this, TargetInventory, TargetSlot, TargetIndex))
			return EINV_OperationFailure::SpawnFailed;
		SourceItem->SetRotated(bTargetRotated);
	}
	else
	{
		const int32 NewItemCount = SourceItem->IsStackable() ? Context.Amount : 0;
		if (!IsValid(TargetInventory->CreateItemEntryFromItem(
			SourceItem, NewItemCount, TargetIndex, TargetSlot, bTargetRotated)))
			return EINV_OperationFailure::SpawnFailed;
		SetItemStackCount(SourceItem, SourceItem->GetTotalStackCount() - Context.Amount);
	}
	TargetInventory->AuthorityRevision++;
	TargetInventory->ScheduleRefresh();
	UE_LOG(LogTemp, Log,
		TEXT("[Inventory][Transfer] 事务提交后状态：Source=%s，SourceItems=%d，Source仍含Item=%d；Target=%s，TargetItems=%d。"),
		*GetNameSafe(this), InventoryList.GetAllItems().Num(),
		InventoryList.GetAllItems().Contains(SourceItem) ? 1 : 0,
		*GetNameSafe(TargetInventory), TargetInventory->GetAllInventoryItems().Num());
	return EINV_OperationFailure::None;
}

EINV_OperationFailure UINV_InventoryComponent::SwapItemsBetweenInventories(
	const FINV_InteractionContext& Context, UINV_InventoryComponent* TargetInventory,
	UINV_InventoryItem* TargetItem)
{
	UINV_InventoryItem* SourceItem = Context.Item;
	if (!IsValid(TargetInventory) || !IsValidInventoryItem(SourceItem)
		|| !TargetInventory->IsValidInventoryItem(TargetItem) || SourceItem->IsEquipped()
		|| TargetItem->IsEquipped())
		return EINV_OperationFailure::InvalidItem;
	if (TargetInventory != this && Context.CrossContainerSwapEntries.IsEmpty())
		return EINV_OperationFailure::InvalidRequest;

	const FName SourceSlot = ResolveContainerSlotId(SourceItem, Context.SourceSlotId);
	const FName TargetSlot = ResolveContainerSlotId(TargetItem, Context.TargetSlotId);
	const int32 SourceIndex = SourceItem->GetGridIndex();
	const int32 TargetIndex = Context.TargetIndex;
	if (SourceSlot.IsNone() || TargetSlot.IsNone() || SourceIndex == INDEX_NONE || TargetIndex == INDEX_NONE
		|| !IsValidGridIndex(SourceSlot, SourceIndex) || !TargetInventory->IsValidGridIndex(TargetSlot, TargetIndex))
		return EINV_OperationFailure::InvalidLocation;

	const FIntPoint SourceBase = SourceItem->GetBaseDimension();
	const bool bOldSourceRotated = SourceItem->IsRotated();
	const bool bSourceRotated = SourceItem->CanRotate() && Context.bRotated;
	const FIntPoint SourceDimensions = bSourceRotated
		? FIntPoint(SourceBase.Y, SourceBase.X) : SourceBase;
	const FIntPoint TargetGridSize = TargetInventory->GetGridSize(TargetSlot);
	const FIntPoint SourceGridSize = GetGridSize(SourceSlot);
	const FIntPoint TargetOrigin(TargetIndex % TargetGridSize.X, TargetIndex / TargetGridSize.X);
	const FIntPoint SourceOrigin(SourceIndex % SourceGridSize.X, SourceIndex / SourceGridSize.X);
	TArray<UINV_InventoryItem*> TargetItems;
	for (UINV_InventoryItem* Item : TargetInventory->GetAllInventoryItems())
	{
		if (!IsValid(Item) || Item->IsEquipped() || !TargetInventory->IsItemInContainer(Item, TargetSlot)) continue;
		const int32 Index = Item->GetGridIndex();
		if (Index == INDEX_NONE) continue;
		const FIntPoint Origin(Index % TargetGridSize.X, Index / TargetGridSize.X);
		const FIntPoint Dim = Item->GetItemDimension();
		if (Origin.X < TargetOrigin.X + SourceDimensions.X && TargetOrigin.X < Origin.X + Dim.X
			&& Origin.Y < TargetOrigin.Y + SourceDimensions.Y && TargetOrigin.Y < Origin.Y + Dim.Y)
		{
			TargetItems.AddUnique(Item);
		}
	}
	if (TargetItems.Num() == 0) return EINV_OperationFailure::InvalidItem;

	TArray<int32> TargetDestinationIndices;
	TArray<FName> TargetDestinationSlots;
	TArray<bool> TargetDestinationRotations;
	TargetDestinationIndices.Reserve(TargetItems.Num());
	TargetDestinationSlots.Reserve(TargetItems.Num());
	TargetDestinationRotations.Reserve(TargetItems.Num());
	TArray<int32> TargetOriginalIndices;
	TargetOriginalIndices.Reserve(TargetItems.Num());
	TArray<FIntPoint> TargetRelativeOrigins;
	TargetRelativeOrigins.Reserve(TargetItems.Num());
	for (UINV_InventoryItem* Item : TargetItems)
	{
		const int32 OldIndex = Item->GetGridIndex();
		TargetOriginalIndices.Add(OldIndex);
		const FIntPoint OldOrigin(OldIndex % TargetGridSize.X, OldIndex / TargetGridSize.X);
		TargetRelativeOrigins.Add(OldOrigin - TargetOrigin);
	}

	auto CanPlace = [](const UINV_InventoryComponent* Component, FName Slot, int32 Index,
		const UINV_InventoryItem* Item, const FIntPoint& Dimensions,
		const TArray<UINV_InventoryItem*>& Ignored) -> bool
	{
		if (!Component || !IsValid(Item) || !Component->IsValidGridIndex(Slot, Index)) return false;
		const FINV_GridSlotDefinition* Panel = Component->GetGridPanel(Slot);
		if (!Panel || !INV_AcceptsItemCategory(Panel->AcceptedCategory, Item->GetItemManifest().GetItemCategory())) return false;
		const FIntPoint Size = Component->GetGridSize(Slot);
		const FIntPoint Origin(Index % Size.X, Index / Size.X);
		if (Dimensions.X <= 0 || Dimensions.Y <= 0 || Origin.X + Dimensions.X > Size.X || Origin.Y + Dimensions.Y > Size.Y) return false;
		for (UINV_InventoryItem* Other : Component->GetAllInventoryItems())
		{
			if (!IsValid(Other) || Ignored.Contains(Other) || !Component->IsItemInContainer(Other, Slot)) continue;
			const int32 OtherIndex = Other->GetGridIndex();
			if (OtherIndex == INDEX_NONE) continue;
			const FIntPoint OtherOrigin(OtherIndex % Size.X, OtherIndex / Size.X);
			const FIntPoint OtherDim = Other->GetItemDimension();
			if (Origin.X < OtherOrigin.X + OtherDim.X && OtherOrigin.X < Origin.X + Dimensions.X
				&& Origin.Y < OtherOrigin.Y + OtherDim.Y && OtherOrigin.Y < Origin.Y + Dimensions.Y) return false;
		}
		return true;
	};

	TArray<UINV_InventoryItem*> TargetIgnored = TargetItems;
	TargetIgnored.Add(SourceItem);
	if (!CanPlace(TargetInventory, TargetSlot, TargetIndex, SourceItem, SourceDimensions, TargetIgnored))
		return EINV_OperationFailure::NoSpace;
	TArray<UINV_InventoryItem*> SourceIgnored = TargetItems;
	SourceIgnored.Add(SourceItem);
	TArray<FName> PlannedSlots;
	TArray<int32> PlannedIndices;
	TArray<FIntPoint> PlannedDimensions;
	auto OverlapsPlanned = [&](FName Slot, int32 Index, const FIntPoint& Dimensions) -> bool
	{
		const FIntPoint Size = GetGridSize(Slot);
		const FIntPoint Origin(Index % Size.X, Index / Size.X);
		for (int32 Planned = 0; Planned < PlannedSlots.Num(); ++Planned)
		{
			if (PlannedSlots[Planned] != Slot) continue;
			const FIntPoint OtherOrigin(PlannedIndices[Planned] % Size.X, PlannedIndices[Planned] / Size.X);
			const FIntPoint& OtherDim = PlannedDimensions[Planned];
			if (Origin.X < OtherOrigin.X + OtherDim.X && OtherOrigin.X < Origin.X + Dimensions.X
				&& Origin.Y < OtherOrigin.Y + OtherDim.Y && OtherOrigin.Y < Origin.Y + Dimensions.Y) return true;
		}
		return false;
	};
	if (TargetInventory == this)
	{
	for (int32 Index = 0; Index < TargetItems.Num(); ++Index)
	{
		UINV_InventoryItem* Item = TargetItems[Index];
		const bool bOriginalRotated = Item->IsRotated();
		const FIntPoint OriginalDimensions = Item->GetItemDimension();
		const FIntPoint RotatedDimensions(OriginalDimensions.Y, OriginalDimensions.X);
		const FIntPoint PreferredOrigin = SourceOrigin + TargetRelativeOrigins[Index];
		FName ChosenSlot = SourceSlot;
		int32 ChosenIndex = INDEX_NONE;
		FIntPoint ChosenDimensions = OriginalDimensions;
		bool bChosenRotated = bOriginalRotated;
		auto TryCandidate = [&](FName CandidateSlot, int32 CandidateIndex,
			const FIntPoint& CandidateDimensions, bool bCandidateRotated) -> bool
		{
			if (!CanPlace(this, CandidateSlot, CandidateIndex, Item, CandidateDimensions, SourceIgnored)
				|| OverlapsPlanned(CandidateSlot, CandidateIndex, CandidateDimensions)) return false;
			ChosenSlot = CandidateSlot;
			ChosenIndex = CandidateIndex;
			ChosenDimensions = CandidateDimensions;
			bChosenRotated = bCandidateRotated;
			return true;
		};
		const int32 PreferredIndex = PreferredOrigin.X >= 0 && PreferredOrigin.Y >= 0
			&& PreferredOrigin.X < SourceGridSize.X && PreferredOrigin.Y < SourceGridSize.Y
			? PreferredOrigin.Y * SourceGridSize.X + PreferredOrigin.X : INDEX_NONE;
		bool bFound = PreferredIndex != INDEX_NONE
			&& (TryCandidate(SourceSlot, PreferredIndex, OriginalDimensions, bOriginalRotated)
				|| (Item->CanRotate() && TryCandidate(SourceSlot, PreferredIndex, RotatedDimensions, !bOriginalRotated)));
		if (!bFound)
		{
			TArray<const FINV_GridSlotDefinition*> Panels;
			GetAllGridPanels(Panels);
			for (const FINV_GridSlotDefinition* Panel : Panels)
			{
				if (!Panel || Panel->ContainerType != EINV_GridContainerType::Grid) continue;
				const FIntPoint Size = GetGridSize(Panel->SlotId);
				for (int32 Candidate = 0; Candidate < Size.X * Size.Y; ++Candidate)
				{
					if (TryCandidate(Panel->SlotId, Candidate, OriginalDimensions, bOriginalRotated)
						|| (Item->CanRotate() && TryCandidate(Panel->SlotId, Candidate,
							RotatedDimensions, !bOriginalRotated)))
					{
						bFound = true;
						break;
					}
				}
				if (bFound) break;
			}
		}
		if (!bFound) return EINV_OperationFailure::NoSpace;
		TargetDestinationSlots.Add(ChosenSlot);
		TargetDestinationIndices.Add(ChosenIndex);
		TargetDestinationRotations.Add(bChosenRotated);
		PlannedSlots.Add(ChosenSlot);
		PlannedIndices.Add(ChosenIndex);
		PlannedDimensions.Add(ChosenDimensions);
	}
	}
	else
	{
		TSet<UINV_InventoryItem*> PlannedItems;
		for (UINV_InventoryItem* Item : TargetItems)
		{
			const FINV_CrossContainerSwapEntry* Entry = Context.CrossContainerSwapEntries.FindByPredicate(
				[Item](const FINV_CrossContainerSwapEntry& Candidate) { return Candidate.Item == Item; });
			if (!Entry || PlannedItems.Contains(Item) || Entry->TargetSlotId.IsNone()
				|| !IsValidGridIndex(Entry->TargetSlotId, Entry->TargetIndex)
				|| (Entry->bRotated && !Item->CanRotate())) return EINV_OperationFailure::InvalidLocation;
			const FIntPoint Dim = Entry->bRotated
				? FIntPoint(Item->GetBaseDimension().Y, Item->GetBaseDimension().X)
				: Item->GetBaseDimension();
			if (!CanPlace(this, Entry->TargetSlotId, Entry->TargetIndex, Item, Dim, SourceIgnored)
				|| OverlapsPlanned(Entry->TargetSlotId, Entry->TargetIndex, Dim)) return EINV_OperationFailure::NoSpace;
			TargetDestinationSlots.Add(Entry->TargetSlotId);
			TargetDestinationIndices.Add(Entry->TargetIndex);
			TargetDestinationRotations.Add(Entry->bRotated);
			PlannedSlots.Add(Entry->TargetSlotId);
			PlannedIndices.Add(Entry->TargetIndex);
			PlannedDimensions.Add(Dim);
			PlannedItems.Add(Item);
		}
		if (PlannedItems.Num() != TargetItems.Num()) return EINV_OperationFailure::InvalidLocation;
	}

	if (TargetInventory == this)
	{
		SetItemGridIndex(SourceItem, TargetSlot, TargetIndex);
		SourceItem->SetRotated(bSourceRotated);
		for (int32 Index = 0; Index < TargetItems.Num(); ++Index)
		{
			SetItemGridIndex(TargetItems[Index], TargetDestinationSlots[Index], TargetDestinationIndices[Index]);
			TargetItems[Index]->SetRotated(TargetDestinationRotations[Index]);
		}
	}
	else
	{
		if (!RehostInventoryItem(SourceItem, this, TargetInventory, TargetSlot, TargetIndex)) return EINV_OperationFailure::SpawnFailed;
		SourceItem->SetRotated(bSourceRotated);
		for (int32 Index = 0; Index < TargetItems.Num(); ++Index)
		{
			if (!RehostInventoryItem(TargetItems[Index], TargetInventory, this,
				TargetDestinationSlots[Index], TargetDestinationIndices[Index]))
			{
				for (int32 RollbackIndex = 0; RollbackIndex < Index; ++RollbackIndex)
				{
					RehostInventoryItem(TargetItems[RollbackIndex], this, TargetInventory,
						TargetSlot, TargetOriginalIndices[RollbackIndex]);
				}
				RehostInventoryItem(SourceItem, TargetInventory, this, SourceSlot, SourceIndex);
				SourceItem->SetRotated(bOldSourceRotated);
				return EINV_OperationFailure::SpawnFailed;
			}
			TargetItems[Index]->SetRotated(TargetDestinationRotations[Index]);
		}
	}
	AuthorityRevision++;
	TargetInventory->AuthorityRevision++;
	ScheduleRefresh();
	TargetInventory->ScheduleRefresh();
	return EINV_OperationFailure::None;
}
EINV_OperationFailure UINV_InventoryComponent::TransferAndEquipFromInventory(
	UINV_InventoryComponent* SourceInventory, UINV_InventoryItem* SourceItem,
	FGameplayTag EquipmentSlotType)
{
	if (!IsValid(SourceInventory) || SourceInventory == this
		|| !EquipmentSlotType.IsValid() || !SourceInventory->IsValidInventoryItem(SourceItem))
		return EINV_OperationFailure::InvalidItem;
	if (SourceItem->IsEquipped() || SourceItem->IsStackable()
		|| SourceItem->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable
		|| !SourceItem->GetItemManifest().GetFragmentOfType<FINV_EquipmentFragment>()
		|| !SourceItem->GetItemManifest().GetItemType().MatchesTag(EquipmentSlotType))
		return EINV_OperationFailure::EquipmentMismatch;
	UINV_InventoryItem* ExistingEquipped = nullptr;
	for (UINV_InventoryItem* Existing : InventoryList.GetAllItems())
	{
		if (IsValid(Existing) && Existing->IsEquipped()
			&& Existing->GetEquippedSlotType().MatchesTag(EquipmentSlotType))
		{
			ExistingEquipped = Existing;
			break;
		}
	}
	FName ReturnSlotId = NAME_None;
	int32 ReturnIndex = INDEX_NONE;
	bool bReturnRotated = false;
	if (ExistingEquipped)
	{
		const FINV_GridSlotDefinition* ReturnPanel = SourceInventory->FindGridPanelForCategory(
			ExistingEquipped->GetItemManifest().GetItemCategory());
		if (!ReturnPanel) return EINV_OperationFailure::NoSpace;
		ReturnSlotId = ReturnPanel->SlotId;
		ReturnIndex = SourceInventory->FindFirstFreeGridIndex(
			ReturnSlotId, ExistingEquipped, bReturnRotated, SourceItem);
		if (ReturnIndex == INDEX_NONE) return EINV_OperationFailure::NoSpace;
	}
	const FName SourceOldSlot = SourceItem->GetContainerId();
	const int32 SourceOldIndex = SourceItem->GetGridIndex();
	const bool bSourceOldRotated = SourceItem->IsRotated();
	if (!RehostInventoryItem(SourceItem, SourceInventory, this, NAME_None, INDEX_NONE))
		return EINV_OperationFailure::SpawnFailed;
	if (FINV_EquipmentFragment* Equipment =
		SourceItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FINV_EquipmentFragment>())
	{
		Equipment->bEquipable = false;
		Equipment->SetEquippedActor(nullptr);
	}
	SourceItem->SetEquippedSlotType(EquipmentSlotType);
	SourceItem->SetRotated(false);
	if (ExistingEquipped)
	{
		if (!RehostInventoryItem(ExistingEquipped, this, SourceInventory, ReturnSlotId, ReturnIndex))
		{
			if (RehostInventoryItem(SourceItem, this, SourceInventory, SourceOldSlot, SourceOldIndex))
			{
				SourceItem->SetEquippedSlotType(FGameplayTag());
				SourceItem->SetRotated(bSourceOldRotated);
			}
			return EINV_OperationFailure::SpawnFailed;
		}
		ExistingEquipped->SetEquippedSlotType(FGameplayTag());
		ExistingEquipped->SetRotated(bReturnRotated && ExistingEquipped->CanRotate());
	}
	SourceInventory->AuthorityRevision++;
	SourceInventory->ScheduleRefresh();
	AuthorityRevision++;
	ScheduleRefresh();
	Multicast_EquipSlotClicked(SourceItem, ExistingEquipped);
	return EINV_OperationFailure::None;
}
void UINV_InventoryComponent::Server_RegisterItemPlacement_Implementation(UINV_InventoryItem* Item,
	FName SlotId, int32 Index, bool bRotated)
{
	if (!IsValidInventoryItem(Item) || Item->IsEquipped()) return;
	const FName Slot = ResolveContainerSlotId(Item, SlotId);
	if (!IsValidGridIndex(Slot, Index)) return;
	const bool bOldRotated = Item->IsRotated();
	Item->SetRotated(Item->CanRotate() && bRotated);
	if (!CanItemFitAt(Slot, Index, Item))
	{
		Item->SetRotated(bOldRotated);
		Server_RequestAuthoritativeRefresh_Implementation();
		return;
	}
	SetItemGridIndex(Item, Slot, Index);
}
void UINV_InventoryComponent::Server_MoveItem_Implementation(UINV_InventoryItem* Item, FName FromSlotId, int32 FromIndex,
	FName ToSlotId, int32 ToIndex, bool bRotated)
{
	if (!IsValidInventoryItem(Item)) return;
	const FName FromSlot = ResolveContainerSlotId(Item, FromSlotId);
	const FName ToSlot = ResolveContainerSlotId(Item, ToSlotId);
	if (FromIndex != INDEX_NONE && !IsValidGridIndex(FromSlot, FromIndex)) return;
	if (!IsValidGridIndex(ToSlot, ToIndex)) return;
	if (FromSlot == ToSlot && FromIndex == ToIndex && Item->IsRotated() == bRotated) return;   
	const bool bOldRotated = Item->IsRotated();
	if (Item->CanRotate() && bOldRotated != bRotated)
	{
		Item->SetRotated(bRotated);
	}
	UINV_InventoryItem* Blocking = nullptr;
	if (!CanItemFitAt(ToSlot, ToIndex, Item, Item, nullptr, &Blocking))
	{
		if (Item->IsRotated() != bOldRotated)
		{
			Item->SetRotated(bOldRotated);
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] MoveItem 被拒：%s (%dx%d) 放到 %s[%d] 放不下；阻挡物=%s（它的 GridIndex=%d）；来源 %s[%d]。"),
			*GetNameSafe(Item), Item->GetItemDimension().X, Item->GetItemDimension().Y,
			*ToSlot.ToString(), ToIndex, *GetNameSafe(Blocking),
			Blocking ? Blocking->GetGridIndex() : INDEX_NONE,
			*FromSlot.ToString(), FromIndex);
		return;
	}
	SetItemGridIndex(Item, ToSlot, ToIndex);
	UE_LOG(LogTemp, Log,
		TEXT("[Inventory][Server] MoveItem 执行成功：%s %s[%d] → %s[%d]。"),
		*GetNameSafe(Item), *FromSlot.ToString(), FromIndex, *ToSlot.ToString(), ToIndex);
}
void UINV_InventoryComponent::Server_SwapItems_Implementation(UINV_InventoryItem* ItemA, FName SlotIdA, int32 IndexA,
	UINV_InventoryItem* ItemB, FName SlotIdB, int32 IndexB)
{
	if (!IsValidInventoryItem(ItemA) || !IsValidInventoryItem(ItemB) || ItemA == ItemB) return;
	FINV_InteractionContext Context(GetOwner(), EINV_InteractionType::Transfer);
	Context.Item = ItemA;
	Context.SourceSlotId = SlotIdA;
	Context.SourceIndex = IndexA;
	Context.TargetSlotId = SlotIdB;
	Context.TargetIndex = IndexB;
	Context.bRotated = ItemA->IsRotated();
	SwapItemsBetweenInventories(Context, this, ItemB);
}
void UINV_InventoryComponent::Server_MoveItems_Implementation(const TArray<UINV_InventoryItem*>& Items,
	const TArray<int32>& ToIndices, FName SlotId, const TArray<bool>& RotatedStates)
{
	if (Items.Num() == 0 || Items.Num() != ToIndices.Num()) return;
	TSet<UINV_InventoryItem*> UniqueItems;
	for (UINV_InventoryItem* Item : Items)
	{
		if (!IsValidInventoryItem(Item) || UniqueItems.Contains(Item)) return;
		UniqueItems.Add(Item);
	}
	UE_LOG(LogTemp, Log, TEXT("[Inventory] MoveItems 收到：%d 件（索引数=%d，朝向数=%d）"),
		Items.Num(), ToIndices.Num(), RotatedStates.Num());
	TArray<bool> OldRotatedStates;
	OldRotatedStates.SetNum(Items.Num());
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		UINV_InventoryItem* Item = Items[Index];
		OldRotatedStates[Index] = Item->IsRotated();
		if (!RotatedStates.IsValidIndex(Index) || !Item->CanRotate()) continue;
		if (Item->IsRotated() != RotatedStates[Index])
		{
			Item->SetRotated(RotatedStates[Index]);
		}
	}
	const FName Slot = ResolveContainerSlotId(Items[0], SlotId);
	const FIntPoint GridSize = GetGridSize(Slot);
	const bool bCanExpand = GridSize.X > 0 && GridSize.Y > 0;
	auto RestoreRotations = [&Items, &OldRotatedStates]()
	{
		for (int32 i = 0; i < Items.Num() && i < OldRotatedStates.Num(); ++i)
		{
			if (IsValid(Items[i]) && Items[i]->IsRotated() != OldRotatedStates[i])
			{
				Items[i]->SetRotated(OldRotatedStates[i]);
			}
		}
	};
	TSet<const UINV_InventoryItem*> MovingSet;
	for (UINV_InventoryItem* Item : Items)
	{
		MovingSet.Add(Item);
	}
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const int32 ToIndex = ToIndices[Index];
		if (!IsValidGridIndex(Slot, ToIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inventory] MoveItems 被拒：%s 的目标索引 %d 越界。"),
				*GetNameSafe(Items[Index]), ToIndex);
			RestoreRotations();
			return;
		}
		if (!bCanExpand) continue;
		const FIntPoint Origin(ToIndex % GridSize.X, ToIndex / GridSize.X);
		const FIntPoint Dim = Items[Index]->GetItemDimension();
		if (Dim.X <= 0 || Dim.Y <= 0 || Dim.X > GridSize.X - Origin.X || Dim.Y > GridSize.Y - Origin.Y)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] MoveItems 被拒：%s (%dx%d) 放到 [%d] 会越界（网格 %dx%d）。"),
				*GetNameSafe(Items[Index]), Dim.X, Dim.Y, ToIndex, GridSize.X, GridSize.Y);
			RestoreRotations();
			return;
		}
		for (UINV_InventoryItem* Other : InventoryList.GetAllItems())
		{
			if (!IsValid(Other) || MovingSet.Contains(Other)) continue;
			if (!IsItemInContainer(Other, Slot)) continue;
			const int32 OtherIndex = Other->GetGridIndex();
			if (OtherIndex == INDEX_NONE) continue;
			const FIntPoint OtherCoord(OtherIndex % GridSize.X, OtherIndex / GridSize.X);
			const FIntPoint OtherDim = Other->GetItemDimension();
			const bool bOverlap =
				Origin.X < OtherCoord.X + OtherDim.X && OtherCoord.X < Origin.X + Dim.X &&
				Origin.Y < OtherCoord.Y + OtherDim.Y && OtherCoord.Y < Origin.Y + Dim.Y;
			if (bOverlap)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Inventory] MoveItems 被拒：%s (%dx%d) 的新位置 [%d] 与组外物品 %s（GridIndex=%d）重叠。"),
					*GetNameSafe(Items[Index]), Dim.X, Dim.Y, ToIndex,
					*GetNameSafe(Other), OtherIndex);
				RestoreRotations();
				return;
			}
		}
		for (int32 OtherIdx = Index + 1; OtherIdx < Items.Num(); ++OtherIdx)
		{
			const int32 OtherToIndex = ToIndices[OtherIdx];
			if (OtherToIndex < 0) continue;
			const FIntPoint OtherOrigin(OtherToIndex % GridSize.X, OtherToIndex / GridSize.X);
			const FIntPoint OtherDim = Items[OtherIdx]->GetItemDimension();
			const bool bOverlap =
				Origin.X < OtherOrigin.X + OtherDim.X && OtherOrigin.X < Origin.X + Dim.X &&
				Origin.Y < OtherOrigin.Y + OtherDim.Y && OtherOrigin.Y < Origin.Y + Dim.Y;
			if (bOverlap)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Inventory] MoveItems 被拒：%s 与 %s 的目标区域在 [%d] / [%d] 处重叠。"),
					*GetNameSafe(Items[Index]), *GetNameSafe(Items[OtherIdx]), ToIndex, OtherToIndex);
				RestoreRotations();
				return;
			}
		}
	}
	TArray<int32> FromIndices;
	FromIndices.Reserve(Items.Num());
	for (UINV_InventoryItem* Item : Items)
	{
		FromIndices.Add(Item->GetGridIndex());
	}
	FString Applied;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		SetItemGridIndex(Items[Index], Slot, ToIndices[Index]);
		Applied += FString::Printf(TEXT(" %s %s[%d] → %s[%d]；"),
			*GetNameSafe(Items[Index]), *Slot.ToString(), FromIndices[Index],
			*Slot.ToString(), ToIndices[Index]);
	}
	UE_LOG(LogTemp, Log, TEXT("[Inventory][Server] MoveItems 执行成功：容器=%s，共 %d 件：%s"),
		*Slot.ToString(), Items.Num(), *Applied);
}
void UINV_InventoryComponent::TestMoveOwnerOverlap()
{
	TArray<UINV_InventoryItem*> GridItems;
	for (UINV_InventoryItem* Item : InventoryList.GetAllItems())
	{
		if (IsValid(Item) && Item->GetGridIndex() != INDEX_NONE)
		{
			GridItems.Add(Item);
		}
	}
	if (GridItems.Num() < 2)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory][Test] 组外重叠测试跳过：网格内物品不足 2 件（当前 %d）。请先捡几件东西进背包。"),
			GridItems.Num());
		return;
	}
	UINV_InventoryItem* Mover = GridItems[0];
	const FName Slot = Mover->GetContainerId();
	UINV_InventoryItem* Victim = nullptr;
	for (UINV_InventoryItem* Item : GridItems)
	{
		if (Item != Mover && Item->GetContainerId() == Slot)
		{
			Victim = Item;
			break;
		}
	}
	if (!IsValid(Victim))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory][Test] 组外重叠测试跳过：容器 %s 里找不到第二件物品。"), *Slot.ToString());
		return;
	}
	const int32 VictimIndex = Victim->GetGridIndex();
	UE_LOG(LogTemp, Warning,
		TEXT("[Inventory][Test] 组外重叠测试：把 %s（[%d]，容器=%s）强搬到 %s 占着的 [%d]。预期：服务器打印 MoveItems 被拒。"),
		*GetNameSafe(Mover), Mover->GetGridIndex(), *Slot.ToString(), *GetNameSafe(Victim), VictimIndex);
	TArray<UINV_InventoryItem*> Items{Mover};
	TArray<int32> Indices{VictimIndex};
	TArray<bool> Rotations{Mover->IsRotated()};
	Server_MoveItems(Items, Indices, Slot, Rotations);
}
void UINV_InventoryComponent::Server_SwapStackCounts_Implementation(UINV_InventoryItem* ItemA, UINV_InventoryItem* ItemB)
{
	if (!IsValidInventoryItem(ItemA) || !IsValidInventoryItem(ItemB)) return;
	if (ItemA == ItemB) return;
	if (!ItemA->IsStackable() || !ItemB->IsStackable()) return;
	if (!ItemA->GetItemManifest().GetItemType().MatchesTagExact(ItemB->GetItemManifest().GetItemType()))
	{
		return;
	}
	const int32 CountA = ItemA->GetTotalStackCount();
	const int32 CountB = ItemB->GetTotalStackCount();
	if (!HasValidStack(ItemA) || !HasValidStack(ItemB)
		|| CountB > ItemA->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetMaxStackSize()
		|| CountA > ItemB->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetMaxStackSize()) return;
	SetItemStackCount(ItemA, CountB);
	SetItemStackCount(ItemB, CountA);
	UE_LOG(LogTemp, Log,
		TEXT("[Inventory][Server] SwapStackCounts 执行成功：%s[%d] %d → %d，%s[%d] %d → %d。"),
		*GetNameSafe(ItemA), ItemA->GetGridIndex(), CountA, CountB,
		*GetNameSafe(ItemB), ItemB->GetGridIndex(), CountB, CountA);
}
void UINV_InventoryComponent::Server_SplitStack_Implementation(UINV_InventoryItem* Item, FName SlotId, int32 FromIndex,
	int32 SplitAmount, int32 ToIndex)
{
	if (!IsValidInventoryItem(Item)) return;
	if (!HasValidStack(Item) || Item->IsEquipped() || SplitAmount <= 0
		|| SplitAmount >= Item->GetTotalStackCount()) return;
	const FName Slot = ResolveContainerSlotId(Item, SlotId);
	if (Item->GetContainerId() != Slot || Item->GetGridIndex() != FromIndex) return;
	if (!IsValidGridIndex(Slot, FromIndex)) return;
	if (!Item->IsStackable()) return;
	int32 TargetIndex = ToIndex;
	bool bTargetRotated = Item->IsRotated();
	if (TargetIndex == INDEX_NONE)
	{
		TargetIndex = FindFirstFreeGridIndex(Slot, Item, bTargetRotated);
		if (TargetIndex == INDEX_NONE)
		{
			const FIntPoint GridSize = GetGridSize(Slot);
			FString Occupancy;
			int32 ItemCount = 0;
			for (UINV_InventoryItem* Other : InventoryList.GetAllItems())
			{
				if (!IsValid(Other)) continue;
				++ItemCount;
				if (!IsItemInContainer(Other, Slot)) continue;
				Occupancy += FString::Printf(TEXT(" %s@%d(%dx%d)"),
					*GetNameSafe(Other), Other->GetGridIndex(),
					Other->GetItemDimension().X, Other->GetItemDimension().Y);
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] SplitStack 被拒：%s 在 %s 里已无空位。网格=%dx%d，库存条目=%d；占用：%s"),
				*GetNameSafe(Item), *Slot.ToString(), GridSize.X, GridSize.Y, ItemCount,
				Occupancy.IsEmpty() ? TEXT("（空）") : *Occupancy);
			return;
		}
	}
	else if (!IsValidGridIndex(Slot, TargetIndex))
	{
		return;
	}
	UINV_InventoryItem* Blocking = nullptr;
	const FIntPoint BaseDimensions = Item->GetBaseDimension();
	FIntPoint TargetDimensions = bTargetRotated ? FIntPoint(BaseDimensions.Y, BaseDimensions.X) : BaseDimensions;
	bool bFits = CanItemFitAt(Slot, TargetIndex, Item, nullptr, nullptr, &Blocking, false, TargetDimensions);
	if (!bFits && ToIndex != INDEX_NONE && Item->CanRotate())
	{
		TargetDimensions = FIntPoint(TargetDimensions.Y, TargetDimensions.X);
		bFits = CanItemFitAt(Slot, TargetIndex, Item, nullptr, nullptr, &Blocking, false, TargetDimensions);
		if (bFits) bTargetRotated = !bTargetRotated;
	}
	if (!bFits)
	{
		const FIntPoint GridSize = GetGridSize(Slot);
		const int32 Cols = FMath::Max(1, GridSize.X);
		const FIntPoint Dim = TargetDimensions;
		const FString TargetRect = FString::Printf(
			TEXT("矩形=(%d,%d) 起 %dx%d"), TargetIndex % Cols, TargetIndex / Cols, Dim.X, Dim.Y);
		FString BlockingInfo = TEXT("无");
		if (Blocking)
		{
			const int32 OtherIndex = Blocking->GetGridIndex();
			const FIntPoint OtherDim = Blocking->GetItemDimension();
			BlockingInfo = FString::Printf(
				TEXT("%s（GridIndex=%d，推断容器=%s，矩形=(%d,%d) 起 %dx%d）"),
				*GetNameSafe(Blocking), OtherIndex,
				*ResolveItemSlotId(Blocking).ToString(),
				OtherIndex % Cols, OtherIndex / Cols, OtherDim.X, OtherDim.Y);
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] SplitStack 被拒：%s (%dx%d) 的拆出品放到 %s[%d] 放不下；")
			TEXT("该容器网格=%dx%d，目标 %s；阻挡物=%s。")
			TEXT("若阻挡物的矩形里并不包含目标格，说明它的 GridIndex 属于另一个容器（坐标系错位）。"),
			*GetNameSafe(Item), Dim.X, Dim.Y,
			*Slot.ToString(), TargetIndex,
			GridSize.X, GridSize.Y, *TargetRect, *BlockingInfo);
		return;
	}
	const int32 SourceCount = Item->GetTotalStackCount();
	SetItemStackCount(Item, SourceCount - SplitAmount);
	UINV_InventoryItem* NewItem = CreateItemEntryFromItem(Item, SplitAmount, TargetIndex, Slot, bTargetRotated);
	if (!IsValid(NewItem))
	{
		SetItemStackCount(Item, SourceCount);
		return;
	}
	UE_LOG(LogTemp, Log,
		TEXT("[Inventory][Server] SplitStack 执行成功：%s 拆出 %d 件（源 %d → %d），新条目 %s 落在 %s[%d]。"),
		*GetNameSafe(Item), SplitAmount, SourceCount, SourceCount - SplitAmount,
		*GetNameSafe(NewItem), *Slot.ToString(), TargetIndex);
}
void UINV_InventoryComponent::Server_MergeStack_Implementation(UINV_InventoryItem* FromItem, UINV_InventoryItem* ToItem, int32 Amount)
{
	if (!IsValidInventoryItem(FromItem) || !IsValidInventoryItem(ToItem)) return;
	if (FromItem == ToItem || !HasValidStack(FromItem) || !HasValidStack(ToItem)) return;
	const int32 Maximum = ToItem->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetMaxStackSize();
	if (Amount > Maximum - ToItem->GetTotalStackCount()) return;
	if (!FromItem->IsStackable() || !ToItem->IsStackable())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory] MergeStack 被拒：物品不可堆叠（%s / %s）。"),
			*GetNameSafe(FromItem), *GetNameSafe(ToItem));
		return;
	}
	if (!FromItem->GetItemManifest().GetItemType().MatchesTagExact(ToItem->GetItemManifest().GetItemType()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory] MergeStack 被拒：物品类型不一致（%s / %s）。"),
			*GetNameSafe(FromItem), *GetNameSafe(ToItem));
		return;
	}
	if (Amount <= 0 || Amount > FromItem->GetTotalStackCount())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] MergeStack 被拒：Amount=%d，源条目记录=%d（%s → %s）。")
			TEXT("常见原因是客户端的堆叠数与服务器不同步。"),
			Amount, FromItem->GetTotalStackCount(), *GetNameSafe(FromItem), *GetNameSafe(ToItem));
		return;
	}
	const int32 FromBefore = FromItem->GetTotalStackCount();
	const int32 ToBefore = ToItem->GetTotalStackCount();
	const int32 ToIndex = ToItem->GetGridIndex();
	const int32 NewFromStack = FromBefore - Amount;
	const bool bSourceConsumed = NewFromStack <= 0;
	if (bSourceConsumed)
	{
		SetItemGridIndex(FromItem, NAME_None, INDEX_NONE);
		InventoryList.RemoveEntry(FromItem);
	}
	else
	{
		SetItemStackCount(FromItem, NewFromStack);
	}
	SetItemStackCount(ToItem, ToBefore + Amount);
	if (bSourceConsumed)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Inventory][Server] MergeStack 执行成功：%s 整份 %d 件并入 %s[%d]（%d → %d），源条目已移除。"),
			*GetNameSafe(FromItem), FromBefore, *GetNameSafe(ToItem), ToIndex,
			ToBefore, ToItem->GetTotalStackCount());
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Inventory][Server] MergeStack 执行成功：%s 转出 %d 件（%d → %d）给 %s[%d]（%d → %d）。"),
			*GetNameSafe(FromItem), Amount, FromBefore, FromItem->GetTotalStackCount(),
			*GetNameSafe(ToItem), ToIndex, ToBefore, ToItem->GetTotalStackCount());
	}
}
void UINV_InventoryComponent::ToggleInventoryMenu()
{
	if (bInventoryMenuOpen)
	{
		CloseInventoryMenu();
	}
	else
	{
		OpenInventoryMenu();
	}
	OnInventoryMenuToggled.Broadcast(bInventoryMenuOpen);
}
void UINV_InventoryComponent::AddRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && IsValid(SubObj))
	{
		AddReplicatedSubObject(SubObj);
	}
}
void UINV_InventoryComponent::RemoveRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && IsValid(SubObj))
	{
		RemoveReplicatedSubObject(SubObj);
	}
}
void UINV_InventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ValidateGridLayouts();
	}
	ConstructInventory();
}
void UINV_InventoryComponent::ReadyForReplication()
{
	Super::ReadyForReplication();
	for (UINV_InventoryItem* Item : InventoryList.GetAllItems())
	{
		AddRepSubObj(Item);
	}
}
void UINV_InventoryComponent::ConstructInventory()
{
	OwningController = Cast<APlayerController>(GetOwner());
	if (!OwningController.IsValid() || !OwningController->IsLocalController()) return;
	InventoryMenu = CreateWidget<UINV_InventoryBase>(OwningController.Get(), InventoryMenuClass);
	if (InventoryMenu)
	{
		InventoryMenu->SetVisibility(ESlateVisibility::Collapsed);
		bInventoryMenuOpen = false;
	}
}
bool UINV_InventoryComponent::CanAcceptTransfer_Implementation(AActor* Interactor, AActor* SourceOwner) const
{
	if (!bAllowTransfer || (bRequireContainerOpenForTransfer && !bContainerOpen)
		|| !IsValid(Interactor) || !GetOwner()) return false;
	const AActor* TargetOwner = GetOwner();
	if (TargetOwner == Interactor || TargetOwner == SourceOwner) return true;
	const AActor* InteractorLocationActor = Interactor;
	if (const AController* Controller = Cast<AController>(Interactor))
	{
		InteractorLocationActor = Controller->GetPawn();
	}
	const AActor* TargetLocationActor = TargetOwner;
	if (const AController* Controller = Cast<AController>(TargetOwner))
	{
		TargetLocationActor = Controller->GetPawn();
	}
	if (!IsValid(InteractorLocationActor) || !IsValid(TargetLocationActor)
		|| TransferInteractionDistance <= 0.f) return false;
	return FVector::DistSquared(InteractorLocationActor->GetActorLocation(), TargetLocationActor->GetActorLocation())
		<= FMath::Square(TransferInteractionDistance);
}
void UINV_InventoryComponent::SetContainerOpen(const bool bOpen)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (bContainerOpen == bOpen) return;
	bContainerOpen = bOpen;
}
void UINV_InventoryComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, InventoryList);
	DOREPLIFETIME(ThisClass, bContainerOpen);
}
void UINV_InventoryComponent::OpenInventoryMenu()
{
	if (!IsValid(InventoryMenu) || !OwningController.IsValid()) return;
	InventoryMenu->SetVisibility(ESlateVisibility::Visible);
	bInventoryMenuOpen = true;
	FInputModeGameAndUI InputMode;
	OwningController->SetInputMode(InputMode);
	OwningController->SetShowMouseCursor(true);
}
void UINV_InventoryComponent::CloseInventoryMenu()
{
	if (!IsValid(InventoryMenu) || !OwningController.IsValid()) return;
	InventoryMenu->OnInventoryClosed();
	InventoryMenu->SetVisibility(ESlateVisibility::Collapsed);
	bInventoryMenuOpen = false;
	FInputModeGameOnly InputMode;
	OwningController->SetInputMode(InputMode);
	OwningController->SetShowMouseCursor(false);
}
static FAutoConsoleCommand GINVTestMoveOverlapCmd(
	TEXT("INV.TestMoveOverlap"),
	TEXT("测试 #14：把同一容器里第一件物品强搬到第二件占着的格子，验证 Server_MoveItems 的组外重叠硬拒绝。"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UWorld* TargetWorld = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					TargetWorld = Context.World();
					if (TargetWorld->GetFirstPlayerController()) break;
				}
			}
		}
		APlayerController* PC = TargetWorld ? TargetWorld->GetFirstPlayerController() : nullptr;
		if (!IsValid(PC))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inventory][Test] INV.TestMoveOverlap：找不到 PlayerController。"));
			return;
		}
		UINV_InventoryComponent* Comp = PC->FindComponentByClass<UINV_InventoryComponent>();
		if (!IsValid(Comp) && IsValid(PC->GetPawn()))
		{
			Comp = PC->GetPawn()->FindComponentByClass<UINV_InventoryComponent>();
		}
		if (!IsValid(Comp))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inventory][Test] INV.TestMoveOverlap：找不到本地玩家的 InventoryComponent。"));
			return;
		}
		Comp->TestMoveOwnerOverlap();
	})
);

