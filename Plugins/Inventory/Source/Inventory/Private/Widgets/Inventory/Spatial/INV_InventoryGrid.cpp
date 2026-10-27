#include "Widgets/Inventory/Spatial/INV_InventoryGrid.h"

#include "Inventory.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "InventoryManagement/Layout/INV_GridLayoutTypes.h"
#include "InventoryManagement/Utils/INV_InventoryStatics.h"
#include "Items/INV_InventoryItem.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Items/Fragments/INV_FragmentTags.h"
#include "Widgets/Utils/INV_WidgetUtils.h"
#include "Widgets/Inventory/GridSlots/INV_GridSlot.h"
#include "Widgets/Inventory/SlottedItems/INV_SlottedItem.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Widgets/ItemPopUp/INV_ItemPopUp.h"
#include "Widgets/Inventory/Spatial/INV_SpatialInventory.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/ScopeExit.h"
#include "Blueprint/WidgetTree.h"
#include "Player/INV_PlayerController.h"
#include "InventoryManagement/Transfer/INV_CrossContainerSwapPlanner.h"

int64 UINV_InventoryGrid::SubmitInventoryOperation(FINV_InteractionContext Context)
{
	UINV_InventoryComponent* Component = InventoryComponent.Get();
	if (!IsValid(Component)) return 0;
	if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer());
		IsValid(PC) && PC->GetActiveContainerInventory() == Component)
	{
		PC->SubmitContainerOperation(Component, Context);
		return 0;
	}
	return Component->SubmitInventoryOperation(Context);
}
void UINV_InventoryGrid::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Native instances are configured by the asset-driven container screen after CreateWidget.
	if (GetClass() == StaticClass() || SlotId.IsNone()) return;

	// 顺序很重要：先取组件（布局资产入口），再解析尺寸，最后才构造格子。
	InventoryComponent = UINV_InventoryStatics::GetInventoryComponent(GetOwningPlayer());

	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] %s 取不到 InventoryComponent，无法构造网格。"), *GetNameSafe(this));
		return;
	}

	if (!ResolveGridLayoutFromAsset())
	{
		// 尺寸只有资产一个来源，解析不到就不构造 —— 否则会用 0 尺寸铺出一堆错位格子，
		// 表现是「背包打开后一片空白或错乱」，比直接报错更难查。
		UE_LOG(LogTemp, Error,
			TEXT("[Inventory] %s 无法通过 SlotId='%s' 解析唯一有效的布局资产面板，停止构造网格。"),
			*GetNameSafe(this), *SlotId.ToString());
		return;
	}

	if (!IsValid(CanvasPanel) || !IsValid(SizeBox_GridFrame) || !GridSlotsClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] %s 缺少 CanvasPanel、SizeBox_GridFrame 或 GridSlotsClass，停止构造网格。"),
			*GetNameSafe(this));
		return;
	}

	// 背景和画布共用资产尺寸，避免子格子溢出父布局而仍然可见、鼠标却已被判为离开。
	SizeBox_GridFrame->SetWidthOverride(Columns * TileSize);
	SizeBox_GridFrame->SetHeightOverride(Rows * TileSize);
	ConstructGrid();
	bGridLayoutResolved = true;

	InventoryComponent->OnItemAdded.AddDynamic(this, &ThisClass::AddItem);
	InventoryComponent->OnItemRemoved.AddDynamic(this, &ThisClass::OnInventoryItemRemoved);
	InventoryComponent->OnStackChange.AddDynamic(this, &ThisClass::AddStacks);
	// 服务器权威模式：每次操作后按权威数据重铺（临时开关，见 bServerAuthoritativeUI）
	InventoryComponent->OnInventoryRefreshRequested.AddUObject(this, &ThisClass::RebuildFromInventoryList);
	InventoryComponent->OnInventoryMenuToggled.AddDynamic(this, &ThisClass::OnItemInventoryMenuToggled);
}

void UINV_InventoryGrid::UnbindInventory()
{
	OnHide();
	if (InventoryComponent.IsValid())
	{
		InventoryComponent->OnItemAdded.RemoveDynamic(this, &ThisClass::AddItem);
		InventoryComponent->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnInventoryItemRemoved);
		InventoryComponent->OnStackChange.RemoveDynamic(this, &ThisClass::AddStacks);
		InventoryComponent->OnInventoryRefreshRequested.RemoveAll(this);
		InventoryComponent->OnInventoryMenuToggled.RemoveDynamic(this, &ThisClass::OnItemInventoryMenuToggled);
	}
	InventoryComponent.Reset();
}

void UINV_InventoryGrid::NativeDestruct()
{
	UnbindInventory();
	Super::NativeDestruct();
}

bool UINV_InventoryGrid::InitializeForInventory(UINV_InventoryComponent* Component, FName PanelId)
{
	UnbindInventory();
	InventoryComponent = Component;
	SlotId = PanelId;
	if (!ResolveGridLayoutFromAsset()) return false;
	if (!CanvasPanel)
	{
		SizeBox_GridFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CanvasPanel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		SizeBox_GridFrame->SetContent(CanvasPanel);
		WidgetTree->RootWidget = SizeBox_GridFrame;
	}
	GridSlotsClass = LoadClass<UINV_GridSlot>(nullptr, TEXT("/Inventory/Widgets/Inventory/GridSlots/WBP_INV_GridSlot.WBP_INV_GridSlot_C"));
	SlottedItemClass = LoadClass<UINV_SlottedItem>(nullptr, TEXT("/Inventory/Widgets/Inventory/SlottedItems/WBP_INV_SlottedItem.WBP_INV_SlottedItem_C"));
	if (!GridSlotsClass || !SlottedItemClass) return false;
	CanvasPanel->ClearChildren();
	GridSlots.Reset();
	SlottedItems.Reset();
	SizeBox_GridFrame->SetWidthOverride(Columns * TileSize);
	SizeBox_GridFrame->SetHeightOverride(Rows * TileSize);
	ConstructGrid();
	bGridLayoutResolved = true;
	Component->OnItemAdded.AddDynamic(this, &ThisClass::AddItem);
	Component->OnItemRemoved.AddDynamic(this, &ThisClass::OnInventoryItemRemoved);
	Component->OnStackChange.AddDynamic(this, &ThisClass::AddStacks);
	Component->OnInventoryRefreshRequested.AddUObject(this, &ThisClass::RebuildFromInventoryList);
	RebuildFromInventoryList();
	return true;
}

bool UINV_InventoryGrid::HandleContainerDrop(UINV_ItemPayload* Payload, int32 GridIndex)
{
	AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer());
	if (!Payload || !PC || !PC->GetActiveContainerInventory()) return false;
	UINV_InventoryItem* Item = Payload->GetInventoryItem();
	UINV_InventoryComponent* Source = Payload->SourceInventory.Get();
	// Equipment-slot drags intentionally have no SourceInventory. When the
	// target is an active container, resolve the equipped item back to the
	// player's inventory so the server can perform an unequip-and-transfer.
	if (Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot && !IsValid(Source))
	{
		if (UINV_InventoryComponent* PlayerInventory = UINV_InventoryStatics::GetInventoryComponent(PC);
			IsValid(PlayerInventory) && PlayerInventory != InventoryComponent.Get()
			&& PlayerInventory->GetAllInventoryItems().Contains(Item))
		{
			Source = PlayerInventory;
		}
		else
		{
			return false;
		}
	}
	if (!IsValid(Source) && IsValid(Item))
	{
		if (UINV_InventoryComponent* PlayerInventory = UINV_InventoryStatics::GetInventoryComponent(PC);
			PlayerInventory == InventoryComponent.Get()
			&& PlayerInventory->GetAllInventoryItems().Contains(Item))
		{
			return false;
		}
	}
	// The Session owns the live player/source UI pair. A drag operation can outlive
	// a widget reparent or carry a stale weak pointer, so resolve the source from
	// current authoritative membership whenever possible.
	if (IsValid(Item))
	{
		const bool bSourceOwnsItem = IsValid(Source) && Source->GetAllInventoryItems().Contains(Item);
		// A same-component drag is a normal move/swap. Only reinterpret an equal
		// source when another live inventory can prove ownership of the item.
		if (Source == InventoryComponent.Get())
		{
			if (UINV_InventoryComponent* ActiveSource = PC->GetActiveContainerInventory();
				IsValid(ActiveSource) && ActiveSource != InventoryComponent.Get()
				&& ActiveSource->GetAllInventoryItems().Contains(Item))
			{
				Source = ActiveSource;
			}
			else if (UINV_InventoryComponent* PlayerInventory = UINV_InventoryStatics::GetInventoryComponent(PC);
				IsValid(PlayerInventory) && PlayerInventory != InventoryComponent.Get()
				&& PlayerInventory->GetAllInventoryItems().Contains(Item))
			{
				Source = PlayerInventory;
			}
			else
			{
				return false;
			}
		}
		else if (!bSourceOwnsItem)
		{
			Source = nullptr;
		}
		if (UINV_InventoryComponent* ActiveSource = PC->GetActiveContainerInventory();
			IsValid(ActiveSource) && ActiveSource != InventoryComponent.Get()
			&& ActiveSource->GetAllInventoryItems().Contains(Item))
		{
			Source = ActiveSource;
		}
		else if (UINV_InventoryComponent* PlayerInventory = UINV_InventoryStatics::GetInventoryComponent(PC);
			IsValid(PlayerInventory) && PlayerInventory != InventoryComponent.Get()
			&& PlayerInventory->GetAllInventoryItems().Contains(Item))
		{
			Source = PlayerInventory;
		}
	}
	// Only consume the drop when it is a real cross-InventoryComponent transfer.
	// Same-component moves, swaps, equipment drops and other normal grid paths
	// must continue through OnGridSlotDropped / OnSlottedItemDropped below.
	if (!IsValid(Source) || Source == InventoryComponent.Get())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory][Transfer] 拖放未识别有效来源：Item=%s，Source=%s(%p,Owner=%s)，Target=%s(%p,Owner=%s)。"),
			*GetNameSafe(Item), *GetNameSafe(Source), Source, *GetNameSafe(Source ? Source->GetOwner() : nullptr),
			*GetNameSafe(InventoryComponent.Get()), InventoryComponent.Get(),
			*GetNameSafe(InventoryComponent.IsValid() ? InventoryComponent->GetOwner() : nullptr));
		return false;
	}

	// Use the same final space query as the visual preview. GridIndex is the
	// raw tile under the cursor and can be an invalid left edge for a multi-cell
	// item (for example column 9 in a 10-column grid); ItemDropIndex contains the
	// clamped/auto-rotated landing chosen by the preview.
	if (IsValid(Payload)) RefreshDropQuery(Payload);
	const int32 ResolvedDropIndex = GetItemDropIndex() != INDEX_NONE ? GetItemDropIndex() : GridIndex;
	if (!GetCurrentQueryResult().bHasSpace)
	{
		if (Payload->SourceGrid.IsValid()) Payload->SourceGrid->ClearHoverItem();
		ClearExternalDragPayload();
		ClearHoverItem();
		return true;
	}

	if (IsValid(Item) && InventoryComponent.IsValid()
		&& AcceptsCategory(Item->GetItemManifest().GetItemCategory()))
	{
		UINV_InventoryItem* TargetItem = nullptr;
		for (const auto& Entry : GetCurrentQueryResult().SpaceItemsMap)
		{
			if (UINV_InventoryItem* Candidate = Entry.Key.Get(); IsValid(Candidate) && Candidate != Item)
			{
				TargetItem = Candidate;
				break;
			}
		}
		if (TargetItem == Item) TargetItem = nullptr;
		const int32 TargetIndex = ResolvedDropIndex;
		const int32 Amount = PC->IsInputKeyDown(EKeys::LeftShift) ? 1
			: (Item->IsStackable() ? Item->GetTotalStackCount() : 1);
		TArray<FINV_CrossContainerSwapEntry> SwapEntries;
		if (IsValid(TargetItem) && Payload->SourceInventory.IsValid())
		{
			UINV_InventoryComponent* SourceInventory = Payload->SourceInventory.Get();
			TArray<UINV_InventoryItem*> CoveredItems;
			for (const auto& Entry : GetCurrentQueryResult().SpaceItemsMap)
			{
				if (UINV_InventoryItem* Covered = Entry.Key.Get(); IsValid(Covered)) CoveredItems.AddUnique(Covered);
			}
			FINV_CrossContainerSwapPlanner::BuildPlan(SourceInventory, Item, CoveredItems, SwapEntries);
		}
		UE_LOG(LogTemp, Log, TEXT("[Inventory][Transfer] 提交拖放：Item=%s，Source=%s(%p)，Target=%s(%p)，Slot=%s，Index=%d，Rotated=%d。"),
			*GetNameSafe(Item), *GetNameSafe(Source), Source,
			*GetNameSafe(InventoryComponent.Get()), InventoryComponent.Get(), *SlotId.ToString(),
			TargetIndex, Payload->IsRotated() ? 1 : 0);
		PC->SubmitContainerTransfer(Source, InventoryComponent.Get(), Item, SlotId, TargetIndex,
			Amount, Payload->IsRotated(), TargetItem, SwapEntries);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory][Transfer] 目标网格拒绝类别：Item=%s，Target=%s，Slot=%s。"),
			*GetNameSafe(Item), *GetNameSafe(InventoryComponent.Get()), *SlotId.ToString());
	}

	// A cross-source drop is consumed even when the target rejects it, so the
	// source and target external drag state is cleared without falling into
	// same-grid placement logic.
	if (Payload->SourceGrid.IsValid()) Payload->SourceGrid->ClearHoverItem();
	ClearExternalDragPayload();
	ClearHoverItem();
	return true;
}

bool UINV_InventoryGrid::ResolveGridLayoutFromAsset()
{
	Rows = 0;
	Columns = 0;
	TileSize = 0.f;
	ResolvedCategory = EINV_ItemCategory::None;
	bGridLayoutResolved = false;
	if (!InventoryComponent.IsValid()) return false;

	if (SlotId.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] %s 未配置 SlotId；网格必须显式引用布局资产面板 ID。"), *GetNameSafe(this));
		return false;
	}

	// 不接受重复 ID 的「第一个胜出」行为，避免资产选择依赖数组顺序。
	TArray<const FINV_GridSlotDefinition*> Panels;
	InventoryComponent->GetAllGridPanels(Panels);
	int32 MatchingPanels = 0;
	for (const FINV_GridSlotDefinition* Candidate : Panels)
	{
		if (Candidate && Candidate->SlotId == SlotId) ++MatchingPanels;
	}
	if (MatchingPanels != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] %s 的 SlotId='%s' 匹配到 %d 个资产面板，要求恰好一个。"),
			*GetNameSafe(this), *SlotId.ToString(), MatchingPanels);
		return false;
	}

	const UINV_GridLayoutAsset* LayoutAsset = nullptr;
	const FINV_GridSlotDefinition* Panel = InventoryComponent->GetGridPanel(SlotId, &LayoutAsset);
	if (!Panel || !IsValid(LayoutAsset)) return false;
	const float LayoutTileSize = LayoutAsset->GetTileSize();
	if (Panel->ContainerType != EINV_GridContainerType::Grid
		|| Panel->SlotDimension.X <= 0 || Panel->SlotDimension.Y <= 0
		|| static_cast<int64>(Panel->SlotDimension.X) * Panel->SlotDimension.Y > MAX_int32
		|| !FMath::IsFinite(LayoutTileSize) || LayoutTileSize <= 0.f
		|| !FMath::IsFinite(Panel->SlotDimension.X * LayoutTileSize)
		|| !FMath::IsFinite(Panel->SlotDimension.Y * LayoutTileSize))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Inventory] %s 的资产 %s / SlotId='%s' 配置无效：必须为 Grid、正行列和格宽。"),
			*GetNameSafe(this), *GetNameSafe(LayoutAsset), *SlotId.ToString());
		return false;
	}

	ResolvedCategory = Panel->AcceptedCategory;
	Columns = Panel->SlotDimension.X;
	Rows = Panel->SlotDimension.Y;
	TileSize = LayoutTileSize;
	return true;
}

void UINV_InventoryGrid::NativeTick(const FGeometry& Geometry, float InDeltaTime)
{
	Super::NativeTick(Geometry, InDeltaTime);
	if (!bGridLayoutResolved) return;

	// 旋转输入放在最前面：鼠标移出画布时下面会提前 return，
	// 但「按 R」这件事不该因为鼠标暂时不在网格上就失灵（拖到页签边缘很常见）。
	TryRotateHoverItem();

	const FVector2D CanvasPos = UINV_WidgetUtils::GetWidgetPosition(CanvasPanel);
	const FVector2D MouseViewportPos = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	
	if (CursorExitedCanvas(CanvasPos, UINV_WidgetUtils::GetWidgetSize(CanvasPanel), MouseViewportPos))
	{
		return;	
	}
	UpdateTileParameters(CanvasPos, MouseViewportPos);
}

void UINV_InventoryGrid::TryRotateHoverItem()
{
	APlayerController* PC = GetOwningPlayer();
	// 用 PC 的按键状态而不是输入事件：拖拽期间键盘焦点在 Slate 的拖放流程里，
	// 网格本身收不到 KeyDown，但「这个键此刻按没按」是全局可查的。
	const bool bDown = IsValid(PC) && PC->IsInputKeyDown(EKeys::R);

	// 取上升沿：按住不放只翻转一次（不能每帧翻，否则会疯狂闪烁）
	const bool bJustPressed = bDown && !bRotateKeyHeld;
	bRotateKeyHeld = bDown;

	if (!bJustPressed) return;

	UINV_ItemPayload* Payload = GetActivePayload();
	if (!IsValid(Payload)) return;

	// 正方形物品旋转前后一样，ToggleRotated 会返回 false（不抖动、不发多余请求）
	if (!Payload->ToggleRotated()) return;

	UE_LOG(LogTemp, Log, TEXT("[Inventory] 拖拽中旋转：%s → %dx%d"),
		*GetNameSafe(Payload->GetInventoryItem()),
		Payload->GetGridDimensions().X, Payload->GetGridDimensions().Y);

	// 跟随图元跟着转：只转格子不转图，玩家会以为没生效
	UpdateDragVisualRotation();

	// 立刻用新尺寸重算落点与高亮：尺寸变了但鼠标没动，不主动重算的话
	// 高亮会停在旧尺寸上，直到玩家再挪一下鼠标 —— 表现为「按了 R 没反应」。
	OnTileParametersUpdate(TileParameters);
}

void UINV_InventoryGrid::UpdateDragVisualRotation()
{
	const UINV_ItemPayload* Payload = GetActivePayload();
	if (!IsValid(Payload)) return;
	if (DragOperationPayload.IsValid() && DragOperationPayload.Get() != Payload
		&& DragOperationPayload->GetInventoryItem() == Payload->GetInventoryItem())
	{
		DragOperationPayload->SetRotated(Payload->IsRotated());
	}
	if (UWidget* DragVisual = Payload->GetDragVisual())
	{
		DragVisual->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		DragVisual->SetRenderTransformAngle(Payload->IsRotated() ? 90.f : 0.f);
	}
}

void UINV_InventoryGrid::ApplyPayloadRotationToItem()
{
	UINV_ItemPayload* Payload = GetActivePayload();
	if (!IsValid(Payload)) return;

	UINV_InventoryItem* Item = Payload->GetInventoryItem();
	if (!IsValid(Item)) return;

	// 只写本地预测值；服务器以 RPC 参数为准覆盖（成功路径两者一致）
	if (Item->CanRotate())
	{
		Item->SetRotated(Payload->IsRotated());
	}
}

FINV_SlotAvailabilityResult UINV_InventoryGrid::HasRoomForItem(const UINV_ItemComponent* ItemComponent)
{
	if (!IsValid(ItemComponent)) return FINV_SlotAvailabilityResult();
	const FINV_ItemManifest& Manifest = ItemComponent->GetItemManifest();
	FINV_SlotAvailabilityResult Result = HasRoomForItem(Manifest);
	const FIntPoint Size = GetItemDimensions(Manifest);
	if (Result.TotalRoomToFill == 0 && Size.X != Size.Y)
	{
		Result = HasRoomForItem(Manifest, -1, FIntPoint(Size.Y, Size.X));
		Result.bRotated = true;
	}
	return Result;
}

FINV_SlotAvailabilityResult UINV_InventoryGrid::HasRoomForItem(const UINV_InventoryItem* Item, const int32 StackAmountOverride)
{
	if (!IsValid(Item)) return FINV_SlotAvailabilityResult();

	// 填充量必须取「这个条目自己有多少件」（TotalStackCount），而不是模板 manifest 里记录的初始数量。
	// 反例：模板 StackCount=30、MaxStackSize=10，服务器按上限拆成 3 个条目（各 10 件），
	// 若这里仍按 manifest 的 30 计算，每个条目都会被铺成 3 格 × 10，物品总量凭空变成 90。
	const int32 AmountToFill = StackAmountOverride != -1
		? StackAmountOverride
		: (Item->IsStackable() ? FMath::Max(1, Item->GetTotalStackCount()) : -1);

	// 尺寸必须按物品的实际朝向算，否则「旋转过的 3x2」会被当成 3x2 去找空位，
	// 找到的位置按 2x3 铺下去就溢出了。
	FINV_SlotAvailabilityResult Result = HasRoomForItem(Item->GetItemManifest(), AmountToFill, Item->GetItemDimension());
	Result.bRotated = Item->IsRotated();
	if (Result.TotalRoomToFill == 0 && Item->CanRotate())
	{
		const FIntPoint Size = Item->GetItemDimension();
		Result = HasRoomForItem(Item->GetItemManifest(), AmountToFill, FIntPoint(Size.Y, Size.X));
		Result.bRotated = !Item->IsRotated();
	}
	return Result;
}

FINV_SlotAvailabilityResult UINV_InventoryGrid::HasRoomForItem(const FINV_ItemManifest& Manifest, const int32 StackAmountOverride,
	const FIntPoint& DimensionsOverride)
{
	FINV_SlotAvailabilityResult Result;
	if (!AcceptsCategory(Manifest.GetItemCategory())) return Result;
	// 即便调用方提供旋转后的实例尺寸，也要求物品资产具有有效的占地配置。
	const FIntPoint AssetDimensions = GetItemDimensions(Manifest);
	if (AssetDimensions.X <= 0 || AssetDimensions.Y <= 0) return Result;

	const FIntPoint Dims = (DimensionsOverride.X > 0 && DimensionsOverride.Y > 0)
		? DimensionsOverride
		: AssetDimensions;
	
	// Determine if the item is stackable.
	const FINV_StackableFragment* StackableFragment = Manifest.GetFragmentOfType<FINV_StackableFragment>();
	Result.bStackable = StackableFragment != nullptr;
	// Determine how many stacks to add.
	const int32 MaxStackSize = Result.bStackable ? StackableFragment->GetMaxStackSize() : 1;
	int32 AmountToFill = Result.bStackable ? StackableFragment->GetStackCount() : 1;
	if (StackAmountOverride != -1 && Result.bStackable)
	{
		AmountToFill = StackAmountOverride;
	}
	
	
	TSet<int32> CheckedIndeces;
	// For each Grid Slot:
	for (const auto& GridSlot : GridSlots)
	{
		// If we don't have anymore to fill, break out early.
		if (AmountToFill == 0) break;
		// Is this index claimed yet?
		if (IsIndexClaimed(CheckedIndeces, GridSlot->GetTileIndex())) continue;
		
		// Is the item in grid bounds?
		if (!IsInGridBounds(GridSlot->GetTileIndex(), Dims)) continue;
		
		// Can the item fit here? (i.e. is it out of grid bounds.)
		TSet<int32> TentativelyClaimed;
		if (!HasRoomAtIndex(GridSlot, Dims, CheckedIndeces, TentativelyClaimed, Manifest.GetItemType(), MaxStackSize))
		{
			continue;
		}
		// How much to fill?
		const int32 AmountToFillInSlot = DetermineFillAmountForSlot(Result.bStackable, MaxStackSize, AmountToFill, GridSlot);
		if (AmountToFillInSlot == 0) continue;
		CheckedIndeces.Append(TentativelyClaimed);
		// Update the amount left to fill
		Result.TotalRoomToFill += AmountToFillInSlot;
		Result.SlotAvailabilities.Emplace(
			FINV_SlotAvailability{
				HasValidItem(GridSlot) ? GridSlot->GetUpperLeftIndex() : GridSlot->GetTileIndex(),
				Result.bStackable ? AmountToFillInSlot : 0, 
				HasValidItem(GridSlot)
			}	
		);
		AmountToFill -= AmountToFillInSlot;
		// How much is the Remainder?
		Result.Remainder = AmountToFill;
		if (Result.Remainder == 0) return Result;
	}

	return Result;
}

bool UINV_InventoryGrid::IsIndexClaimed(const TSet<int32>& ClaimedIndices, const int32 Index) const
{
	return ClaimedIndices.Contains(Index);
}


bool UINV_InventoryGrid::HasRoomAtIndex(const UINV_GridSlot* GridSlot, 
                                        const FIntPoint& Dimensions,
                                        const TSet<int32>& CheckedIndices,
                                        TSet<int32>& OutTentativelyClaimed,
                                        const FGameplayTag& ItemType,
                                        int32 MaxStackSize)
{
	bool bHasRoomAtIndex = true;
	// Check any other important conditions. - Foreach2D over a 2D range
	UINV_InventoryStatics::Foreach2D(GridSlots, GridSlot->GetTileIndex(), Dimensions, Columns, [&](const UINV_GridSlot* SubGridSlot)
	{
		// Is there room at this index?(i.e. Are there other items in the way?)
		if (CheckSlotConstraints(GridSlot, SubGridSlot, CheckedIndices, OutTentativelyClaimed, ItemType, MaxStackSize))
		{
			OutTentativelyClaimed.Add(SubGridSlot->GetTileIndex());
		}
		else
		{
			bHasRoomAtIndex = false;
		}
	});
	
	return bHasRoomAtIndex;
}

bool UINV_InventoryGrid::CheckSlotConstraints(const UINV_GridSlot* GridSlot, 
	const UINV_GridSlot* SubGridSlot, 
	const TSet<int32>& CheckedIndices, 
	TSet<int32>& OutTentativelyClaimed,
	const FGameplayTag& ItemType,
	int32 MaxStackSize) const
{
	
	// Index claimed?
	if (IsIndexClaimed(CheckedIndices,SubGridSlot->GetTileIndex())) return false;
	// Has valid item?
	if (!HasValidItem(SubGridSlot))
	{
		OutTentativelyClaimed.Add(SubGridSlot->GetTileIndex());
		return true;
	}
	// Is this Grid Slot an upper left slot?
	if (!IsUpperLeftSlot(GridSlot, SubGridSlot)) return false;
	// Is this a stackable item?
	UINV_InventoryItem* SubItem = SubGridSlot->GetInventoryItem().Get();
	if (!SubItem->IsStackable()) return false;
	// If stackable, is this slot at the max stack size already?
	if (GridSlot->GetStackCount() >= MaxStackSize) return false;
	// Is this item the same type as the item we're trying to add?
	if (!DoesItemTypeMatch(SubItem, ItemType)) return false;
	
	return true;
}

bool UINV_InventoryGrid::HasValidItem(const UINV_GridSlot* GridSlot) const
{
	return GridSlot->GetInventoryItem().IsValid();
}

bool UINV_InventoryGrid::IsUpperLeftSlot(const UINV_GridSlot* GridSlot, const UINV_GridSlot* SubGridSlot) const
{
	return SubGridSlot->GetUpperLeftIndex() == GridSlot->GetTileIndex();
}

bool UINV_InventoryGrid::DoesItemTypeMatch(const UINV_InventoryItem* SubItem, const FGameplayTag& ItemType) const
{
	return SubItem->GetItemManifest().GetItemType().MatchesTagExact(ItemType);
}

bool UINV_InventoryGrid::IsInGridBounds(const int32 StartIndex, const FIntPoint& ItemDimensions) const
{
	if (StartIndex < 0 || StartIndex >= GridSlots.Num()) return false;
	
	const int32 EndColumn = (StartIndex % Columns) + ItemDimensions.X;
	const int32 EndRow = (StartIndex / Columns) + ItemDimensions.Y;
	return EndColumn <= Columns && EndRow <= Rows;
	
}

bool UINV_InventoryGrid::IsLandingInBounds(const FIntPoint& AfterPos, const FIntPoint& ItemDimensions) const
{
	return AfterPos.X >= 0 && AfterPos.Y >= 0 &&
		AfterPos.X + ItemDimensions.X <= Columns &&
		AfterPos.Y + ItemDimensions.Y <= Rows;
}

bool UINV_InventoryGrid::DoRectsOverlap(const FIntPoint& APos, const FIntPoint& ADim,
	const FIntPoint& BPos, const FIntPoint& BDim) const
{
	return APos.X < BPos.X + BDim.X && BPos.X < APos.X + ADim.X &&
		APos.Y < BPos.Y + BDim.Y && BPos.Y < APos.Y + ADim.Y;
}

int32 UINV_InventoryGrid::DetermineFillAmountForSlot(const bool bStackable, const int32 MaxStackSize,
	const int32 AmountToFill, const UINV_GridSlot* GridSlot) const
{
	// Calculate room in the slot
	const int32 RoomInSlot = MaxStackSize - GetStackAmount(GridSlot);
	// If stackable, need the minimum between AmountToFill and RoomInSlot.
	return bStackable ? FMath::Min(AmountToFill, RoomInSlot) : 1;
}

int32 UINV_InventoryGrid::GetStackAmount(const UINV_GridSlot* GridSlot) const
{
	int32 CurrentSlotStackCount = GridSlot->GetStackCount();
	// If we are at a slot that doesn't hold the stack count. We must get the actual stack count.
	if (const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex(); UpperLeftIndex != INDEX_NONE)
	{
		UINV_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex];
		CurrentSlotStackCount = UpperLeftGridSlot->GetStackCount();
	}
	return CurrentSlotStackCount;
}

bool UINV_InventoryGrid::IsRightClicked(const FPointerEvent& MouseEvent) const
{
	return MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
}

bool UINV_InventoryGrid::ISLeftClicked(const FPointerEvent& MouseEvent) const
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
}


void UINV_InventoryGrid::PickUp(UINV_InventoryItem* ClickedInventoryItem, const int32 GridIndex)
{
	AssignHoverItem(ClickedInventoryItem, GridIndex, GridIndex);
}

void UINV_InventoryGrid::PutDownOnIndex(const int32 Index, bool bReportToServer, bool bUpdateItemState)
{
	if (!IsValid(ItemPayload)) return;

	UINV_InventoryItem* Item = ItemPayload->GetInventoryItem();
	if (!IsValid(Item)) return;

	const int32 SourceIndex = ItemPayload->GetPreviousGridIndex();
	const bool bStackable = ItemPayload->IsStackable();
	const int32 StackCount = ItemPayload->GetStackCount();

	AddItemAtIndex(Item, Index, bStackable, StackCount);
	UpdateGridSlots(Item, Index, bStackable, StackCount, bUpdateItemState);

	// 服务器权威：物品在容器内换格。
	// SourceIndex 可能是 INDEX_NONE（例如从装备槽卸下、或拆分品首次落位），
	// 这种情况下服务器把它当成「从容器外进入」，只记录目标位置。
	if (bReportToServer)
	{
		if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
		{
			if (Comp->bServerAuthoritativeUI)
			{
				FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Move);
				Context.Item = Item;
				Context.SourceSlotId = SlotId;
				Context.SourceIndex = SourceIndex;
				Context.TargetSlotId = SlotId;
				Context.TargetIndex = Index;
				Context.bRotated = Item->IsRotated();
				SubmitInventoryOperation(Context);
			}
			else
			{
				Comp->Server_MoveItem(Item, SlotId, SourceIndex, SlotId, Index, Item->IsRotated());
			}
			// 服务器权威模式：等服务器处理完（含拒绝）再按权威数据重铺；乐观模式下这里直接返回
			if (!Comp->bServerAuthoritativeUI) Comp->RequestAuthoritativeRefresh();
		}
	}

	ClearHoverItem();
	//ShowCursor();
}

bool UINV_InventoryGrid::CanPlaceAtIndex(const int32 Index) const
{
	if (!GridSlots.IsValidIndex(Index)) return false;
	if (!IsValid(ItemPayload) || !IsValid(ItemPayload->GetInventoryItem())) return false;
	return !GridSlots[Index]->GetInventoryItem().IsValid();
}

bool UINV_InventoryGrid::CanPlaceItemAtIndex(const int32 Index, const FIntPoint& Dimensions) const
{
	if (!GridSlots.IsValidIndex(Index)) return false;
	// 边界校验：以 Index 为左上角、按 Dimensions 展开后不能越界
	if (!IsInGridBounds(Index, Dimensions)) return false;

	// 多格占用校验：范围内所有格子都必须空闲
	bool bAllFree = true;
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [&](const UINV_GridSlot* GridSlot)
	{
		if (GridSlot->GetInventoryItem().IsValid())
		{
			bAllFree = false;
		}
	});
	return bAllFree;
}

bool UINV_InventoryGrid::CanPlaceItemAtIndexIgnoring(const int32 Index, const FIntPoint& Dimensions,
	const UINV_InventoryItem* ItemToIgnore) const
{
	if (!GridSlots.IsValidIndex(Index)) return false;
	if (!IsInGridBounds(Index, Dimensions)) return false;

	// 多格占用校验：范围内除 ItemToIgnore 自身以外的格子都必须空闲
	bool bAllFree = true;
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [&](const UINV_GridSlot* GridSlot)
	{
		if (GridSlot->GetInventoryItem().Get() == ItemToIgnore) return;
		if (GridSlot->GetInventoryItem().IsValid())
		{
			bAllFree = false;
		}
	});
	return bAllFree;
}

int32 UINV_InventoryGrid::FindItemGridIndex(const UINV_InventoryItem* Item) const
{
	if (!IsValid(Item)) return INDEX_NONE;

	for (const UINV_GridSlot* GridSlot : GridSlots)
	{
		if (!IsValid(GridSlot)) continue;
		if (GridSlot->GetInventoryItem().Get() != Item) continue;

		// 只在左上角格子上返回，避免返回物品内部的某个子格
		const int32 UpperLeft = GridSlot->GetUpperLeftIndex();
		return UpperLeft != INDEX_NONE ? UpperLeft : GridSlot->GetTileIndex();
	}
	return INDEX_NONE;
}

void UINV_InventoryGrid::AssignHoverItem(UINV_InventoryItem* InventoryItem, const int32 GridIndex,
                                         const int32 PreviousGridIndex)
{
	AssignHoverItem(InventoryItem);
	
	ItemPayload->SetPreviousGridIndex(PreviousGridIndex);
	ItemPayload->SetStackCount(InventoryItem->IsStackable() ? GridSlots[GridIndex]->GetStackCount() : 0);
}

void UINV_InventoryGrid::RemoveItemFromGrid(UINV_InventoryItem* InventoryItem, const int32 GridIndex,
	bool bUpdateItemState)
{
	if (!IsValid(InventoryItem)) return;

	const FINV_GridFragment* GridFragment = GetFragment<FINV_GridFragment>(InventoryItem, FragmentTags::GridFragment);
	if (!GridFragment) return;
	
	// 尺寸走物品统一的尺寸出口（含朝向）：旋转过的物品占的是 X/Y 互换后的格子，
	// 直接读 GridFragment 会按原始尺寸去清格，留下「半个身位的残留占用」。
	UINV_InventoryStatics::Foreach2D(GridSlots, GridIndex, InventoryItem->GetItemDimension(), Columns,[&](UINV_GridSlot* GridSlot)
	{
		GridSlot->SetInventoryItem(nullptr);
		GridSlot->SetUpperLeftIndex(INDEX_NONE);
		GridSlot->SetUnoccupiedTexture();
		GridSlot->SetAvailable(true);
		GridSlot->SetStackCount(0);
	});
	
	RemoveSlottedItemEntriesForItem(InventoryItem);

	// 位置真相同步清空：物品已经不在网格里了（被拿起 / 被并走 / 被装备 / 交换的中间态），
	// 否则后续读到的仍是一个「东西早已不在那儿」的旧坐标。
	// 交换类操作是「先 Remove 再 Add」，Add 一侧的 UpdateGridSlots 会把新位置写回去。
	// 容器归属一并清空：GridIndex 是「容器内索引」，离开网格后脱离容器维度。
	if (bUpdateItemState)
	{
		InventoryItem->SetContainerId(NAME_None);
		InventoryItem->SetGridIndex(INDEX_NONE);
	}
}

void UINV_InventoryGrid::RebuildFromInventoryList()
{
	UINV_InventoryComponent* Comp = InventoryComponent.Get();
	if (!IsValid(Comp)) return;

	// 拖拽中的缓存先清掉：图标马上要被整体销毁，残留的 Payload 指向已移除的图标，
	// 下次 PutHoverItemBack 会往网格里塞幽灵物品。
	ClearHoverItem();
	ClearExternalDragPayload();

	// 1) 清空现有表现（图标 + 占用 + 堆叠数）
	for (const TPair<int32, TObjectPtr<UINV_SlottedItem>>& Pair : SlottedItems)
	{
		if (IsValid(Pair.Value)) Pair.Value->RemoveFromParent();
	}
	SlottedItems.Reset();

	for (UINV_GridSlot* GridSlot : GridSlots)
	{
		if (!IsValid(GridSlot)) continue;
		GridSlot->SetInventoryItem(nullptr);
		GridSlot->SetUpperLeftIndex(INDEX_NONE);
		GridSlot->SetUnoccupiedTexture();
		GridSlot->SetAvailable(true);
		GridSlot->SetStackCount(0);
	}

	// 2) 按权威 GridIndex 重铺
	int32 TotalItems = 0;
	int32 DrawnItems = 0;
	int32 SkippedPlacement = 0;
	FString ItemList;

	for (UINV_InventoryItem* Item : Comp->GetAllInventoryItems())
	{
		++TotalItems;
		// 用容器归属过滤，而不是纯分类：重铺时物品已有权威 ContainerId，
		// 两个同分类网格若按分类判断会把同一件物品各铺一份。
		if (!BelongsToThisGrid(Item)) continue;

		const int32 Index = Item->GetGridIndex();
		// 明细带上数量：数量类问题（拆分/合并后显示不变）直接看这里就能判断
		// 是「客户端读到的值就不对」还是「值对了但没画上去」。
		ItemList += FString::Printf(TEXT(" %s@%d x%d(%dx%d)"), *GetNameSafe(Item), Index,
			Item->GetTotalStackCount(), Item->GetItemDimension().X, Item->GetItemDimension().Y);

		// 装备中 / 在别的容器 / 尚未落位：不属于本网格，跳过
		if (Index == INDEX_NONE) continue;

		if (!CanPlaceItemAtIndex(Index, Item->GetItemDimension()))
		{
			// 权威位置在本地放不下（越界或与另一件冲突）：不画。
			// 画出来只会让玩家基于一个并不存在的状态继续操作，比少一个图标更难查。
			++SkippedPlacement;
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] %s：条目「%s」的权威位置 %d 无法落地（越界或冲突），本次不绘制。"),
				*GetNameSafe(this), *GetNameSafe(Item), Index);
			continue;
		}

		const bool bStackable = Item->IsStackable();
		const int32 Amount = bStackable ? Item->GetTotalStackCount() : 0;
		AddItemAtIndex(Item, Index, bStackable, Amount);
		UpdateGridSlots(Item, Index, bStackable, Amount);
		++DrawnItems;
	}

	// 两端条目数对不上时这条最有价值：客户端若收不到某些条目，这里会明显偏少
	UE_LOG(LogTemp, Log, TEXT("[Inventory] %s 重铺完成：网格=%s，条目=%d，绘制=%d，落不下=%d；明细：%s"),
		*GetNameSafe(this), *SlotId.ToString(), TotalItems, DrawnItems, SkippedPlacement,
		ItemList.IsEmpty() ? TEXT("（空）") : *ItemList);
}

void UINV_InventoryGrid::UpdateTileParameters(const FVector2D& CanvasPosition, const FVector2D& MousePosition)
{
	// If mouse not in canvas panel, return.
	if (!bMouseWithinCanvas)
	{
		InvalidateDropQuery();
		return;
	}
	// Calculate the tile quadrant
	const FIntPoint HoveredTileCoordinates = CalculateHoveredCoordinates(CanvasPosition, MousePosition);
	LastTileParameters = TileParameters;
	TileParameters.TileCoordinates = HoveredTileCoordinates;
	TileParameters.TileIndex = UINV_WidgetUtils::GetIndexFromPosition(HoveredTileCoordinates, Columns);
	TileParameters.TileQuadrant = CalculateTileQuadrant(CanvasPosition, MousePosition);
	// Handle the highlight/Unhighlight of the GridSlots.
	OnTileParametersUpdate(TileParameters);
}

void UINV_InventoryGrid::OnTileParametersUpdate(const FINV_TileParameters Parameters)
{
	RefreshPlacementPreview(Parameters);
	if ((!HasHoverItem() && !HasExternalDragPayload()) || !GridSlots.IsValidIndex(ItemDropIndex))
	{
		UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
		LastHighlightedIndex = INDEX_NONE;
		LastHighlightedDimensions = FIntPoint::ZeroValue;
		return;
	}
	const FIntPoint Dimensions = GetActivePayload()->GetGridDimensions();
	if (CurrentQueryResult.bHasSpace)
	{
		SelectSlots(ItemDropIndex, Dimensions);
		return;
	}
	
	GrayedOutSlots(ItemDropIndex, Dimensions);
}

bool UINV_InventoryGrid::RefreshPlacementPreview(const FINV_TileParameters& Parameters)
{
	InvalidateDropQuery();
	if (!bMouseWithinCanvas || (!HasHoverItem() && !HasExternalDragPayload()) || Columns <= 0 || Rows <= 0
		|| Parameters.TileQuadrant >= EINV_TileQuadrant::None
		|| Parameters.TileCoordinates.X < 0 || Parameters.TileCoordinates.X >= Columns
		|| Parameters.TileCoordinates.Y < 0 || Parameters.TileCoordinates.Y >= Rows) return false;
	UINV_ItemPayload* Payload = GetActivePayload();
	const bool bOriginalRotation = Payload->IsRotated();
	const bool bCanRotate = Payload->GetInventoryItem()->CanRotate();
	FINV_SpaceQueryResult FailedQuery;
	int32 FailedIndex = INDEX_NONE;
	auto TryCandidate = [&](bool bRotated, bool bClampToEdge)
	{
		Payload->SetRotated(bRotated);
		const FIntPoint Dimensions = Payload->GetGridDimensions();
		if (Dimensions.X <= 0 || Dimensions.Y <= 0 || Dimensions.X > Columns || Dimensions.Y > Rows) return false;
		FIntPoint Start = CalculateStartCoordinate(Parameters.TileCoordinates, Dimensions, Parameters.TileQuadrant);
		if (bClampToEdge)
		{
			Start.X = FMath::Clamp(Start.X, 0, Columns - Dimensions.X);
			Start.Y = FMath::Clamp(Start.Y, 0, Rows - Dimensions.Y);
		}
		if (!IsLandingInBounds(Start, Dimensions)) return false;
		FINV_SpaceQueryResult Query = CheckHoverPosition(Start, Dimensions);
		const int32 Index = UINV_WidgetUtils::GetIndexFromPosition(Start, Columns);
		if (!Query.bHasSpace)
		{
			if (bRotated == bOriginalRotation)
			{
				FailedIndex = Index;
				FailedQuery = MoveTemp(Query);
			}
			return false;
		}
		ItemDropIndex = Index;
		CurrentQueryResult = MoveTemp(Query);
		return true;
	};

	// 先保留鼠标中心对应的落点；两种朝向均失败时，再将候选贴回边界。
	// 每组候选都优先当前朝向，不搜索其他空位；已选朝向下一帧仍优先，避免抖动。
	const bool bFound = TryCandidate(bOriginalRotation, false)
		|| (bCanRotate && TryCandidate(!bOriginalRotation, false))
		|| TryCandidate(bOriginalRotation, true)
		|| (bCanRotate && TryCandidate(!bOriginalRotation, true));
	if (!bFound)
	{
		Payload->SetRotated(bOriginalRotation);
		ItemDropIndex = FailedIndex;
		CurrentQueryResult = MoveTemp(FailedQuery);
	}
	if (Payload->IsRotated() != bOriginalRotation) UpdateDragVisualRotation();
	return bFound;
}

FINV_SpaceQueryResult UINV_InventoryGrid::CheckHoverPosition(const FIntPoint& Position,
	const FIntPoint& Dimensions) const
{
	FINV_SpaceQueryResult Result;
	
	UINV_ItemPayload* ActivePayload = GetActivePayload();
	if (!IsValid(ActivePayload)) return Result;
	const bool bCrossInventory = ActivePayload->SourceInventory.IsValid()
		&& ActivePayload->SourceInventory.Get() != InventoryComponent.Get();

	// 大前提：拖拽物品的类型必须与当前网格匹配。不匹配直接 bHasSpace=false（保持默认），
	// 不再做任何放置/互换判定 —— 避免「消耗品拖到装备页签也能高亮」这类越界放置。
	// 这是所有高亮判定的前置门槛，先于边界检查与精确覆盖逻辑。
	if (UINV_InventoryItem* DraggedItem = ActivePayload->GetInventoryItem();
		!IsValid(DraggedItem) || !MatchesCategory(DraggedItem))
	{
		return Result;
	}

	// 必须先校验二维坐标，负 X 转成一维索引会回卷到上一行。
	if (Dimensions.X <= 0 || Dimensions.Y <= 0 || !IsLandingInBounds(Position, Dimensions)
		|| !IsInGridBounds(UINV_WidgetUtils::GetIndexFromPosition(Position, Columns), Dimensions)) return Result;
	Result.bHasSpace = true;
	Result.CurrentItem = ActivePayload->GetInventoryItem();
	
	// 源区域必须按物品的旧朝向计算；Dimensions 是拖拽中的新朝向。
	// 用新尺寸会把旧区域外的格子误判为自身重叠，影响旋转分支的选择。
	if (const UINV_InventoryItem* SourceItem = ActivePayload->GetInventoryItem();
		IsValid(SourceItem) && GridSlots.IsValidIndex(ActivePayload->GetPreviousGridIndex())
		&& GridSlots[ActivePayload->GetPreviousGridIndex()]->GetInventoryItem().Get() == SourceItem)
	{
		UINV_InventoryStatics::Foreach2D(GridSlots, ActivePayload->GetPreviousGridIndex(),
			SourceItem->GetItemDimension(), Columns, [&](const UINV_GridSlot* GridSlot)
		{
			Result.OriginIndices.Add(GridSlot->GetTileIndex());
		});
	}
	
	//查询包围范围内的所有物品指针和用于检查的索引
	UINV_InventoryStatics::Foreach2D(GridSlots, UINV_WidgetUtils::GetIndexFromPosition(Position, Columns), Dimensions, Columns, [&](const UINV_GridSlot* GridSlot)
	{
		Result.ClickedIndices.Add(GridSlot->GetTileIndex());
		
		if (TWeakObjectPtr<UINV_InventoryItem> InventoryItem = GridSlot->GetInventoryItem(); InventoryItem.IsValid())
		{
			Result.SpaceItemsMap.Add(InventoryItem,GridSlot->GetUpperLeftIndex());
		}
		
		
		if (Result.OriginIndices.Contains(GridSlot->GetTileIndex()))
		{
			Result.OverlapIndices.Add(GridSlot->GetTileIndex());
		}
		else
		{
			if (GridSlot->IsAvailable())
			{
				Result.EmptyIndices.Add(GridSlot->GetTileIndex());
			}
			else
			{
				Result.ValidateIndices.Add(GridSlot->GetTileIndex());
			}
		}
	});
	
	if (!Result.OverlapIndices.IsEmpty())
	{
		Result.bIsOverlap = true;
	}
	// 旋转后与旧占区重叠且目标区还有别的物品时，当前不做二维挤压。
	// 在高亮阶段就灰显，放置路径只需通过 bHasSpace 守卫。
	if (!bCrossInventory && IsHoverRotationChanged() && Result.bIsOverlap && Result.SpaceItemsMap.Num() > 1)
	{
		Result.bHasSpace = false;
		return Result;
	}
	// 有效性检测, 即当前的所有物品是否正好占满ValidIndices
	// 检查所有Item占用的格子与ValidIndices是否一致
	
	// ===== 装备槽来源（跨 slot 放置）：统一校验，只回答「这块区域能不能放下」 =====
	// 统一规则（对所有接受该装备的网格一视同仁，不再只对 Grid_Equippables 生效）：
	//   1. 类型不匹配 → bHasSpace=false（已在上方 MatchesCategory 前置门槛拦截，拖不到这里）；
	//   2. 放置区域内有任意物品 → bHasSpace=false（ValidateIndices 非空即灰显）。
	// 即装备从装备槽拖到任何接受它的网格，都要求「区域全空」才可放下。
	// 同一网格内部互相拖动（来源为 InventoryGrid）仍然走下面的精确覆盖 / 互换逻辑。
	// 装备本身不在本网格内，不存在与自己重叠的情况，相关字段一并清掉。
	const bool bFromEquippedSlot = ActivePayload->GetDragSource() == EINV_ItemDragSource::EquippedSlot;
	if (bFromEquippedSlot)
	{
		Result.bHasSpace = Result.ValidateIndices.IsEmpty();
		Result.bIsOverlap = false;
		Result.OverlapIndices.Reset();
		Result.OriginIndices.Reset();
		return Result;
	}

	// ===== 有效性校验:范围内物品占用的格子集合 == ValidateIndices (精确覆盖) =====
	// 1. 收集范围内每个被占用格子所属物品的左上角 (去重)
	// 2. 对每个物品,以左上角为起点、按当前朝向的尺寸展开完整占用区域
	// 3. 该物品的每一个格子都必须落在 ValidateIndices 内(不能溢出到范围外),
	//    且所有物品的占用并集必须恰好等于 ValidateIndices
	if (Result.bHasSpace)
	{
		TSet<int32> UpperLeftIndices;   // 范围内所有物品的左上角
		for (const int32 ValidatedIndex : Result.ValidateIndices)
		{
			const UINV_GridSlot* ValidSlot = GridSlots[ValidatedIndex];
			const int32 UpperLeft = ValidSlot->GetUpperLeftIndex();
			if (UpperLeft != INDEX_NONE)
			{
				UpperLeftIndices.Add(UpperLeft);
			}
		}

		TSet<int32> ItemsClaimedIndices; // 范围内物品实际占用的格子并集
		bool bAllItemsInRange = true;

		for (const int32 UpperLeft : UpperLeftIndices)
		{
			const UINV_GridSlot* UpperLeftSlot = GridSlots[UpperLeft];
			UINV_InventoryItem* Item = UpperLeftSlot->GetInventoryItem().Get();
			if (!IsValid(Item)) { bAllItemsInRange = false; break; }

			const FINV_GridFragment* GridFragment = GetFragment<FINV_GridFragment>(Item, FragmentTags::GridFragment);
			if (!GridFragment) { bAllItemsInRange = false; break; }

			// 展开该物品占据的全部格子（按实际尺寸，含朝向）
			UINV_InventoryStatics::Foreach2D(GridSlots, UpperLeft, Item->GetItemDimension(), Columns,
				[&](const UINV_GridSlot* OccupiedSlot)
				{
					const int32 OccupiedIndex = OccupiedSlot->GetTileIndex();
					ItemsClaimedIndices.Add(OccupiedIndex);
					// 该格若不在校验区,说明物品溢出到范围外(部分在外面)
					if (!Result.ValidateIndices.Contains(OccupiedIndex))
					{
						bAllItemsInRange = false;
					}
				});
		}

		// 精确覆盖:物品占用并集 == ValidateIndices,且无溢出
		const bool bExactMatch = bAllItemsInRange && (ItemsClaimedIndices.Num() == Result.ValidateIndices.Num());
		const bool bCrossSwapArea = bCrossInventory && bAllItemsInRange && !Result.SpaceItemsMap.IsEmpty();
		Result.bHasSpace = Result.bHasSpace && (bExactMatch || bCrossSwapArea);
	}

	if (bCrossInventory && Result.SpaceItemsMap.Num() > 0)
	{
		UINV_InventoryComponent* SourceInventory = ActivePayload->SourceInventory.Get();
		TArray<UINV_InventoryItem*> CoveredItems;
		for (const auto& Entry : Result.SpaceItemsMap)
		{
			if (UINV_InventoryItem* Covered = Entry.Key.Get(); IsValid(Covered)) CoveredItems.AddUnique(Covered);
		}
		TArray<FINV_CrossContainerSwapEntry> PlannedEntries;
		Result.bHasSpace = Result.bHasSpace && FINV_CrossContainerSwapPlanner::BuildPlan(
			SourceInventory, ActivePayload->GetInventoryItem(), CoveredItems, PlannedEntries);
	}
	
	return Result;
}

bool UINV_InventoryGrid::CursorExitedCanvas(const FVector2D& BoundaryPos, const FVector2D& BoundarySize, const FVector2D& Location)
{
	bLastMouseWithinCanvas = bMouseWithinCanvas;
	bMouseWithinCanvas = UINV_WidgetUtils::IsWithinBounds(BoundaryPos, BoundarySize, Location);
	if (!bMouseWithinCanvas) InvalidateDropQuery();
	
	if (!bMouseWithinCanvas && bLastMouseWithinCanvas)
	{
		UnhighlightSlots(LastHighlightedIndex,LastHighlightedDimensions);
		return true;
	} 
	return false;
}

void UINV_InventoryGrid::GrayedOutSlots(const int32 Index, const FIntPoint& Dimensions)
{
	if (!bMouseWithinCanvas) return;
	UnhighlightSlots(LastHighlightedIndex,LastHighlightedDimensions);
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [](UINV_GridSlot* GridSlot)
	{
		GridSlot->SetGrayedOutTexture();
	});
	LastHighlightedIndex = Index;
	LastHighlightedDimensions = Dimensions;
}


void UINV_InventoryGrid::SelectSlots(const int32 Index, const FIntPoint& Dimensions)
{
	if (!bMouseWithinCanvas) return;
	UnhighlightSlots(LastHighlightedIndex,LastHighlightedDimensions);
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [](UINV_GridSlot* GridSlot)
	{
		GridSlot->SetSelectedTexture();
	});
	LastHighlightedIndex = Index;
	LastHighlightedDimensions = Dimensions;
}


void UINV_InventoryGrid::HighlightSlots(const int32 Index, const FIntPoint& Dimensions)
{
	if (!bMouseWithinCanvas) return;
	UnhighlightSlots(LastHighlightedIndex,LastHighlightedDimensions);
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [](UINV_GridSlot* GridSlot)
	{
		GridSlot->SetOccupiedTexture();
	});
	LastHighlightedIndex = Index;
	LastHighlightedDimensions = Dimensions;
}

void UINV_InventoryGrid::UnhighlightSlots(const int32 Index, const FIntPoint& Dimensions)
{
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [](UINV_GridSlot* GridSlot)
	{
		if (GridSlot->IsAvailable())
		{
			GridSlot->SetUnoccupiedTexture();
		}
		else
		{
			GridSlot->SetOccupiedTexture();	
		}
	});
	
}

void UINV_InventoryGrid::ChangeHoverType(const int32 Index, const FIntPoint& Dimensions,
	EINV_GridSlotState GridSlotState)
{
	UnhighlightSlots(LastHighlightedIndex,LastHighlightedDimensions);
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [State = GridSlotState](UINV_GridSlot* GridSlot)
	{
		switch (State)
		{
		case EINV_GridSlotState::Occupied:
			GridSlot->SetOccupiedTexture();
			break;
		case EINV_GridSlotState::Unoccupied:
			GridSlot->SetUnoccupiedTexture();
			break;
		case EINV_GridSlotState::GrayedOut:
			GridSlot->SetGrayedOutTexture();
			break;
		case EINV_GridSlotState::Selected:
			GridSlot->SetSelectedTexture();
			break;
		}
	});
	
	LastHighlightedIndex = Index;
	LastHighlightedDimensions = Dimensions;
}

void UINV_InventoryGrid::ClearHoverItem()
{
	if (!bProcessingDrop) InvalidateDropQuery();
	if (!IsValid(ItemPayload))
	{
		DragOperationPayload.Reset();
		return;
	}
	
	ItemPayload->Clear();
	ItemPayload = nullptr;
	DragOperationPayload.Reset();
	
//	ShowCursor();
}

UUserWidget* UINV_InventoryGrid::GetVisibleCursorWidget()
{
	if (!IsValid(GetOwningPlayer())) return nullptr;
	if (!IsValid(VisibleCursorWidget))
	{
		VisibleCursorWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), VisibleCursorWidgetClass);
	}
	return VisibleCursorWidget;
}

UUserWidget* UINV_InventoryGrid::GetHiddenCursorWidget()
{
	if (!IsValid(GetOwningPlayer())) return nullptr;
	if (!IsValid(HiddenCursorWidget))
	{
		HiddenCursorWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), HiddenCursorWidgetClass);
	}
	return HiddenCursorWidget;
}

bool UINV_InventoryGrid::IsSameStackable(const UINV_InventoryItem* ClickedInventoryItem) const
{
	if (!IsValid(ClickedInventoryItem)) return false;
	if (!ClickedInventoryItem->IsStackable()) return false;

	// 只比较物品标签，不比较指针：
	// 拆分（以及一次拾取超过单堆叠上限）之后，同一物品在背包里会存在多个独立条目，
	// 指针必然不同，若要求「同一指针」就永远判为不同类，同类堆叠之间只能交换、无法叠加。
	UINV_InventoryItem* HoveredItem = IsValid(ItemPayload) ? ItemPayload->GetInventoryItem() : nullptr;
	if (!IsValid(HoveredItem)) return false;

	const FGameplayTag HoveredType = HoveredItem->GetItemManifest().GetItemType();
	const FGameplayTag ClickedType = ClickedInventoryItem->GetItemManifest().GetItemType();
	// 空标签之间也会 MatchesTagExact，必须先排除
	if (!HoveredType.IsValid() || !ClickedType.IsValid()) return false;

	return HoveredType.MatchesTagExact(ClickedType) && HoveredItem->IsStackable();
}

void UINV_InventoryGrid::SwapWithHoverItem(UINV_InventoryItem* ClickedInventoryItem, int32 ClickedIndex,
	int32 HoverTargetIndex)
{
	if (!IsValid(ItemPayload)) return;
	
	if (!IsValid(ClickedInventoryItem)) return;
	if (!GridSlots.IsValidIndex(ClickedIndex)) return;

	UINV_InventoryItem* TempInventoryItem = ItemPayload->GetInventoryItem();
	if (!IsValid(TempInventoryItem)) return;

	// 落点区域里的「目标物品」就是被拖拽的这一件本身：说明只是拖回自己身上，
	// 不做任何搬运（继续往下走会把同一个实例铺成两份图标）。
	if (ClickedInventoryItem == TempInventoryItem)
	{
		ClearHoverItem();
		return;
	}

	const int32 SourceIndex = ItemPayload->GetPreviousGridIndex();
	if (!GridSlots.IsValidIndex(SourceIndex))
	{
		ClearHoverItem();
		return;
	}

	// 悬挂物品的目标尺寸按 Payload 算（含拖拽中按 R 改的朝向）；目标物品仍按它自己的朝向
	const FIntPoint HoverDim = ItemPayload->GetGridDimensions();
	const FIntPoint ClickedDim = ClickedInventoryItem->GetItemDimension();

	// 落点 = 鼠标位置（HoverTargetIndex 即 ItemDropIndex，已按物品尺寸对齐左上角）。
	// 未指定时退回目标物品左上角。
	const int32 TargetIndex = (HoverTargetIndex == INDEX_NONE) ? ClickedIndex : HoverTargetIndex;
	if (!GridSlots.IsValidIndex(TargetIndex)) return;

	// ===== 单件：只算向量 =====
	// 单件场景永远不会触发挤压逻辑 —— 区域内只有「被挡住的这一件」，
	// 它没有后续物品需要级联推挤，所以和多物品分支用同一个公式，但只应用一次：
	//   向量 = 起点(源位置) - 终点(落点)
	//   被挡住的物品新位置 = 自身位置 + 向量（等价于向反方向移动同样的位移）
	// 两者对齐时它正好落到源位置（常规互换）；落点错开时它被推向反方向。
	const FIntPoint BeforePos = UINV_WidgetUtils::GetPositionFromIndex(SourceIndex, Columns);
	const FIntPoint AfterPos = UINV_WidgetUtils::GetPositionFromIndex(TargetIndex, Columns);
	const FIntPoint RelativeMoveVector = BeforePos - AfterPos;

	const FIntPoint ClickedAfterPos =
		UINV_WidgetUtils::GetPositionFromIndex(ClickedIndex, Columns) + RelativeMoveVector;

	// 越界必须按坐标判：负坐标直接换算线性索引会「回卷」成另一行的合法索引。
	const bool bClickedPosInBounds =
		ClickedAfterPos.X >= 0 && ClickedAfterPos.Y >= 0 &&
		ClickedAfterPos.X + ClickedDim.X <= Columns &&
		ClickedAfterPos.Y + ClickedDim.Y <= Rows;

	const int32 ClickedAfterIndex = bClickedPosInBounds
		? UINV_WidgetUtils::GetIndexFromPosition(ClickedAfterPos, Columns)
		: INDEX_NONE;

	const int32 TempStackCount = ItemPayload->GetStackCount();
	const bool bTempIsStackable = ItemPayload->IsStackable();
	const int32 ClickedStackCount = GridSlots[ClickedIndex]->GetStackCount();
	const bool bClickedIsStackable = ClickedInventoryItem->IsStackable();

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 服务器先验证并提交整组交换，客户端不清格、不改朝向，避免拒绝时污染确认态。
		TArray<UINV_InventoryItem*> MovedItems{TempInventoryItem, ClickedInventoryItem};
		TArray<int32> MovedIndices{TargetIndex, ClickedAfterIndex};
		TArray<bool> MovedRotations{ItemPayload->IsRotated(), ClickedInventoryItem->IsRotated()};
		FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::BatchMove);
		Context.TargetSlotId = SlotId;
		Context.BatchItems = MovedItems;
		Context.BatchTargetIndices = MovedIndices;
		Context.BatchRotatedStates = MovedRotations;
		SubmitInventoryOperation(Context);
		ClearHoverItem();
		return;
	}

	// 两件都先离场，之后才能用「纯格子占用」判落点：
	// 留着的话，目标物品的位移落点和它自己旧位置重叠就会被判成冲突。
	RemoveItemFromGrid(TempInventoryItem, SourceIndex);
	RemoveItemFromGrid(ClickedInventoryItem, ClickedIndex);

	// 落点不可用（越界 / 撞上第三件物品）→ 两件各自放回原位，状态与调用前一致
	if (!CanPlaceItemAtIndex(TargetIndex, HoverDim)
		|| ClickedAfterIndex == INDEX_NONE
		|| !CanPlaceItemAtIndex(ClickedAfterIndex, ClickedDim))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] 交换被拒：%s (%dx%d) 落点 [%d] 与 %s (%dx%d) 位移落点 [%d] 放不下，操作取消。"),
			*GetNameSafe(TempInventoryItem), HoverDim.X, HoverDim.Y, TargetIndex,
			*GetNameSafe(ClickedInventoryItem), ClickedDim.X, ClickedDim.Y, ClickedAfterIndex);

		AddItemAtIndex(TempInventoryItem, SourceIndex, bTempIsStackable, TempStackCount);
		UpdateGridSlots(TempInventoryItem, SourceIndex, bTempIsStackable, TempStackCount);
		AddItemAtIndex(ClickedInventoryItem, ClickedIndex, bClickedIsStackable, ClickedStackCount);
		UpdateGridSlots(ClickedInventoryItem, ClickedIndex, bClickedIsStackable, ClickedStackCount);
		ClearHoverItem();
		return;
	}

	// 悬挂物品落到鼠标落点，被挡住的物品落到「按向量平移后的位置」
	// 落点校验用 HoverDim 通过了，此刻才把朝向真正写回物品（回滚路径在上面，绝不能提前写）
	ApplyPayloadRotationToItem();
	AddItemAtIndex(TempInventoryItem, TargetIndex, bTempIsStackable, TempStackCount);
	UpdateGridSlots(TempInventoryItem, TargetIndex, bTempIsStackable, TempStackCount);

	AddItemAtIndex(ClickedInventoryItem, ClickedAfterIndex, bClickedIsStackable, ClickedStackCount);
	UpdateGridSlots(ClickedInventoryItem, ClickedAfterIndex, bClickedIsStackable, ClickedStackCount);

	// 服务器权威：整组一次提交（与多物品挤压分支同一条路径）。
	// 不用 SwapItems —— 它的语义是严格互换坐标，表达不了位移。
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
	{
		TArray<UINV_InventoryItem*> MovedItems{TempInventoryItem, ClickedInventoryItem};
		TArray<int32> MovedIndices{TargetIndex, ClickedAfterIndex};
		// 只有被拖的那件可能改了朝向（按 R）；被挡开的那件保持原样
		TArray<bool> MovedRotations{TempInventoryItem->IsRotated(), ClickedInventoryItem->IsRotated()};
		Comp->Server_MoveItems(MovedItems, MovedIndices, SlotId, MovedRotations);
		Comp->RequestAuthoritativeRefresh();
	}

	ClearHoverItem();
}

bool UINV_InventoryGrid::ShouldSwapStackCounts(const int32 RoomInClickSlot, const int32 HoveredStackCount,
	const int32 MaxStackSize) const
{
	// 目标格已满（Room <= 0）→ 一件都放不进去，只能交换数量。这里不再区分 hover 是否满：
	//   · Hovered <  Max：目标变成 Hovered、源变成 Clicked，两堆换了位置
	//   · Hovered == Max：两边都是满堆，互相赋值后数量不变，物品原地不动
	// 第二种是「合并不了、也无位置可换」的正确结果 —— 同类满堆互换位置对玩家不可见，
	// 没必要真的搬运实例（省掉四次 Remove/Add 和两次图标重建）。
	if (MaxStackSize <= 0) return false;   // 上限配置无效，不做任何改动
	if (HoveredStackCount <= 0) return false;

	return RoomInClickSlot <= 0;
}

void UINV_InventoryGrid::SwapStackCounts(const int32 ClickedStackCount, const int32 HoveredStackCount,
	const int32 Index)
{
	// 目标格已满（Room <= 0 才会进到这里）：只交换数量，不搬运实例。
	// 两堆都满时两边赋值相同，等价于原地不动。
	// 拖拽路径下源格从一开始就没有被清空（见 OnSlottedItemDragStart），
	// 所以这里必须把源格的数量一起改掉，否则目标格变了、源格还是老数量 —— 物品凭空翻倍。
	const int32 SourceIndex = IsValid(ItemPayload) ? ItemPayload->GetPreviousGridIndex() : INDEX_NONE;

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 服务器权威：本地不动数量，等服务器互换完再按权威数据重铺
		UINV_InventoryItem* SourceItem = IsValid(ItemPayload) ? ItemPayload->GetInventoryItem() : nullptr;
		UINV_InventoryItem* TargetItem =
			GridSlots.IsValidIndex(Index) ? GridSlots[Index]->GetInventoryItem().Get() : nullptr;

		if (IsValid(SourceItem) && IsValid(TargetItem) && SourceItem != TargetItem)
		{
			FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::SwapStackCounts);
			Context.Item = SourceItem;
			Context.SecondaryItem = TargetItem;
			SubmitInventoryOperation(Context);
		}

		ClearHoverItem();
		return;
	}

	if (GridSlots.IsValidIndex(Index))
	{
		GridSlots[Index]->SetStackCount(HoveredStackCount);
		if (UINV_SlottedItem* ClickedSlottedItem = SlottedItems.FindRef(Index); IsValid(ClickedSlottedItem))
		{
			ClickedSlottedItem->UpdateStackCount(HoveredStackCount);
		}
	}

	// 源格拿回目标原来的数量（物品实例本来就还在源格）
	if (GridSlots.IsValidIndex(SourceIndex))
	{
		GridSlots[SourceIndex]->SetStackCount(ClickedStackCount);
		if (UINV_SlottedItem* SourceSlottedItem = SlottedItems.FindRef(SourceIndex); IsValid(SourceSlottedItem))
		{
			SourceSlottedItem->UpdateStackCount(ClickedStackCount);
		}
	}

	// 服务器权威：两堆数量对调。两堆都满时数值相等，服务器侧等同于不动。
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
	{
		UINV_InventoryItem* SourceItem = IsValid(ItemPayload) ? ItemPayload->GetInventoryItem() : nullptr;
		UINV_InventoryItem* TargetItem =
			GridSlots.IsValidIndex(Index) ? GridSlots[Index]->GetInventoryItem().Get() : nullptr;

		if (IsValid(SourceItem) && IsValid(TargetItem) && SourceItem != TargetItem)
		{
			Comp->Server_SwapStackCounts(SourceItem, TargetItem);
			Comp->RequestAuthoritativeRefresh();
		}
	}

	ClearHoverItem();
}

bool UINV_InventoryGrid::ShouldConsumeStackCounts(const int32 RoomInClickSlot, const int32 HoveredStackCount) const
{
	return RoomInClickSlot >= HoveredStackCount;
}

void UINV_InventoryGrid::ConsumeHoverItemStacks(const int32 ClickedStackCount, const int32 HoveredStackCount,
	const int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return;

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 服务器权威：整份并走由服务器删条目。本地既不改数量、也不手动清源格，
		// 刷新会按权威数据把源图标去掉（条目已不在列表里）。
		if (IsValid(ItemPayload))
		{
			UINV_InventoryItem* SourceItem = ItemPayload->GetInventoryItem();
			UINV_InventoryItem* TargetItem = GridSlots[Index]->GetInventoryItem().Get();
			if (IsValid(SourceItem) && IsValid(TargetItem) && SourceItem != TargetItem)
			{
				FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Merge);
				Context.Item = SourceItem;
				Context.SecondaryItem = TargetItem;
				Context.Amount = HoveredStackCount;
				Context.SourceSlotId = SourceItem->GetContainerId();
				Context.SourceIndex = SourceItem->GetGridIndex();
				Context.TargetSlotId = TargetItem->GetContainerId();
				Context.TargetIndex = TargetItem->GetGridIndex();
				SubmitInventoryOperation(Context);
			}
		}

		ClearHoverItem();
		ShowCursor();

		// SubmitInventoryOperation 的终态回执已经携带权威快照并触发重绘。
		return;
	}

	const int32 NewClickedStackCount = ClickedStackCount + HoveredStackCount;

	GridSlots[Index]->SetStackCount(NewClickedStackCount);
	if (UINV_SlottedItem* ClickedItem = SlottedItems.FindRef(Index); IsValid(ClickedItem))
	{
		ClickedItem->UpdateStackCount(NewClickedStackCount);
	}

	// 源堆叠被整份并走：把源格连同图标一起清掉。
	// 拖拽路径不会在拿起时移除源格，这一步必须在这里做完，否则源格会留下同样数量的幽灵堆叠。
	bool bReportedToServer = false;
	if (IsValid(ItemPayload))
	{
		if (UINV_InventoryItem* SourceItem = ItemPayload->GetInventoryItem())
		{
			// 先把来源格取出来：下面的刷新在本机权威时会同步重铺并清空 ItemPayload
			const int32 SourcePreviousIndex = ItemPayload->GetPreviousGridIndex();

			// 服务器权威：源整份并进目标，转移量就是 hover 持有的全部数量。
			// 必须赶在 RemoveItemFromGrid 之前取指针 —— 之后源物品的 GridIndex 会被清空。
			if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
			{
				UINV_InventoryItem* TargetItem = GridSlots[Index]->GetInventoryItem().Get();
				if (IsValid(TargetItem) && TargetItem != SourceItem)
				{
					Comp->Server_MergeStack(SourceItem, TargetItem, HoveredStackCount);
					bReportedToServer = true;
				}
			}

			RemoveItemFromGrid(SourceItem, SourcePreviousIndex);
		}
	}

	ClearHoverItem();
	ShowCursor();

	// 刷新放到最后：它会同步重铺（本机权威），中途调用会让上面的 ItemPayload 变成空指针
	if (bReportedToServer)
	{
		if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
		{
			Comp->RequestAuthoritativeRefresh();
		}
	}
}

bool UINV_InventoryGrid::ShouldFillInStack(const int32 RoomInClickSlot, const int32 HoveredStackCount) const
{
	return RoomInClickSlot > 0 && RoomInClickSlot < HoveredStackCount;
}

void UINV_InventoryGrid::FillInStack(const int32 FillAmount, const int32 Remainder, const int32 Index)
{
	const int32 SourceIndex = IsValid(ItemPayload) ? ItemPayload->GetPreviousGridIndex() : INDEX_NONE;

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 服务器权威：本地不改两堆数量，等服务器合并完再按权威数据重铺
		UINV_InventoryItem* SourceItem = IsValid(ItemPayload) ? ItemPayload->GetInventoryItem() : nullptr;
		UINV_InventoryItem* TargetItem =
			GridSlots.IsValidIndex(Index) ? GridSlots[Index]->GetInventoryItem().Get() : nullptr;

		if (IsValid(SourceItem) && IsValid(TargetItem) && SourceItem != TargetItem && FillAmount > 0)
		{
			FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Merge);
			Context.Item = SourceItem;
			Context.SecondaryItem = TargetItem;
			Context.Amount = FillAmount;
			Context.SourceSlotId = SourceItem->GetContainerId();
			Context.SourceIndex = SourceItem->GetGridIndex();
			Context.TargetSlotId = TargetItem->GetContainerId();
			Context.TargetIndex = TargetItem->GetGridIndex();
			SubmitInventoryOperation(Context);
			// 数量变化 / 源条目被移除都会经复制回调通知；本机权威才需要手动重绘
		}

		ClearHoverItem();
		return;
	}

	// 目标格接收能装下的部分
	if (GridSlots.IsValidIndex(Index))
	{
		const int32 NewStackCount = GridSlots[Index]->GetStackCount() + FillAmount;
		GridSlots[Index]->SetStackCount(NewStackCount);
		if (UINV_SlottedItem* ClickedItem = SlottedItems.FindRef(Index); IsValid(ClickedItem))
		{
			ClickedItem->UpdateStackCount(NewStackCount);
		}
	}

	// 源格留下没被并走的部分（源物品一直在源格，不需要重新放置）
	if (GridSlots.IsValidIndex(SourceIndex))
	{
		GridSlots[SourceIndex]->SetStackCount(Remainder);
		if (UINV_SlottedItem* SourceSlottedItem = SlottedItems.FindRef(SourceIndex); IsValid(SourceSlottedItem))
		{
			SourceSlottedItem->UpdateStackCount(Remainder);
		}
	}

	// 服务器权威：只把目标容纳得下的那部分并过去，源条目保留余量
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
	{
		UINV_InventoryItem* SourceItem = IsValid(ItemPayload) ? ItemPayload->GetInventoryItem() : nullptr;
		UINV_InventoryItem* TargetItem =
			GridSlots.IsValidIndex(Index) ? GridSlots[Index]->GetInventoryItem().Get() : nullptr;

		if (IsValid(SourceItem) && IsValid(TargetItem) && SourceItem != TargetItem && FillAmount > 0)
		{
			Comp->Server_MergeStack(SourceItem, TargetItem, FillAmount);
			Comp->RequestAuthoritativeRefresh();
		}
	}

	ClearHoverItem();
}

void UINV_InventoryGrid::ShowCursor()
{
	if (!IsValid(GetOwningPlayer())) return;
	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, GetVisibleCursorWidget());
}

void UINV_InventoryGrid::HideCursor()
{
	if (!IsValid(GetOwningPlayer())) return;
	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, GetHiddenCursorWidget());
}

void UINV_InventoryGrid::SetOwningCanvas(UCanvasPanel* OwningCanvas)
{
	OwningCanvasPanel = OwningCanvas;
}

FIntPoint UINV_InventoryGrid::CalculateStartCoordinate(const FIntPoint& Coordinates, const FIntPoint& Dimensions,
                                                       EINV_TileQuadrant TileQuadrant) const
{
	const int32 HasEvenWidth = Dimensions.X % 2 == 0 ? 1 : 0;
	const int32 HasEvenHeight = Dimensions.Y % 2 == 0 ? 1 : 0;
	
	FIntPoint StartingCoord;
	switch (TileQuadrant)
	{
	case EINV_TileQuadrant::TopLeft:
		StartingCoord.X = Coordinates.X - FMath::FloorToInt(0.5f * Dimensions.X);
		StartingCoord.Y = Coordinates.Y - FMath::FloorToInt(0.5f * Dimensions.Y);
	break;
	case EINV_TileQuadrant::TopRight:
		StartingCoord.X = Coordinates.X - FMath::FloorToInt(0.5f * Dimensions.X) + HasEvenWidth;
		StartingCoord.Y = Coordinates.Y - FMath::FloorToInt(0.5f * Dimensions.Y);
	break;
	case EINV_TileQuadrant::BottomLeft:
		StartingCoord.X = Coordinates.X - FMath::FloorToInt(0.5f * Dimensions.X);
		StartingCoord.Y = Coordinates.Y - FMath::FloorToInt(0.5f * Dimensions.Y) + HasEvenHeight;
	break;
	case EINV_TileQuadrant::BottomRight:
		StartingCoord.X = Coordinates.X - FMath::FloorToInt(0.5f * Dimensions.X) + HasEvenWidth;
		StartingCoord.Y = Coordinates.Y - FMath::FloorToInt(0.5f * Dimensions.Y) + HasEvenHeight;
	break;
	default:
		UE_LOG(LogInventory, Error, TEXT("Invalid tile quadrant"));
		return FIntPoint(-1, -1);
	}
	return StartingCoord;
}




FIntPoint UINV_InventoryGrid::CalculateHoveredCoordinates(const FVector2D& CanvasPosition,
                                                          const FVector2D& MousePosition) const 
{
	return FIntPoint {
		static_cast<int32>((FMath::FloorToInt(MousePosition.X - CanvasPosition.X) / TileSize)),
		static_cast<int32>((FMath::FloorToInt(MousePosition.Y - CanvasPosition.Y) / TileSize))
	};
}

EINV_TileQuadrant UINV_InventoryGrid::CalculateTileQuadrant(const FVector2D& CanvasPosition,
	const FVector2D& MousePosition) const
{
	// Calculae relative position whthin the current tile
	const float TileLocalX = FMath::Fmod(MousePosition.X - CanvasPosition.X ,TileSize);
	const float TileLocalY = FMath::Fmod(MousePosition.Y - CanvasPosition.Y ,TileSize);
 
	// Determine which quardrant the mouse in
	float HalfTileSize = TileSize / 2.f;
	const bool bIsTop = TileLocalY < HalfTileSize;
	const bool bIsLeft = TileLocalX < HalfTileSize;
	
	if (bIsTop && bIsLeft)   return EINV_TileQuadrant::TopLeft;
	if (bIsTop && !bIsLeft)  return EINV_TileQuadrant::TopRight;
	if (!bIsTop && bIsLeft)  return EINV_TileQuadrant::BottomLeft;
	if (!bIsTop && !bIsLeft) return EINV_TileQuadrant::BottomRight;
	
	return EINV_TileQuadrant::None;
}


void UINV_InventoryGrid::AssignHoverItem(UINV_InventoryItem* InventoryItem)
{
	if (!bProcessingDrop) InvalidateDropQuery();
	if (!IsValid(ItemPayload))
	{
		ItemPayload = NewObject<UINV_ItemPayload>(this);
	}

	ItemPayload->SetInventoryItem(InventoryItem);
	// 传未旋转尺寸 + 当前朝向：Payload 自己按朝向算实际占地（GetGridDimensions）
	ItemPayload->SetGridDimensions(InventoryItem->GetBaseDimension());
	ItemPayload->SetRotated(InventoryItem->IsRotated());
	ItemPayload->SetIsStackable(InventoryItem->IsStackable());
	ItemPayload->SetDragSource(EINV_ItemDragSource::InventoryGrid);
}

void UINV_InventoryGrid::OnHide()
{
	PutHoverItemBack();
	// 即使 Payload 无效或本次从装备槽拖入，也必须清掉所有拖拽派生状态。
	ClearHoverItem();
	ClearExternalDragPayload();
	UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
	LastHighlightedIndex = INDEX_NONE;
	LastHighlightedDimensions = FIntPoint::ZeroValue;
	InvalidateDropQuery();
	bRotateKeyHeld = false;
	bMouseWithinCanvas = false;
	bLastMouseWithinCanvas = false;
}

void UINV_InventoryGrid::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	if (!bProcessingDrop) InvalidateDropQuery();

	// 外部来源（装备槽或其它 InventoryComponent）的 Payload 必须进入本网格
	// 的 ActivePayload，NativeTick 才能计算目标区域高亮。
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	const bool bCrossInventory = IsValid(Payload) && Payload->SourceInventory.IsValid()
		&& Payload->SourceInventory.Get() != InventoryComponent.Get();
	if (IsValid(Payload) && (Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot || bCrossInventory))
	{
		ExternalDragPayload = Payload;
		BindDragCancellation(Payload);
	}
}

void UINV_InventoryGrid::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	ClearExternalDragPayload();
}

UINV_ItemPayload* UINV_InventoryGrid::GetActivePayload() const
{
	if (IsValid(ItemPayload)) return ItemPayload;
	if (IsValid(ExternalDragPayload)) return ExternalDragPayload;
	return nullptr;
}

bool UINV_InventoryGrid::HasExternalDragPayload() const
{
	return IsValid(ExternalDragPayload);
}

void UINV_InventoryGrid::ClearExternalDragPayload()
{
	if (!bProcessingDrop) InvalidateDropQuery();
	if (IsValid(ExternalDragPayload))
	{
		UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
		ExternalDragPayload = nullptr;
	}
}


void UINV_InventoryGrid::AddStacks(const FINV_SlotAvailabilityResult& Result)
{
	// 归属判断，与 AddItem 一致：两个同分类网格时，只有物品真正归属的网格才处理堆叠。
	if (!BelongsToThisGrid(Result.Item.Get())) return;

	// 只处理「叠加进已有格子」的部分（bItemAtIndex == true）。
	// 「新格子」不在这里铺：服务器会把超出单条目上限的数量按 MaxStackSize 拆成新的 InventoryItem，
	// 它们经 OnItemAdded -> AddItem -> AutoPlaceItem 各自落到自己的格子上。
	// 若在这里拿 Result.Item（已有的那个条目）去铺新格子，就会让同一个实例占多格，
	// 而空间索引 SpaceItemsMap 一个实例只有一条记录，拖拽交换时只会处理其中一格。
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		if (!Availability.bItemAtIndex) continue;
		if (!GridSlots.IsValidIndex(Availability.Index)) continue;

		UINV_GridSlot* GridSlot = GridSlots[Availability.Index];
		UINV_SlottedItem* SlottedItem = SlottedItems.FindRef(Availability.Index);
		if (!IsValid(GridSlot) || !IsValid(SlottedItem)) continue;

		const int32 NewStackCount = GridSlot->GetStackCount() + Availability.AmountToFill;
		SlottedItem->UpdateStackCount(NewStackCount);
		GridSlot->SetStackCount(NewStackCount);
	}
}



FIntPoint UINV_InventoryGrid::GetItemDimensions(const FINV_ItemManifest& Manifest) const
{
	const FINV_GridFragment* GridFragment = Manifest.GetFragmentOfType<FINV_GridFragment>();
	if (!GridFragment || GridFragment->GetGridSize().X <= 0 || GridFragment->GetGridSize().Y <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] %s：物品 %s 缺少有效 GridFragment 占地配置，拒绝放置。"),
			*GetNameSafe(this), *Manifest.GetItemType().ToString());
		return FIntPoint::ZeroValue;
	}
	return GridFragment->GetGridSize();
}

bool UINV_InventoryGrid::HasRoomForItemInstance(const UINV_InventoryItem* Item)
{
	if (!IsValid(Item)) return false;
	return !HasRoomForItem(Item).SlotAvailabilities.IsEmpty();
}

bool UINV_InventoryGrid::AutoPlaceItem(UINV_InventoryItem* Item)
{
	if (!IsValid(Item)) return false;
	if (!MatchesCategory(Item)) return false;

	FINV_SlotAvailabilityResult Result = HasRoomForItem(Item);
	// 一个可放位置都没有：不做任何改动，交给调用方决定后续（例如保持装备状态）
	if (Result.SlotAvailabilities.IsEmpty()) return false;

	// Only commit rotation after a valid landing has been found.
	Item->SetRotated(Result.bRotated);
	AddItemToIndeces(Result, Item);
	return true;
}

int32 UINV_InventoryGrid::FindFreeSlotForItem(const UINV_InventoryItem* Item) const
{
	if (!IsValid(Item)) return INDEX_NONE;

	// 要求整片区域为空：拆分出来的堆叠必须独立占位，不能叠进未满的同类堆叠
	const FIntPoint CurrentDimensions = Item->GetItemDimension();
	for (int32 Direction = 0; Direction < (Item->CanRotate() ? 2 : 1); ++Direction)
	{
		const FIntPoint Dimensions = Direction == 0 ? CurrentDimensions
			: FIntPoint(CurrentDimensions.Y, CurrentDimensions.X);
		for (int32 Index = 0; Index < GridSlots.Num(); ++Index)
		{
			if (!IsValid(GridSlots[Index])) continue;
			if (CanPlaceItemAtIndex(Index, Dimensions)) return Index;
		}
	}
	return INDEX_NONE;
}

int32 UINV_InventoryGrid::FindFreeSlotForItemIgnoring(const UINV_InventoryItem* Item,
	const UINV_InventoryItem* IgnoreItem, bool& bOutRotated) const
{
	bOutRotated = false;
	if (!IsValid(Item)) return INDEX_NONE;

	const FIntPoint BaseDimensions = Item->GetBaseDimension();
	for (int32 Direction = 0; Direction < (Item->CanRotate() ? 2 : 1); ++Direction)
	{
		const FIntPoint Dimensions = Direction == 0
			? BaseDimensions
			: FIntPoint(BaseDimensions.Y, BaseDimensions.X);

		for (int32 Index = 0; Index < GridSlots.Num(); ++Index)
		{
			if (!IsValid(GridSlots[Index])) continue;
			if (!CanPlaceItemAtIndexIgnoring(Index, Dimensions, IgnoreItem)) continue;

			bOutRotated = Direction != 0;
			return Index;
		}
	}

	return INDEX_NONE;
}

void UINV_InventoryGrid::OnInventoryItemRemoved(UINV_InventoryItem* Item)
{
	if (!IsValid(Item)) return;

	UINV_InventoryComponent* Comp = InventoryComponent.Get();

	if (IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 权威模式：整体按权威数据重铺。注意 PreReplicatedRemove 是在条目**真正移除之前**广播的，
		// 此刻列表里还能查到它，所以不能就地按当前状态重铺，必须排到下一拍。
		Comp->ScheduleRefresh();
		return;
	}

	// 乐观模式：只把这一件的图标摘掉，其余不动
	const int32 Index = FindItemGridIndex(Item);
	if (Index != INDEX_NONE)
	{
		RemoveItemFromGrid(Item, Index);
	}
}

void UINV_InventoryGrid::AddItem(UINV_InventoryItem* Item)
{
	if (!IsValid(Item)) return;
	// 用容器归属判断，而不是纯分类：两个同分类网格时分类判断会让两个网格都认领同一件物品。
	if (!BelongsToThisGrid(Item)) return;

	// 幂等：物品已经在本网格里就不再铺第二份图标。
	// OnItemAdded 可能对同一个条目重复触达，重复 Add 会造出多个指向同一实例的 SlottedItem，
	// 表现为「开关背包后多出一个物品」。
	// 局限：本函数只能判断「在不在本网格」，物品若在别的页签看不出区别 —— 这是 #11 里
	// 全局占据表要解决的问题，当前由下面的 CanPlaceItemAtIndex 兜住大多数情况。
	if (FindItemGridIndex(Item) != INDEX_NONE) return;

	// 服务器已经指定了落点（拆分出来的新堆叠）：直接放到该格。
	// 不能走 AutoPlaceItem —— 它会优先叠加到未满的同类堆叠上，把刚拆出来的又并回源堆叠。
	const int32 AssignedIndex = Item->GetGridIndex();
	if (const UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI
		&& AssignedIndex == INDEX_NONE)
	{
		// 权威刷新尚未给出落点时，不能在客户端自行找空位并回报服务器。
		// 否则一次被拒绝的装备事务会被重铺逻辑伪造为第二次卸下，造成两件装备同时进包。
		return;
	}
	if (AssignedIndex != INDEX_NONE && CanPlaceItemAtIndex(AssignedIndex, Item->GetItemDimension()))
	{
		const bool bStackable = Item->IsStackable();
		const int32 Amount = bStackable ? Item->GetTotalStackCount() : 0;
		AddItemAtIndex(Item, AssignedIndex, bStackable, Amount);
		UpdateGridSlots(Item, AssignedIndex, bStackable, Amount);

		if (const UINV_InventoryComponent* Comp = InventoryComponent.Get(); !IsValid(Comp) || !Comp->bServerAuthoritativeUI)
		{
			ReportPlacementToServer(Item, AssignedIndex);
		}
		return;
	}
	if (const UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 权威索引越界或与本地已确认占用冲突时等待服务器快照纠正，不自动换位。
		return;
	}

	AutoPlaceItem(Item);

	// AutoPlaceItem 算出的位置只有它自己知道，这里回查一次再上报。
	// 不上报的话服务器会一直认为这件东西没落在任何格子（GridIndex == INDEX_NONE），
	// 占用校验对它完全失效。
	if (const int32 PlacedIndex = FindItemGridIndex(Item); PlacedIndex != INDEX_NONE)
	{
		ReportPlacementToServer(Item, PlacedIndex);
	}
}

void UINV_InventoryGrid::ReportPlacementToServer(UINV_InventoryItem* Item, int32 Index)
{
	if (!IsValid(Item) || Index == INDEX_NONE) return;

	// 服务器端也会执行到 AddItem（监听服务器 / 单机的 OnItemAdded 广播），
	// 那时这条 RPC 相当于自己调自己，写入相同的值，无害。
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
	{
		Comp->Server_RegisterItemPlacement(Item, SlotId, Index, Item->IsRotated());
	}
}

bool UINV_InventoryGrid::HasHoverItem() const
{
	return IsValid(GetActivePayload()) && IsValid(GetActivePayload()->GetInventoryItem());
}

UINV_ItemPayload* UINV_InventoryGrid::GetHoverItem() const
{
	return ItemPayload;
}

void UINV_InventoryGrid::AddItemToIndeces(const FINV_SlotAvailabilityResult& Result, UINV_InventoryItem* NewItem)
{
	
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		AddItemAtIndex(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);
		UpdateGridSlots(NewItem,Availability.Index, Result.bStackable, Availability.AmountToFill);
	}
	
}

void UINV_InventoryGrid::AddItemAtIndex(UINV_InventoryItem* Item, const int32 Index, const bool bStackable,
	const int32 StackAmount)
{
	const FINV_GridFragment* GridFragment = GetFragment<FINV_GridFragment>(Item, FragmentTags::GridFragment);
	const FINV_ImageFragment* ImageFragment = GetFragment<FINV_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!GridFragment || !ImageFragment) return;
	RemoveSlottedItemEntriesForItem(Item);
	UINV_SlottedItem* SlottedItem = CreateSlottedItem(Item, Index, bStackable, StackAmount, GridFragment, ImageFragment);
	AddSlottedItemToCanvas(Index, GridFragment, SlottedItem, Item);
	SlottedItems.Add(Index, SlottedItem);
}

void UINV_InventoryGrid::RemoveSlottedItemEntriesForItem(const UINV_InventoryItem* Item, int32 KeepIndex)
{
	if (!IsValid(Item)) return;
	for (auto It = SlottedItems.CreateIterator(); It; ++It)
	{
		const int32 EntryIndex = It.Key();
		UINV_SlottedItem* SlottedItem = It.Value();
		if (EntryIndex == KeepIndex || (IsValid(SlottedItem) && SlottedItem->GetInventoryItem() != Item)) continue;

		if (IsValid(SlottedItem)) SlottedItem->RemoveFromParent();
		It.RemoveCurrent();
	}
}

void UINV_InventoryGrid::CreateItemPopUp(const int32 GridIndex)
{
	if (!GridSlots.IsValidIndex(GridIndex)) return;
	if (!IsValid(ItemPopUpClass)) return;
	if (!OwningCanvasPanel.IsValid()) return;

	UINV_GridSlot* GridSlot = GridSlots[GridIndex];
	UINV_InventoryItem* RightClickedItem = GridSlot->GetInventoryItem().Get();
	if (!IsValid(RightClickedItem)) return;
	if (IsValid(GridSlot->GetItemPopUp())) return;

	ItemPopUp = CreateWidget<UINV_ItemPopUp>(this, ItemPopUpClass);
	if (!IsValid(ItemPopUp)) return;
	GridSlot->SetItemPopUp(ItemPopUp);
	
	ItemPopUp->SetGridIndex(GridIndex);
	
	// 堆叠数量为 1 时没有可拆分的余量，直接隐藏拆分按钮
	const int32 SliderMax = GridSlot->GetStackCount() - 1;
	if (RightClickedItem->IsStackable() && SliderMax > 0)
	{
		ItemPopUp->OnSplit.BindDynamic(this, &ThisClass::OnPopUpMenuSplit);
		ItemPopUp->SetSliderParams(SliderMax, FMath::Max(1,GridSlot->GetStackCount() / 2));
	}
	else
	{
		ItemPopUp->CollapseSplitButton();
	}
	
	ItemPopUp->OnDrop.BindDynamic(this, &ThisClass::OnPopUpMenuDrop);

	if (RightClickedItem->IsConsumable())
	{
		ItemPopUp->OnConsume.BindDynamic(this, &ThisClass::OnPopUpMenuConsume);
	}
	else
	{
		ItemPopUp->CollapseConsumeButton();
	}
	
	OwningCanvasPanel->AddChild(ItemPopUp);
	
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(ItemPopUp);
	if (!IsValid(CanvasSlot)) return;

	// 菜单不要弹出可视区域外
	const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	const FVector2D PopUpSize = ItemPopUp->GetBoxSize();
	const FVector2D DesiredPosition = MousePosition - ItemPopUpOffset;
	const FVector2D CanvasSize = OwningCanvasPanel->GetCachedGeometry().GetLocalSize();
	const FVector2D ClampedPosition(
		FMath::Max(0.f, FMath::Min(DesiredPosition.X, CanvasSize.X - PopUpSize.X)),
		FMath::Max(0.f, FMath::Min(DesiredPosition.Y, CanvasSize.Y - PopUpSize.Y)));

	CanvasSlot->SetPosition(ClampedPosition);
	CanvasSlot->SetSize(PopUpSize);
}

void UINV_InventoryGrid::PutHoverItemBack()
{
	if (!IsValid(ItemPayload) || !IsValid(ItemPayload->GetInventoryItem())) return;

	UINV_InventoryItem* Item = ItemPayload->GetInventoryItem();

	// 幂等保护：物品仍在本网格里，说明这次拖拽并没有真正把它移走
	//（放置失败 / 换装被拒 / 拖拽中断 / 堆叠分支已就地处理完）。
	// 此时只清悬持状态，绝不能再放一次，否则会出现「多出一个物品」或数量翻倍。
	// 拖拽路径不再于「拿起」时移除源格，所以这个判断对堆叠物品同样必要。
	if (FindItemGridIndex(Item) != INDEX_NONE)
	{
		ClearHoverItem();
		return;
	}

	// 物品确实已经不在网格里：按手上的数量重新铺格子放回。
	// 注意必须用 AddItemToIndeces —— AddStacks 只处理「叠加进已有格子」，
	// 不会创建新格子，拿它来放回会让物品直接消失。
	const int32 AmountToPlace = FMath::Max(1, ItemPayload->GetStackCount());
	FINV_SlotAvailabilityResult Result = HasRoomForItem(Item, AmountToPlace);
	if (Result.SlotAvailabilities.IsEmpty())
	{
		ClearHoverItem();
		return;
	}
	Result.Item = Item;
	AddItemToIndeces(Result, Item);
	ClearHoverItem();
}

void UINV_InventoryGrid::DropItem()
{
	if (!IsValid(ItemPayload)) return;

	UINV_InventoryItem* Item = ItemPayload->GetInventoryItem();
	if (!IsValid(Item)) return;

	// Non-stackable entries store 0 in the payload; the server drop API counts physical units.
	const int32 StackCount = Item->IsStackable() ? ItemPayload->GetStackCount() : 1;

	//Tell the Server to active drop the item
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
	{
		if (AINV_PlayerController* PC = Cast<AINV_PlayerController>(GetOwningPlayer());
			IsValid(PC) && PC->GetActiveContainerInventory() == Comp)
		{
			PC->SubmitContainerDrop(Comp, Item, StackCount);
			ClearHoverItem();
			return;
		}
		if (Comp->bServerAuthoritativeUI)
		{
			FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Drop);
			Context.Item = Item;
			Context.Amount = StackCount;
			Context.SourceSlotId = Item->GetContainerId();
			Context.SourceIndex = Item->GetGridIndex();
			SubmitInventoryOperation(Context);
		}
		else
		{
			Comp->Server_DropItem(Item, StackCount);
		}
	}

	// Keep confirmed fields and occupied slots until the server accepts and refreshes them.
	// In standalone the widget and server share Item; clearing it here would corrupt a rejected drop.
	ClearHoverItem();

	// 刷新放在最后：等服务器处理完（含拒绝）再按权威数据重铺
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get())
	{
		Comp->RequestAuthoritativeRefresh();
	}
}

void UINV_InventoryGrid::SuppressQuickEquipForItem(UINV_InventoryItem* Item, float DurationSeconds)
{
	SuppressedQuickEquipItem = Item;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	SuppressedQuickEquipUntil = Now + FMath::Max(0.0f, DurationSeconds);
}

void UINV_InventoryGrid::OnSlottedItemClicked(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	// 左键已被拖拽接管，这里只处理右键：打开物品操作菜单
	if (!IsRightClicked(MouseEvent)) return;
	if (!GridSlots.IsValidIndex(GridIndex)) return;

	// 拖拽进行中不弹菜单（悬持物品时右键会打断拖拽状态）
	if (HasHoverItem() || HasExternalDragPayload()) return;

	UINV_InventoryItem* ClickedInventoryItem = GridSlots[GridIndex]->GetInventoryItem().Get();
	if (!IsValid(ClickedInventoryItem)) return;

	UINV_InventoryStatics::ItemUnHovered(GetOwningPlayer());

	CreateItemPopUp(GridIndex);
}

void UINV_InventoryGrid::OnSlottedItemDoubleClicked(int32 GridIndex)
{
	if (!GridSlots.IsValidIndex(GridIndex)) return;

	// 拖拽进行中不响应双击（悬持物品 / 外部拖拽时状态不干净）
	if (HasHoverItem() || HasExternalDragPayload()) return;

	UINV_GridSlot* GridSlot = GridSlots[GridIndex];
	if (!IsValid(GridSlot)) return;

	UINV_InventoryItem* DoubleClickedItem = GridSlot->GetInventoryItem().Get();
	if (!IsValid(DoubleClickedItem)) return;
	OnItemDoubleClicked.Broadcast(DoubleClickedItem, GridIndex);
	if (bSourceInteractionMode) return;

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (SuppressedQuickEquipItem.Get() == DoubleClickedItem)
	{
		const bool bSuppressed = Now <= SuppressedQuickEquipUntil;
		SuppressedQuickEquipItem = nullptr;
		SuppressedQuickEquipUntil = 0.0;
		if (bSuppressed) return;
	}

	// 该格已经有操作菜单打开：先让菜单自己处理，避免两个入口互相打断
	if (IsValid(GridSlot->GetItemPopUp())) return;

	UINV_InventoryStatics::ItemUnHovered(GetOwningPlayer());

	// 1) 装备类（非堆叠 + Equippable）：装到类型匹配的装备槽，槽里已有装备则换装
	const bool bEquippable = !DoubleClickedItem->IsStackable()
		&& DoubleClickedItem->GetItemManifest().GetItemCategory() == EINV_ItemCategory::Equippable;

	if (bEquippable)
	{
		UINV_SpatialInventory* SpatialInventory =
			Cast<UINV_SpatialInventory>(UINV_InventoryStatics::GetInventoryWidget(GetOwningPlayer()));
		if (IsValid(SpatialInventory) && SpatialInventory->TryQuickEquipItem(DoubleClickedItem))
		{
			return;
		}
	}

	// 2) 消耗类：走与右键菜单「使用」相同的流程
	if (DoubleClickedItem->IsConsumable())
	{
		OnPopUpMenuConsume(GridIndex);
	}
}



UINV_SlottedItem* UINV_InventoryGrid::CreateSlottedItem(UINV_InventoryItem* Item, const int32 Index,
                                                        const bool bStackable, const int32 StackAmount, const FINV_GridFragment* GridFragment,
                                                        const FINV_ImageFragment* ImageFragment)
{
	UINV_SlottedItem* SlottedItem = CreateWidget<UINV_SlottedItem>(GetOwningPlayer(), SlottedItemClass);
	SetSlottedItemImage(SlottedItem, GridFragment, ImageFragment);
	SlottedItem->SetInventoryItem(Item);
	// 记录实际占地（含朝向），供外部读取/调试
	SlottedItem->SetGridDimensions(Item->GetItemDimension());
	// 朝向分两层施加，都在 AddSlottedItemToCanvas 里（那里才知道画布槽位的尺寸与坐标）：
	//   布局层：给 Image 的 CanvasPanelSlot 设「未旋转尺寸」+ 居中偏移；
	//   渲染层：SetRotationVisual 只把 Image 绕自身中心转 90°。
	SlottedItem->SetGridIndex(Index);
	SlottedItem->SetIsStackable(bStackable);
	SlottedItem->UpdateStackCount(bStackable ? StackAmount : 0);
	
	// 右键点击图标 → 打开物品操作菜单（拆分 / 丢弃 / 使用）
	SlottedItem->OnSlottedItemClicked.AddDynamic(this, &ThisClass::OnSlottedItemClicked);

	// 左键双击图标 → 快速装备 / 快速使用
	SlottedItem->OnSlottedItemDoubleClicked.AddDynamic(this, &ThisClass::OnSlottedItemDoubleClicked);

	SlottedItem->OnSlottedItemDragStart.AddDynamic(this, &ThisClass::OnSlottedItemDragStart);
	SlottedItem->OnSlottedItemDropped.AddDynamic(this, &ThisClass::OnSlottedItemDropped);
	return SlottedItem;
}

void UINV_InventoryGrid::OnSlottedItemDragStart(int32 GridIndex, UINV_ItemPayload* Payload)
{
	if (!bProcessingDrop) InvalidateDropQuery();
	if (!GridSlots.IsValidIndex(GridIndex) || !IsValid(GridSlots[GridIndex])) return;
	
	UINV_InventoryItem* Item = GridSlots[GridIndex]->GetInventoryItem().Get(); 
	if (!IsValid(Item)) return;
	
	AssignHoverItem(Item, GridIndex,GridIndex);
	BindDragCancellation(Payload);

	// 把拖拽跟随图元的引用搬过来：网格自己那个 ItemPayload 与拖拽 Operation 里的
	// Payload 是两个对象，不搬的话按 R 只能转高亮、手里那张图不动。
	if (IsValid(Payload) && IsValid(ItemPayload))
	{
		ItemPayload->SetDragVisual(Payload->GetDragVisual());
		Payload->SourceGrid = this;
		Payload->SourceInventory = InventoryComponent;
		ItemPayload->SourceGrid = this;
		ItemPayload->SourceInventory = InventoryComponent;
		DragOperationPayload = Payload;
	}
}

bool UINV_InventoryGrid::IsHoverRotationChanged() const
{
	// 两层分派的唯一判据：**放置前后朝向是否改变**。
	// 比较「物品当前持久朝向」与「拖拽中的临时朝向」——
	// Payload 的 bRotated 只在拖拽中按 R 时翻转，落位时才写回物品（见 ApplyPayloadRotationToItem）。
	const UINV_ItemPayload* Payload = GetActivePayload();
	if (!IsValid(Payload)) return false;

	const UINV_InventoryItem* Item = Payload->GetInventoryItem();
	if (!IsValid(Item) || !Item->CanRotate()) return false;

	return Item->IsRotated() != Payload->IsRotated();
}

void UINV_InventoryGrid::PlaceHoverItemAtDropIndex()
{
	if (!IsDropQueryCurrent())
	{
		InvalidateDropQuery();
		return;
	}
	if (TryHandleStackDrop()) return;
	// 「放置」的唯一入口：物品在 Payload 上，落点已由高亮阶段算好（ItemDropIndex）。
	// 两个回调（OnSlottedItemDropped / OnGridSlotDropped）只保留各自的前置守卫，都汇到这里。
	//
	// 第一层分派：**放置前后朝向是否改变**
	//   朝向不变 → 主物品放置前后占地相同，落点与挤压基准可以用同一套尺寸；
	//   朝向改变 → 占地 X/Y 互换，目标区形状、邻居该往哪推都可能不同，单独一条路径。
	// 第二层分派：两个维度内各自再按「是否与自身重叠」→「目标区里是 0 / 1 / 多件物品」分派。
	if (IsHoverRotationChanged())
	{
		PlaceHoverItemWithRotationChanged();
	}
	else
	{
		PlaceHoverItemWithSameRotation();
	}
}

bool UINV_InventoryGrid::TryHandleStackDrop()
{
	if (!IsDropQueryCurrent() || !IsValid(ItemPayload) || CurrentQueryResult.bIsOverlap
		|| !CurrentQueryResult.EmptyIndices.IsEmpty() || CurrentQueryResult.SpaceItemsMap.Num() != 1) return false;

	const int32 SourceIndex = ItemPayload->GetPreviousGridIndex();
	UINV_InventoryItem* SourceItem = ItemPayload->GetInventoryItem();
	const auto Target = CurrentQueryResult.SpaceItemsMap.begin();
	UINV_InventoryItem* TargetItem = Target->Key.Get();
	const int32 TargetIndex = Target->Value;
	if (!GridSlots.IsValidIndex(SourceIndex) || !GridSlots.IsValidIndex(TargetIndex)
		|| GridSlots[SourceIndex]->GetInventoryItem().Get() != SourceItem
		|| GridSlots[TargetIndex]->GetInventoryItem().Get() != TargetItem
		|| SourceItem == TargetItem || !IsSameStackable(TargetItem)) return false;

	// 查询已验证完整覆盖，数量合并无需搬动物品，也无需把临时朝向写回原堆。
	const FINV_StackableFragment* Stackable = TargetItem->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>();
	const int32 TargetCount = GridSlots[TargetIndex]->GetStackCount();
	const int32 SourceCount = ItemPayload->GetStackCount();
	if (!Stackable || Stackable->GetMaxStackSize() <= 0 || SourceCount <= 0 || TargetCount < 0) return true;
	const int32 MaxCount = Stackable->GetMaxStackSize();
	const int32 Room = MaxCount - TargetCount;
	if (ShouldSwapStackCounts(Room, SourceCount, MaxCount))
	{
		SwapStackCounts(TargetCount, SourceCount, TargetIndex);
	}
	else if (ShouldConsumeStackCounts(Room, SourceCount))
	{
		ConsumeHoverItemStacks(TargetCount, SourceCount, TargetIndex);
	}
	else if (ShouldFillInStack(Room, SourceCount))
	{
		FillInStack(Room, SourceCount - Room, TargetIndex);
	}
	return true;
}

void UINV_InventoryGrid::PlaceHoverItemWithRotationChanged()
{
	// 维度二：主物品朝向改变。重叠且有其他物品的情况已由 CheckHoverPosition 灰显；
	// 此处处理仅与自身重叠的直接落位，以及新旧区域分离时的重排。
	if (!CurrentQueryResult.bHasSpace || !IsValid(ItemPayload)) return;

	UINV_InventoryItem* HoverItem = ItemPayload->GetInventoryItem();
	const int32 SourceIndex = ItemPayload->GetPreviousGridIndex();
	if (!IsValid(HoverItem) || !GridSlots.IsValidIndex(SourceIndex) || !GridSlots.IsValidIndex(ItemDropIndex)
		|| GridSlots[SourceIndex]->GetInventoryItem().Get() != HoverItem) return;

	const FIntPoint OldDim = HoverItem->GetItemDimension();
	const FIntPoint NewDim = ItemPayload->GetGridDimensions();
	if (OldDim.X <= 0 || OldDim.Y <= 0 || NewDim != FIntPoint(OldDim.Y, OldDim.X)) return;

	const FIntPoint SourcePos = UINV_WidgetUtils::GetPositionFromIndex(SourceIndex, Columns);
	const FIntPoint DropPos = UINV_WidgetUtils::GetPositionFromIndex(ItemDropIndex, Columns);
	if (!IsLandingInBounds(SourcePos, OldDim) || !IsLandingInBounds(DropPos, NewDim)) return;
	if (CurrentQueryResult.bIsOverlap)
	{
		RelocateHoverItemToDropIndex();
		return;
	}

	if (CurrentQueryResult.SpaceItemsMap.IsEmpty())
	{
		RelocateHoverItemToDropIndex();
		return;
	}
	if (!IsValid(InventoryComponent.Get())) return;

	struct FRotatedLanding
	{
		UINV_InventoryItem* Item = nullptr;
		int32 FromIndex = INDEX_NONE;
		int32 ToIndex = INDEX_NONE;
		int32 StackCount = 0;
		FIntPoint Position = FIntPoint::ZeroValue;
		FIntPoint Dimension = FIntPoint::ZeroValue;
	};
	TArray<FRotatedLanding> Landings;
	TArray<UINV_InventoryItem*> GuardItems;
	TArray<FIntPoint> GuardPositions;
	TArray<FIntPoint> GuardDimensions;
	// 两次相反的旋转必须使用互为逆变换的落点映射，否则换回原朝向时邻居无法回到原格。
	const bool bRotatedAfterDrop = ItemPayload->IsRotated();

	for (const auto& Entry : CurrentQueryResult.SpaceItemsMap)
	{
		UINV_InventoryItem* Item = Entry.Key.Get();
		const int32 FromIndex = Entry.Value;
		if (!IsValid(Item) || Item == HoverItem || !GridSlots.IsValidIndex(FromIndex)
			|| GridSlots[FromIndex]->GetInventoryItem().Get() != Item) return;

		const FIntPoint BeforeDim = Item->GetItemDimension();
		const FIntPoint BeforePos = UINV_WidgetUtils::GetPositionFromIndex(FromIndex, Columns);
		if (BeforeDim.X <= 0 || BeforeDim.Y <= 0 || BeforePos.X < DropPos.X || BeforePos.Y < DropPos.Y
			|| BeforePos.X + BeforeDim.X > DropPos.X + NewDim.X
			|| BeforePos.Y + BeforeDim.Y > DropPos.Y + NewDim.Y) return;

		// 目标区映回旧占区：转到旋转态用顺时针，转回原态用逆时针。
		// 半开矩形的整数变换避免中心点的 .5 舍入；两种公式互为逆变换。
		const FIntPoint RelativePos = BeforePos - DropPos;
		const FIntPoint RotatedPos = bRotatedAfterDrop
			? FIntPoint(SourcePos.X + OldDim.X - RelativePos.Y - BeforeDim.Y,
				SourcePos.Y + RelativePos.X)
			: FIntPoint(SourcePos.X + RelativePos.Y,
				SourcePos.Y + OldDim.Y - RelativePos.X - BeforeDim.X);
		const FIntPoint RotatedDim(BeforeDim.Y, BeforeDim.X);
		if (!IsLandingInBounds(RotatedPos, RotatedDim)) return;

		FRotatedLanding& Landing = Landings.AddDefaulted_GetRef();
		Landing.Item = Item;
		Landing.FromIndex = FromIndex;
		Landing.ToIndex = UINV_WidgetUtils::GetIndexFromPosition(RotatedPos, Columns);
		Landing.StackCount = GridSlots[FromIndex]->GetStackCount();
		Landing.Position = RotatedPos;
		Landing.Dimension = RotatedDim;
		GuardItems.Add(Item);
		GuardPositions.Add(RotatedPos);
		GuardDimensions.Add(RotatedDim);
	}

	if (!ValidateDisplacedLandings(TEXT("旋转重排取消"), TEXT("旋转落点"),
		GuardItems, GuardPositions, GuardDimensions, DropPos, NewDim)) return;

	// 每个新位置都在旧占区内，旧占区此时只能由主物品占据；异常占用时不做任何本地修改。
	for (const FRotatedLanding& Landing : Landings)
	{
		bool bSourceRegionClear = true;
		UINV_InventoryStatics::Foreach2D(GridSlots, Landing.ToIndex, Landing.Dimension, Columns,
			[&](const UINV_GridSlot* GridSlot)
		{
			if (GridSlot->GetInventoryItem().Get() != HoverItem) bSourceRegionClear = false;
		});
		if (!bSourceRegionClear) return;
	}

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		TArray<UINV_InventoryItem*> SubmitItems = GuardItems;
		TArray<int32> SubmitIndices;
		TArray<bool> SubmitRotations;
		SubmitIndices.Reserve(Landings.Num() + 1);
		SubmitRotations.Reserve(Landings.Num() + 1);
		for (const FRotatedLanding& Landing : Landings)
		{
			SubmitIndices.Add(Landing.ToIndex);
			SubmitRotations.Add(Landing.Item->CanRotate() && !Landing.Item->IsRotated());
		}
		SubmitItems.Add(HoverItem);
		SubmitIndices.Add(ItemDropIndex);
		SubmitRotations.Add(ItemPayload->IsRotated());
		FINV_InteractionContext RequestContext(GetOwningPlayer(), EINV_InteractionType::BatchMove);
		RequestContext.TargetSlotId = SlotId;
		RequestContext.BatchItems = SubmitItems;
		RequestContext.BatchTargetIndices = SubmitIndices;
		RequestContext.BatchRotatedStates = SubmitRotations;
		SubmitInventoryOperation(RequestContext);
		ClearHoverItem();
		return;
	}

	// 先按各自旧朝向清空整批，再旋转并铺到新位置；逐件边清边铺会擦掉前一件的结果。
	RemoveItemFromGrid(HoverItem, SourceIndex);
	for (const FRotatedLanding& Landing : Landings) RemoveItemFromGrid(Landing.Item, Landing.FromIndex);
	ApplyPayloadRotationToItem();

	TArray<UINV_InventoryItem*> MovedItems;
	TArray<int32> MovedIndices;
	for (const FRotatedLanding& Landing : Landings)
	{
		if (Landing.Item->CanRotate()) Landing.Item->SetRotated(!Landing.Item->IsRotated());
		AddItemAtIndex(Landing.Item, Landing.ToIndex, Landing.Item->IsStackable(), Landing.StackCount);
		UpdateGridSlots(Landing.Item, Landing.ToIndex, Landing.Item->IsStackable(), Landing.StackCount);
		MovedItems.Add(Landing.Item);
		MovedIndices.Add(Landing.ToIndex);
	}

	PutDownOnIndex(ItemDropIndex, /*bReportToServer=*/false);
	CommitBatchMove(HoverItem, MovedItems, MovedIndices, TEXT("（旋转重排）"));
}

void UINV_InventoryGrid::PlaceHoverItemWithSameRotation()
{
	// 数量操作已在分派前处理；朝向不变时，空区/仅自身直接落位，其他目标走交换或重排。
	// 所有位置、尺寸和对称轴都用整数表达，校验与实际搬运消费同一份规划结果。
	if (!CurrentQueryResult.bHasSpace || !IsValid(ItemPayload)) return;

	UINV_InventoryItem* HoverItem = ItemPayload->GetInventoryItem();
	const int32 SourceIndex = ItemPayload->GetPreviousGridIndex();
	if (!IsValid(HoverItem) || !GridSlots.IsValidIndex(SourceIndex) || !GridSlots.IsValidIndex(ItemDropIndex)
		|| GridSlots[SourceIndex]->GetInventoryItem().Get() != HoverItem) return;
	if (SourceIndex == ItemDropIndex) return;

	const FIntPoint HoverDim = HoverItem->GetItemDimension();
	const FIntPoint SourcePos = UINV_WidgetUtils::GetPositionFromIndex(SourceIndex, Columns);
	const FIntPoint DropPos = UINV_WidgetUtils::GetPositionFromIndex(ItemDropIndex, Columns);
	if (HoverDim.X <= 0 || HoverDim.Y <= 0 || ItemPayload->GetGridDimensions() != HoverDim
		|| !IsLandingInBounds(SourcePos, HoverDim) || !IsLandingInBounds(DropPos, HoverDim)) return;

	if (CurrentQueryResult.SpaceItemsMap.IsEmpty()
		|| (CurrentQueryResult.SpaceItemsMap.Num() == 1 && CurrentQueryResult.SpaceItemsMap.Contains(HoverItem)))
	{
		RelocateHoverItemToDropIndex();
		return;
	}

	const bool bOverlap = CurrentQueryResult.bIsOverlap;
	if (!bOverlap && CurrentQueryResult.SpaceItemsMap.Num() == 1)
	{
		const auto FirstEntry = CurrentQueryResult.SpaceItemsMap.begin();
		UINV_InventoryItem* ClickedItem = FirstEntry->Key.Get();
		const int32 ClickedIndex = FirstEntry->Value;
		if (!IsValid(ClickedItem) || !GridSlots.IsValidIndex(ClickedIndex)
			|| GridSlots[ClickedIndex]->GetInventoryItem().Get() != ClickedItem) return;

		// 完整覆盖的同类堆叠已由分派前的 TryHandleStackDrop 处理。
		SwapWithHoverItem(ClickedItem, ClickedIndex, ItemDropIndex);
		return;
	}

	if (!IsValid(InventoryComponent.Get())) return;
	const FIntPoint MoveVector = SourcePos - DropPos;
	const bool bMoveX = SourcePos.X != DropPos.X;
	const bool bMoveY = SourcePos.Y != DropPos.Y;
	// 等尺寸矩形的重叠区：左/右半开边界之和 = 源坐标 + 目标坐标 + 尺寸。
	// 对称后的左上角 = 两倍对称中心 - 原左上角 - 物品尺寸；无需除以 2。
	const FIntPoint ReflectionSum = SourcePos + DropPos + HoverDim;
	const TCHAR* Context = bOverlap ? TEXT("挤压取消") : TEXT("批量交换取消");
	const TCHAR* LandingWord = bOverlap ? TEXT("对称落点") : TEXT("位移落点");

	TArray<UINV_InventoryItem*> MovedItems;
	TArray<int32> FromIndices;
	TArray<int32> StackCounts;
	TArray<FIntPoint> LandingPositions;
	TArray<FIntPoint> Dimensions;
	for (const auto& Entry : CurrentQueryResult.SpaceItemsMap)
	{
		UINV_InventoryItem* Item = Entry.Key.Get();
		if (Item == HoverItem) continue;
		const int32 FromIndex = Entry.Value;
		if (!IsValid(Item) || !GridSlots.IsValidIndex(FromIndex)
			|| GridSlots[FromIndex]->GetInventoryItem().Get() != Item) return;

		const FIntPoint Dim = Item->GetItemDimension();
		const FIntPoint BeforePos = UINV_WidgetUtils::GetPositionFromIndex(FromIndex, Columns);
		if (Dim.X <= 0 || Dim.Y <= 0 || !IsLandingInBounds(BeforePos, Dim)) return;
		FIntPoint LandingPos = BeforePos + MoveVector;
		if (bOverlap)
		{
			LandingPos = BeforePos;
			if (bMoveX) LandingPos.X = ReflectionSum.X - BeforePos.X - Dim.X;
			if (bMoveY) LandingPos.Y = ReflectionSum.Y - BeforePos.Y - Dim.Y;
		}

		MovedItems.Add(Item);
		FromIndices.Add(FromIndex);
		StackCounts.Add(GridSlots[FromIndex]->GetStackCount());
		LandingPositions.Add(LandingPos);
		Dimensions.Add(Dim);
	}
	if (MovedItems.IsEmpty()
		|| !ValidateDisplacedLandings(Context, LandingWord, MovedItems, LandingPositions, Dimensions, DropPos, HoverDim)) return;

	// 提前排除组外占用；到这里尚未清格，任何拒绝都直接保留原状态。
	TSet<const UINV_InventoryItem*> MovingSet;
	MovingSet.Add(HoverItem);
	for (const UINV_InventoryItem* Item : MovedItems) MovingSet.Add(Item);
	auto FindExternalOccupant = [&](const int32 Index, const FIntPoint& Dim) -> UINV_InventoryItem*
	{
		UINV_InventoryItem* Blocker = nullptr;
		UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dim, Columns, [&](const UINV_GridSlot* GridSlot)
		{
			UINV_InventoryItem* Occupant = GridSlot->GetInventoryItem().Get();
			if (IsValid(Occupant) && !MovingSet.Contains(Occupant)) Blocker = Occupant;
		});
		return Blocker;
	};
	if (IsValid(FindExternalOccupant(ItemDropIndex, HoverDim))) return;
	TArray<int32> MovedIndices;
	for (int32 I = 0; I < MovedItems.Num(); ++I)
	{
		// 坐标级校验已通过，此处才转换为一维索引，避免负坐标回卷。
		const int32 ToIndex = UINV_WidgetUtils::GetIndexFromPosition(LandingPositions[I], Columns);
		if (UINV_InventoryItem* Blocker = FindExternalOccupant(ToIndex, Dimensions[I]); IsValid(Blocker))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inventory] %s：%s 的%s被组外物品 %s 占用，整批取消。"),
				Context, *GetNameSafe(MovedItems[I]), LandingWord, *GetNameSafe(Blocker));
			return;
		}
		MovedIndices.Add(ToIndex);
	}

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		TArray<UINV_InventoryItem*> SubmitItems = MovedItems;
		TArray<int32> SubmitIndices = MovedIndices;
		TArray<bool> SubmitRotations;
		SubmitRotations.Reserve(SubmitItems.Num() + 1);
		for (const UINV_InventoryItem* Item : SubmitItems)
		{
			SubmitRotations.Add(IsValid(Item) && Item->IsRotated());
		}
		SubmitItems.Add(HoverItem);
		SubmitIndices.Add(ItemDropIndex);
		SubmitRotations.Add(ItemPayload->IsRotated());
		FINV_InteractionContext RequestContext(GetOwningPlayer(), EINV_InteractionType::BatchMove);
		RequestContext.TargetSlotId = SlotId;
		RequestContext.BatchItems = SubmitItems;
		RequestContext.BatchTargetIndices = SubmitIndices;
		RequestContext.BatchRotatedStates = SubmitRotations;
		SubmitInventoryOperation(RequestContext);
		ClearHoverItem();
		return;
	}

	// 先清整批旧占格，再铺整批新格；落点可能覆盖其他组员的旧格，逐件搬运会擦掉已铺结果。
	DetachHoverItemFromSourceGrid();
	for (int32 I = 0; I < MovedItems.Num(); ++I) RemoveItemFromGrid(MovedItems[I], FromIndices[I]);
	for (int32 I = 0; I < MovedItems.Num(); ++I)
	{
		const bool bStackable = MovedItems[I]->IsStackable();
		AddItemAtIndex(MovedItems[I], MovedIndices[I], bStackable, StackCounts[I]);
		UpdateGridSlots(MovedItems[I], MovedIndices[I], bStackable, StackCounts[I]);
	}
	PutDownOnIndex(ItemDropIndex, /*bReportToServer=*/false);
	CommitBatchMove(HoverItem, MovedItems, MovedIndices, bOverlap ? TEXT("（挤压）") : TEXT(""));
}

void UINV_InventoryGrid::DetachHoverItemFromSourceGrid()
{
	if (!IsValid(ItemPayload)) return;

	// 按**旧朝向**把物品从原格清干净，之后再才会写回新朝向（见 ApplyPayloadRotationToItem）。
	// 顺序反了就会「按新尺寸清旧位置」，留下半格残留占用 —— 这是 #18 踩过的坑。
	RemoveItemFromGrid(ItemPayload->GetInventoryItem(), ItemPayload->GetPreviousGridIndex());
	ApplyPayloadRotationToItem();
}

void UINV_InventoryGrid::RelocateHoverItemToDropIndex()
{
	// 直接落位的最后一道护栏：绝不能清源后覆盖另一个物品的占格或图标。
	if (!IsValid(ItemPayload)
		|| !CanPlaceItemAtIndexIgnoring(ItemDropIndex, ItemPayload->GetGridDimensions(), ItemPayload->GetInventoryItem())) return;

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 悲观提交：服务器先处理移动，客户端保留确认态和占格，收到结果后再重铺。
		UINV_InventoryItem* Item = ItemPayload->GetInventoryItem();
		const int32 SourceIndex = ItemPayload->GetPreviousGridIndex();
		if (IsValid(Item) && GridSlots.IsValidIndex(SourceIndex))
		{
			FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Move);
			Context.Item = Item;
			Context.SourceSlotId = SlotId;
			Context.SourceIndex = SourceIndex;
			Context.TargetSlotId = SlotId;
			Context.TargetIndex = ItemDropIndex;
			Context.bRotated = ItemPayload->IsRotated();
			SubmitInventoryOperation(Context);
			ClearHoverItem();
		}
		return;
	}

	// 无挤压的直接落位：清原格 → 写朝向 → 落到 ItemDropIndex
	DetachHoverItemFromSourceGrid();
	PutDownOnIndex(ItemDropIndex);
}

bool UINV_InventoryGrid::ValidateDisplacedLandings(const TCHAR* Context, const TCHAR* LandingWord,
	const TArray<UINV_InventoryItem*>& Items, const TArray<FIntPoint>& Positions,
	const TArray<FIntPoint>& Dimensions, const FIntPoint& MainPos, const FIntPoint& MainDim) const
{
	check(Items.Num() == Positions.Num());
	check(Items.Num() == Dimensions.Num());

	// 越界必须按坐标判：负坐标经 GetIndexFromPosition 会被回卷成合法索引，用索引判越界会漏。
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		if (!IsLandingInBounds(Positions[I], Dimensions[I]))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] %s：%s %s (%d,%d) 越界，整批回退。"),
				Context, *GetNameSafe(Items[I]), LandingWord, Positions[I].X, Positions[I].Y);
			return false;
		}
	}

	// 主物品无效或落点非法时（MainDim 为 0）只校验越界，跳过重叠校验 —— 与原实现一致
	if (MainDim.X <= 0 || MainDim.Y <= 0) return true;

	// 本批内重叠：每件与主物品的目标区，以及本批各件之间。
	// 与**组外**物品的重叠不在这里管：那部分由服务器 Server_MoveItems 硬拒绝。
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		if (DoRectsOverlap(Positions[I], Dimensions[I], MainPos, MainDim))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Inventory] %s：%s 落点 (%d,%d) 与主物品目标区重叠，整批回退。"),
				Context, *GetNameSafe(Items[I]), Positions[I].X, Positions[I].Y);
			return false;
		}

		for (int32 J = I + 1; J < Items.Num(); ++J)
		{
			if (DoRectsOverlap(Positions[I], Dimensions[I], Positions[J], Dimensions[J]))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Inventory] %s：%s 与 %s 的落点重叠，整批回退。"),
					Context, *GetNameSafe(Items[I]), *GetNameSafe(Items[J]));
				return false;
			}
		}
	}

	return true;
}

void UINV_InventoryGrid::CommitBatchMove(UINV_InventoryItem* HoverItem,
	TArray<UINV_InventoryItem*>& MovedItems, TArray<int32>& MovedIndices, const TCHAR* LogSuffix)
{
	// PutDownOnIndex 已清空 Payload；主物品由调用方提前保留，确保它也进入服务器的 MovingSet。
	// 服务器上它还记着旧 GridIndex，不入组会把邻居的新位置误判成组外重叠。
	if (IsValid(HoverItem))
	{
		MovedItems.Add(HoverItem);
		MovedIndices.Add(ItemDropIndex);
	}

	UINV_InventoryComponent* Comp = InventoryComponent.Get();
	if (!IsValid(Comp) || MovedItems.Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory] 批量移动未提交%s：MovedItems=%d，组件有效=%d"),
			LogSuffix, MovedItems.Num(), IsValid(Comp) ? 1 : 0);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Inventory] 提交批量移动%s：网格=%s，%d 件（含主物品）"),
		LogSuffix, *SlotId.ToString(), MovedItems.Num());

	// 朝向随批次一起提交：主物品可能是拖拽中按 R 转过的那件，被推开的邻居保持各自原朝向。
	TArray<bool> MovedRotations;
	MovedRotations.Reserve(MovedItems.Num());
	for (const UINV_InventoryItem* MovedItem : MovedItems)
	{
		MovedRotations.Add(IsValid(MovedItem) && MovedItem->IsRotated());
	}

	Comp->Server_MoveItems(MovedItems, MovedIndices, SlotId, MovedRotations);
	Comp->RequestAuthoritativeRefresh();
}

void UINV_InventoryGrid::InvalidateDropQuery()
{
	ItemDropIndex = INDEX_NONE;
	CurrentQueryResult = FINV_SpaceQueryResult();
}

void UINV_InventoryGrid::BindDragCancellation(UINV_ItemPayload* Payload)
{
	if (UDragDropOperation* Operation = IsValid(Payload) ? Cast<UDragDropOperation>(Payload->GetOuter()) : nullptr)
	{
		Operation->OnDragCancelled.AddUniqueDynamic(this, &ThisClass::OnDragOperationCancelled);
	}
}

void UINV_InventoryGrid::OnDragOperationCancelled(UDragDropOperation* Operation)
{
	const UINV_ItemPayload* CancelledPayload = IsValid(Operation) ? Cast<UINV_ItemPayload>(Operation->Payload) : nullptr;
	const UINV_ItemPayload* ActivePayload = GetActivePayload();
	if (!IsValid(CancelledPayload) || !IsValid(ActivePayload)
		|| CancelledPayload->GetInventoryItem() != ActivePayload->GetInventoryItem()) return;
	UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
	ClearHoverItem();
	ClearExternalDragPayload();
}

bool UINV_InventoryGrid::IsDropQueryCurrent() const
{
	const UINV_ItemPayload* ActivePayload = GetActivePayload();
	return CurrentQueryResult.bHasSpace && IsValid(ActivePayload)
		&& IsValid(ActivePayload->GetInventoryItem())
		&& CurrentQueryResult.CurrentItem.Get() == ActivePayload->GetInventoryItem()
		&& GridSlots.IsValidIndex(ItemDropIndex);
}

bool UINV_InventoryGrid::RefreshDropQuery(UINV_ItemPayload* Payload)
{
	InvalidateDropQuery();
	if (!IsValid(Payload) || !IsValid(Payload->GetInventoryItem())) return false;
	const bool bCrossInventory = Payload->SourceInventory.IsValid()
		&& Payload->SourceInventory.Get() != InventoryComponent.Get();
	if (Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot || bCrossInventory)
	{
		ExternalDragPayload = Payload;
	}
	const UINV_ItemPayload* ActivePayload = GetActivePayload();
	// 网格内部 Payload 与拖放操作的 Payload 可以是不同对象，临时旋转也可以不同。
	if (!IsValid(ActivePayload) || ActivePayload->GetInventoryItem() != Payload->GetInventoryItem()
		|| ActivePayload->GetDragSource() != Payload->GetDragSource()) return false;
	const bool bExternalPayload = ActivePayload == ExternalDragPayload;
	if ((!bExternalPayload && !HasHoverItem()) || !IsValid(CanvasPanel) || !IsValid(GetOwningPlayer())
		|| Columns <= 0 || Rows <= 0 || TileSize <= 0.f) return false;

	const FVector2D CanvasPos = UINV_WidgetUtils::GetWidgetPosition(CanvasPanel);
	const FVector2D MousePos = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	bMouseWithinCanvas = UINV_WidgetUtils::IsWithinBounds(CanvasPos, UINV_WidgetUtils::GetWidgetSize(CanvasPanel), MousePos);
	if (!bMouseWithinCanvas) return false;

	// 高亮 Tick 与松手事件可能不在同一帧；这里重新计算落点和占用，之后才决定合并/交换/移动。
	UpdateTileParameters(CanvasPos, MousePos);
	return IsDropQueryCurrent();
}

void UINV_InventoryGrid::OnSlottedItemDropped(int32 GridIndex, UINV_ItemPayload* Payload)
{
	if (HandleContainerDrop(Payload, GridIndex)) return;
	TGuardValue<bool> ProcessingGuard(bProcessingDrop, true);
	ON_SCOPE_EXIT { InvalidateDropQuery(); };
	if (!RefreshDropQuery(Payload))
	{
		UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
		ClearExternalDragPayload();
		ClearHoverItem();
		return;
	}

	// 来源为装备槽：不参与网格内部交换，交给卸下链路处理（带上本网格，便于上游判断索引是否有意义）
	if (Payload && Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot)
	{
		// 高亮判定失败（类型不匹配 / 放不下）→ 不卸下，只清拖拽缓存，装备留在装备槽。
		// 之前这里直接广播卸下，导致灰显（bHasSpace=false）时松手仍触发快速卸下。
		if (!CurrentQueryResult.bHasSpace)
		{
			ClearExternalDragPayload();
			ClearHoverItem();
			return;
		}

		OnEquippedItemDroppedOnGrid.Broadcast(this, Payload, ItemDropIndex);
		// 兜底清理：无论上游处理成功还是失败，装备来源都不应在本网格留下拖拽缓存
		ClearExternalDragPayload();
		ClearHoverItem();
		return;
	}

	// 前置守卫：拖拽路径不会在「拿起」时移除源格，因此要求源格仍有物品。
	// 原代码直接 GridSlots[GridIndex] 下标访问，越界会崩，这里补了 IsValidIndex。
	if (GridSlots.IsValidIndex(GridIndex) && GridSlots[GridIndex]->GetInventoryItem().IsValid())
	{
		PlaceHoverItemAtDropIndex();
	}

	UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
	ClearHoverItem();
}



void UINV_InventoryGrid::AddSlottedItemToCanvas(const int32 Index, const FINV_GridFragment* GridFragment,
                                                UINV_SlottedItem* SlottedItem, const UINV_InventoryItem* Item) const
{
	if (!IsValid(Item) || !IsValid(SlottedItem)) return;

	CanvasPanel->AddChild(SlottedItem);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(SlottedItem);

	const FIntPoint OccupiedDim = Item->GetItemDimension();   // 含朝向：旋转过的物品是 X/Y 互换
	const bool bRotated = Item->IsRotated();
	const float GridPadding = GridFragment->GetGridPadding();

	// 容器盒 = 实际占地（旋转后尺寸）：这样热区、高亮与视觉三者边界一致。
	// 图不跟着盒拉伸 —— 布局尺寸由下面那段 CanvasPanelSlot 显式给（未旋转尺寸），
	// 朝向由 SetRotationVisual 只做旋转，两件事分开，互不影响。
	CanvasSlot->SetSize(GetDrawSizeForDims(OccupiedDim, GridPadding));

	// 位置直接按左上角格坐标，不需要再补偿：图标是「绕自身中心旋转」，
	// 视觉包围盒的中心与容器盒中心重合，转完正好盖住盒子。
	const FVector2D CellPos = UINV_WidgetUtils::GetPositionFromIndex(Index, Columns) * TileSize;
	const FVector2D DrawPos = CellPos + FVector2D(GridPadding);
	CanvasSlot->SetPosition(DrawPos);

	// 图标定位：容器盒是「旋转后尺寸」（如 162x108），而图元必须保持「未旋转尺寸」（108x162）
	// 再转 90° —— 两者高度不一致。若 Image 仍处在蓝图原来的 Overlay 里，会被父容器高度夹住
	// （实测 108x162 被压成 108x108，就是玩家看到的「图标被压缩」）。
	// 蓝图把 Image_Icon 放进 CanvasPanel 后，这里用 CanvasPanelSlot 给显式尺寸 + 居中偏移：
	// CanvasPanel 不限制子控件尺寸，图可以按原比例溢出容器，旋转完正好铺满。
	// 若蓝图还没改（Image 仍在 Overlay 里），Cast 失败会直接跳过，保持原行为，不会崩。
	if (UImage* IconImage = SlottedItem->GetImageIcon())
	{
		if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(IconImage->Slot))
		{
			const float IconUnit = TileSize - GridPadding * 2;
			const FIntPoint BaseDim = Item->GetBaseDimension();
			const FVector2D BaseSize(BaseDim.X * IconUnit, BaseDim.Y * IconUnit);
			const FVector2D OccupiedSize(OccupiedDim.X * IconUnit, OccupiedDim.Y * IconUnit);

			IconSlot->SetAutoSize(false);
			IconSlot->SetSize(BaseSize);
			IconSlot->SetPosition((OccupiedSize - BaseSize) * 0.5f);
		}
	}

	SlottedItem->SetRotationVisual(bRotated);
}

void UINV_InventoryGrid::UpdateGridSlots(UINV_InventoryItem* NewItem, const int32 Index, bool bStackable,
	const int32 StackAmount, bool bUpdateItemState)
{
	if (!IsValid(NewItem)) return;
	check(GridSlots.IsValidIndex(Index));

	// ===== 位置真相的唯一写入口 =====
	// 所有落位路径（AddItem / AddItemToIndeces / PutDownOnIndex / SwapWithHoverItem / 批量移动）
	// 最终都会走到这里，所以把「物品在哪」写在这一处就不会再出现
	//「UI 已经挪了、GridIndex 还停在旧格」的陈旧状态 —— 那种状态会让任何基于 GridIndex
	// 的判断静默失效（拆分校验就是这么被坑的，见待办 #11）。
	// 专用服务器下客户端这份仍会被服务器复制值覆盖，真正的双端权威要等 #3 让每次移动都走 RPC。
	// 容器归属与位置一起写：占用校验靠 ContainerId 过滤「别的容器」的物品（R1 根因）。
	if (bUpdateItemState)
	{
		NewItem->SetContainerId(SlotId);
		NewItem->SetGridIndex(Index);
		NewItem->SetEquippedSlotType(FGameplayTag());
	}

	if (bStackable)
	{
		GridSlots[Index]->SetStackCount(StackAmount);
	}
	const FINV_GridFragment* GridFragment = GetFragment<FINV_GridFragment>(NewItem, FragmentTags::GridFragment);
	// 同 RemoveItemFromGrid：占用格必须按「实际尺寸（含朝向）」打，不能用资产原始尺寸
	const FIntPoint Dimensions = NewItem->GetItemDimension();
	if (!GridFragment || Dimensions.X <= 0 || Dimensions.Y <= 0) return;
	
	
	UINV_InventoryStatics::Foreach2D(GridSlots, Index, Dimensions, Columns, [&](UINV_GridSlot* GridSlot)
	{
		GridSlot->SetInventoryItem(NewItem);
		GridSlot->SetUpperLeftIndex(Index);
		GridSlot->SetAvailable(false);
		GridSlot->SetOccupiedTexture();
	});
	
}

FVector2D UINV_InventoryGrid::GetDrawSize(const FINV_GridFragment* GridFragment) const 
{
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2;
	return GridFragment->GetGridSize() * IconTileWidth;
}

FVector2D UINV_InventoryGrid::GetDrawSizeForDims(const FIntPoint& Dimensions, float GridPadding) const
{
	const float IconTileWidth = TileSize - GridPadding * 2;
	return FVector2D(Dimensions.X * IconTileWidth, Dimensions.Y * IconTileWidth);
}

void UINV_InventoryGrid::SetSlottedItemImage(const UINV_SlottedItem* SlottedItem, const FINV_GridFragment* GridFragment,
	const FINV_ImageFragment* ImageFragment) const
{
	FSlateBrush Brush;
	Brush.SetResourceObject(ImageFragment->GetIcon());
	Brush.DrawAs = ESlateBrushDrawType::Image;
	// 注意：这里用**未旋转**的尺寸。旋转是由图标自己绕中心转 90° 实现的，
	// 若这里也按旋转后尺寸缩放，就会「先压扁再旋转」，图会变形。
	Brush.ImageSize = GetDrawSizeForDims(GridFragment->GetGridSize(), GridFragment->GetGridPadding());
	SlottedItem->SetImageBrush(Brush);
}

void UINV_InventoryGrid::ConstructGrid()
{
	GridSlots.Reserve(Rows * Columns);
	
	for (int32 j = 0; j < Rows; ++j)
	{
		for (int32 i = 0; i < Columns; ++i)
		{
			UINV_GridSlot* GridSlot = CreateWidget<UINV_GridSlot>(this, GridSlotsClass);

			CanvasPanel->AddChild(GridSlot);
			
			const FIntPoint TilePosition(i, j);
			GridSlot->SetTileIndex(UINV_WidgetUtils::GetIndexFromPosition(TilePosition, Columns));

			UCanvasPanelSlot* GridCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(GridSlot);
			GridCPS->SetSize(FVector2D(TileSize, TileSize));
			GridCPS->SetPosition(TilePosition * TileSize);

			GridSlots.Add(GridSlot);
			
			//GridSlot->GridSlotClicked.AddDynamic(this, &ThisClass::OnGridSlotClicked);
			GridSlot->GridSlotHovered.AddDynamic(this, &ThisClass::OnGridSlotHovered);
			GridSlot->GridSlotUnhovered.AddDynamic(this, &ThisClass::OnGridSlotUnhovered);
			GridSlot->GridSlotDropped.AddDynamic(this, &ThisClass::OnGridSlotDropped);
			GridSlot->GridSlotDragEnter.AddDynamic(this, &ThisClass::OnGridSlotDragEnter);
			GridSlot->GridSlotDragLeave.AddDynamic(this, &ThisClass::OnGridSlotDragLeave);
		}
	}
}

void UINV_InventoryGrid::OnGridSlotDragEnter(int32 GridIndex, UINV_ItemPayload* Payload)
{
	if (!bProcessingDrop && ExternalDragPayload != Payload) InvalidateDropQuery();
	const bool bCrossInventory = IsValid(Payload) && Payload->SourceInventory.IsValid()
		&& Payload->SourceInventory.Get() != InventoryComponent.Get();
	if (IsValid(Payload) && (Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot || bCrossInventory))
	{
		ExternalDragPayload = Payload;
		BindDragCancellation(Payload);
	}
	// The source grid keeps a local hover Payload while Slate carries a separate
	// operation Payload. Sync the live rotation before the target starts previewing;
	// otherwise a rotated player item is previewed and submitted with its old shape.
	if (IsValid(Payload) && Payload->SourceGrid.IsValid())
	{
		if (UINV_ItemPayload* SourceHover = Payload->SourceGrid->GetHoverItem();
			IsValid(SourceHover) && SourceHover->GetInventoryItem() == Payload->GetInventoryItem())
		{
			Payload->SetRotated(SourceHover->IsRotated());
			if (UWidget* DragVisual = Payload->GetDragVisual())
			{
				DragVisual->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
				DragVisual->SetRenderTransformAngle(Payload->IsRotated() ? 90.f : 0.f);
			}
		}
	}
}

void UINV_InventoryGrid::OnGridSlotDragLeave(int32 GridIndex, UINV_ItemPayload* Payload)
{
	// 拖出网格（仍在网格内部格子间移动也会触发 leave/enter，这里不做清理，
	// 由 NativeOnDragLeave 或放下成功后 ClearExternalDragPayload 统一清理）
}

void UINV_InventoryGrid::OnGridSlotDropped(int32 GridIndex, UINV_ItemPayload* Payload)
{
	if (HandleContainerDrop(Payload, GridIndex)) return;
	TGuardValue<bool> ProcessingGuard(bProcessingDrop, true);
	ON_SCOPE_EXIT { InvalidateDropQuery(); };
	if (!RefreshDropQuery(Payload))
	{
		UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
		ClearExternalDragPayload();
		ClearHoverItem();
		return;
	}

	// 来源为装备槽：不参与网格内部交换，交给卸下链路处理（带上本网格，便于上游判断索引是否有意义）
	if (Payload && Payload->GetDragSource() == EINV_ItemDragSource::EquippedSlot)
	{
		// 高亮判定失败（类型不匹配 / 放不下）→ 不卸下，只清拖拽缓存，装备留在装备槽。
		// 之前这里直接广播卸下，导致灰显（bHasSpace=false）时松手仍触发快速卸下。
		if (!CurrentQueryResult.bHasSpace)
		{
			ClearExternalDragPayload();
			ClearHoverItem();
			return;
		}

		OnEquippedItemDroppedOnGrid.Broadcast(this, Payload, ItemDropIndex);
		// 兜底清理：无论上游处理成功还是失败，装备来源都不应在本网格留下拖拽缓存
		ClearExternalDragPayload();
		ClearHoverItem();
		return;
	}

	// 前置守卫：从「悬持状态」落到空格 —— 手里必须有物品、落点有效、且不是原地放下。
	// 网格内拖拽（OnSlottedItemDropped）的守卫条件不同（要求源格仍有物品），
	// 因此守卫留在各自入口，公共方法只负责「落点已确定」之后的分派。
	if (HasHoverItem()
		&& GridSlots.IsValidIndex(ItemDropIndex)
		&& IsValid(ItemPayload)
		&& ItemDropIndex != ItemPayload->GetPreviousGridIndex())
	{
		PlaceHoverItemAtDropIndex();
	}

	UnhighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
	ClearHoverItem();
}


void UINV_InventoryGrid::OnGridSlotHovered(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	if (IsValid(ItemPayload)) return;
	
	UINV_GridSlot* GridSlot = GridSlots[GridIndex];
	if(GridSlot->IsAvailable())
	{
		GridSlot->SetOccupiedTexture();
	}
	
}

void UINV_InventoryGrid::OnGridSlotUnhovered(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	if (IsValid(ItemPayload)) return;
	UINV_GridSlot* GridSlot = GridSlots[GridIndex];
	if(GridSlot->IsAvailable())
	{
		GridSlot->SetUnoccupiedTexture();
	}
}

void UINV_InventoryGrid::OnPopUpMenuSplit(int32 SplitAmount, int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return;

	UINV_GridSlot* GridSlot = GridSlots[Index];
	if (!IsValid(GridSlot)) return;

	UINV_InventoryItem* Item = GridSlot->GetInventoryItem().Get();
	if (!IsValid(Item) || !Item->IsStackable()) return;

	const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex();
	if (!GridSlots.IsValidIndex(UpperLeftIndex)) return;

	UINV_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex];
	if (!IsValid(UpperLeftGridSlot)) return;

	const int32 SourceCount = UpperLeftGridSlot->GetStackCount();
	if (SplitAmount <= 0 || SplitAmount >= SourceCount) return;

	UINV_InventoryComponent* Comp = InventoryComponent.Get();
	if (!IsValid(Comp)) return;

	if (Comp->bServerAuthoritativeUI)
	{
		// 服务器权威模式：不在本地扣数量，也不预判落点。
		// 落点传 INDEX_NONE 让服务器在自己那份占据数据里挑 —— 客户端选的落点基于它自己
		// 那份（可能已陈旧的）占据数据，一旦两端不一致就会出现「客户端说空、服务器说被占」的假拒绝。
		FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Split);
		Context.Item = Item;
		Context.Amount = SplitAmount;
		Context.SourceSlotId = SlotId;
		Context.SourceIndex = UpperLeftIndex;
		Context.TargetSlotId = SlotId;
		Context.TargetIndex = INDEX_NONE;
		SubmitInventoryOperation(Context);
		return;
	}

	// 拆出来的堆叠要落到本页签的一片空地上。
	// 一个能完整放下的空位都没有 → 直接忽略，源堆叠保持原样。
	const int32 TargetIndex = FindFreeSlotForItem(Item);
	if (TargetIndex == INDEX_NONE) return;

	// 乐观 UI：源堆叠先减。新堆叠由服务器创建条目后复制回来，
	// AddItem 会按 Item->GetGridIndex() 直接落到 TargetIndex，不会叠回源堆叠。
	const int32 NewSourceCount = SourceCount - SplitAmount;
	UpperLeftGridSlot->SetStackCount(NewSourceCount);
	if (UINV_SlottedItem* SlottedItem = SlottedItems.FindRef(UpperLeftIndex); IsValid(SlottedItem))
	{
		SlottedItem->UpdateStackCount(NewSourceCount);
	}

	Comp->Server_SplitStack(Item, SlotId, UpperLeftIndex, SplitAmount, TargetIndex);
}

void UINV_InventoryGrid::OnPopUpMenuDrop(int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return;
	UINV_InventoryItem* Item = GridSlots[Index]->GetInventoryItem().Get();
	if (!IsValid(Item)) return;

	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); IsValid(Comp) && Comp->bServerAuthoritativeUI)
	{
		// 权威模式保留确认中的容器和索引，直接提交丢弃；服务器拒绝时无需依赖本地回填。
		AssignHoverItem(Item, Index, Index);
		DropItem();
		return;
	}

	PickUp(Item, Index);
	if (UINV_InventoryComponent* Comp = InventoryComponent.Get(); !IsValid(Comp) || !Comp->bServerAuthoritativeUI)
	{
		RemoveItemFromGrid(Item, Index);
	}
	DropItem();
}

void UINV_InventoryGrid::OnPopUpMenuConsume(int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return;
	UINV_InventoryItem* Item = GridSlots[Index]->GetInventoryItem().Get();
	if (!IsValid(Item)) return;
	
	const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex();
	if (!GridSlots.IsValidIndex(UpperLeftIndex)) return;
	UINV_GridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex];

	UINV_InventoryComponent* Comp = InventoryComponent.Get();
	if (!IsValid(Comp)) return;

	//Tell a server we're consuming an item
	if (Comp->bServerAuthoritativeUI)
	{
		// 服务器权威模式：本地不扣数量。
		// 原实现在这里又扣了一次（服务器 Server_UseItem 也扣），是待办 #8 的「双扣」缺陷；
		// 权威模式下数量只认服务器复制回来的值。
		FINV_InteractionContext Context(GetOwningPlayer(), EINV_InteractionType::Use);
		Context.Item = Item;
		Context.Amount = 1;
		Context.SourceSlotId = Item->GetContainerId();
		Context.SourceIndex = UpperLeftIndex;
		SubmitInventoryOperation(Context);
		return;
	}

	Comp->Server_UseItem(Item);

	const int32 NewStackCount = UpperLeftGridSlot->GetStackCount() - 1;
	if (NewStackCount > 0)
	{
		// 图标可能已被移除，取不到就只同步堆叠数
		if (UINV_SlottedItem* SlottedItem = SlottedItems.FindRef(UpperLeftIndex); IsValid(SlottedItem))
		{
			SlottedItem->UpdateStackCount(NewStackCount);
		}
		UpperLeftGridSlot->SetStackCount(NewStackCount);
	}
	else
	{
		RemoveItemFromGrid(Item, UpperLeftIndex);
	}
	
}

void UINV_InventoryGrid::OnItemInventoryMenuToggled(bool bOpen)
{
	if (!bOpen)
	{
		PutHoverItemBack();
	}
}

bool UINV_InventoryGrid::MatchesCategory(const UINV_InventoryItem* Item) const
{
	return IsValid(Item) && AcceptsCategory(Item->GetItemManifest().GetItemCategory());
}

bool UINV_InventoryGrid::BelongsToThisGrid(const UINV_InventoryItem* Item) const
{
	if (!IsValid(Item) || Item->IsEquipped()) return false;

	// 优先看容器归属：物品已经落位（ContainerId 非空）时，只看它是否属于本网格。
	// 两个同分类网格时，分类判断会让两个网格都认领同一件物品（重复放置），
	// 用 ContainerId 就能精确归属 —— 只有物品真正落位的那个网格才认领它。
	const FName ItemContainer = Item->GetContainerId();
	if (!ItemContainer.IsNone())
	{
		return ItemContainer == SlotId;
	}

	// 新拾取条目尚无落点时，按资产的接受分类分配首次落位（不读取旧蓝图分类）。
	return MatchesCategory(Item);
}

bool UINV_InventoryGrid::AcceptsCategory(EINV_ItemCategory Category) const
{
	return bGridLayoutResolved && INV_AcceptsItemCategory(ResolvedCategory, Category);
}




