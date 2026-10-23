#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExitPoint.generated.h"

class ABoidsAB;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExitBoidExited, AExitPoint*, Exit, ABoidsAB*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExitQueueDepthChanged, AExitPoint*, Exit, int32, NewDepth);

UCLASS()
class THESIS_API AExitPoint : public AActor
{
    GENERATED_BODY()

public:
    AExitPoint();

    virtual void Tick(float DeltaSeconds) override;

    /** Assign a queue slot for a fleeing boid. Returns false if the queue is full. */
    bool RequestQueueSlot(ABoidsAB* Boid, int32& OutQueueIndex);

    /** Called when a boid reaches its assigned slot; may start exit processing. */
    void NotifyBoidReady(ABoidsAB* Boid);

    /** Remove from queue (incident end or re-flee). Handles the currently processing boid. */
    void LeaveQueue(ABoidsAB* Boid);

    /** World position for a queue index (nav-snapped when possible). */
    FVector GetQueueWorldLocation(int32 QueueIndex) const;

    /** Project to navmesh and validate a sidewalk-only path from the agent. */
    bool TryGetReachableQueueSlot(const ABoidsAB* Agent, int32 QueueIndex, FVector& OutLocation) const;

    bool IsQueueFull() const { return Queue.Num() >= MaxQueueSize; }

    UFUNCTION(BlueprintCallable, Category = "Exit")
    int32 GetQueueLength() const { return Queue.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Exit")
    int32 GetMaxQueueSize() const { return MaxQueueSize; }

    UFUNCTION(BlueprintCallable, Category = "Exit")
    float GetExitProcessDuration() const { return ExitProcessDuration; }

    UFUNCTION(BlueprintCallable, Category = "Exit")
    bool IsProcessing() const { return bIsProcessing; }

    UFUNCTION(BlueprintCallable, Category = "Exit")
    float GetEstimatedWaitSeconds() const;

    UPROPERTY(BlueprintAssignable, Category = "Exit|Events")
    FOnExitBoidExited OnBoidExited;

    UPROPERTY(BlueprintAssignable, Category = "Exit|Events")
    FOnExitQueueDepthChanged OnQueueDepthChanged;

protected:
    UPROPERTY(EditAnywhere, Category = "Exit")
    float QueueSpacing = 120.f;

    UPROPERTY(EditAnywhere, Category = "Exit|Navigation", meta = (ClampMin = "50"))
    float QueueNavSearchRadius = 200.f;

    UPROPERTY(EditAnywhere, Category = "Exit")
    int32 MaxQueueSize = 20;

    UPROPERTY(EditAnywhere, Category = "Exit")
    float ExitProcessDuration = 1.5f;

    /** Cap path updates per frame after a slot change to avoid nav query spikes. Front of queue always updates immediately. */
    UPROPERTY(EditAnywhere, Category = "Exit|Performance", meta = (ClampMin = "1", ClampMax = "10"))
    int32 PathUpdatesPerFrame = 2;

    /** Index 0 = front of queue. */
    UPROPERTY()
    TArray<ABoidsAB*> Queue;

private:
    UPROPERTY()
    ABoidsAB* CurrentProcessingBoid = nullptr;

    bool bIsProcessing = false;

    FTimerHandle ExitProcessTimer;

    UPROPERTY()
    TSet<ABoidsAB*> ReadyBoids;

    struct FPendingPathUpdate
    {
        TWeakObjectPtr<ABoidsAB> Boid;
        FVector TargetLocation;
    };

    TArray<FPendingPathUpdate> PendingPathUpdates;

    /** Start the front boid; skips invalid entries without recursion. */
    void StartProcessingNextBoid();

    void FinishProcessingCurrentBoid();

    /** Front boid updates immediately; remaining slots are staggered across ticks. */
    void ScheduleQueueAdvancement();

    void ProcessPendingPathUpdates();

    /** Drop destroyed/pending-kill entries and re-advance slots if needed. */
    void PurgeInvalidEntries();

    FVector ComputeRawQueueSlotLocation(int32 QueueIndex) const;

    bool ProjectToSidewalkNav(const FVector& QueryPoint, FVector& OutLocation) const;

    bool TryResolveQueueSlotLocation(int32 QueueIndex, FVector& OutLocation, const ABoidsAB* PathTestAgent) const;

    bool HasSidewalkPath(const ABoidsAB* Agent, const FVector& TargetLocation) const;
};
