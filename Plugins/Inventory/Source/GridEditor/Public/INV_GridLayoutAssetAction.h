// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/**
 * 网格布局资产的资产类型动作：
 * 负责在内容浏览器中提供图标颜色、所属分类，以及双击时打开自定义编辑器。
 */
class FINV_GridLayoutAssetAction : public FAssetTypeActions_Base
{
public:
	FINV_GridLayoutAssetAction(EAssetTypeCategories::Type Category);

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual uint32 GetCategories() override;

private:
	EAssetTypeCategories::Type AssetCategory;
};
