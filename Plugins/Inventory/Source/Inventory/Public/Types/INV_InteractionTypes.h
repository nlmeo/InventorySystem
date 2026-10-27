#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "INV_InteractionTypes.generated.h"

class UINV_InventoryItem;
class UINV_ItemComponent;
class UINV_InventoryComponent;

/**
 * 交互类型：一次交互可以有多种形态。
 * 例如：E 打开容器、F 搜索附近物品、H 扛起物体。
 * 后续可在此枚举上继续扩展（交易、对话等）。
 */
UENUM(BlueprintType)
enum class EINV_InteractionType : uint8
{
	None			UMETA(DisplayName = "None"),
	OpenContainer	UMETA(DisplayName = "Open Container"),	// E —— 打开容器/宝箱
	Search			UMETA(DisplayName = "Search"),			// F —— 搜索附近物品
	Carry			UMETA(DisplayName = "Carry"),			// H —— 扛起物体
	Pickup			UMETA(DisplayName = "Pickup"),			// 拾取物品
	Transfer		UMETA(DisplayName = "Transfer"),			// 跨 InventoryComponent 转移
	Equip			UMETA(DisplayName = "Equip"),			// 装备
	Drop			UMETA(DisplayName = "Drop"),			// 丢弃
	Use				UMETA(DisplayName = "Use"),
	Move			UMETA(DisplayName = "Move"),
	BatchMove		UMETA(DisplayName = "Batch Move"),
	Swap			UMETA(DisplayName = "Swap"),
	SwapStackCounts UMETA(DisplayName = "Swap Stack Counts"),
	SwapEquipped	UMETA(DisplayName = "Swap Equipped"),
	Split			UMETA(DisplayName = "Split"),
	Merge			UMETA(DisplayName = "Merge"),
	Unequip			UMETA(DisplayName = "Unequip"),
	Rotate			UMETA(DisplayName = "Rotate"),
	DropEquipped	UMETA(DisplayName = "Drop Equipped")
};

/**
 * 交互上下文：组件间交互的统一数据契约（结构体做参数）。
 * 发起方构造一个 Context，交由目标组件处理，避免方法签名零散化。
 * 注意：本结构体通过 RPC 传递时，其中的 UObject 指针只在其拥有者处有效，
 * 服务器端需要按 Item 是否属于该玩家做合法性校验（防作弊）。
 */
USTRUCT(BlueprintType)
struct INVENTORY_API FINV_CrossContainerSwapEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<UINV_InventoryItem> Item = nullptr;
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FName TargetSlotId = NAME_None;
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	int32 TargetIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	bool bRotated = false;
};

USTRUCT(BlueprintType)
struct INVENTORY_API FINV_InteractionContext
{
	GENERATED_BODY()

	/** 谁发起了这次交互 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<AActor> Interactor = nullptr;

	/** 交互目标（容器 / 物品 Actor / 可扛物体等） */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<AActor> TargetActor = nullptr;

	/** 跨容器转移的目标库存组件；源组件由提交该请求的 InventoryComponent 表示。 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<UINV_InventoryComponent> TargetInventoryComponent = nullptr;

	/** 交互类型 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	EINV_InteractionType Type = EINV_InteractionType::None;

	/** 涉及的物品实例（拾取/移动/装备时使用） */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<UINV_InventoryItem> Item = nullptr;

	/** 世界中的可拾取物组件；Pickup 类型使用它创建或合并库存条目。 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<UINV_ItemComponent> ItemComponent = nullptr;

	/** 第二个物品（交换/合并时使用） */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<UINV_InventoryItem> SecondaryItem = nullptr;

	/** 物品在网格中的位置（服务器权威 GridIndex） */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	int32 GridIndex = INDEX_NONE;

	/** 数量（拾取/拆分/使用数量） */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	int32 Amount = 1;

	/** 装备槽标签（Equip 类型时使用） */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FGameplayTag EquipmentSlotTag;

	/** 装备槽互换时的来源槽标签。 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FGameplayTag SourceEquipmentSlotTag;

	/** 拾取时优先尝试的空装备槽。 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FGameplayTag EmptyEquipmentSlot;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	int32 SourceIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	int32 TargetIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	int32 SecondaryIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FName SourceSlotId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FName TargetSlotId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	FName ReturnSlotId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	bool bRotated = false;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	bool bReturnRotated = false;

	/** 批量挤压/重排使用的目标位置和朝向。 */
	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TArray<TObjectPtr<UINV_InventoryItem>> BatchItems;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TArray<int32> BatchTargetIndices;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TArray<bool> BatchRotatedStates;

	UPROPERTY(BlueprintReadWrite, Category = "Interaction")
	TArray<FINV_CrossContainerSwapEntry> CrossContainerSwapEntries;

	FINV_InteractionContext() {}

	FINV_InteractionContext(AActor* InInteractor, EINV_InteractionType InType)
		: Interactor(InInteractor), Type(InType) {}
};
