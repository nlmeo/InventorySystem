#pragma once

#include "CoreMinimal.h"
#include "Manifest/INV_ItemManifest.h"
#include "UObject/Object.h"
#include "StructUtils/InstancedStruct.h"
#include "INV_InventoryItem.generated.h"

class UINV_InventoryComponent;

UCLASS()
class INVENTORY_API UINV_InventoryItem : public UObject
{
	GENERATED_BODY()
public:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	virtual bool IsSupportedForNetworking() const override {return true;}

	/**
	 * 权威值到达客户端时的通知。
	 *
	 * 为什么需要它（而不是只靠「操作完请求服务器回推一次」）：
	 * 回推走的是 RPC，数量和位置走的是**子对象属性复制**，两者不是同一条通道。
	 * 实测 RPC 会比属性先到，于是客户端重铺时读到的仍是旧数量 ——
	 * 表现就是「拆了 5 件，源堆叠显示还是原值」「合并后目标数量不变」，而服务器数据其实是对的。
	 * 把刷新挂在这里，就意味着「值真的到了」才重绘，天然不会差一拍。
	 */
	UFUNCTION()
	void OnRep_GridIndex();

	UFUNCTION()
	void OnRep_TotalStackCount();

	UFUNCTION()
	void OnRep_ContainerId();

	UFUNCTION()
	void OnRep_Rotated();

	UFUNCTION()
	void OnRep_EquippedSlotType();

	/** 所属背包组件：仅用于把变更冒泡给 UI，弱引用，不参与物品生命周期 */
	void SetOwningInventory(UINV_InventoryComponent* InComponent);
	
	void SetItemManifest(const FINV_ItemManifest& Manifest);
	const FINV_ItemManifest& GetItemManifest() const { return ItemManifest.Get<FINV_ItemManifest>(); }
	FINV_ItemManifest& GetItemManifestMutable() { return ItemManifest.GetMutable<FINV_ItemManifest>(); }
	bool IsStackable() const;
	int32 GetTotalStackCount() const{return TotalStackCount;}
	void SetTotalStackCount(int32 StackCount){TotalStackCount = StackCount;}
	bool IsConsumable() const;
	/** 实际占地尺寸：已含朝向（旋转 90° 时 X/Y 互换）。全工程应以此为准。 */
	FIntPoint GetItemDimension() const;
	/** 资产配置的原始尺寸（不含朝向）。只在需要「未旋转尺寸」的场合用（图标绘制、旋转判定）。 */
	FIntPoint GetBaseDimension() const;
	/** 是否可旋转：非正方形才有意义（1x1、2x2 旋转后等价，直接拒绝，避免无意义的网络同步）。 */
	bool CanRotate() const;
	bool IsRotated() const { return bRotated; }
	void SetRotated(bool bInRotated) { bRotated = bInRotated; }
	bool IsEquipped() const { return EquippedSlotType.IsValid(); }
	FGameplayTag GetEquippedSlotType() const { return EquippedSlotType; }
	void SetEquippedSlotType(FGameplayTag InSlotType) { EquippedSlotType = InSlotType; }
	int32 GetGridIndex() const { return GridIndex; }
	void SetGridIndex(int32 Index) { GridIndex = Index; }

	/**
	 * 物品当前所在容器的身份键（当前 = 网格 SlotId，塔科夫化后 = 容器实例 ID）。
	 *
	 * GridIndex 只是「容器内索引」，脱离容器维度无法解析出全局位置。服务器做占用校验时
	 * 用这个字段过滤「别的容器」的物品 —— 否则装备在装备槽里的网格坐标会被拿到消耗品
	 * 坐标系里算，造成假冲突（R1 根因）。NAME_None 表示物品不在任何网格里（装备中 / 已拿起 / 未落位）。
	 */
	FName GetContainerId() const { return ContainerId; }
	void SetContainerId(FName InContainerId) { ContainerId = InContainerId; }
private:
	/** 把「我这一条被服务器改了」冒泡给所属组件；组件负责合并同一帧的多条变更再通知 UI */
	void NotifyReplicatedChange();

	TWeakObjectPtr<UINV_InventoryComponent> OwningInventory;

	//实例化结构体：可以将结构体与其拥有者一起实例化，让子类构造后无需等待父类Initialized方法进行注入和广播
	UPROPERTY(VisibleAnywhere, meta = (BaseStruct = "/Script/Inventory.INV_ItemManifest"), Replicated)
	FInstancedStruct ItemManifest;
	
	UPROPERTY(ReplicatedUsing = OnRep_TotalStackCount)
	int32 TotalStackCount{0};

	// 服务器权威位置：物品在网格中的左上角格子索引（一维 GridIndex）
	UPROPERTY(ReplicatedUsing = OnRep_GridIndex)
	int32 GridIndex{INDEX_NONE};

	// 服务器权威容器归属：物品所在容器的身份键。NAME_None 表示不在任何网格里。
	UPROPERTY(ReplicatedUsing = OnRep_ContainerId)
	FName ContainerId{NAME_None};

	/**
	 * 朝向：true = 顺时针旋转 90°，即占地尺寸 X/Y 互换。
	 *
	 * 为什么放在物品上而不是「移动参数」里：朝向是**已落位物品的状态**，
	 * 重连、重开背包、持久化都必须还原；如果只当作一次移动的入参，
	 * 服务器重铺时就无从知道该用哪个尺寸画（UI 会退回原朝向）。
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Rotated)
	bool bRotated{false};

	// Persistent equipment membership, including direct pickup into an empty slot.
	UPROPERTY(ReplicatedUsing = OnRep_EquippedSlotType)
	FGameplayTag EquippedSlotType;
	
};

template<typename FragmentType>
const FragmentType* GetFragment(const UINV_InventoryItem* Item, const FGameplayTag& Tag)
{
	if (!IsValid(Item)) return nullptr;
	
	const FINV_ItemManifest& Manifest = Item->GetItemManifest();
	return Manifest.GetFragmentOfTypeWithTag<FragmentType>(Tag);
}
