#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentLeadershipComponent.generated.h"

class ABoidsAB;

UENUM(BlueprintType)
enum class ELeadershipRole : uint8
{
	None UMETA(DisplayName = "None"),
	Leader UMETA(DisplayName = "Leader"),
	Follower UMETA(DisplayName = "Follower")
};

/**
 * Generic leader/follower attachment. Destination overrides are applied by the priority layer.
 * Roles are type-agnostic (parent, police, tour guide, etc.).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class THESIS_API UAgentLeadershipComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentLeadershipComponent();

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	ELeadershipRole GetRole() const { return Role; }

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	void SetRole(ELeadershipRole NewRole);

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	ABoidsAB* GetAssignedLeader() const { return AssignedLeader.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	FName GetAssignedLeaderId() const { return AssignedLeaderId; }

	/** Assign a leader by pointer. Sets this agent to Follower. */
	UFUNCTION(BlueprintCallable, Category = "Leadership")
	bool AssignLeader(ABoidsAB* Leader);

	/** Assign a leader by AgentId (resolved later if needed). */
	UFUNCTION(BlueprintCallable, Category = "Leadership")
	void AssignLeaderById(FName LeaderAgentId);

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	void ClearLeadership();

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	void BecomeLeader();

	/** Per-agent follow distance; < 0 uses global DefaultFollowDistance. */
	UFUNCTION(BlueprintCallable, Category = "Leadership")
	float GetEffectiveFollowDistance() const;

	UFUNCTION(BlueprintCallable, Category = "Leadership")
	void SetFollowDistanceOverride(float Distance) { FollowDistanceOverride = Distance; }

	/** True when feature is on, role is Follower, and a valid leader exists. */
	UFUNCTION(BlueprintCallable, Category = "Leadership")
	bool IsActivelyFollowing() const;

	/** World location for StartMoveTo — behind the leader along their forward. */
	UFUNCTION(BlueprintCallable, Category = "Leadership")
	bool ComputeFollowDestination(FVector& OutDestination) const;

	/** Resolve AssignedLeaderId to a live boid if pointer is missing. */
	UFUNCTION(BlueprintCallable, Category = "Leadership")
	bool ResolveLeaderIfNeeded();

	float ConsumeUpdateTimer(float DeltaTime);
	void ResetUpdateTimer(float Interval);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	ELeadershipRole Role = ELeadershipRole::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	FName AssignedLeaderId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership", meta = (ClampMin = "-1"))
	float FollowDistanceOverride = -1.f;

	UPROPERTY()
	TWeakObjectPtr<ABoidsAB> AssignedLeader;

	float UpdateTimer = 0.f;
};
