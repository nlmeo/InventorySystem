// Copyright 2025, ProjectXHG. All right reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "INV_GridLayoutAssetFactory.generated.h"

/** 用于在内容浏览器中创建 UINV_GridLayoutAsset 的工厂 */
UCLASS()
class UINV_GridLayoutAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UINV_GridLayoutAssetFactory(const FObjectInitializer& ObjectInitializer);

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool CanCreateNew() const override;
};
