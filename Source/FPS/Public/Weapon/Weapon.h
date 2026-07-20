// Copyright Leyolabs

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "ShooterTypes/ShooterTypes.h"
#include "Weapon.generated.h"

enum EPhysicalSurface : int; // Add "PhysicsCore" to Build.cs (PublicDependencyModuleNames)

UENUM(BlueprintType)
enum class EFireType : uint8
{
	Auto UMETA(DisplayName = "Automatic"),
	SemiAuto UMETA(DisplayName = "SemiAutomatic"),
};

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS()
class FPS_API AWeapon : public AActor
{
	GENERATED_BODY()

public:
	AWeapon();
	virtual void OnRep_Instigator() override;
	
	USkeletalMeshComponent* GetMesh1P() const;
	USkeletalMeshComponent* GetMesh3P() const;
	UMaterialInstanceDynamic* GetReticleDynamicMaterialInstance();
	UMaterialInstanceDynamic* GetAmmoCounterDynamicMaterialInstance();

	void AttachToOwningPawn() const;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|WeaponType")
	FGameplayTag WeaponType;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Aiming")
	float AimFieldOfView;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Trace")
	float TraceRadius;
	
	void WeaponTrace(FHitResult& OutHit, float TraceLength);
	
	void Local_Fire(const FVector& ImpactPoint, const FVector& ImpactNormal, 
		TEnumAsByte<EPhysicalSurface> ImpactSurfaceType, bool bIsFirstPerson);
	
	void Auth_Fire();
	void Rep_Fire(int32 AuthAmmo);
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|FireType") // EditAnywhere so we can change it on instances if needed
	EFireType FireType;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPS|FireType")
	float FireTime;
	
	// Ammo --> not replicated, because we follow the client-side prediction algorythm (lecture 39)
	UPROPERTY(EditAnywhere, Category = "FPS|Ammo")
	int32 Ammo; // In the magazine/clip in the weapon
	
	UPROPERTY(EditAnywhere, Category = "FPS|Ammo")
	int32 MagCapacity;
	
	UPROPERTY(EditAnywhere, Category = "FPS|Ammo")
	int32 StartingCarriedAmmo; // The total starting ammo for this weapon
		
	UFUNCTION(BlueprintImplementableEvent)
	void DryFireEffects(); // Called from CombatComponent
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Reticle")
	FReticleParams ReticleParams;
	
protected:
	virtual void BeginPlay() override;
	
	UFUNCTION(BlueprintImplementableEvent)
	void FireEffects(const FVector& ImpactPoint, const FVector& ImpactNormal, 
		EPhysicalSurface ImpactSurfaceType, bool bIsFirstPerson);
	
	// Weapon mesh 1st person view
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="FPS|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh1P;
	
	// Weapon mesh 3rd person view (what other players will see)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="FPS|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh3P;

private:
	void SetMeshVisiblities(APawn* OwningPawn) const;
	
	int32 Sequence; // Part of the client-side prediction algorythm
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	TObjectPtr<UMaterialInterface> ReticleMaterial;
	
	UPROPERTY(EditDefaultsOnly, Category = "FPS|Weapon")
	TObjectPtr<UMaterialInterface> AmmoCounterMaterial;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMatInst_Reticle;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMatInst_AmmoCounter;
};
