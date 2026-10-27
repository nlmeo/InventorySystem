// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * 网格绘制控件：在 OnPaint 中按格子尺寸绘制网格线，
 * 选中时使用黄色高亮，否则使用资产定义的网格线颜色。
 */
class SINV_GridDrawer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SINV_GridDrawer)
		: _OnMouseButtonDown()
		, _GridSize(50.f)
		, _LineThickness(2.f)
		, _GridDimensions(FIntPoint(0, 0))
		, _IsSelected(false)
		, _GridColor(FLinearColor::White)
		{ }

	SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)

	SLATE_ATTRIBUTE(float, GridSize)
	SLATE_ATTRIBUTE(float, LineThickness)
	SLATE_ATTRIBUTE(FIntPoint, GridDimensions)
	SLATE_ATTRIBUTE(bool, IsSelected)
	SLATE_ATTRIBUTE(FLinearColor, GridColor)

	SLATE_END_ARGS()

	/** 使用 InArgs 构造控件 */
	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** 单个格子的像素尺寸 */
	float GridSize = 0.f;

	/** 网格线粗细 */
	float LineThickness = 0.f;

	/** 网格列数 / 行数 */
	FIntPoint GridDimensions = FIntPoint(0, 0);

	/** 是否被选中（选中时高亮） */
	bool bIsSelected = false;

	/** 网格线颜色 */
	FLinearColor GridColor = FLinearColor::White;
};
