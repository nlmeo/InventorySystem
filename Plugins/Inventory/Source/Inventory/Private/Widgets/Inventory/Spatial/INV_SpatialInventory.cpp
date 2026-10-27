#include "Widgets/Inventory/Spatial/INV_SpatialInventory.h"

#include "Inventory.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/DragDropOperation.h"
#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "InventoryManagement/Utils/INV_InventoryStatics.h"
#include "Widgets/ItemDescription/INV_ItemDescription.h"
#include "Components/CanvasPanelSlot.h"
#include "Items/INV_InventoryItem.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Widgets/Inventory/GridSlots/INV_EquippedGridSlot.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Widgets/Inventory/SlottedItems/INV_EquippedSlottedItem.h"
#include "Player/INV_PlayerController.h"

void UINV_SpatialInventory::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	
	if (!IsValid(ItemDescription)) return;
	
	SetItemDescriptionSizeAndPosition(ItemDescription, CanvasPanel);
}


void UINV_SpatialInventory::SetItemDescriptionSizeAndPosition(UINV_ItemDescription* Description, UCanvasPanel* Canvas) const
{
	UCanvasPanelSlot* ItemDescriptionCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(Description);
	if (!IsValid(ItemDescriptionCPS)) return;
	
	const FVector2D ItemDescriptionSize = Description->GetBoxSize();
	ItemDescriptionCPS->SetSize(ItemDescriptionSize);
	
	FVector2D ClampedPosition = UINV_WidgetUtils::GetClampedWidgetPosition(
		UINV_WidgetUtils::GetWidgetSize(Canvas), 
		ItemDescriptionSize, 
		UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer()));

	ItemDescriptionCPS->SetPosition(ClampedPosition);
}

UINV_EquippedGridSlot* UINV_SpatialInventory::FindSlotWithEquippedItem(UINV_InventoryItem* EquippedItem) const
{
	auto* FoundEquippedGridSlot = EquippedGridSlots.FindByPredicate([EquippedItem](const UINV_GridSlot* GridSlot)
	{
		return GridSlot->GetInventoryItem() == EquippedItem;
	});
	
	return FoundEquippedGridSlot ? *FoundEquippedGridSlot : nullptr;
}

UINV_InventoryGrid* UINV_SpatialInventory::FindGridContainingItem(const UINV_InventoryItem* Item) const
{
	if (!Item) return nullptr;

	UINV_InventoryGrid* Grids[] = {Grid_Equippables, Grid_Consumables, Grid_Craftables};
	for (UINV_InventoryGrid* Grid : Grids)
	{
		if (!IsValid(Grid)) continue;
		if (Grid->FindItemGridIndex(Item) != INDEX_NONE) return Grid;
	}

	return nullptr;
}

bool UINV_SpatialInventory::TryQuickEquipItem(UINV_InventoryItem* Item)
{
	if (!IsValid(Item)) return false;

	// 堆叠物品 / 非装备类不参与快速装备
	if (Item->IsStackable()) return false;
	if (Item->GetItemManifest().GetItemCategory() != EINV_ItemCategory::Equippable) return false;

	// 已经装备在某个槽里的物品，无需重复装备
	if (IsValid(FindSlotWithEquippedItem(Item))) return true;

	// 找目标槽：优先空槽；没有空槽时用第一个已装备的同类槽 → 换装
	UINV_EquippedGridSlot* EmptySlot = nullptr;
	UINV_EquippedGridSlot* OccupiedSlot = nullptr;

	for (UINV_EquippedGridSlot* EquippedSlot : EquippedGridSlots)
	{
		if (!IsValid(EquippedSlot)) continue;
		if (!CanEquipItemToSlot(Item, EquippedSlot->GetEquipmentTypeTag())) continue;

		if (IsValid(EquippedSlot->GetInventoryItem().Get()))
		{
			if (!IsValid(OccupiedSlot))
			{
				OccupiedSlot = EquippedSlot;
			}
		}
		else
		{
			EmptySlot = EquippedSlot;
			break;
		}
	}

	UINV_EquippedGridSlot* TargetSlot = IsValid(EmptySlot) ? EmptySlot : OccupiedSlot;
	if (!IsValid(TargetSlot)) return false;

	// 复用拖拽的统一入口：构造一个「背包来源」的临时 Payload，走同一条校验 / 换装 / 广播链路
	UINV_ItemPayload* Payload = NewObject<UINV_ItemPayload>(this);
	Payload->InitByItem(Item);
	Payload->SetDragSource(EINV_ItemDragSource::InventoryGrid);

	EquippedGridSlotClicked(TargetSlot, TargetSlot->GetEquipmentTypeTag(), Payload);
	return true;
}

bool UINV_SpatialInventory::FindQuickTransferDestination(UINV_InventoryItem* Item,
	FName& OutSlotId, int32& OutIndex, bool& bOutRotated) const
{
	OutSlotId = NAME_None;
	OutIndex = INDEX_NONE;
	bOutRotated = IsValid(Item) ? Item->IsRotated() : false;
	if (!IsValid(Item)) return false;

	UINV_InventoryGrid* Grids[] = { Grid_Equippables, Grid_Consumables, Grid_Craftables };
	for (UINV_InventoryGrid* Grid : Grids)
	{
		if (!IsValid(Grid) || !Grid->AcceptsCategory(Item->GetItemManifest().GetItemCategory())) continue;
		const int32 Index = Grid->FindFreeSlotForItem(Item);
		if (Index != INDEX_NONE)
		{
			OutSlotId = Grid->GetSlotId();
			OutIndex = Index;
			return true;
		}
	}
	return false;
}

void UINV_SpatialInventory::ClearSlotOfItem(UINV_EquippedGridSlot* EquippedGridSlot, bool bUpdateItemState)
{
	if (IsValid(EquippedGridSlot))
	{
		if (UINV_InventoryItem* Item = EquippedGridSlot->GetInventoryItem().Get())
		{
			if (bUpdateItemState)
			{
				Item->SetEquippedSlotType(FGameplayTag());
			}
		}
		EquippedGridSlot->SetEquippedSlottedItem(nullptr);
		EquippedGridSlot->SetInventoryItem(nullptr);
	}
}

void UINV_SpatialInventory::RemoveEquippedSlottedItem(UINV_EquippedSlottedItem* EquippedSlottedItem)
{
	if (!IsValid(EquippedSlottedItem)) return;
	
	EquippedSlottedItem->RemoveFromParent();
}

void UINV_SpatialInventory::MakeEquippedSlottedItem(UINV_EquippedSlottedItem* EquippedSlottedItem,
	UINV_EquippedGridSlot* EquippedGridSlot, UINV_InventoryItem* ItemToEquip)
{
	if (!IsValid(EquippedSlottedItem)) return;
	
	UINV_EquippedSlottedItem* SlottedItem = EquippedGridSlot->OnItemEquipped(
		ItemToEquip, 
		EquippedSlottedItem->GetEquipmentTypeTag(),
		UINV_InventoryStatics::GetInventoryWidget(GetOwningPlayer())->GetTileSize());
	
	if (IsValid(SlottedItem))
	{
		//SlottedItem->OnEquippedSlottedItemClicked.AddDynamic(this, &ThisClass::EquippedSlottedItemClicked);
		// OnEquippedSlottedItemDropped 的转发已由 UINV_EquippedGridSlot::OnItemEquipped 统一绑定，
		// 这里不能再绑一次，否则一次放下会被处理两遍。
		EquippedGridSlot->SetEquippedSlottedItem(SlottedItem);
		//SlottedItem->OnSlottedItemDragStart.AddDynamic(this,);
	}
}

void UINV_SpatialInventory::BroadcastSlotClickedDelegates(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToUnequip,
	FName ReturnSlotId, int32 ReturnIndex, bool bReturnRotated, FGameplayTag EquipmentSlotTypeOverride) const
{
	UINV_InventoryComponent* InventoryComponent = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	check(IsValid(InventoryComponent));
	
	// 卸下事务的 ItemToEquip 为空；不能用空指针去查槽位，否则会命中第一个空槽，
	// 把真实的 Cloaks 误传成 Masks 等其它装备槽。
	const UINV_EquippedGridSlot* TargetSlot = IsValid(ItemToEquip)
		? FindSlotWithEquippedItem(ItemToEquip)
		: nullptr;
	// 卸下路径会先清空本地槽位，因此 ItemToEquip 为空且无法再通过槽控件取 tag。
	// 被卸下物品仍携带服务器确认的装备槽标签，作为事务请求的回退值。
	const FGameplayTag EquipmentSlotType = EquipmentSlotTypeOverride.IsValid()
		? EquipmentSlotTypeOverride
		: IsValid(TargetSlot)
		? TargetSlot->GetEquipmentTypeTag()
		: (IsValid(ItemToUnequip) ? ItemToUnequip->GetEquippedSlotType() : FGameplayTag());
	const EINV_InteractionType OperationType = IsValid(ItemToEquip)
		? EINV_InteractionType::Equip : EINV_InteractionType::Unequip;
	FINV_InteractionContext Context(GetOwningPlayer(), OperationType);
	Context.Item = IsValid(ItemToEquip) ? ItemToEquip : ItemToUnequip;
	Context.SecondaryItem = ItemToUnequip;
	Context.EquipmentSlotTag = EquipmentSlotType;
	Context.ReturnSlotId = ReturnSlotId;
	Context.TargetIndex = ReturnIndex;
	Context.bReturnRotated = bReturnRotated;
	InventoryComponent->SubmitInventoryOperation(Context);
}

void UINV_SpatialInventory::BroadcastEquippedItemMoved(UINV_InventoryItem* ItemToEquip,
	UINV_InventoryItem* ItemToSwap) const
{
	UINV_InventoryComponent* InventoryComponent = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InventoryComponent)) return;

	// 装备槽之间移动/互换：两件物品都还在装备状态，不能走 EquipSlotClicked（它会 Unequip 掉物品）,
	// 用专用 RPC 让服务器按「先卸下、再装备」重建两个槽位的装备外观。
	const UINV_EquippedGridSlot* TargetSlot = FindSlotWithEquippedItem(ItemToEquip);
	if (!IsValid(TargetSlot)) return;
	FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::SwapEquipped);
	Context.Item = ItemToEquip;
	Context.SecondaryItem = ItemToSwap;
	Context.EquipmentSlotTag = TargetSlot->GetEquipmentTypeTag();
	Context.SourceEquipmentSlotTag = IsValid(ItemToSwap)
		? ItemToSwap->GetEquippedSlotType() : TargetSlot->GetEquipmentTypeTag();
	InventoryComponent->SubmitInventoryOperation(Context);
}


void UINV_SpatialInventory::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	Button_Equippables->OnClicked.AddDynamic(this, &UINV_SpatialInventory::ShowEquippables);
	Button_Consumables->OnClicked.AddDynamic(this, &UINV_SpatialInventory::ShowConsumables);
	Button_Craftables->OnClicked.AddDynamic(this, &UINV_SpatialInventory::ShowCraftables);

	Grid_Equippables->SetOwningCanvas(CanvasPanel);
	Grid_Consumables->SetOwningCanvas(CanvasPanel);
	Grid_Craftables->SetOwningCanvas(CanvasPanel);

	// 绑定「装备物品拖回网格 → 卸下」
	Grid_Equippables->OnEquippedItemDroppedOnGrid.AddDynamic(this, &ThisClass::EquippedItemDroppedOnGrid);
	Grid_Consumables->OnEquippedItemDroppedOnGrid.AddDynamic(this, &ThisClass::EquippedItemDroppedOnGrid);
	Grid_Craftables->OnEquippedItemDroppedOnGrid.AddDynamic(this, &ThisClass::EquippedItemDroppedOnGrid);
	
	ShowEquippables();

	WidgetTree->ForEachWidget([this](UWidget* widget)
	{
		if (UINV_EquippedGridSlot* EquippedGridSlot = Cast<UINV_EquippedGridSlot>(widget);IsValid(EquippedGridSlot))
		{
			EquippedGridSlots.Add(EquippedGridSlot);
			EquippedGridSlot->OnEquippedGridSlotDropped.AddDynamic(this, &ThisClass::EquippedGridSlotClicked);
			// 拖拽装备槽物品时立即切页签（时机在放下之前）
			EquippedGridSlot->OnEquippedSlotDragStarted.AddDynamic(this, &ThisClass::OnEquippedSlotDragStarted);
			// 双击装备槽 → 快速卸下
			EquippedGridSlot->OnEquippedSlotDoubleClicked.AddDynamic(this, &ThisClass::OnEquippedSlotDoubleClicked);
		}
	});
	if (UINV_InventoryComponent* Component = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer()))
	{
		Component->OnItemAdded.AddDynamic(this, &ThisClass::OnInventoryEntryAdded);
		Component->OnItemRemoved.AddDynamic(this, &ThisClass::OnInventoryEntryAdded);
		Component->OnInventoryRefreshRequested.AddUObject(this, &ThisClass::HandleInventoryRefreshRequested);
	}
}

FGameplayTag UINV_SpatialInventory::FindEmptyEquipmentSlot(const FINV_ItemManifest& Manifest) const
{
	if (Manifest.GetItemCategory() != EINV_ItemCategory::Equippable
		|| Manifest.GetFragmentOfType<FINV_StackableFragment>()
		|| !Manifest.GetFragmentOfType<FINV_EquipmentFragment>()) return FGameplayTag();
	for (const UINV_EquippedGridSlot* EquipmentSlot : EquippedGridSlots)
	{
		if (IsValid(EquipmentSlot) && !EquipmentSlot->GetInventoryItem().IsValid() && !IsValid(EquipmentSlot->GetEquippedSlottedItem())
			&& Manifest.GetItemType().MatchesTag(EquipmentSlot->GetEquipmentTypeTag())) return EquipmentSlot->GetEquipmentTypeTag();
	}
	return FGameplayTag();
}

void UINV_SpatialInventory::OnInventoryEntryAdded(UINV_InventoryItem* Item)
{
	if (UINV_InventoryComponent* Component = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer()))
	{
		Component->ScheduleRefresh();
	}
}

void UINV_SpatialInventory::HandleInventoryRefreshRequested()
{
	// 权威刷新既可能来自成功操作，也可能来自服务器拒绝。
	// 两种情况都必须先终止旧拖拽和高亮，避免旧 Payload 在重铺后再次提交。
	ResetDragStates();
	OnItemUnHovered();
	if (ActiveGrid.IsValid()) ActiveGrid->ShowCursor();
	RefreshEquipmentSlots();
}

void UINV_SpatialInventory::RefreshEquipmentSlots()
{
	UINV_InventoryComponent* Component = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(Component)) return;
	const TArray<UINV_InventoryItem*> Items = Component->GetAllInventoryItems();
	TSet<UINV_InventoryItem*> Assigned;
	for (UINV_EquippedGridSlot* EquipmentSlot : EquippedGridSlots)
	{
		if (!IsValid(EquipmentSlot)) continue;
		UINV_InventoryItem* Desired = nullptr;
		for (UINV_InventoryItem* Item : Items)
		{
			if (IsValid(Item) && Item->IsEquipped() && !Assigned.Contains(Item)
				&& Item->GetEquippedSlotType() == EquipmentSlot->GetEquipmentTypeTag()
				&& CanEquipItemToSlot(Item, EquipmentSlot->GetEquipmentTypeTag()))
			{
				Desired = Item;
				Assigned.Add(Item);
				break;
			}
		}
		if (EquipmentSlot->GetInventoryItem().Get() == Desired && (!Desired || IsValid(EquipmentSlot->GetEquippedSlottedItem()))) continue;
		UINV_InventoryItem* Previous = EquipmentSlot->GetInventoryItem().Get();
		const FGameplayTag PreviousType = IsValid(Previous) ? Previous->GetEquippedSlotType() : FGameplayTag();
		ClearEquippedSlot(EquipmentSlot);
		// Clearing a stale visual must not erase a newer replicated slot assignment.
		if (IsValid(Previous)) Previous->SetEquippedSlotType(PreviousType);
		if (Desired) PlaceItemInSlot(EquipmentSlot, Desired);
	}
}

void UINV_SpatialInventory::ResetDragStates()
{
	UINV_InventoryGrid* Grids[] = {Grid_Equippables, Grid_Consumables, Grid_Craftables};
	for (UINV_InventoryGrid* Grid : Grids)
	{
		if (!IsValid(Grid)) continue;
		Grid->ClearHoverItem();
		Grid->ClearExternalDragPayload();
	}
}

void UINV_SpatialInventory::ClearDragInteractionState()
{
	ResetDragStates();
}

void UINV_SpatialInventory::OnInventoryClosed()
{
	Super::OnInventoryClosed();

	// 跟随图元属于 Slate 的拖拽操作，隐藏背包本身不会移除它。
	// 仅取消物品 Payload 的拖拽，保留引擎正常的 DragLeave / DragCancelled 回调。
	UDragDropOperation* Operation = UWidgetBlueprintLibrary::GetDragDroppingContent();
	if (IsValid(Operation) && IsValid(Cast<UINV_ItemPayload>(Operation->Payload)))
	{
		UWidgetBlueprintLibrary::CancelDragDrop();
	}

	// 源网格可能不是当前页签；先让各网格安全归还悬持物，再清除缓存和高亮。
	UINV_InventoryGrid* Grids[] = {Grid_Equippables, Grid_Consumables, Grid_Craftables};
	for (UINV_InventoryGrid* Grid : Grids)
	{
		if (IsValid(Grid)) Grid->OnHide();
	}

	// 隐藏不会保证触发 MouseLeave，延迟显示的物品描述也要一并收起。
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DescriptionTimer);
	}
	if (IsValid(ItemDescription))
	{
		ItemDescription->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UINV_SpatialInventory::CanEquipItemToSlot(const UINV_InventoryItem* Item,
	const FGameplayTag& SlotEquipmentTypeTag) const
{
	if (!IsValid(Item)) return false;

	// 堆叠物品不能装备；只接受 Equippable 类别，且类型必须匹配该槽的 EquipmentTypeTag
	if (Item->IsStackable()) return false;

	const FINV_ItemManifest& Manifest = Item->GetItemManifest();
	if (Manifest.GetItemCategory() != EINV_ItemCategory::Equippable) return false;

	return Manifest.GetItemType().MatchesTag(SlotEquipmentTypeTag);
}

UINV_EquippedSlottedItem* UINV_SpatialInventory::PlaceItemInSlot(UINV_EquippedGridSlot* EquippedSlot,
	UINV_InventoryItem* Item, bool bUpdateItemState)
{
	if (!IsValid(EquippedSlot) || !IsValid(Item)) return nullptr;

	UINV_InventoryBase* InventoryWidget = UINV_InventoryStatics::GetInventoryWidget(GetOwningPlayer());
	if (!IsValid(InventoryWidget)) return nullptr;

	// 用槽自身的 tag 创建图标：OnItemEquipped 内部是 MatchesTagExact 校验
	UINV_EquippedSlottedItem* SlottedItem =
		EquippedSlot->OnItemEquipped(Item, EquippedSlot->GetEquipmentTypeTag(), InventoryWidget->GetTileSize());
	if (!IsValid(SlottedItem)) return nullptr;

	EquippedSlot->SetEquippedSlottedItem(SlottedItem);
	if (bUpdateItemState)
	{
		Item->SetEquippedSlotType(EquippedSlot->GetEquipmentTypeTag());
	}
	EquippedSlot->SetAvailable(false);
	EquippedSlot->SetOccupiedTexture();
	return SlottedItem;
}

void UINV_SpatialInventory::ClearEquippedSlot(UINV_EquippedGridSlot* EquippedSlot, bool bUpdateItemState)
{
	if (!IsValid(EquippedSlot)) return;

	// 先取出图标再清空槽数据，否则指针会先被置空导致图标残留在 Overlay 上
	UINV_EquippedSlottedItem* SlottedItem = EquippedSlot->GetEquippedSlottedItem();
	ClearSlotOfItem(EquippedSlot, bUpdateItemState);
	RemoveEquippedSlottedItem(SlottedItem);

	EquippedSlot->SetAvailable(true);
	EquippedSlot->SetUnoccupiedTexture();
}

void UINV_SpatialInventory::EquippedGridSlotClicked(UINV_EquippedGridSlot* EquippedGridSlot,
	const FGameplayTag& EquipmentTypeTag, UINV_ItemPayload* Payload)
{
	// 任何失败分支都必须清理拖拽缓存，否则残留 Payload 会在开关背包时被重新放进网格
	if (!IsValid(Payload) || !IsValid(Payload->GetInventoryItem()) || !IsValid(EquippedGridSlot))
	{
		ResetDragStates();
		return;
	}

	// 拖拽来源：背包物品（InventoryGrid）或已装备物品（EquippedSlot）
	const EINV_ItemDragSource Source = Payload->GetDragSource();
	UINV_InventoryItem* ItemToEquip = Payload->GetInventoryItem();
	if (Payload->SourceInventory.IsValid())
	{
		if (UINV_InventoryComponent* PlayerInventory = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
			Payload->SourceInventory.Get() != PlayerInventory)
		{
			if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer()))
			{
				PC->SubmitContainerEquip(Payload->SourceInventory.Get(), ItemToEquip, EquipmentTypeTag);
			}
			if (Payload->SourceGrid.IsValid()) Payload->SourceGrid->ClearHoverItem();
			ResetDragStates();
			return;
		}
	}

	// 目标槽当前已装备的物品（可能为空）
	UINV_InventoryItem* ItemToUnequip = EquippedGridSlot->GetInventoryItem().Get();

	// 情况0：拖回自己所在的原槽 → 不做任何处理（不改数据、不广播）
	if (ItemToEquip == ItemToUnequip)
	{
		ResetDragStates();
		return;
	}

	// 槽位校验：物品必须是非堆叠的可装备物品，且类型匹配目标槽
	if (!CanEquipItemToSlot(ItemToEquip, EquipmentTypeTag))
	{
		ResetDragStates();
		return;
	}

	UINV_EquippedGridSlot* SourceGridSlot = nullptr;
	UINV_InventoryGrid* SourceGrid = nullptr; // 背包来源物品所在的网格
	int32 SourceGridIndex = INDEX_NONE; // 背包来源物品在网格中的原索引
	int32 ReturnGridIndex = INDEX_NONE; // 换下物品在来源网格中的回填索引
	bool bReturnRotated = false;

	if (Source == EINV_ItemDragSource::EquippedSlot)
	{
		// 情况1/2：装备槽 → 另一个装备槽
		SourceGridSlot = FindSlotWithEquippedItem(ItemToEquip);
		if (!IsValid(SourceGridSlot) || SourceGridSlot == EquippedGridSlot)
		{
			ResetDragStates();
			return;
		}

		// 目标槽已有装备 → 互换：被换下的那件必须能装进源槽（源槽的槽位校验）
		if (IsValid(ItemToUnequip) && !CanEquipItemToSlot(ItemToUnequip, SourceGridSlot->GetEquipmentTypeTag()))
		{
			ResetDragStates();
			return;
		}
	}
	else if (Source == EINV_ItemDragSource::InventoryGrid)
	{
		// 物品可能来自装备品 / 消耗品 / 材料三个网格之一，按物品实际所在网格定位。
		// 兜底到 Grid_Equippables 是刻意的兼容设计：装备类物品的归属网格固定是装备网格，
		// 即使用户当时停留在消耗品/材料页签（装备网格被折叠、取不到索引），也按装备网格处理。
		SourceGrid = FindGridContainingItem(ItemToEquip);
		if (!IsValid(SourceGrid))
		{
			SourceGrid = Grid_Equippables;
		}

		// 拖拽 Operation 里的 Payload 是 NativeOnDragDetected 新建的对象，PreviousGridIndex 不可靠；
		// 以物品在网格中的真实位置为准，取不到再回退到网格 ItemPayload 记录的值。
		SourceGridIndex = SourceGrid->FindItemGridIndex(ItemToEquip);
		if (SourceGridIndex == INDEX_NONE && SourceGrid->HasHoverItem() && IsValid(SourceGrid->GetHoverItem()))
		{
			SourceGridIndex = SourceGrid->GetHoverItem()->GetPreviousGridIndex();
		}
		if (SourceGridIndex == INDEX_NONE)
		{
			ResetDragStates();
			return;
		}


		// 在任何本地状态改变前，为换下物品确定一个真实可用的回填位置。
		// 优先使用待装备物品原来的位置；大件换小件时若原位放不下，
		// 再扫描当前网格（并忽略 ItemToEquip 即将释放的占用区域）。
		ReturnGridIndex = SourceGridIndex;
		if (IsValid(ItemToUnequip))
		{
			const FIntPoint BaseDimensions = ItemToUnequip->GetBaseDimension();
			const FIntPoint RotatedDimensions(BaseDimensions.Y, BaseDimensions.X);
			const bool bFitsBase = SourceGrid->CanPlaceItemAtIndexIgnoring(
				SourceGridIndex, BaseDimensions, ItemToEquip);
			const bool bFitsRotated = ItemToUnequip->CanRotate() &&
				SourceGrid->CanPlaceItemAtIndexIgnoring(SourceGridIndex, RotatedDimensions, ItemToEquip);

			if (bFitsBase)
			{
				bReturnRotated = false;
			}
			else if (bFitsRotated)
			{
				bReturnRotated = true;
			}
			else
			{
				ReturnGridIndex = SourceGrid->FindFreeSlotForItemIgnoring(
					ItemToUnequip, ItemToEquip, bReturnRotated);
			}

			if (ReturnGridIndex == INDEX_NONE)
			{
				// 当前网格没有能容纳换下装备的位置：保持原装备和原网格状态。
				ResetDragStates();
				return;
			}
		}
	}
	else
	{
		ResetDragStates();
		return;
	}

	// ===== 校验全部通过，以下开始真正改动，中途不再 return =====
	// 权威模式先提交完整装备事务；本地表现更新放到 RPC 之后，避免共享对象先变成半成品状态。
	if (Source == EINV_ItemDragSource::InventoryGrid)
	{
		const FName TransactionReturnSlot = IsValid(ItemToUnequip) ? SourceGrid->GetSlotId() : NAME_None;
		const int32 TransactionReturnIndex = IsValid(ItemToUnequip) ? ReturnGridIndex : INDEX_NONE;
		BroadcastSlotClickedDelegates(ItemToEquip, ItemToUnequip, TransactionReturnSlot,
			TransactionReturnIndex, bReturnRotated, EquipmentTypeTag);
	}

	// 先按背包中的旧朝向清格，再归一化装备朝向，否则旋转物品会按原始尺寸清旧格而留下残格。
	if (Source == EINV_ItemDragSource::InventoryGrid)
	{
		SourceGrid->RemoveItemFromGrid(ItemToEquip, SourceGridIndex, false);
	}
	if (IsValid(ItemToUnequip))
	{
		ItemToUnequip->SetRotated(bReturnRotated);
	}

	// 1. 腾空两个槽位（互换时目标槽的物品要挪到源槽，必须先空出位置）
	ClearEquippedSlot(EquippedGridSlot, Source == EINV_ItemDragSource::InventoryGrid ? false : true);
	if (IsValid(SourceGridSlot))
	{
		ClearEquippedSlot(SourceGridSlot);
	}

	// 2. 被拖拽的物品装到目标槽
	PlaceItemInSlot(EquippedGridSlot, ItemToEquip, Source == EINV_ItemDragSource::InventoryGrid ? false : true);

	if (Source == EINV_ItemDragSource::InventoryGrid)
	{
		// 3a. 换装时把被卸下的装备按事务选定的朝向回填到原格。
		if (IsValid(ItemToUnequip))
		{
			SourceGrid->AssignHoverItem(ItemToUnequip);
			SourceGrid->PutDownOnIndex(ReturnGridIndex, false, false);
		}
		if (UINV_InventoryComponent* Comp = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer()))
		{
			Comp->RequestAuthoritativeRefresh();
		}
	}
	else
	{
		// 3b. 装备槽来源：目标槽原有的装备装回源槽（互换）；
		//     目标槽为空时 ItemToUnequip 无效，等同于「从源槽卸下后装到目标槽」
		if (IsValid(ItemToUnequip))
		{
			PlaceItemInSlot(SourceGridSlot, ItemToUnequip);
		}

		// 4b. 两件物品都仍处于装备状态，只是换了槽位，需要按「先卸下再装备」重建外观
		BroadcastEquippedItemMoved(ItemToEquip, ItemToUnequip);
	}

	// 5. 统一清理拖拽缓存
	ResetDragStates();
}

bool UINV_SpatialInventory::UnloadItemToEquippablesGrid(UINV_EquippedGridSlot* EquippedGridSlot,
	UINV_InventoryItem* ItemToUnequip)
{
	if (!IsValid(EquippedGridSlot) || !IsValid(ItemToUnequip)) return false;

	// 找「接受装备分类且有空间」的网格，第一个满就 fallback 到下一个。
	// 原来只查 Grid_Equippables，第一个装备网格满就直接取消卸下，其余同类型网格没机会。
	UINV_InventoryGrid* TargetGrid = nullptr;
	UINV_InventoryGrid* Grids[] = {Grid_Equippables, Grid_Consumables, Grid_Craftables};
	for (UINV_InventoryGrid* Grid : Grids)
	{
		if (!IsValid(Grid)) continue;
		if (!Grid->AcceptsCategory(EINV_ItemCategory::Equippable)) continue;
		if (Grid->HasRoomForItemInstance(ItemToUnequip))
		{
			TargetGrid = Grid;
			break;
		}
	}

	// 所有接受装备的网格都放不下：整体取消，物品继续保持装备状态
	if (!IsValid(TargetGrid)) return false;

	// 先只计算回填位置，不修改物品字段。装备事务必须在 ItemToUnequip
	// 仍带有装备槽状态时提交，否则本机权威对象会被本地 AutoPlace 提前改成网格物品。
	bool bReturnRotated = false;
	const int32 PlacedIndex = TargetGrid->FindFreeSlotForItemIgnoring(
		ItemToUnequip, nullptr, bReturnRotated);
	if (PlacedIndex == INDEX_NONE)
	{
		return false;
	}

	// 单笔事务完成卸下和回填；不要再追加 Server_MoveItem。
	BroadcastSlotClickedDelegates(nullptr, ItemToUnequip, TargetGrid->GetSlotId(), INDEX_NONE, bReturnRotated);

	// 事务已经提交后再更新本地表现。PutDownOnIndex 只更新网格，不重复发移动 RPC。
	UINV_EquippedSlottedItem* EquippedSlottedItem = EquippedGridSlot->GetEquippedSlottedItem();
	ClearSlotOfItem(EquippedGridSlot);
	RemoveEquippedSlottedItem(EquippedSlottedItem);
	EquippedGridSlot->SetAvailable(true);
	EquippedGridSlot->SetUnoccupiedTexture();
	ItemToUnequip->SetRotated(bReturnRotated);
	TargetGrid->AssignHoverItem(ItemToUnequip);
	TargetGrid->PutDownOnIndex(PlacedIndex, false, false);
	TargetGrid->SuppressQuickEquipForItem(ItemToUnequip);

	if (UINV_InventoryComponent* Comp = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer()))
	{
		Comp->RequestAuthoritativeRefresh();
	}
	return true;
}

void UINV_SpatialInventory::EquippedItemDroppedOnGrid(UINV_InventoryGrid* SourceGrid, UINV_ItemPayload* Payload,
	int32 GridIndex)
{
	// 所有失败分支都要清理拖拽缓存，否则关闭背包时 PutHoverItemBack 会把物品再放一遍，造成重复
	if (!IsValid(Payload) || !IsValid(Payload->GetInventoryItem()))
	{
		ResetDragStates();
		return;
	}

	UINV_InventoryItem* ItemToUnequip = Payload->GetInventoryItem();
	UINV_EquippedGridSlot* EquippedGridSlot = FindSlotWithEquippedItem(ItemToUnequip);
	if (!IsValid(EquippedGridSlot))
	{
		ResetDragStates();
		return;
	}

	// 统一逻辑：拖到的网格若接受装备分类，就精确落到该网格；否则（消耗品/材料等页签）忽略索引，
	// 走快速卸下（UnloadItemToEquippablesGrid 找第一个可放置的装备网格）。
	// 这样多同类型网格时，拖到哪个页签就精确落到哪个页签；只有「双击快速卸下」才走自动查找。
	const bool bTargetAcceptsEquippable = IsValid(SourceGrid) && SourceGrid->AcceptsCategory(EINV_ItemCategory::Equippable);
	if (!bTargetAcceptsEquippable)
	{
		UnloadItemToEquippablesGrid(EquippedGridSlot, ItemToUnequip);

		// 清缓存：SetActiveGrid 之类的后续操作可能触发 OnHide → PutHoverItemBack，
		// 残留的 hover payload 会被再放一次，表现为「多出一个物品」。
		ResetDragStates();
		return;
	}

	const FIntPoint Dimensions = Payload->GetGridDimensions();

	// 当前空间查询结果：SpaceItemsMap 记录了放置区域内已有的物品（物品 → 其左上角索引）
	const FINV_SpaceQueryResult& QueryResult = SourceGrid->GetCurrentQueryResult();

	// 1) 先尝试客户端 Payload 的落点和旋转。
	const bool bPayloadFits = QueryResult.SpaceItemsMap.IsEmpty()
		&& SourceGrid->CanPlaceItemAtIndex(GridIndex, Dimensions);
	int32 PlacementIndex = INDEX_NONE;
	bool bReturnRotated = false;
	bool bUseClientPlacement = false;
	if (bPayloadFits)
	{
		PlacementIndex = GridIndex;
		bReturnRotated = ItemToUnequip->CanRotate() && Payload->IsRotated();
		bUseClientPlacement = true;
	}
	else
	{
		// 2) Payload 落点不可用时，按快速卸下规则在当前网格寻找完整空位。
		PlacementIndex = SourceGrid->FindFreeSlotForItemIgnoring(ItemToUnequip, nullptr, bReturnRotated);
	}

	// 3) 两种方式都没有位置：装备继续留在装备槽。
	if (PlacementIndex == INDEX_NONE)
	{
		ResetDragStates();
		return;
	}

	// 客户端直接落点交给服务器复核；回退落点让服务器用自己的占用表重新选址。
	BroadcastSlotClickedDelegates(nullptr, ItemToUnequip, SourceGrid->GetSlotId(),
		bUseClientPlacement ? PlacementIndex : INDEX_NONE, bReturnRotated);

	// 事务提交后更新本地表现，PutDownOnIndex 不再追加移动 RPC。
	UINV_EquippedSlottedItem* EquippedSlottedItem = EquippedGridSlot->GetEquippedSlottedItem();
	ClearSlotOfItem(EquippedGridSlot);
	RemoveEquippedSlottedItem(EquippedSlottedItem);
	EquippedGridSlot->SetAvailable(true);
	EquippedGridSlot->SetUnoccupiedTexture();

	ItemToUnequip->SetRotated(bReturnRotated);
	SourceGrid->AssignHoverItem(ItemToUnequip);
	SourceGrid->PutDownOnIndex(PlacementIndex, false, false);
	SourceGrid->SuppressQuickEquipForItem(ItemToUnequip);

	if (UINV_InventoryComponent* Comp = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer()))
	{
		Comp->RequestAuthoritativeRefresh();
	}

	ResetDragStates();
}

void UINV_SpatialInventory::EquippedSlottedItemDropped(UINV_EquippedSlottedItem* TargetSlottedItem, UINV_ItemPayload* Payload)
{
	if (!IsValid(TargetSlottedItem) || !IsValid(Payload) || !IsValid(Payload->GetInventoryItem()))
	{
		ResetDragStates();
		return;
	}

	// 找到目标装备物品所在的装备槽
	UINV_InventoryItem* TargetItem = TargetSlottedItem->GetInventoryItem();
	UINV_EquippedGridSlot* TargetGridSlot = FindSlotWithEquippedItem(TargetItem);
	if (!IsValid(TargetGridSlot))
	{
		ResetDragStates();
		return;
	}

	// 转发到统一的装备槽放下逻辑（换装）
	const FGameplayTag& EquipmentTypeTag = TargetGridSlot->GetEquipmentTypeTag();
	EquippedGridSlotClicked(TargetGridSlot, EquipmentTypeTag, Payload);
}

bool UINV_SpatialInventory::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	const FVector2D Position = InDragDropEvent.GetScreenSpacePosition();
	auto IsOver = [&Position](const UWidget* Widget)
	{
		if (!IsValid(Widget) || !Widget->IsVisible()) return false;
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D Size = Geometry.GetLocalSize();
		return Size.X > 0.f && Size.Y > 0.f && Geometry.IsUnderLocation(Position);
	};
	// Successful grid/slot handlers consume the drop before it bubbles here.
	// An unhandled drop inside the page is cancellation, never a world drop.
	return HandleBackgroundDrop(InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr,
		IsOver(Switcher) || IsOver(ActiveGrid.Get()));
}

bool UINV_SpatialInventory::HandleBackgroundDrop(UINV_ItemPayload* Payload, bool bOverGridPage)
{
	if (!IsValid(Payload) || !IsValid(Payload->GetInventoryItem())) return false;
	if (bOverGridPage)
	{
		ResetDragStates();
		return true;
	}
	if (Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot)
	{
		UINV_InventoryItem* Item = Payload->GetInventoryItem();
		const UINV_EquippedGridSlot* SourceSlot = FindSlotWithEquippedItem(Item);
		UINV_InventoryComponent* Component = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
		if (IsValid(Component) && IsValid(SourceSlot))
		{
			// No local slot mutation: both rejection and spawn failure leave the equipment visible.
			FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::DropEquipped);
			Context.Item = Item;
			Context.EquipmentSlotTag = SourceSlot->GetEquipmentTypeTag();
			Component->SubmitInventoryOperation(Context);
		}
		ResetDragStates();
		return true;
	}
	if (Payload->GetDragSource() != EINV_ItemDragSource::InventoryGrid) return false;
	if (UINV_InventoryGrid* Grid = FindGridContainingItem(Payload->GetInventoryItem()); IsValid(Grid))
	{
		if (Grid->HasHoverItem() && Grid->GetHoverItem()->GetInventoryItem() == Payload->GetInventoryItem())
		{
			Grid->DropItem();
		}
	}
	return true;
}

FINV_SlotAvailabilityResult UINV_SpatialInventory::HasRoomForItem(UINV_ItemComponent* ItemComponent) const 
{
	const EINV_ItemCategory Category = UINV_InventoryStatics::GetInventoryCategoryFromItemComponent(ItemComponent);

	// 遍历所有网格，找「接受该分类且有空间」的第一个。
	// 两个同类型网格时，第一个满了会 fallback 到下一个（塔科夫化「多同类型容器」的前置）。
	// 原来这里按分类硬编码只查一个网格，第一个满就直接报无空间，其余同类型网格根本没机会。
	UINV_InventoryGrid* Grids[] = {Grid_Equippables, Grid_Consumables, Grid_Craftables};
	for (UINV_InventoryGrid* Grid : Grids)
	{
		if (!IsValid(Grid)) continue;
		if (!Grid->AcceptsCategory(Category)) continue;

		FINV_SlotAvailabilityResult Result = Grid->HasRoomForItem(ItemComponent);
		if (Result.TotalRoomToFill > 0)
		{
			return Result;
		}
	}

	// 所有接受该分类的网格都满了，返回空结果（TotalRoomToFill == 0 表示无空间）。
	return FINV_SlotAvailabilityResult();
}

void UINV_SpatialInventory::OnItemHovered(UINV_InventoryItem* Item)
{
	const auto& Manifest = Item->GetItemManifest();
	UINV_ItemDescription* DescriptionWidget = GetItemDescription();
	
	DescriptionWidget->Collapse();
	DescriptionWidget->SetVisibility(ESlateVisibility::Collapsed);
	
	
	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(DescriptionTimer);
	
	FTimerDelegate DescriptionDelegate;
	DescriptionDelegate.BindLambda([this, &Manifest, DescriptionWidget]()
	{
		// Assimilate the manifest into the Item Description widget.
		Manifest.AssimilateInventoryFragments(DescriptionWidget);
		GetItemDescription()->SetVisibility(ESlateVisibility::HitTestInvisible);
	});
	
	GetOwningPlayer()->GetWorldTimerManager().SetTimer(DescriptionTimer, DescriptionDelegate, DescriptionTimerDelay, false);
}

void UINV_SpatialInventory::OnItemUnHovered()
{
	GetItemDescription()->SetVisibility(ESlateVisibility::Collapsed);
	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(DescriptionTimer);
}

bool UINV_SpatialInventory::HasHoverItem() const
{
	bool a = Grid_Equippables->HasHoverItem();
	bool b = Grid_Craftables->HasHoverItem();
	bool c = Grid_Consumables->HasHoverItem();
	return  a || b || c;
}

UINV_ItemPayload* UINV_SpatialInventory::GetHoverItem() const
{
	if (!ActiveGrid.IsValid()) return nullptr;
	return ActiveGrid->GetHoverItem();
}

float UINV_SpatialInventory::GetTileSize() const
{
	// TileSize 现在来自布局资产；网格若因配置缺失而没构造出来，这里会拿到 0，
	// 直接解引用会崩，所以判空并返回 0 让调用方（图标尺寸计算）自然退化。
	if (!IsValid(Grid_Equippables)) return 0.f;
	return Grid_Equippables->GetTileSize();
}

UINV_ItemDescription* UINV_SpatialInventory::GetItemDescription()
{
	if (!IsValid(ItemDescription))
	{
		ItemDescription = CreateWidget<UINV_ItemDescription>(GetOwningPlayer(), ItemDescriptionClass);
		CanvasPanel->AddChild(ItemDescription);
	}
	return ItemDescription;
}

void UINV_SpatialInventory::ShowEquippables()
{
	SetActiveGrid(Grid_Equippables, Button_Equippables);
}

void UINV_SpatialInventory::OnEquippedSlotDragStarted(UINV_EquippedGridSlot* EquippedGridSlot)
{
	if (!IsValid(EquippedGridSlot)) return;

	// 拖拽时保持当前页签，不强制切到装备页签。
	// 统一逻辑后（EquippedItemDroppedOnGrid 按 SourceGrid 是否接受装备分类分流），
	// 玩家拖到哪个接受装备的页签就精确落到哪个页签，无需在拖拽开始就切页签。
	// 仍清一次拖拽缓存，避免上一次操作的残留 payload 影响本次拖拽。
	ResetDragStates();
}

void UINV_SpatialInventory::OnEquippedSlotDoubleClicked(UINV_EquippedGridSlot* EquippedGridSlot)
{
	if (!IsValid(EquippedGridSlot)) return;

	UINV_InventoryItem* ItemToUnequip = EquippedGridSlot->GetInventoryItem().Get();
	if (!IsValid(ItemToUnequip)) return; // 空槽双击无意义

	// 复用卸下链路：校验装备网格是否放得下 → 清槽 → 自动落到第一个可用位置 → 广播卸下 RPC。
	// 放不下时整体取消，物品保持装备状态（不做半截改动）。
	UnloadItemToEquippablesGrid(EquippedGridSlot, ItemToUnequip);

	// 双击会移除装备槽图标（EquippedSlottedItem 被销毁），鼠标仍停在原位置但
	// NativeOnMouseLeave 不会触发（widget 被移除而非鼠标移出），悬停的 ItemDescription 会残留。
	// 手动取消一次悬浮，收起描述弹窗。双击前必然 hover 过图标，ItemDescription 一定已创建，安全。
	OnItemUnHovered();

	// 卸下的物品落在装备网格上；当前不在该页签就切过去，
	// 让玩家能直接看到结果（网格满、卸下失败时也能看到装备仍在槽里）。
	if (ActiveGrid.Get() != Grid_Equippables)
	{
		ResetDragStates();
		ShowEquippables();
	}
}

void UINV_SpatialInventory::ShowConsumables()
{
	SetActiveGrid(Grid_Consumables, Button_Consumables);

}

void UINV_SpatialInventory::ShowCraftables()
{
	SetActiveGrid(Grid_Craftables, Button_Craftables);
}

void UINV_SpatialInventory::DisableButton(UButton* Button)
{
	Button_Equippables->SetIsEnabled(true);
	Button_Consumables->SetIsEnabled(true);
	Button_Craftables->SetIsEnabled(true);
	Button->SetIsEnabled(false);
}

void UINV_SpatialInventory::SetActiveGrid(UINV_InventoryGrid* Grid, UButton* Button)
{
	//隐藏当前的鼠标
	if (ActiveGrid.IsValid())
	{
		ActiveGrid->HideCursor();
		ActiveGrid->OnHide();
	} 
	
	ActiveGrid = Grid;
	if (ActiveGrid.IsValid()) Grid->ShowCursor();
	DisableButton(Button);
	Switcher->SetActiveWidget(Grid);
}


