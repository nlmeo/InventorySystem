#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryManagement/FastArray/INV_FastArray.h"
#include "Types/INV_OperationTypes.h"
#include "INV_InventoryComponent.generated.h"
class UINV_InventoryItem;
class UINV_InventoryBase;
class UINV_ItemComponent;
class UINV_GridLayoutAsset;
struct FINV_GridSlotDefinition;
struct FINV_ItemManifest;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInventoryItemChange, UINV_InventoryItem*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoRoomInInventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStackChange, const FINV_SlotAvailabilityResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FItemEquipStatusChanged, UINV_InventoryItem*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FItemInventoryMenuToggled, bool, bOpen);
DECLARE_MULTICAST_DELEGATE(FInventoryRefreshRequested);
DECLARE_MULTICAST_DELEGATE_OneParam(FInventoryOperationCompleted, const FINV_InventoryOperationResult&);
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent),Blueprintable)
class INVENTORY_API UINV_InventoryComponent : public UActorComponent
{
	GENERATED_BODY()
public:	
	UINV_InventoryComponent();
	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	int64 SubmitInventoryOperation(FINV_InteractionContext Context);
	int64 SubmitInventoryOperationFromServer(FINV_InteractionContext Context);
	UFUNCTION(BlueprintPure, Category = "Inventory|Operations")
	bool IsInventoryRequestPending(int64 RequestId) const;
	UFUNCTION(BlueprintPure, Category = "Inventory|Operations")
	FINV_InventoryOperationResult GetLastOperationResult() const { return LastOperationResult; }
	FInventoryOperationCompleted OnInventoryOperationCompleted;
	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Transfer")
	bool CanAcceptTransfer(AActor* Interactor, AActor* SourceOwner) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Transfer")
	void SetContainerOpen(bool bOpen);
	UFUNCTION(BlueprintPure, Category = "Inventory|Transfer")
	bool IsContainerOpen() const { return bContainerOpen; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Transfer")
	bool bAllowTransfer = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Transfer")
	bool bRequireContainerOpenForTransfer = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory|Transfer")
	bool bContainerOpen = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Transfer", meta = (ClampMin = "0.0"))
	float TransferInteractionDistance = 250.0f;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	void TryAddItem(UINV_ItemComponent* ItemComponent);
	UFUNCTION(Server, Reliable)
	void Server_PickupItem(UINV_ItemComponent* ItemComponent, FGameplayTag EmptyEquipmentSlot);
	UFUNCTION(Client, Reliable)
	void Client_PickupResult(int32 AddedCount, int32 RemainingCount);
	UFUNCTION(Server, Reliable)
	void Server_AddNewItem(UINV_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);
	UFUNCTION(Server, Reliable)
	void Server_AddStacksToItem(UINV_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);
	UFUNCTION(Server, Reliable)
	void Server_DropItem(UINV_InventoryItem* Item, int32 StackCount);
	UFUNCTION(Server, Reliable)
	void Server_DropEquippedItem(UINV_InventoryItem* Item, FGameplayTag ExpectedSlotType);
	UFUNCTION(Server, Reliable)
	void Server_UseItem(UINV_InventoryItem* Item, int32 StackCount = 1);
	UFUNCTION(Server, Reliable)
	void Server_EquipSlotClicked(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToUnequip,
		FGameplayTag EquipmentSlotType);
	UFUNCTION(Server, Reliable)
	void Server_EquipTransaction(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToUnequip,
		FGameplayTag EquipmentSlotType, FName ReturnSlotId, int32 ReturnIndex, bool bReturnRotated);
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_EquipSlotClicked(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToUnequip);
	UFUNCTION(Server, Reliable)
	void Server_MoveEquippedItem(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToSwap,
		FGameplayTag TargetSlotType, FGameplayTag SourceSlotType);
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_MoveEquippedItem(UINV_InventoryItem* ItemToEquip, UINV_InventoryItem* ItemToSwap);
	UFUNCTION(Server, Reliable)
	void Server_RegisterItemPlacement(UINV_InventoryItem* Item, FName SlotId, int32 Index, bool bRotated);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_MoveItem(UINV_InventoryItem* Item, FName FromSlotId, int32 FromIndex, FName ToSlotId, int32 ToIndex,
		bool bRotated = false);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_SwapItems(UINV_InventoryItem* ItemA, FName SlotIdA, int32 IndexA,
		UINV_InventoryItem* ItemB, FName SlotIdB, int32 IndexB);
	UFUNCTION(Server, Reliable)
	void Server_MoveItems(const TArray<UINV_InventoryItem*>& Items, const TArray<int32>& ToIndices, FName SlotId,
		const TArray<bool>& RotatedStates);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_SplitStack(UINV_InventoryItem* Item, FName SlotId, int32 FromIndex, int32 SplitAmount, int32 ToIndex);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_SwapStackCounts(UINV_InventoryItem* ItemA, UINV_InventoryItem* ItemB);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_MergeStack(UINV_InventoryItem* FromItem, UINV_InventoryItem* ToItem, int32 Amount);
	void TestMoveOwnerOverlap();
	void ToggleInventoryMenu();
	void AddRepSubObj(UObject* SubObj);
	void RemoveRepSubObj(UObject* SubObj);
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Net")
	bool bServerAuthoritativeUI = true;
	void RequestAuthoritativeRefresh();
	void RequestLocalRefreshIfAuthority();
	void ScheduleRefresh();
	void NotifyItemReplicatedChange(UINV_InventoryItem* Item);
	void SyncReplicatedEquipmentEvents();
	void BroadcastEquipmentEvent(UINV_InventoryItem* Item, bool bEquip);
	FInventoryRefreshRequested OnInventoryRefreshRequested;
	TArray<UINV_InventoryItem*> GetAllInventoryItems() const { return InventoryList.GetAllItems(); }
	bool SpawnDroppedItem(UINV_InventoryItem* Item, int32 StackCount, APlayerController* SpawnController = nullptr);
	EINV_OperationFailure DropItemFromInteractor(APlayerController* Interactor, UINV_InventoryItem* Item, int32 StackCount);
	UINV_InventoryBase* GetInventoryMenu() const { return InventoryMenu; } 
	EINV_OperationFailure TransferAndEquipFromInventory(UINV_InventoryComponent* SourceInventory,
		UINV_InventoryItem* SourceItem, FGameplayTag EquipmentSlotType);
	EINV_OperationFailure SwapItemsBetweenInventories(const FINV_InteractionContext& Context,
		UINV_InventoryComponent* TargetInventory, UINV_InventoryItem* TargetItem);
	bool RehostInventoryItem(UINV_InventoryItem* Item, UINV_InventoryComponent* SourceInventory,
		UINV_InventoryComponent* TargetInventory, FName TargetSlot, int32 TargetIndex);
	bool IsMenuOpen() const {return bInventoryMenuOpen;}
	UPROPERTY(EditAnywhere, Category = "Inventory|Grid")
	TArray<TObjectPtr<UINV_GridLayoutAsset>> GridLayouts;
	const FINV_GridSlotDefinition* GetGridPanel(FName InSlotId, const UINV_GridLayoutAsset** OutLayout = nullptr) const;
	const FINV_GridSlotDefinition* FindGridPanelForCategory(EINV_ItemCategory Category, const UINV_GridLayoutAsset** OutLayout = nullptr) const;
	void GetAllGridPanels(TArray<const FINV_GridSlotDefinition*>& OutPanels) const;
	UFUNCTION(BlueprintPure, Category = "Inventory")
	FIntPoint GetGridSize(FName InSlotId) const;
	FInventoryItemChange OnItemAdded;
	FInventoryItemChange OnItemRemoved;
	FNoRoomInInventory NoRoomInInventory;
	FStackChange OnStackChange;
	FItemEquipStatusChanged OnItemEquipped;
	FItemEquipStatusChanged OnItemUnequipped;
	FItemInventoryMenuToggled OnInventoryMenuToggled;
protected:
	virtual void BeginPlay() override;
	virtual void ReadyForReplication() override;
private:
	friend class FINV_PickupLifecycleTest;
	friend class FINV_ProxyEquipmentTest;
	friend class FINV_AuthorityUIAuditTest;
	friend class FINV_ServerValidationTest;
	friend class FINV_EquippedDropTest;
	friend class FINV_DropSpawnTest;
	friend class FINV_OperationProtocolTest;
	friend class FINV_SplitRotationTest;
	friend class FINV_TransferTest;
	void ConstructInventory();
	void ValidateGridLayouts() const;
	UINV_InventoryItem* CreateItemEntry(UINV_ItemComponent* ItemComponent, int32 AmountThisEntry,
		FName InSlotId = NAME_None, int32 Index = INDEX_NONE, bool bRotated = false,
		FGameplayTag EquipmentSlotType = FGameplayTag());
	void AddPickupOnServer(UINV_ItemComponent* ItemComponent, FGameplayTag EmptyEquipmentSlot);
	bool FindPickupPlacement(const FINV_ItemManifest& Manifest, FName& OutSlotId,
		int32& OutIndex, bool& bOutRotated) const;
	bool CanAutoEquipPickup(const FINV_ItemManifest& Manifest, FGameplayTag SlotType) const;
	UINV_InventoryItem* CreateItemEntryFromItem(UINV_InventoryItem* SourceItem, int32 AmountThisEntry, int32 GridIndex, FName SlotId, bool bRotated);
	bool IsValidInventoryItem(UINV_InventoryItem* Item) const;
	bool IsValidGridIndex(FName InSlotId, int32 Index) const;
	bool IsValidGridIndexForItem(int32 Index, const UINV_InventoryItem* Item) const;
	FName ResolveItemSlotId(const UINV_InventoryItem* Item) const;
	bool IsItemInContainer(const UINV_InventoryItem* Item, FName SlotId) const;
	FName ResolveContainerSlotId(const UINV_InventoryItem* Item, FName InSlotId) const;
	void SetItemStackCount(UINV_InventoryItem* Item, int32 NewCount);
	bool CanItemFitAt(FName SlotId, int32 Index, const UINV_InventoryItem* Item,
		const UINV_InventoryItem* IgnoreA = nullptr, const UINV_InventoryItem* IgnoreB = nullptr,
		UINV_InventoryItem** OutBlocking = nullptr, bool bIgnoreSelf = true,
		TOptional<FIntPoint> DimensionsOverride = TOptional<FIntPoint>()) const;
	void SetItemGridIndex(UINV_InventoryItem* Item, FName SlotId, int32 NewIndex);
	EINV_OperationFailure TransferItemToInventory(const FINV_InteractionContext& Context,
		UINV_InventoryComponent* TargetInventory);
	UFUNCTION(Server, Reliable)
	void Server_SubmitInventoryOperation(int64 RequestId, const FINV_InteractionContext& Context);
	UFUNCTION(Client, Reliable)
	void Client_InventoryOperationCompleted(const FINV_InventoryOperationResult& Result, const FINV_InventoryStateFrame& Frame);
	void ProcessInventoryOperation(int64 RequestId, const FINV_InteractionContext& Context);
	void DeliverOperationResult(const FINV_InventoryOperationResult& Result, const FINV_InventoryStateFrame& Frame);
	FINV_InventoryStateFrame CaptureInventoryStateFrame() const;
	int32 FindFirstFreeGridIndex(FName SlotId, const UINV_InventoryItem* Item,
		bool& bOutRotated, const UINV_InventoryItem* IgnoreItem = nullptr) const;
	UFUNCTION(Server, Reliable)
	void Server_RequestAuthoritativeRefresh();
	UFUNCTION(Client, Reliable)
	void Client_RefreshInventoryFromServer(const TArray<FINV_ItemStateSnapshot>& States, int64 Revision = 0, int64 ProcessedSequence = 0);
	void Client_RefreshInventoryFromServer_Implementation(const TArray<FINV_ItemStateSnapshot>& States, int64 Revision, int64 ProcessedSequence);
	void Client_RefreshInventoryFromServer_Implementation(const TArray<FINV_ItemStateSnapshot>& States);
	void FlushRefresh();
	bool bRefreshScheduled{false};
	bool bEquipmentSyncScheduled{false};
	int64 NextClientRequestId{1};
	int64 NextServerRequestId{1LL << 60};
	int64 AuthorityRevision{0};
	int64 CompletionSequence{0};
	TMap<int64, EINV_InteractionType> PendingOperations;
	TMap<int64, FINV_InventoryOperationResult> CompletedOperations;
	TSet<TObjectPtr<UINV_InventoryItem>> KnownReplicatedEquippedItems;
	FINV_InventoryOperationResult LastOperationResult;
	FINV_InventoryStateFrame LastOperationStateFrame;
	int64 LastRefreshRevision{0};
	int64 LastRefreshCompletionSequence{0};
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated)
	FINV_InventoryFastArray InventoryList;
	TWeakObjectPtr<APlayerController> OwningController;
	UPROPERTY()
	TObjectPtr<UINV_InventoryBase> InventoryMenu;
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UINV_InventoryBase> InventoryMenuClass;
	bool bInventoryMenuOpen;
	void OpenInventoryMenu();
	void CloseInventoryMenu();
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnAngleMin = -85.f;
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnAngleMax = 85.f;
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnDistanceMin = 10.f;
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnDistanceMax = 50.f;
	UPROPERTY(editAnywhere, Category = "Inventory")
	float RelativeSpawnElevation = 50.f;
};

