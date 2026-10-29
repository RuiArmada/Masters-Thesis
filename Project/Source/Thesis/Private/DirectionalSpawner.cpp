#include "DirectionalSpawner.h"
#include "AgentIdManager.h"
#include "ScheduleManager.h"
#include "Kismet/GameplayStatics.h"

ADirectionalSpawner::ADirectionalSpawner() {
  PrimaryActorTick.bCanEverTick = false;

  AreaOfCollision = CreateDefaultSubobject<UBoxComponent>(
      TEXT("Directional Spawner | Area of Collision"));
  RootComponent = AreaOfCollision;
  AreaOfCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  AreaOfCollision->SetCollisionResponseToAllChannels(ECR_Ignore);

  Direction = CreateDefaultSubobject<UArrowComponent>(
      TEXT("Directional Spawner | Direction"));
  Direction->SetupAttachment(RootComponent);

  SpawnerMesh = CreateDefaultSubobject<UStaticMeshComponent>(
      TEXT("Directional Spawner | Spawner Mesh"));
  SpawnerMesh->SetupAttachment(RootComponent);
  SpawnerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  SpawnerMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

  NumBoids = 0;
  SpawnRate = 1.0f;
  SpawnType = ESpawnerType::Burst;
}

AAgentIdManager* IdManager = nullptr;

void ADirectionalSpawner::BeginPlay() {
  Super::BeginPlay();

  TArray<AActor*> Found;
  UGameplayStatics::GetAllActorsOfClass(
      GetWorld(),
      AAgentIdManager::StaticClass(),
      Found
  );

  if (Found.Num() > 0)
  {
      IdManager = Cast<AAgentIdManager>(Found[0]);
  }

  ensureMsgf(IdManager, TEXT("No AgentIdManager found in level"));

  SpawnTimerDelegate.BindUFunction(this, FName("SpawnBoids"), NumBoids);
  SpawnBoids(NumBoids);
}

void ADirectionalSpawner::SpawnBoids(int32 Num) {
  if (!AssignedFlockController) {
    UE_LOG(LogTemp, Warning, TEXT("No Flock Controller assigned to spawner %s"),
           *GetName());
    return;
  }

  if (!BoidClass) {
    UE_LOG(LogTemp, Warning, TEXT("No Boid Type assigned to spawner %s"),
           *GetName());
    return;
  }

  FVector SpawnLocation;
  FRotator SpawnRotation = Direction->GetComponentRotation();
  FActorSpawnParameters SpawnParams;
  SpawnParams.SpawnCollisionHandlingOverride =
      ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  SpawnParams.Owner = AssignedFlockController;

  AScheduleManager* ScheduleManager = nullptr;
  if (!ScheduleOverride.IsEmpty())
  {
    ScheduleManager = Cast<AScheduleManager>(
        UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass()));
    if (!ScheduleManager)
    {
      UE_LOG(LogTemp, Warning,
             TEXT("[DirectionalSpawner] ScheduleOverride set on %s but no ScheduleManager in level"),
             *GetName());
    }
  }

  for (int32 i = 0; i < Num; ++i) {
    SpawnLocation.X =
        FMath::FRandRange(-AreaOfCollision->GetScaledBoxExtent().X,
                          AreaOfCollision->GetScaledBoxExtent().X);
    SpawnLocation.Y =
        FMath::FRandRange(-AreaOfCollision->GetScaledBoxExtent().Y,
                          AreaOfCollision->GetScaledBoxExtent().Y);
    SpawnLocation.Z =
        FMath::FRandRange(-AreaOfCollision->GetScaledBoxExtent().Z,
                          AreaOfCollision->GetScaledBoxExtent().Z);
    SpawnLocation += this->GetActorLocation();

    ABoidsAB *SpawnedBoid = GetWorld()->SpawnActor<ABoidsAB>(
        BoidClass, SpawnLocation, SpawnRotation, SpawnParams);
    if (SpawnedBoid && IdManager) {
      SpawnedBoid->AgentId = IdManager->GenerateAgentId();

      // Optional: assign spawner schedule template to this runtime AgentId.
      if (ScheduleManager && !ScheduleOverride.IsEmpty())
      {
        ScheduleManager->AssignScheduleOverrideToAgent(SpawnedBoid->AgentId, ScheduleOverride);
      }

      AssignedFlockController->AddBoidToTheFlock(SpawnedBoid);
    }
  }

  if (SpawnType == ESpawnerType::Flow) {
    GetWorldTimerManager().SetTimer(SpawnTimerHandle, SpawnTimerDelegate,
                                    SpawnRate, false);
  }
}
