// Copyright 2025, ProjectXHG. All right reserved.

#include "GridEditor.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "INV_GridLayoutAssetAction.h"

void FGridEditorModule::StartupModule()
{
	// 注册「Grid Inventory」资产分类，并把网格布局资产的资产类型动作注册到内容浏览器
	IAssetTools& AssetTools = IAssetTools::Get();

	const EAssetTypeCategories::Type GridInventoryCategory =
		AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("GridInventory")), FText::FromString(TEXT("Grid Inventory")));

	RegisteredAssetTypeActions = MakeShareable(new FINV_GridLayoutAssetAction(GridInventoryCategory));
	AssetTools.RegisterAssetTypeActions(RegisteredAssetTypeActions.ToSharedRef());
}

void FGridEditorModule::ShutdownModule()
{
	// 模块卸载时反注册资产类型动作，避免悬垂引用
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")) && RegisteredAssetTypeActions.IsValid())
	{
		IAssetTools::Get().UnregisterAssetTypeActions(RegisteredAssetTypeActions.ToSharedRef());
		RegisteredAssetTypeActions.Reset();
	}
}

IMPLEMENT_MODULE(FGridEditorModule, GridEditor)
