#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Actors/INV_TestContainer.h"
#include "Interaction/INV_ContainerInteractable.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Widgets/Inventory/Container/INV_ContainerInventoryWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FINV_ContainerInteractionTest, "Inventory.Container.Interaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FINV_ContainerInteractionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("InventoryContainerInteractionWorld"),
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);

	AINV_TestContainer* Container = World->SpawnActor<AINV_TestContainer>();
	AActor* Interactor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);

	TestNotNull(TEXT("Test container has an inventory component"), Container);
	if (Container)
	{
		Container->bCanInteract = true;
		Container->InteractionDistance = 250.0f;
		TestTrue(TEXT("Test container implements the container interface"),
			Container->GetClass()->ImplementsInterface(UINV_ContainerInteractable::StaticClass()));
		TestTrue(TEXT("Direct container access accepts nearby interactor"),
			Container->CanInteractWith_Implementation(Interactor));
		TestNotNull(TEXT("Container inventory component is valid"), Container->GetContainerInventory_Implementation());
		TestTrue(TEXT("Unified container access accepts nearby interactor"),
			INV_ContainerInteraction::CanInteract(Container, Interactor));
		TestTrue(TEXT("Nearby player can interact with container"),
			INV_ContainerInteraction::CanInteract(Container, Interactor));

		Container->SetActorLocation(FVector(1000.0f, 0.0f, 0.0f));
		TestFalse(TEXT("Distant player cannot interact with container"),
			INV_ContainerInteraction::CanInteract(Container, Interactor));

		Container->SetActorLocation(FVector::ZeroVector);
		Container->bCanInteract = false;
		TestFalse(TEXT("Disabled container cannot interact"),
			INV_ContainerInteraction::CanInteract(Container, Interactor));

		UINV_InventoryComponent* PlayerInventory = NewObject<UINV_InventoryComponent>(Interactor);
		TestNotNull(TEXT("Player inventory exists for container UI session"), PlayerInventory);
		if (PlayerInventory)
		{
			PlayerInventory->RegisterComponent();
			UINV_ContainerInventoryWidget* ContainerWidget = CreateWidget<UINV_ContainerInventoryWidget>(
				World, UINV_ContainerInventoryWidget::StaticClass());
			TestNotNull(TEXT("Container UI widget can be created"), ContainerWidget);
			if (ContainerWidget)
			{
				ContainerWidget->InitializeContainer(Container, Container->GetContainerInventory_Implementation());
				TestFalse(TEXT("Container UI without a bound SourceGrid refuses initialization"), ContainerWidget->IsContainerOpen());
				TestNull(TEXT("Container UI clears source inventory when SourceGrid is missing"),
					ContainerWidget->GetContainerInventory());
				ContainerWidget->CloseSource();
				TestFalse(TEXT("Container UI session closes cleanly"), ContainerWidget->IsContainerOpen());
				TestNull(TEXT("Container UI clears container reference on close"), ContainerWidget->GetContainerActor());
			}
		}
	}

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
