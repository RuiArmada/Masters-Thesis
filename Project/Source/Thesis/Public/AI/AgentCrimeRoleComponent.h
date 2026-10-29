#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentCrimeRoleComponent.generated.h"

class ABoidsAB;

UENUM(BlueprintType)
enum class ECrimeRole : uint8
{
	Civilian UMETA(DisplayName = "Civilian"),
	Police UMETA(DisplayName = "Police"),
	Robber UMETA(DisplayName = "Robber")
};

UENUM(BlueprintType)
enum class ECrimeBehaviorState : uint8
{
	None UMETA(DisplayName = "None"),
	Crime UMETA(DisplayName = "Crime"),
	Fleeing UMETA(DisplayName = "Fleeing"),
	Pursuing UMETA(DisplayName = "Pursuing"),
	Avoiding UMETA(DisplayName = "Avoiding"),
	Captured UMETA(DisplayName = "Captured")
};

/**
 * Police / Robber / Civilian role state for the cops-and-robbers demo.
 * Destination overrides are applied by the CrimeResponse priority layer.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class THESIS_API UAgentCrimeRoleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentCrimeRoleComponent();

	UFUNCTION(BlueprintCallable, Category = "Crime")
	ECrimeRole GetRole() const { return Role; }

	UFUNCTION(BlueprintCallable, Category = "Crime")
	void SetRole(ECrimeRole NewRole);

	UFUNCTION(BlueprintCallable, Category = "Crime")
	ECrimeBehaviorState GetBehaviorState() const { return BehaviorState; }

	UFUNCTION(BlueprintCallable, Category = "Crime")
	ABoidsAB* GetFocusTarget() const { return FocusTarget.Get(); }

	/** True when feature is on and this agent needs CrimeResponse priority. */
	UFUNCTION(BlueprintCallable, Category = "Crime")
	bool WantsCrimeResponse() const;

	/** Advance timers, detection, crime rolls, capture checks. Call every tick when feature enabled. */
	void UpdateCrimeLogic(float DeltaTime, ABoidsAB* Self);

	/** World location for StartMoveTo (pursue / flee / avoid). */
	UFUNCTION(BlueprintCallable, Category = "Crime")
	bool ComputeCrimeDestination(FVector& OutDestination) const;

	float ConsumeUpdateTimer(float DeltaTime);
	void ResetUpdateTimer(float Interval);

	/** Parse optional schedule/XML role string (Civilian|Police|Robber). */
	static bool TryParseRoleString(const FString& RoleStr, ECrimeRole& OutRole);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime")
	ECrimeRole Role = ECrimeRole::Civilian;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crime")
	ECrimeBehaviorState BehaviorState = ECrimeBehaviorState::None;

	UPROPERTY()
	TWeakObjectPtr<ABoidsAB> FocusTarget;

	float CrimeCheckTimer = 0.f;
	float CrimeActiveTimer = 0.f;
	float CapturedCooldown = 0.f;
	float UpdateTimer = 0.f;
	float PursuitElapsed = 0.f;
	bool bResponseCounted = false;

	void ClearFocus();
	void EnterCrime(ABoidsAB* Self);
	void ExitCrime();
	void BeginPursuit(ABoidsAB* Self, ABoidsAB* Robber);
	void EndPursuit(bool bCaptured);
	void BeginFlee(ABoidsAB* Self, ABoidsAB* Police);
	void BeginAvoid(ABoidsAB* Self, ABoidsAB* Robber);

	/** Raise MaxWalkSpeed for chase/flee; caches the previous walk speed once. */
	void ApplyChaseRunSpeed(ABoidsAB* Self);
	/** Restore cached walk MaxWalkSpeed when chase/flee ends. */
	void RestoreWalkSpeed(ABoidsAB* Self);

	ABoidsAB* FindNearestRobberInCrime(ABoidsAB* Self, float Radius) const;
	ABoidsAB* FindNearestPolice(ABoidsAB* Self, float Radius) const;
	ABoidsAB* FindNearestThreateningRobber(ABoidsAB* Self, float Radius) const;

	float CachedWalkSpeed = 0.f;
	bool bUsingChaseRunSpeed = false;
};
