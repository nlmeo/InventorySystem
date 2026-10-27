#include "Widgets/Inventory/GridSlots/INV_EquippedGridSlot.h"

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Items/INV_InventoryItem.h"
#include "Items/Fragments/INV_FragmentTags.h"
#include "Items/Fragments/INV_ItemFragment.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"
#include "Widgets/Inventory/SlottedItems/INV_EquippedSlottedItem.h"

void UINV_EquippedGridSlot::NativeTick(const FGeometry& Geometry, float InDeltaTime)
{
	Super::NativeTick(Geometry, InDeltaTime);
	if (!IsValid(EquippedSlottedItem) || !IsValid(Overlay_Root)) return;
	const FVector2D OverlaySize = Overlay_Root->GetCachedGeometry().GetLocalSize();
	if (OverlaySize.X <= 0.f || OverlaySize.Y <= 0.f) return;
	UImage* Icon = EquippedSlottedItem->GetImageIcon();
	UOverlaySlot* IconSlot = UWidgetLayoutLibrary::SlotAsOverlaySlot(EquippedSlottedItem);
	if (!IsValid(Icon) || !IsValid(IconSlot)) return;
	const FVector2D DrawSize = Icon->GetBrush().ImageSize;
	const FMargin IconPadding((OverlaySize.X - DrawSize.X) * 0.5f, (OverlaySize.Y - DrawSize.Y) * 0.5f, 0.f, 0.f);
	if (IconSlot->GetPadding() != IconPadding) IconSlot->SetPadding(IconPadding);
	if (EquippedSlottedItem->GetVisibility() == ESlateVisibility::Hidden)
		EquippedSlottedItem->SetVisibility(ESlateVisibility::Visible);
}


void UINV_EquippedGridSlot::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	// 拖入高亮：空槽和「已装备的槽」都可作为放下目标（后者代表换装 / 互换），
	// 因此不再用 IsAvailable() 提前拦截，改为以拖拽 Payload 的类型是否匹配槽位为准。
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	if (!IsValid(Payload)) return;
	if (Payload->GetItemType().MatchesTag(EquipmentTypeTag))
	{
		SetOccupiedTexture();
		Image_GrayedOutIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UINV_EquippedGridSlot::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);

	// 槽里本来就有装备：拖出时保持已占用外观，回到装备前的视觉状态
	if (IsValid(EquippedSlottedItem))
	{
		SetOccupiedTexture();
		return;
	}

	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	if (IsValid(Payload) && Payload->GetItemType().MatchesTag(EquipmentTypeTag))
	{
		SetUnoccupiedTexture();
	}
}

bool UINV_EquippedGridSlot::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	OnEquippedGridSlotDropped.Broadcast(this, EquipmentTypeTag, Payload);
	return true;
}
/**
FReply UINV_EquippedGridSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	EquippedGridSlotClicked.Broadcast(this, EquipmentTypeTag);
	return FReply::Handled();
}
*/
UINV_EquippedSlottedItem* UINV_EquippedGridSlot::OnItemEquipped(UINV_InventoryItem* Item,
	const FGameplayTag& EquipmentTag, float TileSize)
{
	// 与服务器 CanEquipItemToSlot 保持同一套标签层级匹配规则。
	// 自动装备的物品类型可能是装备槽标签的子标签，Exact 匹配会导致服务器已装备成功，
	// 但 UI 没有创建可双击卸下的装备图标。
	if (!IsValid(Item) || !Item->GetItemManifest().GetItemType().MatchesTag(EquipmentTag)) return nullptr;
	
	// 装备入槽已归一为未旋转；装备图标、拖拽 Payload 与回包占地统一从原始尺寸出发。
	const FINV_GridFragment* GridFragment = GetFragment<FINV_GridFragment>(Item, FragmentTags::GridFragment);
	if (!GridFragment) return nullptr;
	const FIntPoint GridDimensions = Item->GetBaseDimension();
	
	// 与背包网格 GetDrawSizeForDims 同口径：每格像素扣除两侧 padding，避免拖回页签时比例变化。
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2.f;
	const FVector2D DrawSize = GridDimensions * IconTileWidth;
	
	// Create the Equipped Slotted Item and set attributes
	EquippedSlottedItem = CreateWidget<UINV_EquippedSlottedItem>(GetOwningPlayer(), EquippedSlottedItemClass);
	EquippedSlottedItem->SetInventoryItem(Item);
	EquippedSlottedItem->SetGridDimensions(GridDimensions);
	EquippedSlottedItem->SetEquipmentTypeTag(EquipmentTag);
	// 装备图标不属于任何网格：索引显式置无效，避免把未初始化的值当网格索引用
	EquippedSlottedItem->SetGridIndex(INDEX_NONE);
	EquippedSlottedItem->UpdateStackCount(0); //隐藏库存数字

	// 图标会吞掉 Drop，必须把它的放下事件转发回本槽，否则「拖到已装备的槽」不会触发换装
	EquippedSlottedItem->OnEquippedSlottedItemDropped.AddDynamic(
		this, &ThisClass::HandleEquippedSlottedItemDropped);

	// 拖拽开始同样要转发：上层需要在拖拽一开始就切到装备页签
	EquippedSlottedItem->OnSlottedItemDragStart.AddDynamic(
		this, &ThisClass::HandleEquippedSlottedItemDragStart);

	// 双击转发：快速卸下
	EquippedSlottedItem->OnSlottedItemDoubleClicked.AddDynamic(
		this, &ThisClass::HandleEquippedSlottedItemDoubleClick);
	
	SetInventoryItem(Item);
	
	const FINV_ImageFragment* ImageFragment = GetFragment<FINV_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!ImageFragment) return nullptr;
	FSlateBrush Brush;
	Brush.SetResourceObject(ImageFragment->GetIcon());
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.ImageSize = DrawSize;
	EquippedSlottedItem->SetImageBrush(Brush);
	EquippedSlottedItem->SetRotationVisual(false);
	
	Overlay_Root->AddChildToOverlay(EquippedSlottedItem);
	const FVector2D OverlaySize = Overlay_Root->GetCachedGeometry().GetLocalSize();
	// Direct pickup can equip while the inventory has never been laid out.
	// Keep layout participation, then reveal the icon once NativeTick has a valid slot geometry.
	if (OverlaySize.X <= 0.f || OverlaySize.Y <= 0.f)
		EquippedSlottedItem->SetVisibility(ESlateVisibility::Hidden);
	
	const float LeftPadding = OverlaySize.X / 2.f - DrawSize.X / 2.f;
	const float TopPadding = OverlaySize.Y / 2.f - DrawSize.Y / 2.f;
	
	UOverlaySlot* OverlaySlot = UWidgetLayoutLibrary::SlotAsOverlaySlot(EquippedSlottedItem);
	OverlaySlot->SetPadding(FMargin(LeftPadding, TopPadding, 0.f, 0.f));
	
	return EquippedSlottedItem;
}

void UINV_EquippedGridSlot::HandleEquippedSlottedItemDropped(UINV_EquippedSlottedItem* SlottedItem,
	UINV_ItemPayload* Payload)
{
	// 装备图标盖在槽位上，是 Drop 的实际命中者。这里等价于「把物品放到了本槽」，
	// 统一转发到槽的放下委托，让上层只在 EquippedGridSlotClicked 一处处理全部槽位放下逻辑。
	OnEquippedGridSlotDropped.Broadcast(this, EquipmentTypeTag, Payload);
}

void UINV_EquippedGridSlot::HandleEquippedSlottedItemDragStart(int32 GridIndex, UINV_ItemPayload* Payload)
{
	// 装备图标没有网格索引（它不属于任何 InventoryGrid），这里只用「发生了拖拽」这一个事实
	(void)GridIndex;
	// Payload 由拖拽 Operation 自带，网格在 DragEnter 时已经能拿到它（含跟随图元），这里不需要额外处理
	(void)Payload;

	// 拖拽刚被检测到（此时还没有产生 hover / external payload），通知上层切到装备页签
	OnEquippedSlotDragStarted.Broadcast(this);
}

void UINV_EquippedGridSlot::HandleEquippedSlottedItemDoubleClick(int32 GridIndex)
{
	// 装备图标不属于任何 InventoryGrid，GridIndex 恒为无效值，这里只用「发生了双击」
	(void)GridIndex;

	OnEquippedSlotDoubleClicked.Broadcast(this);
}

void UINV_EquippedGridSlot::SetOccupiedTexture()
{
	Super::SetOccupiedTexture();
	Image_GrayedOutIcon->SetVisibility(ESlateVisibility::Collapsed);
}

void UINV_EquippedGridSlot::SetUnoccupiedTexture()
{
	Super::SetUnoccupiedTexture();
	Image_GrayedOutIcon->SetVisibility(ESlateVisibility::Visible);
}

