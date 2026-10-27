#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Binding/BrushBinding.h"
#include "Storage/Nodes/FileEntry.h"
#include "StructUtils/InstancedStruct.h"
#include "INV_ItemFragment.generated.h"

class AINV_EquipActor;
class UINV_CompositeBase;
class APlayerController;

USTRUCT(BlueprintType)
struct FINV_ItemFragment
{
	GENERATED_BODY()
	
	FINV_ItemFragment() {};
	FINV_ItemFragment(const FINV_ItemFragment&) = default;
	FINV_ItemFragment(FINV_ItemFragment&&) = default;
	FINV_ItemFragment& operator=(const FINV_ItemFragment&) = default;
	FINV_ItemFragment& operator=(FINV_ItemFragment&&) = default;
	virtual ~FINV_ItemFragment() {};
	
	FGameplayTag GetFragmentTag() const { return FragmentTag; };
	void SetFragmentTag(const FGameplayTag& tag) { FragmentTag = tag; };
	virtual void Manifest() {}
	
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories="FragmentTags"))
	FGameplayTag FragmentTag = FGameplayTag::EmptyTag;
};

/**
 * Item fragment specifically for assimilation into a widget
 */
USTRUCT(BlueprintType)
struct FINV_InventoryItemFragment : public FINV_ItemFragment
{
	GENERATED_BODY()
	virtual void Assimilate(UINV_CompositeBase* Composite) const;
protected:
	bool MatchesWidgetTag(const UINV_CompositeBase* Composite) const;
};

USTRUCT(BlueprintType)
struct FINV_ImageFragment : public FINV_InventoryItemFragment
{
	GENERATED_BODY()
	
	UTexture2D* GetIcon() const { return Icon; };
	
	virtual void Assimilate(UINV_CompositeBase* Composite) const override;
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TObjectPtr<UTexture2D> Icon{nullptr};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FVector2D IconDimensions{44.f,44.f};
};


USTRUCT(BlueprintType)
struct FINV_TextFragment : public FINV_InventoryItemFragment
{
	GENERATED_BODY()
	
	FText GetText() const { return FragmentText; };
	void SetText(const FText& Text) { FragmentText = Text; };
	virtual void Assimilate(UINV_CompositeBase* Composite) const override;
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FText FragmentText;
	
};

USTRUCT(BlueprintType)
struct FINV_LabeledNumberFragment : public FINV_InventoryItemFragment
{
	GENERATED_BODY()

	// When manifesting for the first time, this fragment will randomize. However, once equipped
	// and dropped, an item should retain the same value, so randomization should not occur.
	bool bRandomizeOnManifest{true};
	
	virtual void Assimilate(UINV_CompositeBase* Composite) const override;
	virtual void Manifest() override;
	float GetValue() const { return Value; }
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FText Text_Label{};
	
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	float Value{0.f};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float Min{0.f};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float Max{0.f};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	bool bCollapseLabel{false};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	bool bCollapseValue{false};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MinFractionalDigits{1};


	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MaxFractionalDigits{1};
};

USTRUCT(BlueprintType)
struct FINV_GridFragment : public FINV_ItemFragment
{
	GENERATED_BODY()
	
	FIntPoint GetGridSize() const { return GridSize; };
	void SetGridSize(const FIntPoint& Size) { GridSize = Size; };
	float GetGridPadding() const { return GridPadding; };
	void SetGridPadding(float Padding) { GridPadding = Padding; };
	
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FIntPoint GridSize{1,1};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float GridPadding{0.f};
};



USTRUCT(BlueprintType)
struct FINV_StackableFragment : public FINV_ItemFragment
{
	GENERATED_BODY()
	int32 GetMaxStackSize() const { return MaxStacks; };
	int32 GetStackCount() const { return StackCount; };
	void SetStackCount(int32 Count) { StackCount = Count; };
	
private:

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MaxStacks{1};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 StackCount{1};
	
};


// Consume Fragments 
USTRUCT(BlueprintType)
struct FINV_ConsumeModifier : public FINV_LabeledNumberFragment
{
	GENERATED_BODY()
	virtual void OnConsume(APlayerController* PC) {};
};

// Composite Pattern
USTRUCT(BlueprintType)
struct FINV_ConsumableFragment : public FINV_InventoryItemFragment
{
	GENERATED_BODY()
	virtual void OnConsume(APlayerController* PC);
	virtual void Assimilate(UINV_CompositeBase* Composite) const override;
	virtual void Manifest() override;
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory", meta=(ExcludeBaseStruct))
	TArray<TInstancedStruct<FINV_ConsumeModifier>> ConsumeModifiers;
	
};

USTRUCT()
struct FINV_HealthPotionFragment : public FINV_ConsumeModifier
{
	GENERATED_BODY()

	virtual void OnConsume(APlayerController* PC) override;
};

USTRUCT()
struct FINV_ManaPotionFragment : public FINV_ConsumeModifier
{
	GENERATED_BODY()
	
	virtual void OnConsume(APlayerController* PC) override;
};

// Equipment


USTRUCT(BlueprintType)
struct FINV_EquipmentModifier : public FINV_LabeledNumberFragment
{
	GENERATED_BODY()
	
	virtual void OnEquip(APlayerController* PC) {};
	virtual void OnUnequip(APlayerController* PC) {};

};

USTRUCT(BlueprintType)
struct FINV_StrengthModifier : public FINV_EquipmentModifier
{
	GENERATED_BODY()
	
	virtual void OnEquip(APlayerController* PC) override;
	virtual void OnUnequip(APlayerController* PC) override;

};



USTRUCT(BlueprintType)
struct FINV_EquipmentFragment : public FINV_InventoryItemFragment
{
	GENERATED_BODY()
	
	bool bEquipable{false};
	virtual void OnEquip(APlayerController* PC);
	virtual void OnUnequip(APlayerController* PC);
	virtual void Assimilate(UINV_CompositeBase* Composite) const override;
	virtual void Manifest() override;
	
	AINV_EquipActor* SpawnAttachedActor(USkeletalMeshComponent* AttachMesh) const;
	void DestroyAttachedActor() const;
	FGameplayTag GetEquipmentType() const { return EquipmentType;}
	
	void SetEquippedActor(AINV_EquipActor* EquipActor);
private:
	
	UPROPERTY(EditAnywhere, Category = "Inventory", meta=(ExcludeBaseStruct))
	TArray<TInstancedStruct<FINV_EquipmentModifier>> EquipModifiers;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<AINV_EquipActor> EquipActorClass = nullptr;
	
	TWeakObjectPtr<AINV_EquipActor> EquippedActor = nullptr;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FName SocketAttackPoint{NAME_None};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FGameplayTag EquipmentType = FGameplayTag::EmptyTag;
};