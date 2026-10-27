// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FINV_GridLayoutEditorApp;

/** 主视图标签页：展示资产内所有网格面板的格子预览 */
class FINV_GridLayoutPrimaryTabFactory : public FWorkflowTabFactory
{
public:
	FINV_GridLayoutPrimaryTabFactory(TSharedPtr<FINV_GridLayoutEditorApp> InApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;

private:
	TWeakPtr<FINV_GridLayoutEditorApp> App;
};
