// Fill out your copyright notice in the Description page of Project Settings.


#include "Items/INV_InventoryItem.h"

#include "GameFramework/Actor.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Net/UnrealNetwork.h"

void UINV_InventoryItem::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(ThisClass, ItemManifest);
	DOREPLIFETIME(ThisClass, TotalStackCount);
	DOREPLIFETIME(ThisClass, GridIndex);
	DOREPLIFETIME(ThisClass, ContainerId);
	DOREPLIFETIME(ThisClass, bRotated);
	DOREPLIFETIME(ThisClass, EquippedSlotType);
}

void UINV_InventoryItem::SetOwningInventory(UINV_InventoryComponent* InComponent)
{
	// 放在 .cpp：TWeakObjectPtr 赋值需要 UINV_InventoryComponent 的完整类型，
	// 头文件里只有前置声明。
	OwningInventory = InComponent;
}

void UINV_InventoryItem::OnRep_GridIndex()
{
	NotifyReplicatedChange();
}

void UINV_InventoryItem::OnRep_TotalStackCount()
{
	NotifyReplicatedChange();
}

void UINV_InventoryItem::OnRep_ContainerId()
{
	NotifyReplicatedChange();
}

void UINV_InventoryItem::OnRep_Rotated()
{
	// 朝向变了 → 占地尺寸变了 → 必须重铺（图标尺寸、占用格、高亮范围都要跟着变）。
	// 复用同一条「条目被服务器改了」的冒泡通道，和 GridIndex / 数量的处理保持一致。
	NotifyReplicatedChange();
}

void UINV_InventoryItem::OnRep_EquippedSlotType()
{
	NotifyReplicatedChange();
}

void UINV_InventoryItem::NotifyReplicatedChange()
{
	UINV_InventoryComponent* Component = OwningInventory.Get();

	if (!IsValid(Component))
	{
		// 关键：客户端上的条目是**复制出来的**，从来没走过 FINV_InventoryFastArray::AddEntry，
		// 所以 OwningInventory 一直是空的 —— 之前 RepNotify 冒泡到这里就静默 return，
		// 表现为「属性复制到了，但 UI 收不到通知」（拆分/合并后数量不变就是这个）。
		// 这里按 Outer 链兜底找一次并缓存：条的 Outer 是 OwnerActor（PlayerController），
		// 背包组件是它的子对象。
		AActor* OwnerActor = GetTypedOuter<AActor>();
		Component = OwnerActor ? OwnerActor->FindComponentByClass<UINV_InventoryComponent>() : nullptr;
		OwningInventory = Component;
	}

	if (IsValid(Component))
	{
		Component->NotifyItemReplicatedChange(this);
	}
}

void UINV_InventoryItem::SetItemManifest(const FINV_ItemManifest& Manifest)
{
	ItemManifest = FInstancedStruct::Make<FINV_ItemManifest>(Manifest);
}

bool UINV_InventoryItem::IsStackable() const
{
	const FINV_StackableFragment* Stackable = GetItemManifest().GetFragmentOfType<FINV_StackableFragment>();
	return Stackable != nullptr;
}

bool UINV_InventoryItem::IsConsumable() const
{
	return GetItemManifest().GetItemCategory() == EINV_ItemCategory::Consumable;
}

FIntPoint UINV_InventoryItem::GetBaseDimension() const
{
	const FINV_GridFragment* GridFragment = GetItemManifest().GetFragmentOfType<FINV_GridFragment>();

	return GridFragment ? GridFragment->GetGridSize() : FIntPoint(1,1);
}

FIntPoint UINV_InventoryItem::GetItemDimension() const
{
	const FIntPoint Base = GetBaseDimension();
	// 旋转 90°：宽高互换。这是全工程唯一的尺寸出口，
	// 只要这里认朝向，格子占用、越界判定、高亮范围、服务器校验就全都跟着对了。
	return bRotated ? FIntPoint(Base.Y, Base.X) : Base;
}

bool UINV_InventoryItem::CanRotate() const
{
	const FIntPoint Base = GetBaseDimension();
	return Base.X > 0 && Base.Y > 0 && Base.X != Base.Y;
}
