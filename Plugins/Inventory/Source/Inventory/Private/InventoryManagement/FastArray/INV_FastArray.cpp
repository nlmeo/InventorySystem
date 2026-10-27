#include "InventoryManagement/FastArray/INV_FastArray.h"
#include "Items/INV_InventoryItem.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/Components/INV_ItemComponent.h"

TArray<UINV_InventoryItem*> FINV_InventoryFastArray::GetAllItems() const
{
	TArray<UINV_InventoryItem*> Results;
	Results.Reserve(Entries.Num());
	for (const auto& Entry : Entries)
	{
		if (!IsValid(Entry.Item)) continue;
		Results.Add(Entry.Item);
	}
	return Results;
}

void FINV_InventoryFastArray::PreReplicatedRemove(const TArrayView<int32> RemoveIndices, int32 FinalSize)
{
	UINV_InventoryComponent* IC = Cast<UINV_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

	for (int32 Index : RemoveIndices)
	{
		IC->OnItemRemoved.Broadcast(Entries[Index].Item);
	}
}

void FINV_InventoryFastArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	UINV_InventoryComponent* IC = Cast<UINV_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

	for (int32 Index : AddedIndices)
	{
		if (IsValid(Entries[Index].Item))
		{
			Entries[Index].Item->SetOwningInventory(IC);
		}
		IC->OnItemAdded.Broadcast(Entries[Index].Item);
	}
}

UINV_InventoryItem* FINV_InventoryFastArray::AddEntry(UINV_ItemComponent* ItemComponent)
{
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());
	UINV_InventoryComponent* IC = Cast<UINV_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return nullptr;
	
	FINV_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = ItemComponent->GetItemManifest().Manifest(OwningActor); 
	IC->AddRepSubObj(NewEntry.Item);
	// 挂上归属组件：条目自身的 RepNotify 要靠它把「权威值到了」冒泡给 UI
	NewEntry.Item->SetOwningInventory(IC);
	MarkItemDirty(NewEntry);
	
	return NewEntry.Item;
}

UINV_InventoryItem* FINV_InventoryFastArray::AddEntry(UINV_InventoryItem* Item)
{
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());

	FINV_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = Item;

	if (IsValid(Item))
	{
		Item->SetOwningInventory(Cast<UINV_InventoryComponent>(OwnerComponent));
	}

	MarkItemDirty(NewEntry);
	return Item;
}

void FINV_InventoryFastArray::RemoveEntry(UINV_InventoryItem* Item)
{
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt)
	{
		FINV_InventoryEntry& Entry = *EntryIt;
		if (Entry.Item == Item)
		{
			EntryIt.RemoveCurrent();
			MarkArrayDirty();

			// PreReplicatedRemove 只在客户端收到复制时触发；服务器 / 单机本地移除不会走它，
			// 所以本机 UI 收不到任何通知（丢弃、用尽、整份合并后图标留在原地）。
			// 这里补一次本地广播，保证两条路径都有「条目没了」的信号。
			if (UINV_InventoryComponent* IC = Cast<UINV_InventoryComponent>(OwnerComponent))
			{
				IC->OnItemRemoved.Broadcast(Item);
			}
			break;
		}
	}
}

UINV_InventoryItem* FINV_InventoryFastArray::FindFirstItemByType(const FGameplayTag& ItemType) const
{
	auto* FoundItem =  Entries.FindByPredicate([ItemType](const FINV_InventoryEntry& Entry)
	{
		return IsValid(Entry.Item) && Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(ItemType);
	});
	return FoundItem ? FoundItem->Item : nullptr;
	
}

UINV_InventoryItem* FINV_InventoryFastArray::FindFirstItemWithRoom(const FGameplayTag& ItemType,
	int32 MaxStackSize) const
{
	for (const FINV_InventoryEntry& Entry : Entries)
	{
		if (!IsValid(Entry.Item)) continue;
		if (!Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(ItemType)) continue;
		// 已满的条目不能再用：一个条目最多装 MaxStackSize 件
		if (Entry.Item->GetTotalStackCount() >= MaxStackSize) continue;
		return Entry.Item;
	}
	return nullptr;
}



