#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "InventoryManagement/Transfer/INV_CrossContainerSwapPlanner.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "Items/INV_InventoryItem.h"
#include "Items/INV_ItemTags.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_TransferTest, "Inventory.Transfer.Atomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_TransferTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryTransferWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);

	const FName Bag(TEXT("Bag"));
	UClass* PickupClass = LoadClass<AActor>(nullptr,
		TEXT("/Inventory/Items/Consumables/BP_INV_Potion_Small_Red.BP_INV_Potion_Small_Red_C"));
	if (!TestNotNull(TEXT("Project pickup class"), PickupClass))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	auto MakeInventory = [&](AActor* ExternalOwner = nullptr)
	{
		AActor* Owner = ExternalOwner;
		if (!Owner)
		{
			APlayerController* PlayerOwner = World->SpawnActor<APlayerController>();
			PlayerOwner->Possess(World->SpawnActor<APawn>());
			Owner = PlayerOwner;
		}
		UINV_InventoryComponent* Component = NewObject<UINV_InventoryComponent>(Owner);
		Owner->AddInstanceComponent(Component);
		Component->OwningController = Cast<APlayerController>(Owner);
		UINV_GridLayoutAsset* Layout = NewObject<UINV_GridLayoutAsset>(Component);
		UINV_GridSlotDefinitionWrapper* Panel = NewObject<UINV_GridSlotDefinitionWrapper>(Layout);
		Panel->SlotDefinition.SlotId = Bag;
		Panel->SlotDefinition.SlotDimension = FIntPoint(4, 3);
		Panel->SlotDefinition.AcceptedCategory = EINV_ItemCategory::All;
		Layout->Slots.Add(Panel);
		Component->GridLayouts.Add(Layout);
		return Component;
	};

	auto AddStack = [&](UINV_InventoryComponent* Component, int32 Count, int32 Index)
	{
		FINV_ItemManifest Manifest;
		FindFProperty<FClassProperty>(FINV_ItemManifest::StaticStruct(), TEXT("PickupActorClass"))
			->SetObjectPropertyValue_InContainer(&Manifest, PickupClass);
		FEnumProperty* Category = FindFProperty<FEnumProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemCategory"));
		Category->GetUnderlyingProperty()->SetIntPropertyValue(
			Category->ContainerPtrToValuePtr<void>(&Manifest), static_cast<int64>(EINV_ItemCategory::Consumable));
		*FindFProperty<FStructProperty>(FINV_ItemManifest::StaticStruct(), TEXT("ItemType"))
			->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) = FGameplayTag(GameItems::Consumables::Potions::Red::Small);
		FINV_GridFragment Grid;
		Grid.SetGridSize(FIntPoint(1, 1));
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_GridFragment>(Grid));
		FINV_StackableFragment Stack;
		Stack.SetStackCount(Count);
		FindFProperty<FIntProperty>(FINV_StackableFragment::StaticStruct(), TEXT("MaxStacks"))
			->SetPropertyValue_InContainer(&Stack, 10);
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_StackableFragment>(Stack));
		Manifest.GetAllFragmentsMutable().Add(TInstancedStruct<FINV_ItemFragment>::Make<FINV_ConsumableFragment>());
		UINV_InventoryItem* Item = Manifest.Manifest(Component->GetOwner());
		Item->SetContainerId(Bag);
		Item->SetGridIndex(Index);
		Item->SetTotalStackCount(Count);
		Component->InventoryList.AddEntry(Item);
		return Item;
	};

	auto SubmitTransfer = [&](UINV_InventoryComponent* Source, UINV_InventoryComponent* Target,
		UINV_InventoryItem* Item, int32 Amount, int32 SourceIndex, int32 TargetIndex,
		UINV_InventoryItem* TargetItem = nullptr, AActor* TargetActor = nullptr)
	{
		FINV_InteractionContext Context(Source->GetOwner(), EINV_InteractionType::Transfer);
		Context.TargetActor = TargetActor ? TargetActor : Target->GetOwner();
		Context.TargetInventoryComponent = Target;
		Context.Item = Item;
		Context.SecondaryItem = TargetItem;
		Context.Amount = Amount;
		Context.SourceSlotId = Bag;
		Context.SourceIndex = SourceIndex;
		Context.TargetSlotId = Bag;
		Context.TargetIndex = TargetIndex;
		const int64 RequestId = Source->SubmitInventoryOperation(Context);
		return TPair<int64, FINV_InventoryOperationResult>(RequestId, Source->GetLastOperationResult());
	};

	UINV_InventoryComponent* Source = MakeInventory();
	AActor* TargetOwner = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	USceneComponent* TargetRoot = NewObject<USceneComponent>(TargetOwner, TEXT("TransferTargetRoot"));
	TargetOwner->SetRootComponent(TargetRoot);
	TargetRoot->RegisterComponent();
	UINV_InventoryComponent* Target = MakeInventory(TargetOwner);
	UINV_InventoryItem* SourceStack = AddStack(Source, 6, 0);
	const auto Partial = SubmitTransfer(Source, Target, SourceStack, 2, 0, 3);
	TestEqual(TEXT("Partial transfer returns the request id"), Partial.Value.RequestId, Partial.Key);
	TestEqual(TEXT("Partial transfer succeeds"), Partial.Value.Failure, EINV_OperationFailure::None);
	TestEqual(TEXT("Partial transfer reduces the source"), SourceStack->GetTotalStackCount(), 4);
	TestEqual(TEXT("Partial transfer creates one target entry"), Target->GetAllInventoryItems().Num(), 1);
	UINV_InventoryItem* TargetStack = Target->GetAllInventoryItems()[0];
	TestEqual(TEXT("Transferred quantity is preserved"), TargetStack->GetTotalStackCount(), 2);
	TestEqual(TEXT("Transferred item uses the requested target index"), TargetStack->GetGridIndex(), 3);

	UINV_InventoryItem* MergeSource = AddStack(Source, 4, 1);
	UINV_InventoryItem* MergeTarget = AddStack(Target, 3, 4);
	const auto Merge = SubmitTransfer(Source, Target, MergeSource, 2, 1, 4, MergeTarget);
	TestEqual(TEXT("Cross-container merge succeeds"), Merge.Value.Failure, EINV_OperationFailure::None);
	TestEqual(TEXT("Cross-container merge reduces source"), MergeSource->GetTotalStackCount(), 2);
	TestEqual(TEXT("Cross-container merge increases target"), MergeTarget->GetTotalStackCount(), 5);

	const auto InvalidLocation = SubmitTransfer(Source, Target, MergeSource, 1, 1, 99);
	TestEqual(TEXT("Invalid target location is rejected"), InvalidLocation.Value.Failure,
		EINV_OperationFailure::InvalidLocation);
	TestEqual(TEXT("Rejected transfer preserves source"), MergeSource->GetTotalStackCount(), 2);
	TestEqual(TEXT("Rejected transfer preserves target"), MergeTarget->GetTotalStackCount(), 5);

	AActor* WrongTarget = World->SpawnActor<AActor>();
	const auto InvalidTarget = SubmitTransfer(Source, Target, MergeSource, 1, 1, 5, nullptr, WrongTarget);
	TestEqual(TEXT("Mismatched target actor is rejected"), InvalidTarget.Value.Failure,
		EINV_OperationFailure::InvalidTarget);
	TestEqual(TEXT("Target mismatch preserves source"), MergeSource->GetTotalStackCount(), 2);

	const auto InvalidQuantity = SubmitTransfer(Source, Target, MergeSource, 8, 1, 5);
	TestEqual(TEXT("Excessive transfer quantity is rejected"), InvalidQuantity.Value.Failure,
		EINV_OperationFailure::InvalidQuantity);
	TestEqual(TEXT("Quantity rejection preserves source"), MergeSource->GetTotalStackCount(), 2);

	Target->GridLayouts[0]->Slots[0]->SlotDefinition.AcceptedCategory = EINV_ItemCategory::None;
	const auto NoneTarget = SubmitTransfer(Source, Target, MergeSource, 1, 1, 5);
	TestEqual(TEXT("None target rejects every category"), NoneTarget.Value.Failure,
		EINV_OperationFailure::InvalidLocation);
	TestEqual(TEXT("None target rejection preserves source"), MergeSource->GetTotalStackCount(), 2);
	Target->GridLayouts[0]->Slots[0]->SlotDefinition.AcceptedCategory = EINV_ItemCategory::All;

	Target->bAllowTransfer = false;
	const auto ClosedTarget = SubmitTransfer(Source, Target, MergeSource, 1, 1, 5);
	TestEqual(TEXT("Closed or disabled target rejects transfer"), ClosedTarget.Value.Failure,
		EINV_OperationFailure::TransferNotAllowed);
	TestEqual(TEXT("Access rejection preserves source"), MergeSource->GetTotalStackCount(), 2);
	Target->bAllowTransfer = true;
	Target->bRequireContainerOpenForTransfer = true;
	Target->SetContainerOpen(false);
	const auto ClosedContainer = SubmitTransfer(Source, Target, MergeSource, 1, 1, 5);
	TestEqual(TEXT("Closed container rejects transfer"), ClosedContainer.Value.Failure,
		EINV_OperationFailure::TransferNotAllowed);
	TestFalse(TEXT("Container reports closed state"), Target->IsContainerOpen());
	TestEqual(TEXT("Closed container preserves source"), MergeSource->GetTotalStackCount(), 2);
	Target->SetContainerOpen(true);
	TestTrue(TEXT("Container reports open state"), Target->IsContainerOpen());
	Target->GetOwner()->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	const auto DistantTarget = SubmitTransfer(Source, Target, MergeSource, 1, 1, 5);
	TestEqual(TEXT("Distant target rejects transfer"), DistantTarget.Value.Failure,
		EINV_OperationFailure::TransferNotAllowed);
	TestEqual(TEXT("Distance rejection preserves source"), MergeSource->GetTotalStackCount(), 2);

	TArray<UINV_InventoryItem*> CoveredItems;
	CoveredItems.Add(MergeTarget);
	CoveredItems.Add(TargetStack);
	TArray<FINV_CrossContainerSwapEntry> SwapPlan;
	const bool bPlanBuilt = FINV_CrossContainerSwapPlanner::BuildPlan(
		Source, SourceStack, CoveredItems, SwapPlan);
	TestTrue(TEXT("Cross-container planner builds a multi-item plan"), bPlanBuilt);
	TestEqual(TEXT("Planner emits one entry per covered item"), SwapPlan.Num(), CoveredItems.Num());
	for (UINV_InventoryItem* Covered : CoveredItems)
	{
		const FINV_CrossContainerSwapEntry* Entry = SwapPlan.FindByPredicate(
			[Covered](const FINV_CrossContainerSwapEntry& Candidate) { return Candidate.Item == Covered; });
		TestNotNull(TEXT("Planner keeps item-to-destination identity"), Entry);
		if (Entry)
		{
			TestTrue(TEXT("Planner selects a valid destination slot"), !Entry->TargetSlotId.IsNone());
			TestTrue(TEXT("Planner selects a valid destination index"), Entry->TargetIndex >= 0);
		}
	}

	FINV_InteractionContext MissingPlanContext(Source->GetOwner(), EINV_InteractionType::Transfer);
	MissingPlanContext.Item = SourceStack;
	MissingPlanContext.TargetInventoryComponent = Target;
	MissingPlanContext.TargetActor = Target->GetOwner();
	MissingPlanContext.SourceSlotId = Bag;
	MissingPlanContext.SourceIndex = SourceStack->GetGridIndex();
	MissingPlanContext.TargetSlotId = Bag;
	MissingPlanContext.TargetIndex = MergeTarget->GetGridIndex();
	MissingPlanContext.Amount = 1;
	TestEqual(TEXT("Cross-container non-stack swap without a client plan is rejected"),
		Source->SwapItemsBetweenInventories(MissingPlanContext, Target, MergeTarget),
		EINV_OperationFailure::InvalidRequest);

	FINV_InteractionContext InvalidPlanContext = MissingPlanContext;
	FINV_CrossContainerSwapEntry InvalidPlanEntry;
	InvalidPlanEntry.Item = MergeTarget;
	InvalidPlanEntry.TargetSlotId = FName(TEXT("Unknown"));
	InvalidPlanEntry.TargetIndex = 0;
	InvalidPlanContext.CrossContainerSwapEntries.Add(InvalidPlanEntry);
	TestEqual(TEXT("Cross-container plan with an unknown target slot is rejected"),
		Source->SwapItemsBetweenInventories(InvalidPlanContext, Target, MergeTarget),
		EINV_OperationFailure::InvalidLocation);

	InvalidPlanContext.CrossContainerSwapEntries[0].TargetSlotId = Bag;
	InvalidPlanContext.CrossContainerSwapEntries[0].TargetIndex = 99;
	TestEqual(TEXT("Cross-container plan with an out-of-range index is rejected"),
		Source->SwapItemsBetweenInventories(InvalidPlanContext, Target, MergeTarget),
		EINV_OperationFailure::InvalidLocation);

	InvalidPlanContext.CrossContainerSwapEntries[0].Item = SourceStack;
	InvalidPlanContext.CrossContainerSwapEntries[0].TargetIndex = MergeTarget->GetGridIndex();
	TestEqual(TEXT("Cross-container plan for an item outside the target inventory is rejected"),
		Source->SwapItemsBetweenInventories(InvalidPlanContext, Target, MergeTarget),
		EINV_OperationFailure::InvalidLocation);

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
