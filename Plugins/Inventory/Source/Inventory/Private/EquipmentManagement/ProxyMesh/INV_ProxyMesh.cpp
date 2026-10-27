#include "EquipmentManagement/ProxyMesh/INV_ProxyMesh.h"

#include "EquipmentManagement/Components/INV_EquipmentComponent.h"
#include "GameFramework/Character.h"
#include "Engine/GameInstance.h"


AINV_ProxyMesh::AINV_ProxyMesh()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	
	RootComponent = CreateDefaultSubobject<USceneComponent>("Root");
	ProxyMesh = CreateDefaultSubobject<USkeletalMeshComponent>("Mesh");
	ProxyMesh->SetupAttachment(RootComponent);
	
	EquipmentComponent = CreateDefaultSubobject<UINV_EquipmentComponent>("Equipment");
	EquipmentComponent->SetOwningSkeletalMesh(ProxyMesh);
	EquipmentComponent->SetIsProxy(true);
}


void AINV_ProxyMesh::BeginPlay()
{
	Super::BeginPlay();
	DelayedInitialization();
}

void AINV_ProxyMesh::DelayedInitializeOwner()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		DelayedInitialization();
		return;
	}
	
	
	//APlayerController* PC = World->GetFirstPlayerController();
	
	UGameInstance* GameInstance = World->GetGameInstance();
	APlayerController* PC = IsValid(GameInstance) ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	
	
	if (!IsValid(PC))
	{
		DelayedInitialization();
		return;
	}
	
	ACharacter* Character = Cast<ACharacter>(PC->GetPawn());
	if (!IsValid(Character))
	{
		DelayedInitialization();
		return;
	}
	
	USkeletalMeshComponent* CharacterMesh = Character->GetMesh();
	if (!IsValid(CharacterMesh))
	{
		DelayedInitialization();
		return;
	}
	SourceMesh = CharacterMesh;
	ProxyMesh->SetSkeletalMesh(SourceMesh->GetSkeletalMeshAsset());
	if (UAnimInstance* AnimInstance = SourceMesh->GetAnimInstance())
	{
		ProxyMesh->SetAnimInstanceClass(AnimInstance->GetClass());
	}
	EquipmentComponent->InitializeOwner(PC);
}

void AINV_ProxyMesh::DelayedInitialization()
{
	FTimerDelegate TimerDelegate;
	TimerDelegate.BindUObject(this, &ThisClass::DelayedInitializeOwner);
	GetWorld()->GetTimerManager().SetTimerForNextTick(TimerDelegate);
}


