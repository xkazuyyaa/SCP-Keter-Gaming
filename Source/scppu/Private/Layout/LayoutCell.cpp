


#include "Layout/LayoutCell.h"
#include "Layout/Layout.h"
#include "Layout/LayoutSpawnValidator.h"
#include "Layout/LayoutSublevelInterface.h"
#include "Engine/LevelStreamingDynamic.h"
#include "DrawDebugHelpers.h"

FVector ULayoutCell::GetWorldLocation()
{
	return (FVector(this->Location.X, this->Location.Y, 0) * this->Owner->CellSize) + this->Owner->GetActorLocation();
}

FRotator ULayoutCell::GetWorldRotation()
{
	return FRotator(0, this->Rotation * 90, 0);
}

FLayoutCellSides ULayoutCell::GetRequiredConnections()
{
	ULayoutCell* CellPX;
	ULayoutCell* CellPY;
	ULayoutCell* CellNX;
	ULayoutCell* CellNY;
	FLayoutCellSides RequiredConnections;
	this->Owner->GetNeighbouringCells(this, false, CellPX, CellPY, CellNX, CellNY);
	RequiredConnections.bPX = IsValid(CellPX) && CellPX->HasConnections.bNX;
	RequiredConnections.bPY = IsValid(CellPY) && CellPY->HasConnections.bNY;
	RequiredConnections.bNX = IsValid(CellNX) && CellNX->HasConnections.bPX;
	RequiredConnections.bNY = IsValid(CellNY) && CellNY->HasConnections.bPY;
	return RequiredConnections;
}

FLayoutCellSides ULayoutCell::GetBlockedConnections()
{
	ULayoutCell* CellPX;
	ULayoutCell* CellPY;
	ULayoutCell* CellNX;
	ULayoutCell* CellNY;
	FLayoutCellSides BlockedConnections;
	this->Owner->GetNeighbouringCells(this, false, CellPX, CellPY, CellNX, CellNY);
	BlockedConnections.bPX = !IsValid(CellPX) || CellPX->IsBlockedByNeighbour() || (CellPX->bIsGenerated && !CellPX->HasConnections.bNX);
	BlockedConnections.bPY = !IsValid(CellPY) || CellPY->IsBlockedByNeighbour() || (CellPY->bIsGenerated && !CellPY->HasConnections.bNY);
	BlockedConnections.bNX = !IsValid(CellNX) || CellNX->IsBlockedByNeighbour() || (CellNX->bIsGenerated && !CellNX->HasConnections.bPX);
	BlockedConnections.bNY = !IsValid(CellNY) || CellNY->IsBlockedByNeighbour() || (CellNY->bIsGenerated && !CellNY->HasConnections.bPY);
	return BlockedConnections;
}

FString ULayoutCell::GetUniqueSublevelName()
{
	return this->GetName() + "_Sublevel";
}

bool ULayoutCell::IsBlockedByNeighbour()
{
	ULayoutCell* CellPX;
	ULayoutCell* CellPY;
	ULayoutCell* CellNX;
	ULayoutCell* CellNY;
	bool bIsBlocked = false;
	this->Owner->GetNeighbouringCells(this, false, CellPX, CellPY, CellNX, CellNY);
	bIsBlocked = bIsBlocked || (IsValid(CellPX) && CellPX->DisableNeighbouringCells.bNX);
	bIsBlocked = bIsBlocked || (IsValid(CellPY) && CellPY->DisableNeighbouringCells.bNY);
	bIsBlocked = bIsBlocked || (IsValid(CellNX) && CellNX->DisableNeighbouringCells.bPX);
	bIsBlocked = bIsBlocked || (IsValid(CellNY) && CellNY->DisableNeighbouringCells.bPY);
	return bIsBlocked;
}

bool ULayoutCell::IsRequiredToGenerate()
{
	return this->GetRequiredConnections() != FLayoutCellSides(false, false, false, false);
}

bool ULayoutCell::IsRowNameValid(FName InRowName, int InRotation)
{
	if (this->Owner->DataTable->FindRowUnchecked(InRowName) == nullptr)
	{
		UE_LOG(LogLayout, Warning, TEXT("%s: '%s' is not a valid row entry inside '%s'"), *this->GetName(), *InRowName.ToString(), *this->Owner->DataTable->GetName());
		return false;
	}

	bool bIsValid = true;
	InRotation = InRotation % 4;

	// Save original properties so we can overwrite them temporarily for the pre spawn validators
	int PrevRotation = this->Rotation;
	FName PrevRowName = this->RowName;
	FLayoutCellSides PrevHasConnection = this->HasConnections;
	FLayoutCellSides PrevDisableNeighbouringCell = this->DisableNeighbouringCells;

	// Get the values from datatable row so the pre spawn validators can use them
	FLayoutCellGenerationSettings Row = *this->Owner->DataTable->FindRow<FLayoutCellGenerationSettings>(InRowName, "");
	this->RowName = InRowName;
	this->HasConnections = Row.HasConnections;
	this->DisableNeighbouringCells = Row.DisableNeighbouringCells;

	// Rotate the values from datatable row
	for (int i = 1; i < InRotation + 1; i++)
	{
		this->Rotation = i;
		this->HasConnections.RotateRight();
		this->DisableNeighbouringCells.RotateRight();
	}

	// Run pre spawn validation
	for (auto Elem : Row.PreSpawnValidators)
	{
		ULayoutSpawnValidator* Validator = Elem.GetDefaultObject();
		bIsValid = bIsValid && Validator->IsValidSpawn(this->Owner, this, FRandomStream(this->UniqueSeed));
	}

	// Get info from neighbouring cells 
	ULayoutCell* CellPX;
	ULayoutCell* CellPY;
	ULayoutCell* CellNX;
	ULayoutCell* CellNY;
	FLayoutCellSides RequiredConnections = this->GetRequiredConnections();
	FLayoutCellSides BlockedConnections = this->GetBlockedConnections();
	this->Owner->GetNeighbouringCells(this, false, CellPX, CellPY, CellNX, CellNY);

	// Check if all connections fit
	//PX
	bIsValid = bIsValid && ((RequiredConnections.bPX && this->HasConnections.bPX) || !RequiredConnections.bPX);
	bIsValid = bIsValid && ((BlockedConnections.bPX && !this->HasConnections.bPX) || !BlockedConnections.bPX);
	bIsValid = bIsValid && (!DisableNeighbouringCells.bPX || !IsValid(CellPX) || (DisableNeighbouringCells.bPX && !CellPX->bIsGenerated && !CellPX->IsRequiredToGenerate()));

	//PY
	bIsValid = bIsValid && ((RequiredConnections.bPY && this->HasConnections.bPY) || !RequiredConnections.bPY);
	bIsValid = bIsValid && ((BlockedConnections.bPY && !this->HasConnections.bPY) || !BlockedConnections.bPY);
	bIsValid = bIsValid && (!DisableNeighbouringCells.bPY || !IsValid(CellPY) || (DisableNeighbouringCells.bPY && !CellPY->bIsGenerated && !CellPY->IsRequiredToGenerate()));

	//NX
	bIsValid = bIsValid && ((RequiredConnections.bNX && this->HasConnections.bNX) || !RequiredConnections.bNX);
	bIsValid = bIsValid && ((BlockedConnections.bNX && !this->HasConnections.bNX) || !BlockedConnections.bNX);
	bIsValid = bIsValid && (!DisableNeighbouringCells.bNX || !IsValid(CellNX) || (DisableNeighbouringCells.bNX && !CellNX->bIsGenerated && !CellNX->IsRequiredToGenerate()));

	//NY
	bIsValid = bIsValid && ((RequiredConnections.bNY && this->HasConnections.bNY) || !RequiredConnections.bNY);
	bIsValid = bIsValid && ((BlockedConnections.bNY && !this->HasConnections.bNY) || !BlockedConnections.bNY);
	bIsValid = bIsValid && (!DisableNeighbouringCells.bNY || !IsValid(CellNY) || (DisableNeighbouringCells.bNY && !CellNY->bIsGenerated && !CellNY->IsRequiredToGenerate()));

	// Reset properties to their original state
	this->Rotation = PrevRotation;
	this->RowName = PrevRowName;
	this->HasConnections = PrevHasConnection;
	this->DisableNeighbouringCells = PrevDisableNeighbouringCell;

	return bIsValid;
}

void ULayoutCell::SetRowName(FName NewRowName, int NewRotation)
{
	if (NewRowName == "None")
	{
		this->bIsGenerated = false;
		this->RowName = "None";
		this->Rotation = 0;
		this->HasConnections = FLayoutCellSides();
		this->DisableNeighbouringCells = FLayoutCellSides();
		this->LevelAsset = nullptr;
		this->UnloadSublevel();
		return;
	}

	if (this->Owner->DataTable->FindRow<FLayoutCellGenerationSettings>(NewRowName, "") == nullptr)
	{
		UE_LOG(LogLayout, Warning, TEXT("%s: '%s' is not a valid row entry inside '%s'"), *this->GetName(), *NewRowName.ToString(), *this->Owner->DataTable->GetName());
		return;
	}

	FRandomStream RStream = FRandomStream(this->UniqueSeed);
	FLayoutCellGenerationSettings Row = *this->Owner->DataTable->FindRow<FLayoutCellGenerationSettings>(NewRowName, "");
	NewRotation = NewRotation % 4;
	this->RowName = NewRowName;
	this->HasConnections = Row.HasConnections;
	this->DisableNeighbouringCells = Row.DisableNeighbouringCells;

	// Rotate the values from row
	for (int i = 1; i < NewRotation + 1; i++)
	{
		this->Rotation = i;
		this->HasConnections.RotateRight();
		this->DisableNeighbouringCells.RotateRight();
	}

	// Set level asset
	if (Row.Levels.Num() > 0)
	{
		this->LevelAsset = Row.Levels[RStream.RandRange(0, Row.Levels.Num() - 1)];
	}

	this->UnloadSublevel();
	bIsGenerated = true;
}

void ULayoutCell::LoadSublevel()
{
	if (IsValid(this->Sublevel))
	{
		return;
	}

	if (this->LevelAsset.IsNull())
	{
		return;
	}

	bool bLevelLoaded;
	this->Sublevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this->Owner->GetWorld(), this->LevelAsset, this->GetWorldLocation(), this->GetWorldRotation(), bLevelLoaded, this->GetUniqueSublevelName());
	if (bLevelLoaded)
	{
		this->Sublevel->bShouldBlockOnLoad = false;
		this->Sublevel->bShouldBlockOnUnload = false;
		this->Sublevel->bDisableDistanceStreaming = true;
		this->Sublevel->SetShouldBeVisible(false);
		this->Sublevel->OnLevelLoaded.AddDynamic(this, &ULayoutCell::OnSublevelLoadedCallback);
	}
}

void ULayoutCell::UnloadSublevel()
{
	if (!IsValid(this->Sublevel))
	{
		return;
	}

	this->Sublevel->SetIsRequestingUnloadAndRemoval(true);
	this->Sublevel = nullptr;
}

void ULayoutCell::GetAllActorsOfClassInSublevel(TSubclassOf<AActor> ActorClass, TArray<AActor*>& OutActors)
{
	if (IsValid(this->Sublevel) && this->Sublevel->HasLoadedLevel())
	{
		for (auto Elem : this->Sublevel->GetLoadedLevel()->Actors)
		{
			if (IsValid(Elem) && Elem->IsA(ActorClass))
			{
				OutActors.Add(Elem);
			}
		}
	}
}

void ULayoutCell::OnSublevelLoadedCallback()
{
	FRandomStream RStream = FRandomStream(this->UniqueSeed);
	TArray<AActor*> LevelActors;
	this->GetAllActorsOfClassInSublevel(AActor::StaticClass(), LevelActors);

	for (auto Elem : LevelActors)
	{
		if (Elem->Implements<ULayoutSublevelInterface>())
		{
			ILayoutSublevelInterface::Execute_OnLayoutDataReceived(Elem, this->Owner, this, RStream.RandRange(0, MAX_int32 - 1));
		}
	}
}

bool ULayoutCell::IsPointInSublevelBounds(FVector Point)
{
	TArray<AActor*> LevelActors;
	this->GetAllActorsOfClassInSublevel(AActor::StaticClass(), LevelActors);

	for (auto Elem : LevelActors)
	{
		FVector Origin;
		FVector Extend;
		Elem->GetActorBounds(false, Origin, Extend, false);

		if ((Point.X < Origin.X + Point.X && Point.X > Origin.X - Point.X) && (Point.Y < Origin.Y + Point.Y && Point.Y > Origin.Y - Point.Y) && (Point.Z < Origin.Z + Point.Z && Point.Z > Origin.Z - Point.Z))
		{
			return true;
		}
	}

	return false;
}

void ULayoutCell::DrawDebug(float Duration, bool bShowText) //Change this into a switchable debug command somehow
{
	if (Duration < 0.0)
	{
		Duration = 1e+18;
	}

	// Connection paths
	if (this->HasConnections.bPX)
	{
		FVector End = this->GetWorldLocation() + FVector(this->Owner->CellSize * 0.4, 0, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Green, false, Duration, 0, 20.0f);
	}

	if (this->HasConnections.bPY)
	{
		FVector End = this->GetWorldLocation() + FVector(0, this->Owner->CellSize * 0.4, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Green, false, Duration, 0, 20.0f);
	}

	if (this->HasConnections.bNX)
	{
		FVector End = this->GetWorldLocation() - FVector(this->Owner->CellSize * 0.4, 0, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Green, false, Duration, 0, 20.0f);
	}

	if (this->HasConnections.bNY)
	{
		FVector End = this->GetWorldLocation() - FVector(0, this->Owner->CellSize * 0.4, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Green, false, Duration, 0, 20.0f);
	}

	// Disabled neighbouring cells
	if (this->DisableNeighbouringCells.bPX)
	{
		FVector End = this->GetWorldLocation() + FVector(this->Owner->CellSize * 0.2, 0, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Red, false, Duration, 0, 20.0f);
	}

	if (this->DisableNeighbouringCells.bPY)
	{
		FVector End = this->GetWorldLocation() + FVector(0, this->Owner->CellSize * 0.2, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Red, false, Duration, 0, 20.0f);
	}

	if (this->DisableNeighbouringCells.bNX)
	{
		FVector End = this->GetWorldLocation() - FVector(this->Owner->CellSize * 0.2, 0, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Red, false, Duration, 0, 20.0f);
	}

	if (this->DisableNeighbouringCells.bNY)
	{
		FVector End = this->GetWorldLocation() - FVector(0, this->Owner->CellSize * 0.2, 0);
		DrawDebugLine(this->Owner->GetWorld(), this->GetWorldLocation(), End, FColor::Red, false, Duration, 0, 20.0f);
	}

	// Propeties
	if (bShowText)
	{
		FString Text;
		Text.Appendf(TEXT("UObject Name: %s\n"), *this->GetName());
		Text.Appendf(TEXT("Grid Location: X=%i Y=%i (i=%i)\n"), this->Location.X, this->Location.Y, this->Location.X * this->Owner->GridSize.X + this->Location.Y);
		Text.Appendf(TEXT("Grid Rotation: %i\n"), this->Rotation);
		Text.Appendf(TEXT("World Location: %s\n"), *this->GetWorldLocation().ToString());
		Text.Appendf(TEXT("World Rotation: %s\n"), *this->GetWorldRotation().ToString());
		Text.Appendf(TEXT("Is Generated: %s\n"), this->bIsGenerated ? TEXT("true") : TEXT("false"));
		Text.Appendf(TEXT("Is Blocked by Neighbour: %s\n"), this->IsBlockedByNeighbour() ? TEXT("true") : TEXT("false"));
		Text.Appendf(TEXT("Unique Seed: %i\n"), this->UniqueSeed);
		Text.Appendf(TEXT("Row Name: %s\n"), *this->RowName.ToString());
		Text.Appendf(TEXT("Sublevel Asset: %s\n"), *this->LevelAsset.ToString());
		Text.Appendf(TEXT("Unique Sublevel Name: %s\n"), *this->GetUniqueSublevelName());

		DrawDebugString(this->Owner->GetWorld(), GetWorldLocation() + FVector(0, 0, 200.0f), Text, nullptr, FColor::White, Duration, true, 1.0f);
	}
}

void ULayoutCell::BeginDestroy()
{
	Super::BeginDestroy();
	this->UnloadSublevel();
}
