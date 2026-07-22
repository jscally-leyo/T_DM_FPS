// Copyright Leyolabs

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Runtime/Engine/Classes/Components/ActorComponent.h"
#include "CombatComponent.generated.h"

class UAnimMontage;
class AWeapon;
class UWeaponData;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FReticleChanged, 
	UMaterialInstanceDynamic*, ReticleDynMatInst,
	const FReticleParams&, ReticleParams,
	bool, bCurrentlyTargetingPlayer
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAmmoCounterChanged, 
	UMaterialInstanceDynamic*, AmmoCounterDynMatInst,
	int32, RoundsCurrent,
	int32, RoundsMax
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRoundFired,
	int32, RoundsCurrent,
	int32, RoundsMax,
	int32, RoundsInReserve
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAimingStatusChanged,
	bool, bIsAiming
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTargetingPlayerStatusChanged,
	bool, bTargeting
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCurrentReserveAmmoChanged,
	int32, RoundsInReserve,
	int32, RoundsInWeapon,
	UMaterialInterface*, WeaponIconMaterial
	);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FPS_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();
	
	UFUNCTION(BlueprintPure, Category = "FPS|Combat")
	static UCombatComponent* FindCombatComponent(const AActor* Actor) 
		{ return ( IsValid(Actor) ? Actor->FindComponentByClass<UCombatComponent>() : nullptr); }
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	void Initiate_CycleWeapon(); // Cycle to the next weapon in the inventory
	void Initiate_FireWeapon_Pressed();
	void Initiate_FireWeapon_Released();
	void Initiate_ReloadWeapon();
	void Initiate_Aim_Pressed();
	void Initiate_Aim_Released();
	
	void Notify_CycleWeapon();
	
	UPROPERTY(BlueprintAssignable)
	FReticleChanged OnReticleChanged;
	
	UPROPERTY(BlueprintAssignable)
	FAmmoCounterChanged OnAmmoCounterChanged;
	
	UPROPERTY(BlueprintAssignable)
	FRoundFired OnRoundFired;
	
	UPROPERTY(BlueprintAssignable)
	FAimingStatusChanged OnAimingStatusChanged;
	
	UPROPERTY(BlueprintAssignable)
	FTargetingPlayerStatusChanged OnTargetingPlayerStatusChanged;
	
	UPROPERTY(BlueprintAssignable)
	FCurrentReserveAmmoChanged OnCurrentReserveAmmoChanged;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<UWeaponData> WeaponData;
	
	void Equip(AWeapon* Weapon);
	
	void EquipWeapon(AWeapon* Weapon);
	
	UFUNCTION(Server, Reliable)
	void Server_EquipWeapon(AWeapon* Weapon);
	
	void SpawnInventory();
	void DestroyInventory();
	
	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bAiming;
	
	UPROPERTY(Transient, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AWeapon> CurrentWeapon;
	
	void InitializeWeaponWidgets();
	
	UPROPERTY(ReplicatedUsing = OnRep_CurrentReserveAmmo)
	int32 CurrentReserveAmmo;
	
	bool bHitPlayer;
	
protected:
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	float TraceLength;
	
	UFUNCTION()
	void BlendOut_CycleWeapon(UAnimMontage* Montage, bool bInterrupted);

private:
	UFUNCTION() // This has to be included for OnRep_... functions like this, otherwise it will throw an error
	void OnRep_CurrentWeapon(AWeapon* LastWeapon);
	
	UPROPERTY(Transient, Replicated)
	TArray<AWeapon*> Inventory;
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	TArray<TSubclassOf<AWeapon>> DefaultWeaponClasses;
	
	AWeapon* SpawnWeapon(TSubclassOf<AWeapon> WeaponClass) const;
	
	UFUNCTION(Server, Reliable) // Aiming is important, so Reliable can be used
	void Server_Aim(bool bPressed);
	
	UFUNCTION(Server, Reliable) // Firing is important, so Reliable can be used
	void Server_FireWeapon(const FHitResult& Hit);
	
	UFUNCTION(NetMulticast, Reliable)
	void MultiCast_FireWeapon(const FHitResult& Hit, int32 Auth_Ammo);
	
	void Local_Aim(bool bPressed);
	void Local_FireWeapon();
	
	// For automatic firing
	bool bTriggerPressed;
	FTimerHandle FireTimer;
	void FireTimerFinished();
	
	bool bHitPlayerLastFrame;
	
	UFUNCTION()
	void OnRep_CurrentReserveAmmo();
	
	TMap<FGameplayTag, int32> ReserveAmmo;
	
	int32 Local_WeaponIndex;
	int32 AdvanceWeaponIndex();
	
	void Local_CycleWeapon(int32 WeaponIndex);
	
	UFUNCTION(Server, Reliable)
	void Server_CycleWeapon(int32 WeaponIndex);
	
	UFUNCTION(NetMulticast, Reliable)
	void MultiCast_CycleWeapon(int32 WeaponIndex);
	
	void SetCurrentWeapon(AWeapon* NewWeapon, AWeapon* LastWeapon);
	
};

