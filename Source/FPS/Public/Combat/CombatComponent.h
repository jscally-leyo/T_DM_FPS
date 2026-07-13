// Copyright Leyolabs

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Components/ActorComponent.h"
#include "CombatComponent.generated.h"

class AWeapon;
class UWeaponData;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FPS_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	void Initiate_CycleWeapon(); // Cycle to the next weapon in the inventory
	void Initiate_FireWeapon_Pressed();
	void Initiate_FireWeapon_Released();
	void Initiate_ReloadWeapon();
	void Initiate_Aim_Pressed();
	void Initiate_Aim_Released();
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Weapon")
	TObjectPtr<UWeaponData> WeaponData;
	
	void Equip(AWeapon* Weapon);
	void SpawnInventory();
	void DestroyInventory();
	
	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bAiming;
	
	UPROPERTY(Transient, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AWeapon> CurrentWeapon;
	
protected:
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	float TraceLength;

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
};

