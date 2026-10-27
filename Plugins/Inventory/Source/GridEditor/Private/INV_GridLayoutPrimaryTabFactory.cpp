// Copyright 2025, ProjectXHG. All right reserved.

#include "INV_GridLayoutPrimaryTabFactory.h"

#include "GridEditor.h"
#include "INV_GridLayoutEditorApp.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"

FINV_GridLayoutPrimaryTabFactory::FINV_GridLayoutPrimaryTabFactory(TSharedPtr<FINV_GridLayoutEditorApp> InApp)
	: FWorkflowTabFactory(GridEditor::INVGridLayoutPrimaryTabName, InApp), App(InApp)
{
	TabLabel = FText::FromString(TEXT("Grid Layout View"));
	ViewMenuDescription = FText::FromString(TEXT("Displays a primary view for grid layout asset"));
	ViewMenuTooltip = FText::FromString(TEXT("Show the primary grid layout view"));
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports");
}

TSharedRef<SWidget> FINV_GridLayoutPrimaryTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FINV_GridLayoutEditorApp> PinnedApp = App.Pin();

	return SNew(SScrollBox)
	+ SScrollBox::Slot()
	.VAlign(VAlign_Fill)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(10.f)
		[
			PinnedApp->CreateGridSlotsContainer()
		]
	];
}

FText FINV_GridLayoutPrimaryTabFactory::GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const
{
	return FText::FromString(TEXT("A primary view for displaying the grid layout"));
}
