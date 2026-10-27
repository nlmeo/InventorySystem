// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FINV_GridLayoutAssetAction;

class FGridEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** 注册到 IAssetTools 的资产类型动作，ShutdownModule 时需要反注册 */
	TSharedPtr<FINV_GridLayoutAssetAction> RegisteredAssetTypeActions;
};

/** 网格布局资产编辑器相关的全局常量 */
namespace GridEditor
{
	/** 网格布局编辑器的应用模式名 */
	inline const FName INVGridLayoutAppMode = FName(TEXT("INV_GridLayoutAppMode"));

	/** 主视图（面板预览）标签页名 */
	inline const FName INVGridLayoutPrimaryTabName = FName(TEXT("INV_GridLayoutPrimaryTab"));

	/** 属性（Details）标签页名 */
	inline const FName INVGridLayoutPropertiesTabName = FName(TEXT("INV_GridLayoutPropertiesTab"));
}
