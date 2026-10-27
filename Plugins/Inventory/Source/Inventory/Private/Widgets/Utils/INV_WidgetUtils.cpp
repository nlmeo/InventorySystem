#include "Widgets/Utils/INV_WidgetUtils.h"

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/Widget.h"

int32 UINV_WidgetUtils::GetIndexFromPosition(const FIntPoint& Position, const int32 Columns)
{
	return Position.X + Position.Y * Columns;
}

FIntPoint UINV_WidgetUtils::GetPositionFromIndex(const int32 Index, const int32 Columns)
{
	return FIntPoint(Index % Columns, Index / Columns);
}

FVector2D UINV_WidgetUtils::GetWidgetPosition(UWidget* Widget)
{
	const FGeometry Geometry = Widget->GetCachedGeometry();
	FVector2D PixelPosition;
	FVector2D ViewportPosition;
	USlateBlueprintLibrary::LocalToViewport(
		Widget, 
		Geometry, 
		USlateBlueprintLibrary::GetLocalTopLeft(Geometry), 
		PixelPosition, 
		ViewportPosition
	);
	return ViewportPosition;
	
}

FVector2D UINV_WidgetUtils::GetWidgetSize(UWidget* Widget)
{
	const FGeometry Geometry = Widget->GetCachedGeometry();
	return Geometry.GetLocalSize();
}

bool UINV_WidgetUtils::IsWithinBounds(const FVector2D& BoundaryPos, const FVector2D& WidgetSize,
                                      const FVector2D& MousePos)
{
	return MousePos.X >= BoundaryPos.X && MousePos.X <= (BoundaryPos.X + WidgetSize.X) &&
		   MousePos.Y >= BoundaryPos.Y && MousePos.Y <= (BoundaryPos.Y + WidgetSize.Y);	
}

FVector2D UINV_WidgetUtils::GetClampedWidgetPosition(const FVector2D& Boundary, const FVector2D& WidgetSize,
	const FVector2D& MousePos)
{
	FVector2D ClampedPosition = MousePos;
	
	// 检查是否左右越界
	if (MousePos.X + WidgetSize.X  > Boundary.X)
	{
		ClampedPosition.X = Boundary.X - WidgetSize.X;
	}
	
	if (MousePos.X < 0.f)
	{
		ClampedPosition.X = 0.f;
	}
	
	
	if (MousePos.Y + WidgetSize.Y > Boundary.Y)
	{
		ClampedPosition.Y = Boundary.Y - WidgetSize.Y;
	}
	
	if (MousePos.Y < 0.f)
	{
		ClampedPosition.Y = 0.f;
	}
	
	
	return ClampedPosition;
}
