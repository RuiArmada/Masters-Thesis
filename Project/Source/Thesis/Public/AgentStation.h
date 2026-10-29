#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/BillboardComponent.h"
#include "AI/AgentStationAnimationTypes.h"
#include "AgentStation.generated.h"

class ABoidsAB;
class UAnimMontage;
class UAnimSequence;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentReleased, ABoidsAB*, ReleasedAgent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentAdmitted, ABoidsAB*, AdmittedAgent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentEnqueued, ABoidsAB*, EnqueuedAgent);

/** Desk/station with optional multi-capacity and a wait queue. Does not drive movement. */
UCLASS()
class THESIS_API AAgentStation : public AActor
{
    GENERATED_BODY()

public:
    AAgentStation();

    /** Enter as occupant if capacity allows, otherwise join the queue. */
    UFUNCTION(BlueprintCallable, Category = "Station")
    bool RequestEnter(ABoidsAB* Agent);

    /** Leave whether occupying or queued; admits the next waiter if a slot opens. */
    UFUNCTION(BlueprintCallable, Category = "Station")
    void LeaveStation(ABoidsAB* Agent);

    UFUNCTION(BlueprintCallable, Category = "Station")
    bool IsOccupied() const;

    /** True when ActiveOccupants < MaxConcurrentOccupants (slot free for admit). */
    UFUNCTION(BlueprintCallable, Category = "Station")
    bool HasAvailableSlot() const;

    UFUNCTION(BlueprintCallable, Category = "Station")
    ABoidsAB* GetCurrentOccupant() const;

    UFUNCTION(BlueprintCallable, Category = "Station")
    bool IsAgentActiveOccupant(ABoidsAB* Agent) const;

    UFUNCTION(BlueprintCallable, Category = "Station")
    int32 GetActiveOccupantCount() const { return ActiveOccupants.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Station")
    int32 GetMaxConcurrentOccupants() const { return FMath::Max(1, MaxConcurrentOccupants); }

    UFUNCTION(BlueprintCallable, Category = "Station")
    void SetMaxConcurrentOccupants(int32 NewCapacity);

    /** Occupants + queue length relative to capacity (may exceed 1.0 under pressure). */
    UFUNCTION(BlueprintCallable, Category = "Station")
    float GetUtilization() const;

    UPROPERTY(BlueprintAssignable, Category = "Station|Events")
    FOnAgentEnqueued OnAgentEnqueued;

    /** Start the interaction timer once the occupant has physically arrived. */
    UFUNCTION(BlueprintCallable, Category = "Station")
    void NotifyOccupantReached(ABoidsAB* Agent);

    UFUNCTION(BlueprintCallable, Category = "Station")
    int32 GetQueueLength() const { return Queue.Num(); }

    /** Match XML stationType / actor tag (e.g. A0, A1). */
    UFUNCTION(BlueprintCallable, Category = "Station")
    bool MatchesIdentifier(const FString& Identifier) const;

    /** True when this station requests a non-None interaction animation asset. */
    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    bool HasInteractionAnimation() const;

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    EStationInteractionAnimType GetInteractionAnimType() const { return InteractionAnimType; }

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    UAnimMontage* GetInteractionMontage() const { return InteractionMontage; }

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    UAnimSequence* GetInteractionSequence() const { return InteractionSequence; }

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    bool ShouldLoopInteractionAnimation() const { return bLoopInteractionAnimation; }

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    FName GetInteractionAnimSlotName() const { return InteractionAnimSlotName; }

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    float GetInteractionAnimBlendIn() const { return InteractionAnimBlendIn; }

    UFUNCTION(BlueprintCallable, Category = "Station|Animation")
    float GetInteractionAnimBlendOut() const { return InteractionAnimBlendOut; }

    /** True when positioning mode is not None (agent should pose before anim). */
    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    bool HasInteractionPositioning() const;

    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    EStationInteractionPositionMode GetInteractionPositionMode() const { return InteractionPositionMode; }

    /** World transform where the agent should stand/sit for this interaction. */
    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    FTransform GetWorldInteractionTransform() const;

    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    float GetInteractionInterpolateSeconds() const { return InteractionInterpolateSeconds; }

    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    bool ShouldAlignInteractionRotation() const { return bAlignInteractionRotation; }

    /**
     * Starts the interaction timer + OnStationInteractStart.
     * Called after arrival (legacy) or after optional pose alignment completes.
     */
    UFUNCTION(BlueprintCallable, Category = "Station")
    void BeginInteractionForAgent(ABoidsAB* Agent);

    UPROPERTY(BlueprintAssignable, Category = "Station|Events")
    FOnAgentReleased OnAgentReleased;

    UPROPERTY(BlueprintAssignable, Category = "Station|Events")
    FOnAgentAdmitted OnAgentAdmitted;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station|Components")
    USceneComponent* SceneRoot;

    /**
     * Optional pose point for Snap/Interpolate modes.
     * Place this component in the editor where the agent should stand/sit.
     * When unset, station actor transform is used as the base.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station|Components")
    USceneComponent* InteractionPoint;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Station|Components")
    UBillboardComponent* EditorBillboard;
#endif

    /** Seconds of use before auto-release. <= 0 means manual LeaveStation only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
    float InteractionDuration = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station", meta = (ClampMin = "1"))
    int32 MaxConcurrentOccupants = 1;

    /** Optional desk id; also matches actor tags when set in XML stationType. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
    FName StationIdentifier;

    /**
     * None = start interaction at arrival location (legacy).
     * Snap / Interpolate = align to InteractionPoint (+ offsets) before animation.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Position")
    EStationInteractionPositionMode InteractionPositionMode = EStationInteractionPositionMode::None;

    /** Added to InteractionPoint (or actor) location in local space of the base transform. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Position",
        meta = (EditCondition = "InteractionPositionMode != EStationInteractionPositionMode::None", EditConditionHides))
    FVector InteractionLocationOffset = FVector::ZeroVector;

    /** Added to InteractionPoint (or actor) rotation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Position",
        meta = (EditCondition = "InteractionPositionMode != EStationInteractionPositionMode::None", EditConditionHides))
    FRotator InteractionRotationOffset = FRotator::ZeroRotator;

    /** Duration for InterpolateToTransform mode (seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Position",
        meta = (EditCondition = "InteractionPositionMode == EStationInteractionPositionMode::InterpolateToTransform", EditConditionHides, ClampMin = "0.01"))
    float InteractionInterpolateSeconds = 0.35f;

    /** When true, agent yaw/pitch/roll align to the interaction transform. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Position",
        meta = (EditCondition = "InteractionPositionMode != EStationInteractionPositionMode::None", EditConditionHides))
    bool bAlignInteractionRotation = true;

    /**
     * Optional interaction animation. None = legacy behavior (no animation).
     * Assign Montage or Sequence in the editor; agents play it on interact start.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation")
    EStationInteractionAnimType InteractionAnimType = EStationInteractionAnimType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation",
        meta = (EditCondition = "InteractionAnimType == EStationInteractionAnimType::Montage", EditConditionHides))
    TObjectPtr<UAnimMontage> InteractionMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation",
        meta = (EditCondition = "InteractionAnimType == EStationInteractionAnimType::Sequence", EditConditionHides))
    TObjectPtr<UAnimSequence> InteractionSequence;

    /** When true, sequence/montage playback loops for the interaction duration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation",
        meta = (EditCondition = "InteractionAnimType != EStationInteractionAnimType::None", EditConditionHides))
    bool bLoopInteractionAnimation = true;

    /** Slot used when playing an Animation Sequence via dynamic montage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation",
        meta = (EditCondition = "InteractionAnimType == EStationInteractionAnimType::Sequence", EditConditionHides))
    FName InteractionAnimSlotName = FName(TEXT("DefaultSlot"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation",
        meta = (EditCondition = "InteractionAnimType != EStationInteractionAnimType::None", EditConditionHides, ClampMin = "0"))
    float InteractionAnimBlendIn = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Animation",
        meta = (EditCondition = "InteractionAnimType != EStationInteractionAnimType::None", EditConditionHides, ClampMin = "0"))
    float InteractionAnimBlendOut = 0.25f;

    UPROPERTY()
    TArray<ABoidsAB*> ActiveOccupants;

    UPROPERTY()
    TArray<ABoidsAB*> Queue;

private:
    TMap<TWeakObjectPtr<ABoidsAB>, FTimerHandle> OccupantTimers;
    TSet<TWeakObjectPtr<ABoidsAB>> AwaitingArrival;

    void StartInteractionTimer(ABoidsAB* Agent);
    void ClearInteractionTimer(ABoidsAB* Agent);
    void OnInteractionTimerExpired(ABoidsAB* Agent);
    void AdmitAgent(ABoidsAB* Agent);
    void AdmitQueuedAgent(ABoidsAB* Agent);
    void TryFillOpenSlots();
    void ScheduleTryFillOpenSlots();

    bool bTryFillScheduled = false;
};
