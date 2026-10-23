#include "Simulation/ValidationDebugPanelSubsystem.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationClock.h"
#include "ScheduleManager.h"
#include "BoidsAB.h"
#include "Actions/AgentAction.h"
#include "Actions/GoToAction.h"
#include "Actions/InteractAction.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ValidationDebugPanelCVars
{
	static TAutoConsoleVariable<int32> CVarShowPanel(
		TEXT("thesis.ValidationDebugPanel"),
		1,
		TEXT("1 = show Validation Statistics on-screen panel when ValidationStats feature is enabled; 0 = hide"),
		ECVF_Default);
}

void UValidationDebugPanelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SampleAccumulator = 0.f;
	bSessionStarted = false;
}

void UValidationDebugPanelSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
		{
			if (Analytics->IsValidationSessionActive())
			{
				Analytics->EndValidationSession();
			}
		}
	}
	bSessionStarted = false;
	Super::Deinitialize();
}

TStatId UValidationDebugPanelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UValidationDebugPanelSubsystem, STATGROUP_Tickables);
}

bool UValidationDebugPanelSubsystem::ShouldShowPanel() const
{
	if (ValidationDebugPanelCVars::CVarShowPanel.GetValueOnGameThread() == 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return false;
	}

	const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::ValidationStats))
	{
		return false;
	}

	return Registry->GetValidationAnalyticsSettings().bShowOnScreenPanel;
}

void UValidationDebugPanelSubsystem::EnsureSessionStarted()
{
	if (bSessionStarted)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
	USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>();
	if (!Registry || !Analytics || !Registry->IsFeatureEnabled(ESimulationFeature::ValidationStats))
	{
		return;
	}

	FString ScenarioName = World->GetMapName();
	ScenarioName.RemoveFromStart(World->StreamingLevelsPrefix);
	Analytics->BeginValidationSession(ScenarioName);
	bSessionStarted = true;
}

void UValidationDebugPanelSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::ValidationStats))
	{
		return;
	}

	EnsureSessionStarted();

	USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>();
	if (!Analytics || !Analytics->IsValidationSessionActive())
	{
		return;
	}

	const FValidationAnalyticsSettings& Settings = Registry->GetValidationAnalyticsSettings();
	SampleAccumulator += DeltaTime;
	if (SampleAccumulator >= Settings.SampleIntervalSeconds)
	{
		Analytics->SampleValidationWorld(SampleAccumulator);
		SampleAccumulator = 0.f;
	}

	if (ShouldShowPanel())
	{
		DrawPanel();
	}
}

void UValidationDebugPanelSubsystem::DrawPanel() const
{
	if (!GEngine)
	{
		return;
	}

	const USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>();
	if (!Analytics)
	{
		return;
	}

	const FValidationAnalyticsSnapshot S = Analytics->GetValidationSnapshot();

	const auto FmtOptionalInt = [](int32 Value) -> FString
	{
		return Value < 0 ? TEXT("n/a") : FString::Printf(TEXT("%d"), Value);
	};
	const auto FmtOptionalFloat = [](float Value) -> FString
	{
		return Value < 0.f ? TEXT("n/a") : FString::Printf(TEXT("%.2f"), Value);
	};

	FString Text;
	Text += TEXT("==============================\n");
	Text += TEXT("SIMULATION\n");
	Text += TEXT("==============================\n");

	FString ModeLabel = TEXT("Visual");
	if (const USimulationFeatureRegistry* Reg = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
	{
		ModeLabel = Reg->GetSimulationModeLabel();
	}

	const AScheduleManager* SM = Cast<AScheduleManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass()));
	if (SM)
	{
		if (const USimulationClock* Clock = SM->GetSimulationClock())
		{
			// Authoritative time-of-day from USimulationClock (not tick/elapsed UI timer).
			Text += FString::Printf(TEXT("Time: %s\n"), *Clock->ToTimeOfDayHHMMString());
			Text += FString::Printf(TEXT("Day: %d\n"), Clock->GetDay() + 1);
			Text += FString::Printf(TEXT("Speed: %.0fx\n"), Clock->GetSimulationSpeed());
			Text += FString::Printf(TEXT("Mode: %s\n"), *ModeLabel);
		}
	}
	else
	{
		Text += FString::Printf(TEXT("Mode: %s\n"), *ModeLabel);
	}

	Text += TEXT("--------------------------------\n");
	Text += TEXT("Validation Statistics\n");
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Scenario Name: %s\n"), *S.ScenarioName);

	if (SM)
	{
		static const FName Agent0Id(TEXT("Agent_0"));
		FString ActionName = TEXT("none");
		FString TargetName = TEXT("-");
		FString NextRule = TEXT("-");

		if (const ABoidsAB* Agent0 = SM->GetBoidById(Agent0Id))
		{
			if (const UAgentAction* Active = Agent0->GetActiveScheduleAction())
			{
				ActionName = Active->GetClass()->GetName();
				ActionName.RemoveFromStart(TEXT("U"));
				ActionName.RemoveFromEnd(TEXT("Action"));
				if (const UGoToAction* GoTo = Cast<UGoToAction>(Active))
				{
					TargetName = GoTo->GetDestinationTag();
				}
				else if (const UInteractAction* Interact = Cast<UInteractAction>(Active))
				{
					TargetName = Interact->GetTargetDisplayName();
					ActionName = TEXT("Interact");
				}
			}
		}

		if (const TArray<FScheduleRule>* Rules = SM->GetRulesForAgent(Agent0Id))
		{
			const int32 Day = SM->DayCounter;
			for (const FScheduleRule& Rule : *Rules)
			{
				if (Rule.LastExecutedDay == Day)
				{
					continue;
				}
				const TCHAR* Act =
					Rule.ThenAction.Action == EScheduledAction::SA_MoveTo ? TEXT("MoveTo") :
					Rule.ThenAction.Action == EScheduledAction::SA_UseStation ? TEXT("UseStation") :
					Rule.ThenAction.Action == EScheduledAction::SA_Interact ? TEXT("Interact") :
					Rule.ThenAction.Action == EScheduledAction::SA_Idle ? TEXT("Idle") :
					Rule.ThenAction.Action == EScheduledAction::SA_Trigger ? TEXT("Trigger") :
					Rule.ThenAction.Action == EScheduledAction::SA_StartEvacuation ? TEXT("StartEvacuation") :
					TEXT("?");
				FString Area = Rule.ThenAction.TargetActorName;
				if (Area.IsEmpty())
				{
					Area = Rule.ThenAction.StationTypeName;
				}
				NextRule = FString::Printf(TEXT("%02d:%02d → %s %s"),
					Rule.TimeMinutes / 60, Rule.TimeMinutes % 60, Act, *Area);
				break;
			}
		}

		Text += TEXT("Agent_0\n");
		Text += FString::Printf(TEXT("  Action: %s\n"), *ActionName);
		Text += FString::Printf(TEXT("  Target: %s\n"), *TargetName);
		Text += FString::Printf(TEXT("  Next Rule: %s\n"), *NextRule);
	}

	Text += FString::Printf(TEXT("Wall/analytics elapsed: %.1fs\n"), S.SimulationTimeSeconds);
	Text += FString::Printf(TEXT("Agents Spawned: %d\n"), S.AgentsSpawned);
	Text += FString::Printf(TEXT("Agents Active: %d\n"), S.AgentsActive);
	Text += FString::Printf(TEXT("Agents Completed: %d\n"), S.AgentsCompleted);
	Text += FString::Printf(TEXT("Agents Failed: %s\n"), *FmtOptionalInt(S.AgentsFailed));
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Average Travel Time: %.2fs\n"), S.AverageTravelTimeSeconds);
	Text += FString::Printf(TEXT("Minimum Travel Time: %.2fs\n"), S.MinTravelTimeSeconds);
	Text += FString::Printf(TEXT("Maximum Travel Time: %.2fs\n"), S.MaxTravelTimeSeconds);
	Text += FString::Printf(TEXT("Average Speed: %.1f\n"), S.AverageSpeed);
	Text += FString::Printf(TEXT("Average Path Length: %s\n"), *FmtOptionalFloat(S.AveragePathLength));
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Throughput (agents/sec): %.3f\n"), S.ThroughputAgentsPerSecond);
	Text += FString::Printf(TEXT("Flow Rate: %.3f\n"), S.FlowRate);
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Average Queue Length: %.2f\n"), S.AverageQueueLength);
	Text += FString::Printf(TEXT("Maximum Queue Length: %d\n"), S.MaximumQueueLength);
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Peak Density: %.0f\n"), S.PeakDensity);
	Text += FString::Printf(TEXT("Current Density: %.0f\n"), S.CurrentDensity);
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Average Waiting Time: %.2fs\n"), S.AverageWaitingTimeSeconds);
	Text += FString::Printf(TEXT("Maximum Waiting Time: %.2fs\n"), S.MaximumWaitingTimeSeconds);
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Average Congestion Time: %.2fs\n"), S.AverageCongestionTimeSeconds);
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("FPS: %.1f\n"), S.FPS);
	Text += TEXT("--------------------------------\n");
	Text += FString::Printf(TEXT("Current Agent Count: %d\n"), S.CurrentAgentCount);
	Text += FString::Printf(TEXT("Current Moving Agents: %d\n"), S.CurrentMovingAgents);
	Text += FString::Printf(TEXT("Current Waiting Agents: %d\n"), S.CurrentWaitingAgents);
	Text += FString::Printf(TEXT("Current Average Velocity: %.1f\n"), S.CurrentAverageVelocity);
	Text += TEXT("\n(toggle: thesis.ValidationDebugPanel 0|1)");

	GEngine->AddOnScreenDebugMessage(
		/*Key*/ 0x56414C44, // 'VALD'
		/*TimeToDisplay*/ 0.f,
		FColor::Cyan,
		Text);
}
