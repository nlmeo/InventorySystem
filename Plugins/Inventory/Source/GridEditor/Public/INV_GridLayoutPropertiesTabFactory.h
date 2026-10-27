// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FINV_GridLayoutEditorApp;

/** 属性标签页：编辑资产整体定义与当前选中面板的定义 */
class FINV_GridLayoutPropertiesTabFactory : public FWorkflowTabFactory
{
public:
	FINV_GridLayoutPropertiesTabFactory(TSharedPtr<FINV_GridLayoutEditorApp> InApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;

private:
	TWeakPtr<FINV_GridLayoutEditorApp> App;
};
