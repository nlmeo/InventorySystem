#pragma once

#include "CoreMinimal.h"
#include "Types/INV_GridTypes.h"
#include "Types/INV_InteractionTypes.h"
#include "INV_OperationTypes.generated.h"

UENUM(BlueprintType)
enum class EINV_OperationStatus : uint8
{
	Succeeded,
	NoChange,
	Rejected
};

UENUM(BlueprintType)
enum class EINV_OperationFailure : uint8
{
	None,
	InvalidRequest,
	InvalidRequestId,
	RequestExpired,
	InvalidInteractor,
	InvalidTarget,
	TransferNotAllowed,
	UnsupportedOperation,
	InvalidItem,
	InvalidQuantity,
	InvalidLocation,
	IncompatibleItem,
	EquipmentMismatch,
	Occupied,
	NoSpace,
	SpawnFailed
};

/** A terminal receipt is independent of whether object references/FastArray entries have arrived. */
USTRUCT(BlueprintType)
struct INVENTORY_API FINV_InventoryOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int64 RequestId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EINV_InteractionType Type = EINV_InteractionType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EINV_OperationStatus Status = EINV_OperationStatus::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EINV_OperationFailure Failure = EINV_OperationFailure::InvalidRequest;

	/** Server revision at the original operation's completion, including on replay. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int64 CompletionSequence = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	bool bReplayed = false;
};

/** Field snapshot only. FastArray remains responsible for entry creation/removal and reference readiness. */
USTRUCT()
struct INVENTORY_API FINV_InventoryStateFrame
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Revision = 0;

	UPROPERTY()
	int64 LastProcessedRequestId = 0;

	UPROPERTY()
	int64 CompletionSequence = 0;

	UPROPERTY()
	TArray<FINV_ItemStateSnapshot> Items;
};
