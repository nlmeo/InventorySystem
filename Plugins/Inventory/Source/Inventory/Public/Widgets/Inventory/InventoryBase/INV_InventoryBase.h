// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/INV_GridTypes.h"
#include "INV_InventoryBase.generated.h"

class UINV_ItemPayload;
class UINV_ItemComponent;
class UINV_InventoryItem;
struct FINV_ItemManifest;

UCLASS()
class INVENTORY_API UINV_InventoryBase : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual FINV_SlotAvailabilityResult HasRoomForItem(UINV_ItemComponent* ItemComponent) const { return FINV_SlotAvailabilityResult();}
	// Only matching empty slots may be suggested for pickup; never requests a swap.
	virtual FGameplayTag FindEmptyEquipmentSlot(const FINV_ItemManifest& Manifest) const { return FGameplayTag(); }
	virtual void OnItemHovered(UINV_InventoryItem* Item) {};
	virtual void OnItemUnHovered() {};
	virtual bool HasHoverItem() const { return false; }
	virtual UINV_ItemPayload* GetHoverItem() const { return nullptr; }
	virtual float GetTileSize() const { return 0.f; }
	// 隐藏背包前调用：派生界面在这里结束拖拽并清理交互状态。
	virtual void OnInventoryClosed() {}
};
