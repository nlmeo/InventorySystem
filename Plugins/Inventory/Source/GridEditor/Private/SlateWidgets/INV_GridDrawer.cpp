// Copyright 2025, ProjectXHG. All right reserved.

#include "SlateWidgets/INV_GridDrawer.h"

void SINV_GridDrawer::Construct(const FArguments& InArgs)
{
	if (InArgs._OnMouseButtonDown.IsBound())
	{
		SetOnMouseButtonDown(InArgs._OnMouseButtonDown);
	}

	GridSize = InArgs._GridSize.Get();
	LineThickness = InArgs._LineThickness.Get();
	GridDimensions = InArgs._GridDimensions.Get();
	bIsSelected = InArgs._IsSelected.Get();
	GridColor = InArgs._GridColor.Get();

	SetCanTick(true);
}

int32 SINV_GridDrawer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FLinearColor LineColor = bIsSelected ? FLinearColor::Yellow : GridColor;

	TArray<FVector2D> LinePoints;

	const int32 GridXSize = GridDimensions.X;
	const int32 GridYSize = GridDimensions.Y;

	// 绘制竖线
	for (int32 x = 0; x <= GridXSize; ++x)
	{
		LinePoints.Reset();
		LinePoints.Add(FVector2D(x * GridSize, 0));
		LinePoints.Add(FVector2D(x * GridSize, GridYSize * GridSize));

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			LineColor,
			false,
			LineThickness
		);
	}

	// 绘制横线
	for (int32 y = 0; y <= GridYSize; ++y)
	{
		LinePoints.Reset();
		LinePoints.Add(FVector2D(0, y * GridSize));
		LinePoints.Add(FVector2D(GridXSize * GridSize, y * GridSize));

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			LineColor,
			false,
			LineThickness
		);
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}
