// Copyright Leyolabs

#include "Combat/CombatComponent.h"

#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Compute/ComputeSocket.h"
#include "Data/WeaponData.h"
#include "Engine/Engine.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "FPS/FPS.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/PlayerInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Weapon/Weapon.h"

/* TEXT FOR DEBUG MESSAGE
	if (IsValid(CurrentWeapon))
	{
		GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::Cyan, FString::Printf(TEXT("Ammo: %d"), CurrentWeapon->Ammo));
	}
*/

UCombatComponent::UCombatComponent()
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Local reached"), false);
	PrimaryComponentTick.bCanEverTick = true;
	TraceLength = 20000.f;

	bAiming = false;
	bTriggerPressed = false;
	
	Local_WeaponIndex = 0;
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// Check if we are aiming at another player
	UObject* OwningTest = GetOwner();
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn) || !OwningPawn->IsLocallyControlled()) return;
	
	APlayerController* PC = Cast<APlayerController>(OwningPawn->GetController());
	if (!IsValid(PC)) return;
	
	FVector EyesWorldLocation;
	FRotator EyesWorldRotation;
	PC->GetActorEyesViewPoint(EyesWorldLocation, EyesWorldRotation);
	const FVector EyesWorldDirection = UKismetMathLibrary::GetForwardVector(EyesWorldRotation);
	
	const FVector Start = EyesWorldLocation;
	const FVector End = Start + EyesWorldDirection * TraceLength;
	
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetAllChannels(ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Block);
	ResponseParams.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Block);
	
	GetWorld()->LineTraceSingleByChannel(Hit, Start, End, FPSTraceChannels::ECC_Weapon, QueryParams, ResponseParams);
	
	bHitPlayer = IsValid(Hit.GetActor()) && Hit.GetActor()->Implements<UPlayerInterface>();
	
	// We want to know if the hit status changed from last frame
	if (bHitPlayer != bHitPlayerLastFrame)
	{
		OnTargetingPlayerStatusChanged.Broadcast(bHitPlayer);
	}
	
	bHitPlayerLastFrame = bHitPlayer;
	/*
	if (IsValid(CurrentWeapon))
	{
		GEngine->AddOnScreenDebugMessage(1, 0.1f, FColor::Cyan, FString::Printf(TEXT("Ammo: %d"), CurrentWeapon->Ammo));
	}
	*/
	
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(UCombatComponent, Inventory); // Inventory needs to be known on clients
	DOREPLIFETIME(UCombatComponent, CurrentWeapon);
	DOREPLIFETIME_CONDITION(UCombatComponent, bAiming, COND_SkipOwner); // Optimized with _CONDITION
	DOREPLIFETIME_CONDITION(UCombatComponent, CurrentReserveAmmo, COND_OwnerOnly); // Optimized with _CONDITION
}

void UCombatComponent::Initiate_CycleWeapon()
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("Initiate_CycleWeapon"), false);
	
	if (!IsValid(CurrentWeapon)) return;
	if (CurrentWeapon->WeaponStatus == EWeaponStatus::Cycling) return;
	
	AdvanceWeaponIndex();
	Local_CycleWeapon(Local_WeaponIndex);
}

void UCombatComponent::Local_CycleWeapon(int32 WeaponIndex)
{
	AWeapon* NextWeapon = Inventory[WeaponIndex];
	if (!IsValid(NextWeapon) || !IsValid(WeaponData)) return;
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Cycling;
	NextWeapon->WeaponStatus = EWeaponStatus::Cycling;
	
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	const bool bIsLocal = IsValid(OwningPawn) && OwningPawn->IsLocallyControlled();
	
	const FMontageData& MontageData = 
		bIsLocal ? 
		WeaponData->FirstPersonMontages.FindChecked(NextWeapon->WeaponType) : 
		WeaponData->ThirdPersonMontages.FindChecked(NextWeapon->WeaponType);
	
	USkeletalMeshComponent* Mesh = 
		bIsLocal ?
		IPlayerInterface::Execute_GetMesh1P(GetOwner()) :
		IPlayerInterface::Execute_GetMesh3P(GetOwner());
	
	if (IsValid(Mesh) && IsValid(MontageData.EquipMontage))
	{
		Mesh->GetAnimInstance()->Montage_Play(MontageData.EquipMontage);
	}
	
	if (bIsLocal)
	{
		Server_CycleWeapon(WeaponIndex);
		Mesh->GetAnimInstance()->OnMontageBlendingOut.AddDynamic(this, &ThisClass::BlendOut_CycleWeapon);
	}
}

void UCombatComponent::Server_CycleWeapon_Implementation(int32 WeaponIndex)
{
	Local_WeaponIndex = WeaponIndex;
	MultiCast_CycleWeapon(WeaponIndex);
}

void UCombatComponent::MultiCast_CycleWeapon_Implementation(int32 WeaponIndex)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn)) return;
	
	if (!OwningPawn->IsLocallyControlled())
	{
		Local_WeaponIndex = WeaponIndex;
		Local_CycleWeapon(WeaponIndex);
	}
}

void UCombatComponent::Notify_CycleWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	
	AWeapon* NewWeapon = Inventory[Local_WeaponIndex];
	
	if (IsValid(NewWeapon))
	{
		EquipWeapon(NewWeapon);
	}
}

void UCombatComponent::BlendOut_CycleWeapon(UAnimMontage* Montage, bool bInterrupted)
{
	UAnimInstance* AnimInstance = IPlayerInterface::Execute_GetMesh1P(GetOwner())->GetAnimInstance();
	if (IsValid(AnimInstance) && AnimInstance->OnMontageBlendingOut.IsAlreadyBound(this, &ThisClass::BlendOut_CycleWeapon))
	{
		AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ThisClass::BlendOut_CycleWeapon);
	};
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	
	OnReticleChanged.Broadcast(CurrentWeapon->GetReticleDynamicMaterialInstance(), CurrentWeapon->ReticleParams, bHitPlayer);
	OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	
	if (bTriggerPressed && CurrentWeapon->FireType == EFireType::Auto && CurrentWeapon->Ammo > 0)
	{
		Local_FireWeapon();
	}
}

void UCombatComponent::Initiate_FireWeapon_Pressed()
{
	if (!IsValid(CurrentWeapon)) return;
	
	bTriggerPressed = true;
	
	if (CurrentWeapon-> WeaponStatus == EWeaponStatus::Idle)
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

void UCombatComponent::Local_FireWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	
	ensure(IsValid(WeaponData)); // Some kind of check, because if this is invalid, that's a big problem
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Firing;
	
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
	
	// Broadcast delegate to make sure our ShooterReticle updates when fired
	OnRoundFired.Broadcast(CurrentWeapon->Ammo, CurrentWeapon->MagCapacity, CurrentReserveAmmo);
	
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &ThisClass::FireTimerFinished, CurrentWeapon->FireTime);
	Server_FireWeapon(Hit);
}

void UCombatComponent::FireTimerFinished()
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;
	
	// Automatic reload when ammo hits 0
	if (CurrentWeapon->Ammo == 0 && CurrentReserveAmmo > 0 && OwningPawn->IsLocallyControlled())
	{
		Local_ReloadWeapon();
		Server_ReloadWeapon();
		return;
	}
	
	if (CurrentWeapon->WeaponStatus == EWeaponStatus::Firing)
	{
		CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	}
	
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
	// Check if we are the listen server (is also then locally controlled as being the host) or a normal locally controlled player
	if (!IsValid(CurrentWeapon)) return;
	
	// Server side ammo validation
	// Not sure in which lecture this was put here, but DM code has <= 0 but then Rep_Fire() is never reached! 
	// Result: Weapon.Sequence stays at 1 instead of going back to 0 for that last round!!
	// So then, after reloading, we already start with a Sequence of 1, which means 2 rounds are deducted when shooting
	// This messes up the ammo count completely for server or standalone! BUT, not on clients, because there Rep_Fire() is still reached via the Multicast (I think)
	// UPDATE: was fixed in lecture 64 'Automatic Reload' by placing an extra if (!GetInstigator()->HasAuthority()) in Local_Fire() and Rep_Fire() in Weapon.cpp
	if (CurrentWeapon->Ammo <= 0) return;
	
	// We only want to do damage on the server
	// AShooterCharacter::DoDamage_Implementation takes care of the multicast part
	if (IsValid(Hit.GetActor()) && Hit.GetActor()->Implements<UPlayerInterface>())
	{
		IPlayerInterface::Execute_DoDamage(Hit.GetActor(), CurrentWeapon->Damage, GetOwner());
	}
	
	//ORIGINAL CODE: if (GetNetMode() != NM_ListenServer || !Cast<APawn>(GetOwner())->IsLocallyControlled())
	// if ((GetNetMode() != NM_ListenServer && GetNetMode() != NM_Standalone ) || !Cast<APawn>(GetOwner())->IsLocallyControlled())
	if (!Cast<APawn>(GetOwner())->IsLocallyControlled())
	{
		CurrentWeapon->Auth_Fire();	
	}
	
	MultiCast_FireWeapon(Hit, CurrentWeapon->Ammo);
}

void UCombatComponent::MultiCast_FireWeapon_Implementation(const FHitResult& Hit, int32 AuthAmmo)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (OwningPawn->IsLocallyControlled())
	{
		// Do locally controlled stuff --> already done at this point with Local_FireWeapon()
		
		// Part of the client-side prediction algorythm
		CurrentWeapon->Rep_Fire(AuthAmmo);
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
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("Initiate_ReloadWeapon"), false);
	if (!IsValid(CurrentWeapon)) return;
	if (CurrentWeapon->WeaponStatus == EWeaponStatus::Cycling || CurrentWeapon->WeaponStatus == EWeaponStatus::Reloading) return;
	if (CurrentWeapon->Ammo == CurrentWeapon->MagCapacity)
	{
		return; // We could do something here like sound effect or text popup "Weapon is already fully loaded"
	}
	if (CurrentReserveAmmo == 0) return;
	
	Local_ReloadWeapon();
	Server_ReloadWeapon();
	
}

void UCombatComponent::Local_ReloadWeapon()
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;
	ensure(WeaponData);
	
	// Player montage
	const bool bIsLocal = OwningPawn->IsLocallyControlled();
	
	UAnimMontage* ReloadMontage = 
		bIsLocal ?
		WeaponData->FirstPersonMontages.FindChecked(CurrentWeapon->WeaponType).ReloadMontage:
		WeaponData->ThirdPersonMontages.FindChecked(CurrentWeapon->WeaponType).ReloadMontage;
	
	USkeletalMeshComponent* Mesh = 
		bIsLocal ?
		IPlayerInterface::Execute_GetMesh1P(OwningPawn):
		IPlayerInterface::Execute_GetMesh3P(OwningPawn);
	
	if (IsValid(Mesh) && IsValid(ReloadMontage))
	{
		Mesh->GetAnimInstance()->Montage_Play(ReloadMontage);
	}
	
	// Weapon montage
	UAnimMontage* WeaponReloadMontage = WeaponData->WeaponMontages.FindChecked(CurrentWeapon->WeaponType).ReloadMontage;
	USkeletalMeshComponent* WeaponMesh = 
		bIsLocal ?
		CurrentWeapon->GetMesh1P():
		CurrentWeapon->GetMesh3P();
	
	if (IsValid(WeaponReloadMontage) && IsValid(WeaponMesh))
	{
		WeaponMesh->GetAnimInstance()->Montage_Play(WeaponReloadMontage);
	}
	
	// Change weapon status
	CurrentWeapon->WeaponStatus = EWeaponStatus::Reloading;
}

void UCombatComponent::Server_ReloadWeapon_Implementation()
{
	Multicast_ReloadWeapon();
}

void UCombatComponent::Multicast_ReloadWeapon_Implementation()
{
	Local_ReloadWeapon();
}

void UCombatComponent::Client_ReloadWeapon_Implementation(int32 NewWeaponAmmo, int32 NewCarriedAmmo)
{
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(CurrentWeapon) || !IsValid(OwningPawn)) return;
	
	if (OwningPawn->IsLocallyControlled())
	{
		CurrentWeapon->Ammo = NewWeaponAmmo;
		CurrentReserveAmmo = NewCarriedAmmo;
		// Update widgets
		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
		OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	}
}

void UCombatComponent::Notify_ReloadWeapon()
{
	if (!IsValid(CurrentWeapon)) return;
	
	if (GetNetMode() == NM_ListenServer || GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_Standalone)
	{
		// Actual reloading ammo calculation
		const int32 EmptySpace = CurrentWeapon->MagCapacity - CurrentWeapon->Ammo;
		const int32 AmountToRefill = FMath::Min(EmptySpace, CurrentReserveAmmo);
		CurrentWeapon->Ammo += AmountToRefill;
		ReserveAmmo[CurrentWeapon->WeaponType] = ReserveAmmo[CurrentWeapon->WeaponType] - AmountToRefill;
		CurrentReserveAmmo = ReserveAmmo[CurrentWeapon->WeaponType];
		// All of that happens on the server only, so we need to do and RPC to the locally controlled client
		Client_ReloadWeapon(CurrentWeapon->Ammo, CurrentReserveAmmo);
	}
	
	CurrentWeapon->WeaponStatus = EWeaponStatus::Idle;
	
	// Optional for if we want to immediately start firing after reloading (while holding down LMB)
	/*
	if (bTriggerPressed && CurrentWeapon->Ammo > 0)
	{
		Local_FireWeapon();
	}
	*/
}

void UCombatComponent::AddAmmo(const FGameplayTag& WeaponType, int32 AmmoAmount)
{
	// This is "triggered" via ammo pickups which are created by just using BP classes
	// See lecture 67 for setting up the BP classes with also custom collision presets & stuff
	
	if (GetOwner()->HasAuthority() && !IsValid(CurrentWeapon)) return;
	
	// If we pick up a type of ammo that we don't have yet, add it to the map
	if (!ReserveAmmo.Contains(WeaponType))
	{
		ReserveAmmo.Add(WeaponType, AmmoAmount);
	}
	
	const int32 NewAmmo = ReserveAmmo.FindChecked(WeaponType) + AmmoAmount;
	ReserveAmmo[WeaponType] = NewAmmo;
	
	if (CurrentWeapon->WeaponType.MatchesTagExact(WeaponType))
	{
		CurrentReserveAmmo = NewAmmo;
		if (CurrentWeapon->Ammo == 0 && NewAmmo > 0)
		{
			Server_ReloadWeapon();
		}
		
		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
		OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	}
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
	OnAimingStatusChanged.Broadcast(bAiming);
}

void UCombatComponent::Equip(AWeapon* Weapon)
{
	CurrentWeapon = Weapon;
	CurrentWeapon->AttachToOwningPawn(Cast<APawn>(GetOwner()));
	
	CurrentReserveAmmo = ReserveAmmo.FindChecked(CurrentWeapon->WeaponType);
	OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, Weapon->Ammo, CurrentWeapon->WeaponIcon);
}

void UCombatComponent::EquipWeapon(AWeapon* Weapon)
{
	if (!IsValid(Weapon) || !IsValid(GetOwner())) return;
	
	if (GetOwner()->GetLocalRole() == ROLE_Authority)
	{
		SetCurrentWeapon(Weapon, CurrentWeapon);
	}
	else
	{
		Server_EquipWeapon(Weapon);
	}
}

void UCombatComponent::Server_EquipWeapon_Implementation(AWeapon* Weapon)
{
	EquipWeapon(Weapon);
}

void UCombatComponent::SetCurrentWeapon(AWeapon* NewWeapon, AWeapon* LastWeapon)
{
	AWeapon* LocalLastWeapon = nullptr;
	
	if (IsValid(LastWeapon))
	{
		LocalLastWeapon = LastWeapon;
	}
	else if (NewWeapon != CurrentWeapon)
	{
		LocalLastWeapon = CurrentWeapon;
	}
	
	if (IsValid(LocalLastWeapon))
	{
		LocalLastWeapon->DetachFromOwningPawn();
		LocalLastWeapon->WeaponStatus = EWeaponStatus::Unequipped;
	}
	
	CurrentWeapon = NewWeapon;
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwningPawn)) return;
	if (!IsValid(CurrentWeapon)) return;
	
	if (OwningPawn->HasAuthority())
	{
		CurrentReserveAmmo = ReserveAmmo.FindChecked(CurrentWeapon->WeaponType);
	}
	
	CurrentWeapon->AttachToOwningPawn(OwningPawn);
	
	// If we swap to a weapon that is empty and we have ammo, we could auto-reload
	if (CurrentWeapon->Ammo == 0 && CurrentReserveAmmo > 0 && OwningPawn->IsLocallyControlled())
	{
		Local_ReloadWeapon();
		Server_ReloadWeapon();
	}
}

void UCombatComponent::SpawnInventory()
{
	if (GetOwner()->GetLocalRole() < ROLE_Authority) return; // We only want to do this on the server, and all of these are replicated variables
	
	for (TSubclassOf<AWeapon>& WeaponClass : DefaultWeaponClasses)
	{
		AWeapon* Weapon = SpawnWeapon(WeaponClass);
		Inventory.AddUnique(Weapon);
		ReserveAmmo.Add(Weapon->WeaponType, Weapon->StartingCarriedAmmo);
	}
	
	if (Inventory.Num() > 0)
	{
		Equip(Inventory[0]);
		InitializeWeaponWidgets();
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


void UCombatComponent::InitializeWeaponWidgets()
{
	if (IsValid(CurrentWeapon))
	{
		OnReticleChanged.Broadcast(CurrentWeapon->GetReticleDynamicMaterialInstance(), CurrentWeapon->ReticleParams, bHitPlayer);
		OnAmmoCounterChanged.Broadcast(CurrentWeapon->GetAmmoCounterDynamicMaterialInstance(), CurrentWeapon->Ammo, CurrentWeapon->MagCapacity);
	}
}

void UCombatComponent::OnRep_CurrentWeapon(AWeapon* LastWeapon)
{
	SetCurrentWeapon(CurrentWeapon, LastWeapon);
	
	IPlayerInterface::Execute_WeaponReplicated(GetOwner());
	InitializeWeaponWidgets();
}

void UCombatComponent::OnRep_CurrentReserveAmmo()
{
	if (IsValid(CurrentWeapon))
	{
		OnCurrentReserveAmmoChanged.Broadcast(CurrentReserveAmmo, CurrentWeapon->Ammo, CurrentWeapon->WeaponIcon);
	}
}

int32 UCombatComponent::AdvanceWeaponIndex()
{
	if (Inventory.Num() >= 2)
	{
		// Take the next index unless we are at the end, then go back
		Local_WeaponIndex = (Local_WeaponIndex + 1) % Inventory.Num();
	}
	return Local_WeaponIndex;
}

