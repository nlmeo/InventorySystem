#include "InventoryManagement/Utils/INV_InventoryStatics.h"

#include "IDetailTreeNode.h"
#include "InventoryManagement/Components/INV_InventoryComponent.h"
#include "Items/Components/INV_ItemComponent.h"
#include "Widgets/Inventory/InventoryBase/INV_InventoryBase.h"
UINV_InventoryComponent* UINV_InventoryStatics::GetInventoryComponent(const APlayerController* PlayerController)
{
    if (!IsValid(PlayerController)) return nullptr;

    UINV_InventoryComponent* InventoryComponent = PlayerController->FindComponentByClass<UINV_InventoryComponent>();

    return InventoryComponent;
}

EINV_ItemCategory UINV_InventoryStatics::GetInventoryCategoryFromItemComponent(UINV_ItemComponent* ItemComponent)
{
    if (!IsValid(ItemComponent)) return EINV_ItemCategory::None;
    return ItemComponent->GetItemManifest().GetItemCategory();
}

void UINV_InventoryStatics::ItemHovered(APlayerController* PC, UINV_InventoryItem* Item)
{
    UINV_InventoryComponent* IC = GetInventoryComponent(PC);
    if (!IsValid(IC)) return;
    
    UINV_InventoryBase* InventoryBase = IC->GetInventoryMenu();
    if (!IsValid(InventoryBase)) return;
    
    if (InventoryBase->HasHoverItem()) return;
    
    InventoryBase->OnItemHovered(Item);
}

void UINV_InventoryStatics::ItemUnHovered(APlayerController* PC)
{
    UINV_InventoryComponent* IC = GetInventoryComponent(PC);
    if (!IsValid(IC)) return;
    
    UINV_InventoryBase* InventoryBase = IC->GetInventoryMenu();
    if (!IsValid(InventoryBase)) return;
    
    InventoryBase->OnItemUnHovered();
}

UINV_ItemPayload* UINV_InventoryStatics::GetHoverItem(APlayerController* PC)
{
    UINV_InventoryComponent* IC = GetInventoryComponent(PC);
    if (!IsValid(IC)) return nullptr;
    
    UINV_InventoryBase* InventoryBase = IC->GetInventoryMenu();
    if (!IsValid(InventoryBase)) return nullptr;
    
    return InventoryBase->GetHoverItem();
    
}

UINV_InventoryBase* UINV_InventoryStatics::GetInventoryWidget(APlayerController* PC)
{
    UINV_InventoryComponent* IC = GetInventoryComponent(PC);
    if (!IsValid(IC)) return nullptr;
    
    return IC->GetInventoryMenu();
}
