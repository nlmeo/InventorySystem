// Copyright 2025, ProjectXHG. All right reserved.

#include "INV_GridLayoutAssetFactory.h"

#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"

UINV_GridLayoutAssetFactory::UINV_GridLayoutAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UINV_GridLayoutAsset::StaticClass();
	// 允许在内容浏览器「新建」菜单中创建本资产，并在创建后立即打开
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UINV_GridLayoutAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UINV_GridLayoutAsset>(InParent, InName, Flags);
}

bool UINV_GridLayoutAssetFactory::CanCreateNew() const
{
	return true;
}
