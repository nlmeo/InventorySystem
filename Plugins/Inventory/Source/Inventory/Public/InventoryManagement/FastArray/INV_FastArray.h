#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "INV_FastArray.generated.h"

struct FGameplayTag;
class UINV_InventoryItem;
class UINV_ItemComponent;
class UINV_InventoryComponent;

/** a single entry in an inventory */
USTRUCT(BlueprintType)
struct INVENTORY_API FINV_InventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FINV_InventoryEntry() {}
private:
	friend struct FINV_InventoryFastArray;
	friend UINV_InventoryComponent;

	UPROPERTY()
	TObjectPtr<UINV_InventoryItem> Item = nullptr;
	
	
	
};


/** List of Inventory Items */
USTRUCT(BlueprintType)
struct INVENTORY_API FINV_InventoryFastArray : public FFastArraySerializer
{
	GENERATED_BODY()
	
	FINV_InventoryFastArray() : OwnerComponent(nullptr) {}
	FINV_InventoryFastArray(UActorComponent* InOwnerComponent) : OwnerComponent(InOwnerComponent) {}
	TArray<UINV_InventoryItem*> GetAllItems() const;
	UINV_InventoryItem* AddEntry(UINV_ItemComponent* ItemComponent);
	UINV_InventoryItem* AddEntry(UINV_InventoryItem* Item);
	void RemoveEntry(UINV_InventoryItem* Item);
	UINV_InventoryItem* FindFirstItemByType(const FGameplayTag& ItemType) const;

	// 找第一个「同类且还能再装」的条目（TotalStackCount < MaxStackSize）。
	// 用于把一次拾取量填进已有堆叠，装满的条目会被跳过。
	UINV_InventoryItem* FindFirstItemWithRoom(const FGameplayTag& ItemType, int32 MaxStackSize) const;
	
	// FFastArraySerializer Contract
	void PreReplicatedRemove(const TArrayView<int32> RemoveIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	// TODO:void PostReplicatedChange(const TArrayView<int32> ChangeIndices, int32 FinalSize);
	// Item 数量， 位置变化 通过fastarray同步后通知
	// End of FFastArraySerializer Contract
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FINV_InventoryEntry, FINV_InventoryFastArray>(Entries, DeltaParams, *this);
	}
private:
	friend UINV_InventoryComponent;

	// Replicated list of items
	UPROPERTY()
	TArray<FINV_InventoryEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UActorComponent> OwnerComponent;
};


template<>
struct TStructOpsTypeTraits<FINV_InventoryFastArray> : public TStructOpsTypeTraitsBase2<FINV_InventoryFastArray>
{
	enum { WithNetDeltaSerializer = true };
};