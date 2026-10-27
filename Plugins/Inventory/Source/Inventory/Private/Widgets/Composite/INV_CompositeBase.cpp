#include "Widgets/Composite/INV_CompositeBase.h"

void UINV_CompositeBase::Collapse()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UINV_CompositeBase::Expand()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

