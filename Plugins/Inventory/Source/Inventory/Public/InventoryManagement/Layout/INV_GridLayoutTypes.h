// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/INV_GridTypes.h"

#include "INV_GridLayoutTypes.generated.h"

/**
 * 容器类型。
 * - Grid：自由网格（背包、安全箱），物品在 N×M 内任意摆放。
 * - FixedSlots：固定槽位列表（口袋、弹挂），每个槽只接受特定标签的物品。
 *   塔科夫化时才需要实现；当前阶段资产里只会用到 Grid，保留枚举是为了避免将来改资产结构导致数据迁移。
 */
UENUM(BlueprintType)
enum class EINV_GridContainerType : uint8
{
	Grid		UMETA(DisplayName = "Grid (自由网格)"),
	FixedSlots	UMETA(DisplayName = "Fixed Slots (固定槽位)")
};

/**
 * 单个网格面板的静态定义（纯布局数据，不含任何运行时物品状态）。
 * 服务器与客户端各自从同一份资产读取，因此无需网络同步。
 */
USTRUCT(BlueprintType)
struct INVENTORY_API FINV_GridSlotDefinition
{
	GENERATED_BODY()

	/**
	 * 面板唯一标识，用作「网格身份」的键（同一资产内不可重复）。
	 *
	 * 不要用 AcceptedCategory 当键：容器能装任何分类的物品，分类描述的是「内容物」，
	 * 不是「容器本身」。一旦出现两个同类型容器（主背包 + 箱子），分类键就不唯一了。
	 * 塔科夫化后物品会通过 ContainerId 指向具体容器，届时这个字段就是容器标识。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FName SlotId;

	/** 容器类型。当前只实现 Grid。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	EINV_GridContainerType ContainerType = EINV_GridContainerType::Grid;

	/** 面板尺寸：X = 列数，Y = 行数 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FIntPoint SlotDimension = FIntPoint(1, 1);

	/** 面板在布局内的像素偏移（当前架构：每个网格是独立 Widget，此值仅在动态多面板布局中生效） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FVector2D PositionOffset = FVector2D::ZeroVector;

	// 单格像素大小不在这里配：它是整份布局共用的，见 FINV_GridLayoutDefinition::TileSize。
	// 原先每个面板各配一份，但代码里只会认实际解析到的那个值，
	// 面板之间配得不一样时没有任何提示 —— 属于纯粹的配置分叉，故上移。

	/**
	 * 该面板接受的物品分类（供 UINV_InventoryGrid 在没有配 SlotId 时兜底匹配，
	 * 也用于「这个容器能装什么」的语义）。All 接受所有有效物品分类，None 不接受任何物品。
	 * 不影响容器身份。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	EINV_ItemCategory AcceptedCategory = EINV_ItemCategory::None;

	/** 可选：进一步限定该面板接受的物品标签（FixedSlots 容器会用它做槽位匹配） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FGameplayTagContainer SlotTags;
};

/**
 * 整份布局的通用参数。
 *
 * 单格像素大小放在这里（而不是每个面板各配一份）：格子大小是「这套 UI 的缩放比例」，
 * 同一份布局里的所有面板必须一样大，否则同一件物品在不同页签大小不同、跨容器拖拽时图标会跳变。
 * 放在 LayoutDefinition 这一层，既保持「资产驱动」，又让格宽只有一个来源。
 */
USTRUCT(BlueprintType)
struct INVENTORY_API FINV_GridLayoutDefinition
{
	GENERATED_BODY()

	/**
	 * 单格像素大小。整份布局共用，所有面板一致。
	 *
	 * 实际图标边长 = TileSize - 物品自身 GridFragment 的 GridPadding*2，
	 * 见 UINV_InventoryGrid::GetDrawSizeForDims。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1.0"))
	float TileSize = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float GridLineThickness = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FLinearColor GridLineColor = FLinearColor::White;

	/** 编辑期自动摆放新面板时使用的水平间距 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float SpaceBetweenElements = 5.f;
};
