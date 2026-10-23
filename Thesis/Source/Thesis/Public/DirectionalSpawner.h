#pragma once

#include "BoidsAB.h"
#include "FlockController.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DirectionalSpawner.generated.h"


class UBoxComponent;
class UArrowComponent;
class UStaticMeshComponent;
class ABoidsAB;
class AFlockController;

UENUM(BlueprintType)
enum class ESpawnerType : uint8
{
  Burst UMETA(DisplayName = "Burst"),
  Flow UMETA(DisplayName = "Flow"),
};

UCLASS()
class THESIS_API ADirectionalSpawner : public AActor {
  GENERATED_BODY()

public:
  ADirectionalSpawner();

protected:
  virtual void BeginPlay() override;

#pragma region Parameters

protected:
  UPROPERTY(VisibleAnywhere, Category = "Spawner | Parameters")
  UBoxComponent *AreaOfCollision;

  UPROPERTY(VisibleAnywhere, Category = "Spawner | Parameters")
  UArrowComponent *Direction;

  UPROPERTY(VisibleAnywhere, Category = "Spawner | Parameters")
  UStaticMeshComponent *SpawnerMesh;

#pragma endregion

#pragma region Spawn

protected:
  UPROPERTY(EditAnywhere, Category = "Spawner | Spawn")
  ESpawnerType SpawnType = ESpawnerType::Burst;

  UPROPERTY(EditAnywhere, Category = "Spawner | Spawn")
  int32 NumBoids;

  UPROPERTY(EditAnywhere, Category = "Spawner | Spawn")
  float SpawnRate;

  FTimerHandle SpawnTimerHandle;
  FTimerDelegate SpawnTimerDelegate;

  UPROPERTY(EditAnywhere, Category = "Spawner | Spawn")
  TSubclassOf<ABoidsAB> BoidClass;

  UPROPERTY(EditAnywhere, Category = "Spawner | Spawn")
  AFlockController
      *AssignedFlockController;

  /**
   * Optional schedule XML assigned to every boid this spawner creates.
   * Relative to project/Saved (e.g. Schedules/Validation/Validation_MoveRight.xml).
   * Empty = use existing global ScheduleManager assignment (no behavior change).
   */
  UPROPERTY(EditAnywhere, Category = "Spawner | Schedule")
  FString ScheduleOverride;

  UFUNCTION(BlueprintCallable, Category = "Spawner | Spawn")
  void SpawnBoids(int32 Num);

#pragma endregion
};
