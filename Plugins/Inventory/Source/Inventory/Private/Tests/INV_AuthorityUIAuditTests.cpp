#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_AuthorityUIAuditTest, "Inventory.Audit.AuthoritySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_AuthorityUIAuditTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryAuthorityAuditWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	const FName Bag(TEXT("Bag"));
	const FGameplayTag Weapon = FGameplayTag::RequestGameplayTag(TEXT("GameItems.Equipment.Weapons"));
	auto MakeInventory = [&]()
	{
		AActor* Owner = World->SpawnActor<AActor>();
		UINV_InventoryComponent* Component = NewObject<UINV_InventoryComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		UINV_GridLayoutAsset* Layout = NewObject<UINV_GridLayoutAsset>(Component);
		UINV_GridSlotDefinitionWrapper* Panel = NewObject<UINV_GridSlotDefinitionWrapper>(Layout);
		Panel->SlotDefinition.SlotId = Bag;
		Panel->SlotDefinition.SlotDimension = FIntPoint(4, 3);
		Panel->SlotDefinition.AcceptedCategory = EINV_ItemCategory::All;
		Layout->Slots.Add(Panel);
		Component->GridLayouts.Add(Layout);
		return Component;
	};
	auto AddItem = [&](UINV_InventoryComponent* Component, FIntPoint Size, int32 Index, int32 Count)
	{
		FINV_ItemManifest Manifest;
		FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		Category->GetUnderlyingProperty()->SetIntPropertyValue(Category->ContainerPtrToValuePtr<void>(&Manifest),
			static_cast<int64>(Count > 0 ? EINV_ItemCategory::Consumable : EINV_ItemCategory::Equippable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = Count > 0
				? FGameplayTag(GameItems::Consumables::Potions::Red::Small) : FGameplayTag(GameItems::Equipment::Weapons::Knife);
		FINV_GridFragment Grid;
		Grid.SetGridSize(Size);
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(Grid));
		if (Count > 0)
		{
			FINV_StackableFragment Stack;
			Stack.SetStackCount(Count);
			FindFProperty<FIntProperty>(FINV_StackableFragment::StaticStruct(), TEXT("MaxStacks"))
				->SetPropertyValue_InContainer(&Stack, 10);
			Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_StackableFragment>(Stack));
		}
		else Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_EquipmentFragment>());
		UINV_InventoryItem* Item = Manifest.Manifest(Component->GetOwner());
		Item->SetContainerId(Bag);
		Item->SetGridIndex(Index);
		Item->SetTotalStackCount(Count);
		Component->InventoryList.AddEntry(Item);
		return Item;
	};
	UINV_InventoryComponent* Component = MakeInventory();
	TestTrue(TEXT("Native default enables authoritative UI flag"), Component->bServerAuthoritativeUI);
	UINV_InventoryItem* Item = AddItem(Component, FIntPoint(2, 1), 0, 4);
	UINV_InventoryItem* Blocker = AddItem(Component, FIntPoint(1, 1), 2, 2);
	AddExpectedError(TEXT("MoveItem 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
	Component->Server_MoveItem(Item, Bag, 0, Bag, 2, true);
	TestEqual(TEXT("Rejected move leaves authoritative index unchanged"), Item->GetGridIndex(), 0);
	TestFalse(TEXT("Rejected move restores trial rotation"), Item->IsRotated());
	AddExpectedError(TEXT("MoveItems 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
	Component->Server_MoveItems({Item, Blocker}, {4, 4}, Bag, {true, false});
	TestEqual(TEXT("Rejected batch preserves first position"), Item->GetGridIndex(), 0);
	TestEqual(TEXT("Rejected batch preserves second position"), Blocker->GetGridIndex(), 2);
	TestFalse(TEXT("Rejected batch restores trial rotation"), Item->IsRotated());

	FINV_ItemStateSnapshot Baseline;
	Baseline.Item = Item;
	Baseline.ContainerId = Bag;
	Baseline.GridIndex = 0;
	Baseline.StackCount = 4;
	Item->SetContainerId(TEXT("PredictedBag"));
	Item->SetGridIndex(8);
	Item->SetTotalStackCount(9);
	Item->SetRotated(true);
	Item->SetEquippedSlotType(Weapon);
	Component->Client_RefreshInventoryFromServer_Implementation({Baseline});
	TestEqual(TEXT("Snapshot restores container"), Item->GetContainerId(), Bag);
	TestEqual(TEXT("Snapshot restores position"), Item->GetGridIndex(), 0);
	TestEqual(TEXT("Snapshot restores quantity"), Item->GetTotalStackCount(), 4);
	TestEqual(TEXT("Snapshot restores fragment quantity"), Item->GetItemManifest().GetFragmentOfType<FINV_StackableFragment>()->GetStackCount(), 4);
	TestFalse(TEXT("Snapshot restores rotation"), Item->IsRotated());
	TestFalse(TEXT("Snapshot restores equipment membership"), Item->IsEquipped());
	FINV_ItemStateSnapshot Versioned = Baseline;
	Versioned.GridIndex = 4;
	Component->Client_RefreshInventoryFromServer_Implementation({Versioned}, 10, 10);
	Component->Client_RefreshInventoryFromServer_Implementation({Baseline}, 9, 9);
	TestEqual(TEXT("Older versioned snapshot cannot roll back position"), Item->GetGridIndex(), 4);
	Component->Client_RefreshInventoryFromServer_Implementation({Baseline});
	TestEqual(TEXT("Legacy unversioned snapshot cannot roll back versioned state"), Item->GetGridIndex(), 4);
	bool bNotified = false;
	Component->OnInventoryRefreshRequested.AddLambda([&]() { bNotified = true; });
	Component->FlushRefresh();
	TestTrue(TEXT("Snapshot schedules UI refresh notification"), bNotified);
	Component->OnInventoryRefreshRequested.Clear();

	// Diagnostic probes below record existing limitations, not requirements that these behaviours are correct.
	FINV_ItemStateSnapshot Newer = Baseline;
	Newer.GridIndex = 4;
	Component->Client_RefreshInventoryFromServer_Implementation({Newer});
	Component->Client_RefreshInventoryFromServer_Implementation({Baseline});
	const bool bOldSnapshotApplied = Item->GetGridIndex() == 0;
	Component->FlushRefresh();
	Item->SetGridIndex(2);
	Item->SetRotated(true);
	AddExpectedError(TEXT("MoveItem 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
	Component->Server_MoveItem(Item, Bag, 0, Bag, 2, true);
	Component->RequestAuthoritativeRefresh();
	Component->FlushRefresh();
	const bool bLocalPredictionSurvivedRejection = Item->GetGridIndex() == 2 && Item->IsRotated();
	Component->Client_RefreshInventoryFromServer_Implementation({Baseline});
	Component->FlushRefresh();
	Component->bServerAuthoritativeUI = false;
	Component->RequestAuthoritativeRefresh();
	Component->NotifyItemReplicatedChange(Item);
	const bool bOptimisticFlagDisablesCorrection = !Component->bRefreshScheduled;
	Component->bServerAuthoritativeUI = true;

	UINV_InventoryItem* OldEquipment = AddItem(Component, FIntPoint(1, 1), INDEX_NONE, 0);
	OldEquipment->SetContainerId(NAME_None);
	OldEquipment->SetEquippedSlotType(Weapon);
	UINV_InventoryItem* NewEquipment = AddItem(Component, FIntPoint(1, 1), 4, 0);
	Component->Server_EquipSlotClicked(NewEquipment, OldEquipment, Weapon);
	AddExpectedError(TEXT("MoveItem 被拒"), EAutomationExpectedErrorFlags::Contains, 1);
	Component->Server_MoveItem(OldEquipment, NAME_None, INDEX_NONE, Bag, 2, false);
	const bool bPartialEquipmentCommit = NewEquipment->IsEquipped() && !OldEquipment->IsEquipped()
		&& OldEquipment->GetGridIndex() == INDEX_NONE && OldEquipment->GetContainerId().IsNone();

	UINV_InventoryComponent* Counts = MakeInventory();
	UINV_InventoryItem* From = AddItem(Counts, FIntPoint(1, 1), 0, 4);
	UINV_InventoryItem* To = AddItem(Counts, FIntPoint(1, 1), 1, 9);
	Counts->Server_MergeStack(From, To, 4);
	const bool bMergeExceededMaximum = To->GetTotalStackCount() > 10;
	const int32 BeforeUse = To->GetTotalStackCount();
	Counts->Server_UseItem(To, -1);
	const bool bNegativeUseAddedQuantity = To->GetTotalStackCount() > BeforeUse;
	TestFalse(TEXT("Server rejects over-capacity merge"), bMergeExceededMaximum);
	TestEqual(TEXT("Rejected merge preserves source quantity"), From->GetTotalStackCount(), 4);
	TestEqual(TEXT("Rejected merge preserves target quantity"), To->GetTotalStackCount(), 9);
	TestFalse(TEXT("Server rejects negative use without increasing quantity"), bNegativeUseAddedQuantity);
	const FString Observations = FString::Printf(
		TEXT("{\n  \"snapshot_field_restore_tests_passed\": %s,\n  \"older_snapshot_overwrote_newer_state\": %s,\n")
		TEXT("  \"standalone_prediction_survived_rejection\": %s,\n  \"optimistic_flag_disables_correction\": %s,\n")
		TEXT("  \"equipment_chain_partially_committed\": %s,\n  \"server_merge_exceeded_stack_limit\": %s,\n")
		TEXT("  \"negative_use_increased_quantity\": %s\n}\n"),
		HasAnyErrors() ? TEXT("false") : TEXT("true"), bOldSnapshotApplied ? TEXT("true") : TEXT("false"),
		bLocalPredictionSurvivedRejection ? TEXT("true") : TEXT("false"), bOptimisticFlagDisablesCorrection ? TEXT("true") : TEXT("false"),
		bPartialEquipmentCommit ? TEXT("true") : TEXT("false"), bMergeExceededMaximum ? TEXT("true") : TEXT("false"),
		bNegativeUseAddedQuantity ? TEXT("true") : TEXT("false"));
	FFileHelper::SaveStringToFile(Observations, *(FPaths::ProjectSavedDir() / TEXT("AuthorityUIAudit/observations.json")));
	AddInfo(TEXT("Known-gap diagnostics exported to Saved/AuthorityUIAudit/observations.json; automation success does not certify these gaps as correct."));
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
