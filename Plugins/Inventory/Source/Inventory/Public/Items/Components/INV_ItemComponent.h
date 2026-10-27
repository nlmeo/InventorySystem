#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/Manifest/INV_ItemManifest.h"
#include "INV_ItemComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) ,Blueprintable)
class INVENTORY_API UINV_ItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UINV_ItemComponent();
	void InitItemManifest(FINV_ItemManifest CopyOfManifest);
	FString GetPickupMessage() const { return PickupMessage; }
	// 返回 const 引用：manifest 内含 InstancedStruct 片段数组，按值返回会让每次调用都深拷贝一份。
	// 注意：需要修改时必须显式拷贝 -> 改 -> InitItemManifest 写回，不要再假设拿到的是可写副本。
	const FINV_ItemManifest& GetItemManifest() const { return ItemManifest; }
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	void PickedUp();
protected:
	// Be called in the public PickedUp function before the ItemComponent destroyed. 
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void OnPickedUp();
private:

	UPROPERTY(EditAnywhere, Category = "Inventory", Replicated)
	FINV_ItemManifest ItemManifest;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FString PickupMessage;
	
};
