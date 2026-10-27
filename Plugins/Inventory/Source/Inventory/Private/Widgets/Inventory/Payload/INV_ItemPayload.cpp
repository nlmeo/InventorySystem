#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

#include "Components/Widget.h"
#include "Items/INV_InventoryItem.h"

void UINV_ItemPayload::InitByItem(UINV_InventoryItem* InInventoryItem)
{
	Clear();
	InventoryItem = InInventoryItem;
	if (!IsValid(InInventoryItem)) return;

	// 拖拽起点沿用物品当前的朝向：拿到手里的东西就应该是它摆在那儿的那个朝向
	SetGridDimensions(InInventoryItem->GetBaseDimension());
	SetRotated(InInventoryItem->IsRotated());
	SetIsStackable(InInventoryItem->IsStackable());
}

void UINV_ItemPayload::Clear()
{
	SourceGrid.Reset();
	SourceInventory.Reset();
	InventoryItem = nullptr;
	DragVisual = nullptr;
	PreviousGridIndex = INDEX_NONE;
	GridDimensions = FIntPoint::ZeroValue;
	bRotated = false;
	StackCount = 0;
	bIsStackable = false;
	DragSource = EINV_ItemDragSource::None;
}

bool UINV_ItemPayload::ToggleRotated()
{
	const UINV_InventoryItem* Item = InventoryItem.Get();
	// 正方形（含 1x1）旋转前后等价，直接不响应：否则会白跑一遍空间查询、还多发一次网络同步
	if (!IsValid(Item) || !Item->CanRotate()) return false;

	bRotated = !bRotated;
	return true;
}

FGameplayTag UINV_ItemPayload::GetItemType() const
{
	if (InventoryItem.IsValid())
	{
		return InventoryItem->GetItemManifest().GetItemType();
	}
	return FGameplayTag::EmptyTag;
}

int32 UINV_ItemPayload::GetStackCount() const
{
	return StackCount;
}

bool UINV_ItemPayload::IsStackable() const
{
	return bIsStackable;
}

int32 UINV_ItemPayload::GetPreviousGridIndex() const
{
 	return PreviousGridIndex;
}

FIntPoint UINV_ItemPayload::GetGridDimensions() const
{
	// 旋转 90° 时宽高互换 —— 与 UINV_InventoryItem::GetItemDimension() 的口径必须完全一致，
	// 否则会出现「高亮按 2x3、落位按 3x2」的错位。
	return bRotated ? FIntPoint(GridDimensions.Y, GridDimensions.X) : GridDimensions;
}

UINV_InventoryItem* UINV_ItemPayload::GetInventoryItem() const
{
	return InventoryItem.Get();
}

EINV_ItemDragSource UINV_ItemPayload::GetDragSource() const
{
	return DragSource;
}

void UINV_ItemPayload::SetInventoryItem(UINV_InventoryItem* Item)
{
	InventoryItem = Item;
}

void UINV_ItemPayload::SetPreviousGridIndex(int32 Index)
{
	PreviousGridIndex = Index;
}

void UINV_ItemPayload::SetGridDimensions(const FIntPoint& Dimensions)
{
	GridDimensions = Dimensions;
}

void UINV_ItemPayload::SetStackCount(int32 Count)
{
	StackCount = Count;
}

void UINV_ItemPayload::SetIsStackable(bool InStackable)
{
	bIsStackable = InStackable;
}

void UINV_ItemPayload::SetDragSource(EINV_ItemDragSource Source)
{
	DragSource = Source;
}

void UINV_ItemPayload::SetDragVisual(UWidget* InVisual)
{
	DragVisual = InVisual;
}

UWidget* UINV_ItemPayload::GetDragVisual() const
{
	return DragVisual.Get();
}
