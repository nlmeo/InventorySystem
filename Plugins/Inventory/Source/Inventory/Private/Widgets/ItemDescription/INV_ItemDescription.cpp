#include "Widgets/ItemDescription/INV_ItemDescription.h"
#include "Components/SizeBox.h"

FVector2D UINV_ItemDescription::GetBoxSize() const
{
	return SizeBox->GetDesiredSize();
}
