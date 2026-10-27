// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/INV_GridTypes.h"
#include "Widgets/Utils/INV_WidgetUtils.h"
#include "INV_InventoryStatics.generated.h"

class UINV_InventoryBase;
class UINV_ItemPayload;
class UINV_ItemComponent;
class UINV_InventoryComponent;

UCLASS()
class INVENTORY_API UINV_InventoryStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static UINV_InventoryComponent* GetInventoryComponent(const APlayerController* PlayerController);
	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static EINV_ItemCategory GetInventoryCategoryFromItemComponent(UINV_ItemComponent* ItemComponent);

	template<typename T, typename FuncT>
	static void Foreach2D(const TArray<T>& Array, int32 Index, const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemHovered(APlayerController* PC, UINV_InventoryItem* Item);
	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemUnHovered(APlayerController* PC);
	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static UINV_ItemPayload* GetHoverItem(APlayerController* PC);
	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static UINV_InventoryBase* GetInventoryWidget(APlayerController* PC);
	
	
};

template<typename T, typename FuncT>
void UINV_InventoryStatics::Foreach2D(const TArray<T>& Array, int32 Index, const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function)
{
	if (!Array.IsValidIndex(Index)) return;
	if (Array.IsEmpty()) return;
	if (Range2D.X <= 0 || Range2D.Y <= 0 ) return;
	if (GridColumns <= 0) return;
	
	
	//边界判断 判断起点Index和Range2D是否在格子内，如果有超出格子范围如何操作（不进入循环或者仅在格子范围内部生效）？ 目前默认格子为矩形区域
	FIntPoint StartPos = UINV_WidgetUtils::GetPositionFromIndex(Index,GridColumns);
	int32 MaxRow = Array.Num() / GridColumns; //默认为矩形区域（非异构格子，故不考虑最后一排格子数量不同导致的越界情况）
	
	bool bIsOutOfBoundsX = StartPos.X < 0 || StartPos.X + (Range2D.X - 1) >= GridColumns;
	bool bIsOutOfBoundsY = StartPos.Y < 0 || StartPos.Y + (Range2D.Y - 1) >= MaxRow;
	if (bIsOutOfBoundsX || bIsOutOfBoundsY) return;
	
	
	for (int j = 0; j < Range2D.Y; ++j)
	{
		for (int i = 0; i < Range2D.X; ++i)
		{
			const FIntPoint Coordinates = StartPos + FIntPoint(i,j);
			const int32 TileIndex = UINV_WidgetUtils::GetIndexFromPosition(Coordinates, GridColumns);
			Function(Array[TileIndex]);
		}
	}
	
}