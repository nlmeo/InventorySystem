// Copyright 2025, ProjectXHG. All right reserved.

#include "INV_GridLayoutAppMode.h"

#include "GridEditor.h"
#include "INV_GridLayoutEditorApp.h"
#include "INV_GridLayoutPrimaryTabFactory.h"
#include "INV_GridLayoutPropertiesTabFactory.h"

FINV_GridLayoutAppMode::FINV_GridLayoutAppMode(TSharedPtr<FINV_GridLayoutEditorApp> InApp)
	: FApplicationMode(GridEditor::INVGridLayoutAppMode), App(InApp)
{
	// 注册本模式下的标签页（主视图 + 属性面板）
	Tabs.RegisterFactory(MakeShareable(new FINV_GridLayoutPrimaryTabFactory(InApp)));
	Tabs.RegisterFactory(MakeShareable(new FINV_GridLayoutPropertiesTabFactory(InApp)));

	TabLayout = FTabManager::NewLayout("INV_GridLayoutAppMode_Layout_v1")->AddArea
	(
		FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)->Split
		(
			FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()->SetSizeCoefficient(0.7f)->AddTab(GridEditor::INVGridLayoutPrimaryTabName, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()->SetSizeCoefficient(0.3f)->AddTab(GridEditor::INVGridLayoutPropertiesTabName, ETabState::OpenedTab)
			)
		)
	);
}

void FINV_GridLayoutAppMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FINV_GridLayoutEditorApp> PinnedApp = App.Pin();
	PinnedApp->PushTabFactories(Tabs);

	FApplicationMode::RegisterTabFactories(InTabManager);
}

void FINV_GridLayoutAppMode::PostActivateMode()
{
	FApplicationMode::PostActivateMode();
}

void FINV_GridLayoutAppMode::PreDeactivateMode()
{
	FApplicationMode::PreDeactivateMode();
}
