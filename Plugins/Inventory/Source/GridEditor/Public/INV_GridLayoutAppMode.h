// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/ApplicationMode.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

class FINV_GridLayoutEditorApp;
class FTabManager;

/** 网格布局编辑器目前只需要一个应用模式 */
class FINV_GridLayoutAppMode : public FApplicationMode
{
public:
	FINV_GridLayoutAppMode(TSharedPtr<FINV_GridLayoutEditorApp> InApp);

	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
	virtual void PostActivateMode() override;
	virtual void PreDeactivateMode() override;

private:
	TWeakPtr<FINV_GridLayoutEditorApp> App;
	FWorkflowAllowedTabSet Tabs;
};
