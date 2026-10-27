#include "Items/Manifest/INV_ItemManifest.h"
#include "Items/INV_InventoryItem.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Widgets/Composite/INV_CompositeBase.h"


UINV_InventoryItem* FINV_ItemManifest::Manifest(UObject* NewOuter) const
{
	//动态构建
	UINV_InventoryItem* Item = NewObject<UINV_InventoryItem>(NewOuter,UINV_InventoryItem::StaticClass());
	Item->SetItemManifest(*this);
	
	for (auto& Fragment : Item->GetItemManifestMutable().GetAllFragmentsMutable())
	{
		Fragment.GetMutable().Manifest();
	}
	// 这里不再清空 this 的 Fragments：本函数是「模板 → 实例」的幂等工厂，
	// 同一个 manifest 需要能反复实例化出多个 InventoryItem（拾取数量超过单堆叠上限时要拆成多个条目）。
	// 防重复拾取由 UINV_ItemComponent::PickedUp() 销毁 actor 负责，不靠清数据。
	return Item;
}

void FINV_ItemManifest::AssimilateInventoryFragments(UINV_CompositeBase* Composite) const
{
	const auto& InventoryItemFragments = GetAllFragmentsOfType<FINV_InventoryItemFragment>();
	for(const auto* Fragment : InventoryItemFragments)
	{
		Composite->ApplyFunction([Fragment](UINV_CompositeBase* widget)
		{
			Fragment->Assimilate(widget);
		});
	}
}

bool FINV_ItemManifest::SpawnPickupActor(const UObject* WorldContextObject, const FVector& SpawnLocation,
                                         const FRotator& SpawnRotation)
{
	if (!IsValid(WorldContextObject) || !WorldContextObject->GetWorld() || !IsValid(PickupActorClass)) return false;
	
	AActor* SpawnedActor = WorldContextObject->GetWorld()->SpawnActor<AActor>(PickupActorClass, SpawnLocation, SpawnRotation);
	if (!IsValid(SpawnedActor)) return false;
	
	// Set the item manifest, item category, item type etc.
	UINV_ItemComponent* ItemComp = SpawnedActor->FindComponentByClass<UINV_ItemComponent>();
	if (!IsValid(ItemComp))
	{
		SpawnedActor->Destroy();
		return false;
	}
	
	ItemComp->InitItemManifest(*this);
	return !SpawnedActor->IsActorBeingDestroyed();
}
