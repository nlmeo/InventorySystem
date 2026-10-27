// Copyright 2025, ProjectXHG. All right reserved.

#include "INV_GridLayoutEditorApp.h"

#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "GridEditor.h"
#include "IDetailsView.h"
#include "INV_GridLayoutAppMode.h"
#include "InputCoreTypes.h"
#include "InventoryManagement/Layout/INV_GridLayoutAsset.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "SlateWidgets/INV_GridDrawer.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

FINV_GridLayoutEditorApp::~FINV_GridLayoutEditorApp()
{
	GEditor->UnregisterForUndo(this);
}

void FINV_GridLayoutEditorApp::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FWorkflowCentricApplication::RegisterTabSpawners(InTabManager);
}

void FINV_GridLayoutEditorApp::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FWorkflowCentricApplication::UnregisterTabSpawners(InTabManager);
}

void FINV_GridLayoutEditorApp::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UObject* InObject)
{
	WorkingAsset = Cast<UINV_GridLayoutAsset>(InObject);
	if (!WorkingAsset.IsValid())
	{
		return;
	}

	// 标记为可事务，以便支持撤销/重做
	WorkingAsset->SetFlags(RF_Transactional);

	if (WorkingAsset->Slots.IsValidIndex(0) && WorkingAsset->Slots[0])
	{
		SelectedSlot = WorkingAsset->Slots[0];
	}

	TArray<UObject*> ObjectsToEdit;
	ObjectsToEdit.Add(InObject);

	GEditor->RegisterForUndo(this);

	InitAssetEditor(Mode, InitToolkitHost, TEXT("INV_GridLayoutEditor"), FTabManager::FLayout::NullLayout, true, true, ObjectsToEdit);

	AddApplicationMode(GridEditor::INVGridLayoutAppMode, MakeShareable(new FINV_GridLayoutAppMode(SharedThis(this))));
	SetCurrentMode(GridEditor::INVGridLayoutAppMode);

	// 工具栏提供 Add/Remove Slot 快捷入口，与属性面板中的按钮行为一致
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender());

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("GridLayoutSlots");

			ToolbarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([this]()
				{
					OnAddNewSlotClicked();
				})),
				NAME_None,
				FText::FromString(TEXT("Add Slot")),
				FText::FromString(TEXT("在布局末尾新增一个网格面板")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
			);

			ToolbarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([this]()
				{
					OnRemoveSelectedSlotClicked();
				})),
				NAME_None,
				FText::FromString(TEXT("Remove Selected Slot")),
				FText::FromString(TEXT("移除当前选中的网格面板")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")
			);

			ToolbarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([this]()
				{
					OnRemoveAllSlotsClicked();
				})),
				NAME_None,
				FText::FromString(TEXT("Remove All Slots")),
				FText::FromString(TEXT("移除全部网格面板")),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")
			);

			ToolbarBuilder.EndSection();
		})
	);

	AddToolbarExtender(ToolbarExtender);
	RegenerateMenusAndToolbars();
}

TSharedRef<SWidget> FINV_GridLayoutEditorApp::CreateGridSlotsContainer()
{
	GridSlotsRootOverlay = SAssignNew(GridSlotsRootOverlay, SOverlay);
	FillSlotsOverlay();

	return GridSlotsRootOverlay.ToSharedRef();
}

TSharedRef<SWidget> FINV_GridLayoutEditorApp::CreatePropertiesWidget()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bSearchInitialKeyFocus = true;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bShowOptions = true;
	DetailsViewArgs.bShowModifiedPropertiesOption = false;
	DetailsViewArgs.bShowScrollBar = false;
	DetailsViewArgs.NotifyHook = this;

	// 资产整体定义的 Details View
	TSharedPtr<IDetailsView> AssetDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	AssetDetailsView->SetObject(GetWorkingAsset());

	// 当前选中面板定义的 Details View
	SelectedSlotDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	SelectedSlotDetailsView->SetObject(SelectedSlot.IsValid() ? SelectedSlot.Get() : nullptr);

	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(5.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.OnClicked(this, &FINV_GridLayoutEditorApp::OnAddNewSlotClicked)
				.Content()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Add Slot")))
					.Justification(ETextJustify::Center)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(5.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.OnClicked(this, &FINV_GridLayoutEditorApp::OnRemoveSelectedSlotClicked)
				.Content()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Remove Selected Slot")))
					.Justification(ETextJustify::Center)
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.f, 5.f, 0.f, 0.f)
		[
			SNew(SButton)
			.OnClicked(this, &FINV_GridLayoutEditorApp::OnRemoveAllSlotsClicked)
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Remove All Slots")))
				.Justification(ETextJustify::Center)
			]
		]
	]
	+ SVerticalBox::Slot()
	.FillHeight(1.0f)
	.HAlign(HAlign_Fill)
	[
		AssetDetailsView.ToSharedRef()
	]
	+ SVerticalBox::Slot()
	.FillHeight(1.0f)
	.HAlign(HAlign_Fill)
	[
		SelectedSlotDetailsView.ToSharedRef()
	];
}

UINV_GridLayoutAsset* FINV_GridLayoutEditorApp::GetWorkingAsset() const
{
	return WorkingAsset.IsValid() ? WorkingAsset.Get() : nullptr;
}

void FINV_GridLayoutEditorApp::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	FNotifyHook::NotifyPostChange(PropertyChangedEvent, PropertyThatChanged);

	if (!WorkingAsset.IsValid() || !GridSlotsRootOverlay.IsValid())
	{
		return;
	}

	FillSlotsOverlay();
}

void FINV_GridLayoutEditorApp::FillSlotsOverlay()
{
	if (!WorkingAsset.IsValid() || !GridSlotsRootOverlay.IsValid())
	{
		return;
	}

	GridSlotsRootOverlay->ClearChildren();

	for (int32 i = 0; i < WorkingAsset->Slots.Num(); ++i)
	{
		// 数组里可能残留空槽位（例如通过 Details 面板的 +/- 操作），跳过以免解引用空指针
		UINV_GridSlotDefinitionWrapper* SlotWrapper = WorkingAsset->Slots[i];
		if (!IsValid(SlotWrapper))
		{
			continue;
		}

		const FINV_GridSlotDefinition& SlotDefinition = SlotWrapper->SlotDefinition;
		// 格宽是整份布局共用的（LayoutDefinition.TileSize）；预览画板必须和运行时同源，
		// 否则编辑期看到的格子大小和进游戏后不一致。
		const float TileSize = WorkingAsset->LayoutDefinition.TileSize;
		const float Width = SlotDefinition.SlotDimension.X * TileSize;
		const float Height = SlotDefinition.SlotDimension.Y * TileSize;

		GridSlotsRootOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(SlotDefinition.PositionOffset.X, SlotDefinition.PositionOffset.Y, 0.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(Width)
			.HeightOverride(Height)
			[
				SNew(SINV_GridDrawer)
				.GridDimensions(SlotDefinition.SlotDimension)
				.GridSize(TileSize)
				.LineThickness(WorkingAsset->LayoutDefinition.GridLineThickness)
				.GridColor(WorkingAsset->LayoutDefinition.GridLineColor)
				.IsSelected_Lambda([this, i]()
				{
					return SelectedSlotIndex == i;
				})
				.OnMouseButtonDown_Lambda([this, i](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
					{
						if (!WorkingAsset.IsValid() || !WorkingAsset->Slots.IsValidIndex(i))
						{
							return FReply::Unhandled();
						}

						SelectedSlot = WorkingAsset->Slots[i];
						SelectedSlotIndex = i;

						if (SelectedSlotDetailsView.IsValid())
						{
							SelectedSlotDetailsView->SetObject(SelectedSlot.IsValid() ? SelectedSlot.Get() : nullptr);
						}

						// 重建布局以便正确高亮当前选中项
						FillSlotsOverlay();
						return FReply::Handled();
					}

					return FReply::Unhandled();
				})
			]
		];
	}
}

void FINV_GridLayoutEditorApp::SelectLastSlot()
{
	if (WorkingAsset.IsValid() && !WorkingAsset->Slots.IsEmpty())
	{
		SelectedSlot = WorkingAsset->Slots.Last();
		SelectedSlotIndex = WorkingAsset->Slots.Num() - 1;
	}
	else
	{
		SelectedSlotIndex = 0;
		SelectedSlot = nullptr;
	}

	if (SelectedSlotDetailsView.IsValid())
	{
		SelectedSlotDetailsView->SetObject(SelectedSlot.IsValid() ? SelectedSlot.Get() : nullptr);
	}

	FillSlotsOverlay();
}

FReply FINV_GridLayoutEditorApp::OnAddNewSlotClicked()
{
	if (!WorkingAsset.IsValid())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Adding new slot")));

	WorkingAsset->Modify();
	WorkingAsset->AddNewSlot();

	// 新生成的面板包装体需要标记为可事务，才能参与撤销/重做
	if (WorkingAsset->Slots.Last())
	{
		WorkingAsset->Slots.Last()->SetFlags(RF_Transactional);
	}

	SelectLastSlot();

	return FReply::Handled();
}

FReply FINV_GridLayoutEditorApp::OnRemoveSelectedSlotClicked()
{
	if (!WorkingAsset.IsValid() || WorkingAsset->Slots.IsEmpty())
	{
		return FReply::Handled();
	}

	const int32 IndexToRemove = WorkingAsset->Slots.IsValidIndex(SelectedSlotIndex)
		? SelectedSlotIndex
		: WorkingAsset->Slots.Num() - 1;

	const FScopedTransaction Transaction(FText::FromString(TEXT("Removing selected slot")));

	WorkingAsset->Modify();
	WorkingAsset->Slots.RemoveAt(IndexToRemove);

	SelectLastSlot();

	return FReply::Handled();
}

FReply FINV_GridLayoutEditorApp::OnRemoveAllSlotsClicked()
{
	if (!WorkingAsset.IsValid() || WorkingAsset->Slots.IsEmpty())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Removing all slots")));

	WorkingAsset->Modify();
	WorkingAsset->EmptySlots();

	SelectLastSlot();

	return FReply::Handled();
}

FName FINV_GridLayoutEditorApp::GetToolkitFName() const
{
	return FName(TEXT("INV_GridLayoutEditorApp"));
}

FText FINV_GridLayoutEditorApp::GetBaseToolkitName() const
{
	return FText::FromString(TEXT("Grid Layout Asset Editor"));
}

FString FINV_GridLayoutEditorApp::GetWorldCentricTabPrefix() const
{
	return TEXT("Grid Layout");
}

FLinearColor FINV_GridLayoutEditorApp::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FINV_GridLayoutEditorApp::GetDocumentationLink() const
{
	return TEXT("");
}

void FINV_GridLayoutEditorApp::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{
	FWorkflowCentricApplication::OnToolkitHostingStarted(Toolkit);
}

void FINV_GridLayoutEditorApp::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{
	FWorkflowCentricApplication::OnToolkitHostingFinished(Toolkit);
}

void FINV_GridLayoutEditorApp::PostUndo(bool bSuccess)
{
	HandleUndoRedo();
}

void FINV_GridLayoutEditorApp::PostRedo(bool bSuccess)
{
	HandleUndoRedo();
}

void FINV_GridLayoutEditorApp::HandleUndoRedo()
{
	SelectLastSlot();
}
