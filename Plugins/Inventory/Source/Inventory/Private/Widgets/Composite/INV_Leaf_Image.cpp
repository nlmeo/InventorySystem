#include "Widgets/Composite/INV_Leaf_Image.h"

#include "Components/Image.h"
#include "Components/SizeBox.h"


void UINV_Leaf_Image::SetImage(UTexture2D* Texture)
{
	Image_Icon->SetBrushFromTexture(Texture);
}

void UINV_Leaf_Image::SetBoxSize(const FVector2D& Size)
{
	SizeBox_Icon->SetWidthOverride(Size.X);
	SizeBox_Icon->SetHeightOverride(Size.Y);
}

void UINV_Leaf_Image::SetImageSize(const FVector2D& Size)
{
	Image_Icon->SetDesiredSizeOverride(Size);
}

FVector2D UINV_Leaf_Image::GetImageSize() const
{
	return Image_Icon->GetDesiredSize();
}
