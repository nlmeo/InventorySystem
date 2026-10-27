// Copyright 2025, ProjectXHG. All right reserved.

#include "INV_GridLayoutPropertiesTabFactory.h"

#include "GridEditor.h"
#include "INV_GridLayoutEditorApp.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"

FINV_GridLayoutPropertiesTabFactory::FINV_GridLayoutPropertiesTabFactory(TSharedPtr<FINV_GridLayoutEditorApp> InApp)
	: FWorkflowTabFactory(GridEditor::INVGridLayoutPropertiesTabName, InApp), App(InApp)
{
	TabLabel = FText::FromString(TEXT("Properties"));
	ViewMenuDescription = FText::FromString(TEXT("Displays properties for grid layout asset"));
	ViewMenuTooltip = FText::FromString(TEXT("Show the properties view"));
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details");
}

TSharedRef<SWidget> FINV_GridLayoutPropertiesTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FINV_GridLayoutEditorApp> PinnedApp = App.Pin();
	return PinnedApp->CreatePropertiesWidget();
}

FText FINV_GridLayoutPropertiesTabFactory::GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const
{
	return FText::FromString(TEXT("A property view for the current asset"));
}
