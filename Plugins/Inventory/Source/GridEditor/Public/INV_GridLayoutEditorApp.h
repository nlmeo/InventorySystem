// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Misc/NotifyHook.h"
#include "WorkflowOrientedApp/WorkflowCentricApplication.h"

class UINV_GridLayoutAsset;
class UINV_GridSlotDefinitionWrapper;
class IToolkitHost;
class IDetailsView;
class SOverlay;
class SWidget;

struct FPropertyChangedEvent;

/**
 * 网格布局资产的自定义编辑器 App。
 * 左侧标签页可视化展示每个网格面板的格子，点击面板后右侧 Details 编辑其定义；
 * 支持 Add / Remove Slot（带事务与 Undo/Redo），属性改动后实时刷新预览。
 */
class FINV_GridLayoutEditorApp : public FWorkflowCentricApplication, public FEditorUndoClient, public FNotifyHook
{
public:
	~FINV_GridLayoutEditorApp();

	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetDocumentationLink() const override;
	virtual void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit) override;

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	void HandleUndoRedo();

	// FNotifyHook：Details 面板属性改动后刷新预览
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* InObject);

	/** 创建主视图容器（各网格面板的预览） */
	TSharedRef<SWidget> CreateGridSlotsContainer();

	/** 创建属性面板（资产整体定义 + 当前选中面板的定义） */
	TSharedRef<SWidget> CreatePropertiesWidget();

	UINV_GridLayoutAsset* GetWorkingAsset() const;

private:
	/** 依据当前资产数据重建所有网格面板预览 */
	void FillSlotsOverlay();

	/** 选中最后一个面板（或清空选中）并刷新预览 */
	void SelectLastSlot();

	FReply OnAddNewSlotClicked();
	FReply OnRemoveSelectedSlotClicked();
	FReply OnRemoveAllSlotsClicked();

	/** 当前编辑的资产 */
	TWeakObjectPtr<UINV_GridLayoutAsset> WorkingAsset = nullptr;

	/** 当前在预览区选中的面板包装体 */
	TWeakObjectPtr<UINV_GridSlotDefinitionWrapper> SelectedSlot = nullptr;

	/** 当前选中面板在 Slots 数组中的下标 */
	int32 SelectedSlotIndex = 0;

	/** 预览区根容器 */
	TSharedPtr<SOverlay> GridSlotsRootOverlay = nullptr;

	/** 编辑当前选中面板定义的 Details View */
	TSharedPtr<IDetailsView> SelectedSlotDetailsView = nullptr;
};
