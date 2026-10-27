#include "Items/Fragments/INV_ItemFragment.h"

#include "EquipmentManagement/EquipActor/INV_EquipActor.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Composite/INV_CompositeBase.h"
#include "Widgets/Composite/INV_Leaf_Image.h"
#include "Widgets/Composite/INV_Leaf_LabeledValue.h"
#include "Widgets/Composite/INV_Leaf_Text.h"

void FINV_InventoryItemFragment::Assimilate(UINV_CompositeBase* Composite) const
{
	if (!MatchesWidgetTag(Composite))
	{
		return;
	}
	Composite->Expand();
}

bool FINV_InventoryItemFragment::MatchesWidgetTag(const UINV_CompositeBase* Composite) const
{
	return Composite->GetFragmentTag().MatchesTagExact(GetFragmentTag());
}

void FINV_ImageFragment::Assimilate(UINV_CompositeBase* Composite) const
{
	FINV_InventoryItemFragment::Assimilate(Composite);
	if (!MatchesWidgetTag(Composite)) return;
	
	UINV_Leaf_Image* Image = Cast<UINV_Leaf_Image>(Composite);
	if (!IsValid(Image)) return;
	
	Image->SetImage(Icon);
	Image->SetBoxSize(IconDimensions);
	Image->SetImageSize(IconDimensions);
}

void FINV_TextFragment::Assimilate(UINV_CompositeBase* Composite) const
{
	FINV_InventoryItemFragment::Assimilate(Composite);
	if (!MatchesWidgetTag(Composite)) return;
	
	UINV_Leaf_Text* Text = Cast<UINV_Leaf_Text>(Composite);
	if (!IsValid(Text)) return;
	
	Text->SetText(FragmentText);
	
}

void FINV_LabeledNumberFragment::Assimilate(UINV_CompositeBase* Composite) const
{
	FINV_InventoryItemFragment::Assimilate(Composite);
	if (!MatchesWidgetTag(Composite)) return;
	
	UINV_Leaf_LabeledValue* LabeledValue = Cast<UINV_Leaf_LabeledValue>(Composite);
	if (!IsValid(LabeledValue)) return;
	
	LabeledValue->SetText_Label(Text_Label, bCollapseLabel);

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = MinFractionalDigits;
	Options.MaximumFractionalDigits = MaxFractionalDigits;
	LabeledValue->SetText_Value(FText::AsNumber(Value, &Options), bCollapseValue);

}

void FINV_LabeledNumberFragment::Manifest()
{
	FINV_InventoryItemFragment::Manifest();
	
	if (bRandomizeOnManifest)
	{
		Value = FMath::FRandRange(Min, Max);
	}
	bRandomizeOnManifest = false;
}

void FINV_ConsumableFragment::OnConsume(APlayerController* PC)
{
	for (auto& Modifier : ConsumeModifiers)
	{
		auto& ModifierRef = Modifier.GetMutable();
		ModifierRef.OnConsume(PC);
	}
}

void FINV_ConsumableFragment::Manifest()
{
	FINV_InventoryItemFragment::Manifest();
	for (auto& Modifier : ConsumeModifiers)
	{
		auto& ModifierRef = Modifier.GetMutable();
		ModifierRef.Manifest();
	}
	
}

void FINV_ConsumableFragment::Assimilate(UINV_CompositeBase* Composite) const
{
	FINV_InventoryItemFragment::Assimilate(Composite);
	for (const auto& Modifier : ConsumeModifiers)
	{
		const auto& ModifierRef = Modifier.Get();
		ModifierRef.Assimilate(Composite);
	}
}



void FINV_HealthPotionFragment::OnConsume(APlayerController* PC)
{
	// Get a Stats component from the PC or the PC->GetPawn()
	// Or get the Ability System Component and apply a Gameplay Effect
	// or call an interface function for Healing()
	
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Health Potion consumed! Healing by: %f"), GetValue()));
}

void FINV_ManaPotionFragment::OnConsume(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, FString::Printf(TEXT("Mana Potion consumed! Mana by: %f"), GetValue()));
}

void FINV_StrengthModifier::OnEquip(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Strength increased by: %f"), GetValue()));
}

void FINV_StrengthModifier::OnUnequip(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Strength decreased by: %f"), GetValue()));
}

void FINV_EquipmentFragment::OnEquip(APlayerController* PC)
{
	if (bEquipable) return;
	bEquipable = true;
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModifierRef = Modifier.GetMutable();
		ModifierRef.OnEquip(PC);
	}
}

void FINV_EquipmentFragment::OnUnequip(APlayerController* PC)
{
	if (!bEquipable) return;
	bEquipable = false;
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModifierRef = Modifier.GetMutable();
		ModifierRef.OnUnequip(PC);
	}
}

void FINV_EquipmentFragment::Assimilate(UINV_CompositeBase* Composite) const
{
	FINV_InventoryItemFragment::Assimilate(Composite);
	
	for (const auto& Modifier : EquipModifiers)
	{
		const auto& ModifierRef = Modifier.Get();
		ModifierRef.Assimilate(Composite);
	}
}

void FINV_EquipmentFragment::Manifest()
{
	FINV_InventoryItemFragment::Manifest();
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModifierRef = Modifier.GetMutable();
		ModifierRef.Manifest();
	}
}

AINV_EquipActor* FINV_EquipmentFragment::SpawnAttachedActor(USkeletalMeshComponent* AttachMesh) const
{
	if (!IsValid(EquipActorClass) || !IsValid(AttachMesh)) return nullptr;
	
	AINV_EquipActor* SpawnActor = AttachMesh->GetWorld()->SpawnActor<AINV_EquipActor>(EquipActorClass);
	SpawnActor->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketAttackPoint);
	return SpawnActor;
}

void FINV_EquipmentFragment::DestroyAttachedActor() const
{
	if (EquippedActor.IsValid())
	{
		EquippedActor->Destroy();
	}
}

void FINV_EquipmentFragment::SetEquippedActor(AINV_EquipActor* EquipActor)
{
	EquippedActor = EquipActor;
}
