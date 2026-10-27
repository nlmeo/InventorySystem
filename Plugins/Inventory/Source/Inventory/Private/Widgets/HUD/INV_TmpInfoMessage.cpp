#include "Widgets/HUD/INV_TmpInfoMessage.h"
#include "Components/TextBlock.h"


void UINV_TmpInfoMessage::SetMessage(const FText& Message)
{
	Text_Message->SetText(Message);
	if (!bIsMessageActive)
	{
		MessageShow();
		bIsMessageActive = true;
	}

	GetWorld()->GetTimerManager().SetTimer(MessageTimer, [this]()
		{
			MessageHide();
			bIsMessageActive = false;
		}, MessageLifeTime, false);
}

void UINV_TmpInfoMessage::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Text_Message->SetText(FText::GetEmpty());
	MessageHide();
}
