// Copyright Leyolabs

#include "Combat/CombatComponent.h"

#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponData.h"
#include "Engine/Engine.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/PlayerInterface.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Weapon/Weapon.h"

UCombatComponent::UCombatComponent()
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Local reached"), false);
	PrimaryComponentTick.bCanEverTick = true;
	TraceLength = 20000.f;

	bAiming = false;
	bTriggerPressed = false;
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(UCombatComponent, Inventory); // Inventory needs to be known on clients
	DOREPLIFETIME(UCombatComponent, CurrentWeapon);
	DOREPLIFETIME_CONDITION(UCombatComponent, bAiming, COND_SkipOwner); // Optimized with _CONDITION
}

void UCombatComponent::Initiate_CycleWeapon()
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("Initiate_CycleWeapon"), false);
}

void UCombatComponent::Initiate_FireWeapon_Pressed()
{
	if (!IsValid(CurrentWeapon)) return;
	
	bTriggerPressed = true;
	
	if (CurrentWeapon->Ammo > 0)
	{
		Local_FireWeapon();
	}
	else
	{
		CurrentWeapon->DryFireEffects();
	}
}

void UCombatComponent::Local_FireWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	
	ensure(IsValid(WeaponData)); // Some kind of check, because if this is invalid, that's a big problem
	
	// Play montage for the 1P mesh
	UAnimMontage* Montage1P = WeaponData->FirstPersonMontages.FindChecked(CurrentWeapon->WeaponType).FireMontage;
	USkeletalMeshComponent* Mesh1P = IPlayerInterface::Execute_GetMesh1P(GetOwner());
	if (IsValid(Montage1P) && IsValid(Mesh1P))
	{
		Mesh1P->GetAnimInstance()->Montage_Play(Montage1P);
	}
	
	FHitResult Hit;
	CurrentWeapon->WeaponTrace(Hit, TraceLength);
	
	EPhysicalSurface ImpactSurfaceType = 
		Hit.PhysMaterial.IsValid(false) ? 
		Hit.PhysMaterial->SurfaceType.GetValue() :
		SurfaceType1;
	
	CurrentWeapon->Local_Fire(Hit.ImpactPoint, Hit.ImpactNormal, ImpactSurfaceType, true);
	
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &ThisClass::FireTimerFinished, CurrentWeapon->FireTime);
	
	Server_FireWeapon(Hit);
}

void UCombatComponent::FireTimerFinished()
{
	if (!IsValid(CurrentWeapon)) return;
	
	// Handle automatic fire
	if (bTriggerPressed && CurrentWeapon->FireType == EFireType::Auto)
	{
		if (CurrentWeapon->Ammo > 0)
		{
			Local_FireWeapon();
		}
		else
		{
			CurrentWeapon->DryFireEffects();
		}
	}
}

void UCombatComponent::Server_FireWeapon_Implementation(const FHitResult& Hit)
{
	// Part of the client-side prediction algorythm
	// Ccheck if we are the listen server (is also then locally controlled as being the host) or a normal locally controlled player
	if (!IsValid(CurrentWeapon)) return;
	if (GetNetMode() != NM_ListenServer || !Cast<APawn>(GetOwner())->IsLocallyControlled())
	{
		CurrentWeapon->Auth_Fire();	
	}
	
	MultiCast_FireWeapon(Hit, CurrentWeapon->Ammo);
}

void UCombatComponent::MultiCast_FireWeapon_Implementation(const FHitResult& Hit, int32 Auth_Ammo)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (OwningPawn->IsLocallyControlled())
	{
		// Do locally controlled stuff --> already done at this point with Local_FireWeapon()
		
		// Part of the client-side prediction algorythm
		CurrentWeapon->Rep_Fire(Auth_Ammo);
	}
	else
	{
		ensure(IsValid(WeaponData)); // Some kind of check, because if this is invalid, that's a big problem
	
		EPhysicalSurface ImpactSurfaceType = 
		Hit.PhysMaterial.IsValid(false) ? 
		Hit.PhysMaterial->SurfaceType.GetValue() :
		SurfaceType1;
	
		CurrentWeapon->Local_Fire(Hit.ImpactPoint, Hit.ImpactNormal, ImpactSurfaceType, false);
		
		// Play montage for the 3P mesh
		UAnimMontage* Montage3P = WeaponData->ThirdPersonMontages.FindChecked(CurrentWeapon->WeaponType).FireMontage;
		USkeletalMeshComponent* Mesh3P = IPlayerInterface::Execute_GetMesh3P(GetOwner());
		if (IsValid(Montage3P) && IsValid(Mesh3P))
		{
			Mesh3P->GetAnimInstance()->Montage_Play(Montage3P);
		}
	}
}

void UCombatComponent::Initiate_FireWeapon_Released()
{
	bTriggerPressed = false;
}

void UCombatComponent::Initiate_ReloadWeapon()
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("Initiate_ReloadWeapon"), false);
}

void UCombatComponent::Initiate_Aim_Pressed()
{
	// Initially presson the aiming button will just happen locally (the owner)...
	Local_Aim(true);
	// ...so we also tell the server, which then replicates the setting of this boolean to everyone except the owner
	Server_Aim(true);
}

void UCombatComponent::Initiate_Aim_Released()
{
	// Initially presson the aiming button with just happen locally (the owner)...
	Local_Aim(false);
	// ...so we also tell the server, which then replicates the setting of this boolean to everyone except the owner
	Server_Aim(false);
}

void UCombatComponent::Server_Aim_Implementation(bool bPressed)
{
	// This is exectured on the server
	Local_Aim(bPressed);
}

void UCombatComponent::Local_Aim(bool bPressed)
{
	// Will set the boolean locally or on server, depending on which function calls it (see functions above)
	bAiming = bPressed;
}

void UCombatComponent::Equip(AWeapon* Weapon)
{
	CurrentWeapon = Weapon;
	CurrentWeapon->AttachToOwningPawn();
}

void UCombatComponent::SpawnInventory()
{
	if (GetOwner()->GetLocalRole() < ROLE_Authority) return;
	
	for (TSubclassOf<AWeapon>& WeaponClass : DefaultWeaponClasses)
	{
		AWeapon* Weapon = SpawnWeapon(WeaponClass);
		Inventory.AddUnique(Weapon);
	}
	
	if (Inventory.Num() > 0)
	{
		Equip(Inventory[0]);
	}
}

void UCombatComponent::DestroyInventory()
{
	for (AWeapon* Weapon : Inventory)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}
}

AWeapon* UCombatComponent::SpawnWeapon(TSubclassOf<AWeapon> WeaponClass) const
{
	AActor* OwningActor = GetOwner();
	if (!IsValid(OwningActor)) return nullptr;
	if (OwningActor->GetLocalRole() < ROLE_Authority) return nullptr;
	
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = Cast<APawn>(OwningActor);
	SpawnInfo.Owner = OwningActor;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	return GetWorld()->SpawnActor<AWeapon>(WeaponClass, SpawnInfo);
}


void UCombatComponent::OnRep_CurrentWeapon(AWeapon* LastWeapon)
{
	if (!IsValid(CurrentWeapon)) return;
	CurrentWeapon->AttachToOwningPawn();
	IPlayerInterface::Execute_WeaponReplicated(GetOwner());
}
