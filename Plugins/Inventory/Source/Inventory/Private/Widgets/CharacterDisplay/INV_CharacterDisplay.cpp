#include "Widgets/CharacterDisplay/INV_CharacterDisplay.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "EquipmentManagement/ProxyMesh/INV_ProxyMesh.h"
#include "Kismet/GameplayStatics.h"


void UINV_CharacterDisplay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	
	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(this, AINV_ProxyMesh::StaticClass(), Actors);
	
	if (!Actors.IsValidIndex(0)) return;
	
	AINV_ProxyMesh* ProxyMesh = Cast<AINV_ProxyMesh>(Actors[0]);
	if (!IsValid(ProxyMesh)) return;
	
	Mesh = ProxyMesh->GetMesh();
}

FReply UINV_CharacterDisplay::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	CurrentMousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	LastMousePosition = CurrentMousePosition;
	
	bIsDragging = true;
	return FReply::Handled();
}

FReply UINV_CharacterDisplay::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bIsDragging = false;
	return FReply::Handled();
}

void UINV_CharacterDisplay::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bIsDragging = false;
}


void UINV_CharacterDisplay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	
	if (!bIsDragging) return;
	
	LastMousePosition = CurrentMousePosition;
	CurrentMousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	
	const float HorizontalDelta = LastMousePosition.X - CurrentMousePosition.X;
	if (!Mesh.IsValid()) return;
	
	Mesh->AddRelativeRotation(FRotator(0.0f, HorizontalDelta, 0.0f));
}
