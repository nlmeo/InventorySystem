#include "Actors/INV_TestContainer.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"

AINV_TestContainer::AINV_TestContainer()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// Inventory items are registered replicated subobjects by the container's
	// InventoryComponent. The owning actor must opt into the registered list as
	// well, otherwise newly transferred entries never reach SourceGrid clients.
	bReplicateUsingRegisteredSubObjectList = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ContainerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContainerMesh"));
	ContainerMesh->SetupAttachment(Root);
	ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InteractionCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(Root);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	InteractionCollision->SetBoxExtent(FVector(60.0f, 60.0f, 60.0f));

	InventoryComponent = CreateDefaultSubobject<UINV_InventoryComponent>(TEXT("InventoryComponent"));
}

UINV_InventoryComponent* AINV_TestContainer::GetContainerInventory_Implementation()
{
	return InventoryComponent;
}

bool AINV_TestContainer::CanInteractWith_Implementation(AActor* Interactor)
{
	if (!bCanInteract || !IsValid(Interactor) || InteractionDistance <= 0.0f) return false;

	const AActor* InteractorLocationActor = Interactor;
	if (const AController* Controller = Cast<AController>(Interactor))
	{
		InteractorLocationActor = Controller->GetPawn();
	}
	if (!IsValid(InteractorLocationActor)) return false;
	return FVector::DistSquared(InteractorLocationActor->GetActorLocation(), GetActorLocation())
		<= FMath::Square(InteractionDistance);
}
