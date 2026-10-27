
#pragma once

#include "GameplayTagContainer.h"
#include "INV_GridTypes.generated.h"


class UINV_GridSlot;
class UINV_InventoryItem;

/** 刷新应答携带实际权威值：被拒绝时复制属性可能没变化，不能只通知客户端重绘。 */
USTRUCT()
struct FINV_ItemStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UINV_InventoryItem> Item = nullptr;

	UPROPERTY()
	FName ContainerId = NAME_None;

	UPROPERTY()
	int32 GridIndex = INDEX_NONE;

	UPROPERTY()
	int32 StackCount = 0;

	UPROPERTY()
	bool bRotated = false;

	UPROPERTY()
	FGameplayTag EquippedSlotType;
};

UENUM(BlueprintType)
enum class EINV_ItemCategory : uint8
{
	Equippable,
	Consumable,
	Craftable,
	All UMETA(DisplayName = "All (所有分类)"),
	None
};

/** AcceptedCategory 的统一语义：None 不接受任何物品，All 接受所有有效物品分类。 */
FORCEINLINE bool INV_AcceptsItemCategory(const EINV_ItemCategory AcceptedCategory, const EINV_ItemCategory ItemCategory)
{
	return ItemCategory != EINV_ItemCategory::None
		&& ItemCategory != EINV_ItemCategory::All
		&& AcceptedCategory != EINV_ItemCategory::None
		&& (AcceptedCategory == EINV_ItemCategory::All || AcceptedCategory == ItemCategory);
}

/** Information for the Individual Slot*/
USTRUCT()
struct FINV_SlotAvailability
{
	GENERATED_BODY()
	
	FINV_SlotAvailability() {}
	FINV_SlotAvailability(int32 ItemIndex, int32 RoomCapacity, bool bHasItem) : Index(ItemIndex), AmountToFill(RoomCapacity), bItemAtIndex(bHasItem) {}
	
	int32 Index{-1};
	int32 AmountToFill{0};
	bool bItemAtIndex{false};
};

/** All the Slots need to be change when add an item */
USTRUCT()
struct FINV_SlotAvailabilityResult
{
	GENERATED_BODY()
	
	FINV_SlotAvailabilityResult() {}
	
	TWeakObjectPtr<UINV_InventoryItem> Item;
	int32 TotalRoomToFill{0};
	int32 Remainder{0};
	bool bStackable{ false };
	bool bRotated{ false };
	
	TArray<FINV_SlotAvailability> SlotAvailabilities;
};

UENUM(BlueprintType)
enum class EINV_TileQuadrant : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	None
};

USTRUCT(BlueprintType)
struct FINV_TileParameters
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	FIntPoint TileCoordinates{};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	int32 TileIndex{INDEX_NONE};
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	EINV_TileQuadrant TileQuadrant{ EINV_TileQuadrant::None };
};

inline bool operator==(const FINV_TileParameters& A, const FINV_TileParameters& B)
{
	return A.TileCoordinates == B.TileCoordinates && A.TileIndex == B.TileIndex && A.TileQuadrant == B.TileQuadrant;
}


//TODO:改成网格为基础的多网格版本
USTRUCT()
struct FINV_SpaceQueryResult
{
	GENERATED_BODY();
	
	// True when the hovered placement is allowed, including supported swaps and rearrangements.
	bool bHasSpace{false};
	
	//检测是否与悬挂物品重叠
	
	bool bIsOverlap{false}; 
	TSet<int32> OverlapIndices;
	
	TWeakObjectPtr<UINV_InventoryItem> CurrentItem = nullptr;
	
	
	
	TSet<int32> ClickedIndices;
	
	TSet<int32> OriginIndices;
	
	TSet<int32> ValidateIndices; //需要校验的区域，与EnptyIndices一起组成ClickedIndices
	
	TSet<int32> EmptyIndices; //空闲格子
	
	TMap<TWeakObjectPtr<UINV_InventoryItem>,int32> SpaceItemsMap;
	
};
