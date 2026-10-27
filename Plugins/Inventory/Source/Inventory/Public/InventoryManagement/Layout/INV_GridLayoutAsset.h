// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InventoryManagement/Layout/INV_GridLayoutTypes.h"

#include "INV_GridLayoutAsset.generated.h"

/** FINV_GridSlotDefinition 的包装体，便于在 Details 面板中内联编辑 */
UCLASS(EditInlineNew, DefaultToInstanced, BlueprintType)
class INVENTORY_API UINV_GridSlotDefinitionWrapper : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Layout")
	FINV_GridSlotDefinition SlotDefinition;
};

/**
 * 网格布局资产：描述一组网格面板的静态布局（尺寸 / 像素 / 位置 / 接受分类）。
 * 只供客户端渲染与服务器校验读取，不含任何运行时物品状态，
 * 因此双端各自加载即可保持一致，不需要通过网络同步。
 */
UCLASS(BlueprintType)
class INVENTORY_API UINV_GridLayoutAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 在末尾追加一个面板，并按 SpaceBetweenElements 自动计算其水平偏移 */
	void AddNewSlot();

	void RemoveLastSlot();

	void EmptySlots();

	int32 GetSlotCount() const { return Slots.Num(); }

	/** 越界返回 nullptr */
	const FINV_GridSlotDefinition* GetSlotDefinition(int32 Index) const;

	/** 本布局的格子像素大小（整份布局共用一个值，来自 LayoutDefinition.TileSize） */
	float GetTileSize() const { return LayoutDefinition.TileSize; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FINV_GridLayoutDefinition LayoutDefinition;

	UPROPERTY(VisibleAnywhere, Instanced, Category = "Layout")
	TArray<TObjectPtr<UINV_GridSlotDefinitionWrapper>> Slots;
};
