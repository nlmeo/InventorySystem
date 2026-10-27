#include "Widgets/Inventory/SlottedItems/INV_SlottedItem.h"

#include "Blueprint/DragDropOperation.h"
#include "Items/INV_InventoryItem.h"
#include "Widgets/Inventory/Payload/INV_ItemPayload.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "InventoryManagement/Utils/INV_InventoryStatics.h"

#include "Engine/World.h"
#include "TimerManager.h"

/**
 * 图标显示的分工（朝向功能）
 *
 * 【尺寸从哪来】
 *   - 本控件（SlottedItem）的尺寸 = 网格分配的「实际占地」（已含朝向，如 3x2 -> 162x108）。
 *     它决定点击热区与落位高亮的边界，不能为了图标好看去改它。
 *   - Image_Icon 的尺寸 = 「未旋转」尺寸（如 2x3 -> 108x162），由
 *     INV_InventoryGrid::AddSlottedItemToCanvas 用 UCanvasPanelSlot::SetSize 显式指定，
 *     并用 (占地-图)/2 居中（旋转时该偏移为负，靠 CanvasPanel 允许溢出）。
 *
 * 【为什么必须是 CanvasPanel】
 *   约束类容器（Overlay / SizeBox / Border 等）会把子控件限制在父的可用空间内。容器高 108
 *   装不下图的原始高 162 时，图会被压成 108x108（宽高只压了一个轴，视觉上就是「压扁」）。
 *   实测：OverlaySlot 即使设成 Center 也没用 —— 对齐只决定摆放位置，不改变可用空间。
 *
 * 【朝向由谁负责】
 *   - 本类只负责「转不转」：绕 Image 自身中心转 90°，不改布局尺寸；
 *   - 蓝图 WBP_INV_SlottedItem 只需把 Image_Icon 放进一个 CanvasPanel：
 *       * CanvasPanel 自身保持 Fill（它期望尺寸恒为 0，改成 Center 之类会塌成 0x0）；
 *       * 不要给 CanvasPanel 设固定宽高（物品尺寸随格数变）；
 *       * 不要给 Image_Icon 设固定大小，尺寸由代码每次落位重算覆盖。
 */

FReply UINV_SlottedItem::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	// 右键：打开物品操作菜单（拆分 / 丢弃 / 使用），不进入拖拽
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSlottedItemClicked.Broadcast(GridIndex, MouseEvent);
		return FReply::Handled();
	}

	// 左键：开始拖拽（物品的「拿起」已由拖拽取代点击）
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	return Super::NativeOnMouseButtonDown(Geometry, MouseEvent);
}

FReply UINV_SlottedItem::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	// 左键双击：请求快速装备 / 快速使用（具体行为由网格根据物品类别分派）
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlottedItemDoubleClicked.Broadcast(GridIndex);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

FReply UINV_SlottedItem::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	//OnSlottedItemClicked.Broadcast(GridIndex, InMouseEvent);
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UINV_SlottedItem::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
                                            UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
	
	//防止拖拽途中显示itemdescription
	//UINV_InventoryStatics::ItemUnHovered(GetOwningPlayer());
	
	//设置OutOperation  
	UDragDropOperation* Operation = NewObject<UDragDropOperation>();
	
	// UI 显示内容：拖拽跟随图元
	// 结构：固定尺寸外框(SizeBox) → CanvasPanel → 显式尺寸的 Image（朝向靠 Image 自己转）
	//
	// 外框：裸 UImage 首帧 DesiredSize 为 0，Slate 会先按鼠标位置摆，等布局算出尺寸后才按
	//       Pivot=CenterCenter 归位 → 表现为「刚拖起来图从鼠标右侧飘到中心」。固定宽高后首帧即确定。
	//       背包来源取当前几何（已含朝向）；装备来源取 brush 原始尺寸，避免装备槽的布局拉伸影响回包图元。
	// 内层：不能靠 SizeBox 居中 —— 代码构造、尚未挂载的控件上设 Slot 对齐不可靠（实测退化成 Fill，
	//       图被拉扁）。改为 CanvasPanelSlot 的显式 Size/Position，与对齐无关，且允许图溢出外框。
	// 图尺寸：背包来源用 BaseDim × 单格像素，单格像素由「外框尺寸 / OccupiedDim」反算；
	//         装备来源直接用 OnItemEquipped 写好的 brush.ImageSize，口径与背包图标一致。
	//         注意朝向不参与尺寸计算：旋转只改渲染角度，布局始终按「未旋转」尺寸走。
	// 注意：这套结构与落位图标（INV_InventoryGrid::AddSlottedItemToCanvas）一致，改一边要同步另一边。
	UINV_InventoryItem* DragItem = InventoryItem.Get();
	// 装备槽内的控件可能被父布局拉伸，不能把它的几何尺寸当作背包每格的像素尺寸。
	FVector2D DragSize = GetCachedGeometry().GetLocalSize();

	// 图元布局尺寸始终是未旋转尺寸。背包来源用 BaseDim × 单格像素；装备来源直接读 brush。
	// 背包物品只知道自己占几格（BaseDim / OccupiedDim），并不知道一格是多少像素，
	// 因此从自己的几何反算：CellUnit = 外框宽 / 占格数（高那边相等）。
	// 等价写法是网格里的 GetDrawSizeForDims(BaseDim, padding)，但那个入口在 grid 侧，这里拿不到。
	const FIntPoint BaseDim = IsValid(DragItem) ? DragItem->GetBaseDimension() : FIntPoint(1, 1);
	const FIntPoint OccupiedDim = IsValid(DragItem) ? DragItem->GetItemDimension() : FIntPoint(1, 1);
	const FVector2D BrushSize = Image_Icon->GetBrush().ImageSize;
	FVector2D IconSize = DragSize;
	if (GetDragSource() == EINV_ItemDragSource::EquippedSlot && BrushSize.X > 1.f && BrushSize.Y > 1.f)
	{
		IconSize = BrushSize;
		DragSize = IsValid(DragItem) && DragItem->IsRotated()
			? FVector2D(BrushSize.Y, BrushSize.X) : BrushSize;
	}
	else if (OccupiedDim.X > 0 && OccupiedDim.Y > 0 && DragSize.X > 1.f && DragSize.Y > 1.f)
	{
		const float CellUnit = DragSize.X / static_cast<float>(OccupiedDim.X);
		IconSize = FVector2D(BaseDim.X * CellUnit, BaseDim.Y * CellUnit);
	}

	USizeBox* GhostBox = NewObject<USizeBox>(Operation);
	UCanvasPanel* GhostCanvas = NewObject<UCanvasPanel>(GhostBox);
	GhostBox->AddChild(GhostCanvas);

	UImage* Ghost = NewObject<UImage>(GhostCanvas);
	Ghost->SetBrush(Image_Icon->GetBrush());
	Ghost->SetVisibility(ESlateVisibility::HitTestInvisible); // 拖拽图元必须不吃命中
	GhostCanvas->AddChild(Ghost);

	// 显式给尺寸并居中：旋转时绕 Image 自身中心转，转完视觉正好等于外框
	if (UCanvasPanelSlot* GhostSlot = Cast<UCanvasPanelSlot>(Ghost->Slot))
	{
		GhostSlot->SetAutoSize(false);
		GhostSlot->SetSize(IconSize);
		GhostSlot->SetPosition((DragSize - IconSize) * 0.5f);
	}

	// 外框尺寸与所选占地朝向一致；装备来源不沿用装备槽中可能被拉伸的控件尺寸。
	if (DragSize.X > 1.f && DragSize.Y > 1.f)
	{
		GhostBox->SetWidthOverride(DragSize.X);
		GhostBox->SetHeightOverride(DragSize.Y);
	}

	Operation->DefaultDragVisual = GhostBox;
	Operation->Pivot = EDragPivot::CenterCenter;
	
	// 需要传输的信息：创建 Payload 作为跨容器拖拽的数据载体
	UINV_ItemPayload* Payload = NewObject<UINV_ItemPayload>(Operation);
	Payload->InitByItem(InventoryItem.Get());
	Payload->SetDragSource(GetDragSource());
	// 跟随图元交给 Payload：拖拽中按 R 旋转时要把这张图一起转，
	// 否则「格子高亮转过去了、手里那张还横着」。
	Payload->SetDragVisual(Ghost);
	if (Payload->IsRotated())
	{
		// 起点朝向就已经是旋转态时，跟随图元一开始就要摆正
		Ghost->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Ghost->SetRenderTransformAngle(90.f);
	}

	// 便于排查尺寸问题：拖拽时打一条，只在起拖瞬间出现，不刷屏
	UE_LOG(LogTemp, Log, TEXT("[Inventory] 拖拽图元：外框=%.0fx%.0f，图=%.0fx%.0f，朝向=%s"),
		DragSize.X, DragSize.Y, IconSize.X, IconSize.Y,
		Payload->IsRotated() ? TEXT("旋转") : TEXT("未旋转"));
	Operation->Payload = Payload;
	
	OutOperation = Operation;
	
	OnSlottedItemDragStart.Broadcast(GridIndex, Payload);
}

bool UINV_SlottedItem::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UINV_ItemPayload* Payload = InOperation ? Cast<UINV_ItemPayload>(InOperation->Payload) : nullptr;
	OnSlottedItemDropped.Broadcast(GridIndex, Payload);
	return true;
}

void UINV_SlottedItem::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UINV_InventoryStatics::ItemHovered(GetOwningPlayer(), InventoryItem.Get());
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Hovered")));
}

void UINV_SlottedItem::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	UINV_InventoryStatics::ItemUnHovered(GetOwningPlayer());
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, FString::Printf(TEXT("UnHovered")));
}



void UINV_SlottedItem::SetInventoryItem(UINV_InventoryItem* Item)
{
	InventoryItem = Item;
}

void UINV_SlottedItem::SetImageBrush(const FSlateBrush& Brush) const
{
	// brush.ImageSize 由网格按「未旋转」尺寸给出（见 GetDrawSizeForDims），
	// 与 AddSlottedItemToCanvas 给图标设的显式布局尺寸一致，因此图不会被非均匀缩放。
	Image_Icon->SetBrush(Brush);
}

void UINV_SlottedItem::SetRotationVisual(bool bInRotated)
{
	if (!IsValid(Image_Icon)) return;

	// 只负责朝向：绕 Image 自身中心转 90°，不改变布局尺寸（布局由网格用 CanvasPanelSlot 给）。
	// 注意不要在这里设 Slot 对齐：约束类容器会把图夹到容器尺寸，
	// 而且代码 NewObject 出来、未挂载的控件上设置对齐并不可靠。
	Image_Icon->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Image_Icon->SetRenderTransformAngle(bInRotated ? 90.f : 0.f);
	// 清掉历史版本可能残留的缩放（旋转后再转回来会带着旧缩放）
	Image_Icon->SetRenderScale(FVector2D(1.f, 1.f));

	// 本体的 RenderTransform 保持归零：旋转只发生在图标上，容器只负责尺寸与热区
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	SetRenderTransformAngle(0.f);

	// ---- 诊断日志（排查图标尺寸/朝向问题）----
	// 布局要等一帧才算完，所以延后一帧打印一次。判读口径：
	//   Slot=CanvasPanelSlot 且 图实际 == brush   → 正常（容器 162x108 里放 108x162 的图，转 90°）
	//   图实际 == 容器且有轴被压小                → 图仍在约束类容器里（Image_Icon 没放进 CanvasPanel）
	//   角度=0 而 bInRotated=true                 → 旋转没生效，查朝向数据与调用时机
	//   Slot=OverlaySlot                          → 蓝图结构还没改，Image_Icon 仍在 Overlay 下
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UINV_SlottedItem> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick([WeakThis]()
		{
			UINV_SlottedItem* Self = WeakThis.Get();
			if (!Self || !IsValid(Self->Image_Icon)) return;

			const FVector2D BoxSize = Self->GetCachedGeometry().GetLocalSize();
			const FVector2D ImgSize = Self->Image_Icon->GetCachedGeometry().GetLocalSize();
			const FVector2D BrushSize = Self->Image_Icon->GetBrush().ImageSize;

			// 图标的父级容器是什么、对齐设成了什么 —— 用来确认蓝图那边的改动有没有生效
			FString SlotInfo = TEXT("无Slot");
			if (UPanelSlot* IconSlot = Self->Image_Icon->Slot)
			{
				SlotInfo = IconSlot->GetClass()->GetName();
				if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(IconSlot))
				{
					SlotInfo += FString::Printf(TEXT("(H=%d,V=%d)"),
						static_cast<int32>(OverlaySlot->GetHorizontalAlignment()),
						static_cast<int32>(OverlaySlot->GetVerticalAlignment()));
				}
			}
		});
	}
}

void UINV_SlottedItem::UpdateStackCount(int32 StackCount)
{
	if (StackCount > 0)
	{
		Text_StackCount->SetVisibility(ESlateVisibility::Visible);
		Text_StackCount->SetText(FText::AsNumber(StackCount));
	}
	else
	{
		Text_StackCount->SetVisibility(ESlateVisibility::Collapsed);
	}
}
