// Copyright Leyolabs

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Components/ActorComponent.h"
#include "CombatComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FPS_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:


private:
	
};
