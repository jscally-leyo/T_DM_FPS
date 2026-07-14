// Copyright Leyolabs

#include "UI/ShooterReticle.h"

#include "Character/ShooterCharacter.h"

void UShooterReticle::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	
	GetOwningPlayer()->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossessedPawnChanged);
	
	AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetOwningPlayer()->GetPawn());
	if (!IsValid(ShooterCharacter)) return;
	
	OnPossessedPawnChanged(nullptr, ShooterCharacter);
	
	if (ShooterCharacter->HasWeaponFirstReplicated())
	{
		// Get Dynamic Material Instances from the weapon
		
	} else
	{
		ShooterCharacter->OnWeaponFirstReplicated.AddDynamic(this, &ThisClass::OnWeaponFirstReplicated);
	}
}

void UShooterReticle::NativeTick(const FGeometry& MyGeometry, float AddFrameTime)
{
	Super::NativeTick(MyGeometry, AddFrameTime);
}

void UShooterReticle::OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	// Unbind from delegates on the old pawn's combat component
	
	// Bind to delegates on the new pawn's combat component
}

void UShooterReticle::OnWeaponFirstReplicated(AWeapon* Weapon)
{
	// Get Dynamic Material Instances from the weapon
	
}
