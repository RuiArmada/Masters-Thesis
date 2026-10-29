#include "AI/AgentFallbackHandlerComponent.h"
#include "AI/AgentAIDebug.h"
#include "BoidsAB.h"

namespace
{
bool IsInfrastructurePlanningBoid(const ABoidsAB* Boid)
{
	return Boid && Boid->IsInfrastructurePlanningActive();
}
}

UAgentFallbackHandlerComponent::UAgentFallbackHandlerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgentFallbackHandlerComponent::ResetTracking()
{
	PathFailCount = 0;
	StuckTimer = 0.f;
	bHasStuckSample = false;
	QueueWaitTimer = 0.f;
	bInStationQueue = false;
	WaitCountdown = 0.f;
	bInWaitState = false;
}

void UAgentFallbackHandlerComponent::OnMovementCommandIssued(ABoidsAB* Boid, const FVector& Target)
{
	LastIssuedTarget = Target;
	PathFailCount = 0;
	StuckTimer = 0.f;
	bHasStuckSample = false;
	LastResolution = EAgentFallbackResolution::None;
	LastResolutionNote.Reset();
	NeedMovementSuppressUntil = -1000.f;
}

void UAgentFallbackHandlerComponent::OnPathResult(ABoidsAB* Boid, bool bSuccess, const FVector& AttemptedTarget)
{
	if (!Boid)
	{
		return;
	}

	if (bSuccess)
	{
		PathFailCount = 0;
		return;
	}

	PathFailCount++;

	if (Boid->IsFleeing())
	{
		ApplyResolution(Boid, EAgentFallbackResolution::Retry, TEXT("Evacuation path failure — delegated recovery"));
		Boid->HandleEvacuationPathFailure(AttemptedTarget);
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::Retry, LastResolutionNote);
		return;
	}

	if (PathFailCount <= MaxPathRetries)
	{
		ApplyResolution(Boid, EAgentFallbackResolution::Retry, FString::Printf(TEXT("Path retry %d/%d"), PathFailCount, MaxPathRetries));
		Boid->RecomputePathToCurrentTarget();
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::Retry, LastResolutionNote);
		return;
	}

	const ENeedNavIntent Intent = Boid->GetActiveNeedNavIntent();
	if (Intent == ENeedNavIntent::CriticalFood && Boid->TryNavigateToAlternateFoodLocation(AttemptedTarget))
	{
		PathFailCount = 0;
		ApplyResolution(Boid, EAgentFallbackResolution::AlternateDestination, TEXT("Trying alternate food location"));
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::AlternateDestination, LastResolutionNote);
		return;
	}
	if (Intent == ENeedNavIntent::CriticalHome && Boid->TryNavigateToAlternateHomeLocation(AttemptedTarget))
	{
		PathFailCount = 0;
		ApplyResolution(Boid, EAgentFallbackResolution::AlternateDestination, TEXT("Trying alternate home location"));
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::AlternateDestination, LastResolutionNote);
		return;
	}

	if (Intent == ENeedNavIntent::CriticalFood || Intent == ENeedNavIntent::CriticalHome)
	{
		PathFailCount = 0;
		const FString Note = Intent == ENeedNavIntent::CriticalFood
			                         ? TEXT("Need food path exhausted; waiting before retry")
			                         : TEXT("Need home path exhausted; waiting before retry");
		ApplyResolution(Boid, EAgentFallbackResolution::Idle, Note);
		Boid->SetIdle(true);
		if (UWorld* World = Boid->GetWorld())
		{
			NeedMovementSuppressUntil = World->GetTimeSeconds() + NeedMovementSuppressSeconds;
		}
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::Idle, LastResolutionNote);
		return;
	}

	if (IsInfrastructurePlanningBoid(Boid))
	{
		PathFailCount = 0;
		ApplyResolution(Boid, EAgentFallbackResolution::Idle, TEXT("UC3: path failed; holding for schedule"));
		Boid->StopMovement();
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::Idle, LastResolutionNote);
		return;
	}

	const FVector Home = Boid->HomeLocation;
	if (!Home.IsZero() && FVector::DistSquared(AttemptedTarget, Home) > FMath::Square(50.f))
	{
		PathFailCount = 0;
		ApplyResolution(Boid, EAgentFallbackResolution::GoHome, TEXT("Path failed; routing home"));
		Boid->LeaveCurrentStation();
		Boid->StartMoveTo(Home);
		LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::GoHome, LastResolutionNote);
		return;
	}

	ApplyResolution(Boid, EAgentFallbackResolution::Idle, TEXT("Path failed; no safe home — idle"));
	Boid->SetIdle(true);
	LogResolution(Boid, EAgentFallbackFailureKind::Pathfinding, EAgentFallbackResolution::Idle, LastResolutionNote);
}

void UAgentFallbackHandlerComponent::NotifyScheduleFailure(ABoidsAB* Boid, EAgentFallbackFailureKind Kind, const FString& Context)
{
	if (!Boid)
	{
		return;
	}

	if (Kind == EAgentFallbackFailureKind::ScheduleTargetMissing || Kind == EAgentFallbackFailureKind::ScheduleAreaMissing
	    || Kind == EAgentFallbackFailureKind::AreaClosed)
	{
		ApplyResolution(Boid, EAgentFallbackResolution::SkipTask, Context);
		LogResolution(Boid, Kind, EAgentFallbackResolution::SkipTask, Context);
		// Do not auto-move; agent continues current activity
		return;
	}

	if (Kind == EAgentFallbackFailureKind::StationUnavailable)
	{
		if (IsInfrastructurePlanningBoid(Boid))
		{
			ApplyResolution(Boid, EAgentFallbackResolution::Idle, TEXT("UC3: station unavailable; holding for schedule"));
			Boid->SetIdle(true);
			LogResolution(Boid, Kind, EAgentFallbackResolution::Idle, LastResolutionNote);
			return;
		}

		const FVector Home = Boid->HomeLocation;
		if (!Home.IsZero())
		{
			ApplyResolution(Boid, EAgentFallbackResolution::GoHome, Context);
			Boid->LeaveCurrentStation();
			Boid->StartMoveTo(Home);
		}
		else
		{
			ApplyResolution(Boid, EAgentFallbackResolution::Idle, Context);
			Boid->SetIdle(true);
		}
		LogResolution(Boid, Kind,
		              !Home.IsZero() ? EAgentFallbackResolution::GoHome : EAgentFallbackResolution::Idle,
		              LastResolutionNote);
	}
}

void UAgentFallbackHandlerComponent::OnEnteredStationQueue(ABoidsAB* Boid)
{
	bInStationQueue = true;
	QueueWaitTimer = 0.f;
}

void UAgentFallbackHandlerComponent::OnLeftStationQueue(ABoidsAB* Boid)
{
	bInStationQueue = false;
	QueueWaitTimer = 0.f;
}

void UAgentFallbackHandlerComponent::TickFallback(float DeltaTime, ABoidsAB* Boid)
{
	if (!Boid || DeltaTime <= 0.f)
	{
		return;
	}

	if (bInWaitState)
	{
		WaitCountdown -= DeltaTime;
		if (WaitCountdown <= 0.f)
		{
			bInWaitState = false;
		}
		return;
	}

	if (bInStationQueue && !Boid->IsFleeing())
	{
		QueueWaitTimer += DeltaTime;
		if (QueueWaitTimer >= StationQueueTimeoutSeconds)
		{
			if (IsInfrastructurePlanningBoid(Boid))
			{
				QueueWaitTimer = 0.f;
				ApplyResolution(Boid, EAgentFallbackResolution::Wait, TEXT("UC3: station queue wait continues"));
				LogResolution(Boid, EAgentFallbackFailureKind::StationQueueTimeout, EAgentFallbackResolution::Wait,
				              LastResolutionNote);
			}
			else
			{
				QueueWaitTimer = 0.f;
				bInStationQueue = false;
				ApplyResolution(Boid, EAgentFallbackResolution::AlternateDestination, TEXT("Station queue timeout"));
				Boid->LeaveCurrentStation();
				const FVector Home = Boid->HomeLocation;
				if (!Home.IsZero())
				{
					Boid->StartMoveTo(Home);
				}
				else
				{
					Boid->SetIdle(true);
				}
				LogResolution(Boid, EAgentFallbackFailureKind::StationQueueTimeout, EAgentFallbackResolution::AlternateDestination, LastResolutionNote);
			}
		}
	}

	if (!Boid->IsMoving() || Boid->IsFleeing() || Boid->IsIdle())
	{
		StuckTimer = 0.f;
		bHasStuckSample = false;
		return;
	}

	const FVector Loc = Boid->GetActorLocation();
	if (!bHasStuckSample)
	{
		LastStuckSampleLocation = Loc;
		bHasStuckSample = true;
		StuckTimer = 0.f;
		return;
	}

	if (FVector::Dist(Loc, LastStuckSampleLocation) < StuckDistanceThreshold)
	{
		StuckTimer += DeltaTime;
		if (StuckTimer >= StuckTimeSeconds)
		{
			if (IsInfrastructurePlanningBoid(Boid))
			{
				const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
				if ((Now - LastUc3StuckRetryTime) < 12.f)
				{
					StuckTimer = 0.f;
					return;
				}
				LastUc3StuckRetryTime = Now;
			}

			StuckTimer = 0.f;
			bHasStuckSample = false;
			ApplyResolution(Boid, EAgentFallbackResolution::Wait, TEXT("Stuck — brief wait then retry path"));
			bInWaitState = true;
			WaitCountdown = WaitDurationSeconds;
			Boid->RecomputePathToCurrentTarget();
			LogResolution(Boid, EAgentFallbackFailureKind::StuckWhileMoving, EAgentFallbackResolution::Wait, LastResolutionNote);
		}
	}
	else
	{
		LastStuckSampleLocation = Loc;
		StuckTimer = 0.f;
	}
}

void UAgentFallbackHandlerComponent::ApplyResolution(ABoidsAB* Boid, EAgentFallbackResolution Res, const FString& Note)
{
	LastResolution = Res;
	LastResolutionNote = Note;
}

bool UAgentFallbackHandlerComponent::ShouldSuppressNeedMovement() const
{
	if (!GetWorld())
	{
		return false;
	}
	return GetWorld()->GetTimeSeconds() < NeedMovementSuppressUntil;
}

void UAgentFallbackHandlerComponent::LogResolution(ABoidsAB* Boid, EAgentFallbackFailureKind Kind, EAgentFallbackResolution Res, const FString& Note) const
{
#if !UE_BUILD_SHIPPING
	if (Res == EAgentFallbackResolution::Retry)
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		if ((Now - LastRetryLogTime) < 6.f)
		{
			return;
		}
		LastRetryLogTime = Now;
	}

	if (Kind == EAgentFallbackFailureKind::StationQueueTimeout
	    && Res == EAgentFallbackResolution::Wait
	    && Note.Contains(TEXT("UC3:")))
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		if ((Now - LastRetryLogTime) < 30.f)
		{
			return;
		}
		LastRetryLogTime = Now;
	}

	THESIS_AGENT_AI_LOG(LogTemp, Warning,
	                    TEXT("[AgentFallback] %s | Kind=%d Res=%d | %s"),
	                    Boid ? *Boid->GetName() : TEXT("NULL"),
	                    static_cast<int32>(Kind),
	                    static_cast<int32>(Res),
	                    *Note);
#else
	(void)Boid;
	(void)Kind;
	(void)Res;
	(void)Note;
#endif
}
