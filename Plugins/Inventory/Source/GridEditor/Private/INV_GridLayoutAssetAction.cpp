// Copyright 2025, ProjectXHG. All right reserved.

#include "INV_GridLayoutAssetAction.h"

#include "INV_GridLayoutEditorApp.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"

FINV_GridLayoutAssetAction::FINV_GridLayoutAssetAction(EAssetTypeCategories::Type Category)
	: AssetCategory(Category)
{
}

FText FINV_GridLayoutAssetAction::GetName() const
{
	return FText::FromString(TEXT("Grid Layout Asset"));
}

FColor FINV_GridLayoutAssetAction::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FINV_GridLayoutAssetAction::GetSupportedClass() const
{
	return UINV_GridLayoutAsset::StaticClass();
}

void FINV_GridLayoutAssetAction::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	// 若在关卡编辑器内打开则使用 WorldCentric，否则独立窗口
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		UINV_GridLayoutAsset* GridLayoutAsset = Cast<UINV_GridLayoutAsset>(Object);
		if (IsValid(GridLayoutAsset))
		{
			TSharedRef<FINV_GridLayoutEditorApp> Editor = MakeShared<FINV_GridLayoutEditorApp>();
			Editor->InitEditor(Mode, EditWithinLevelEditor, GridLayoutAsset);
		}
	}
}

uint32 FINV_GridLayoutAssetAction::GetCategories()
{
	return AssetCategory;
}
