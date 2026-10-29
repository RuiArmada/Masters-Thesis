#include "Simulation/SimulationConfigLoader.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "XmlFile.h"
#include "Misc/Paths.h"

namespace SimulationConfigKeys
{
	static const FString FeatureEvacuation = TEXT("enableEvacuation");
	static const FString FeatureLeadership = TEXT("enableLeadership");
	static const FString FeatureCopsAndRobbers = TEXT("enableCopsAndRobbers");
	static const FString FeatureInfrastructure = TEXT("enableInfrastructureAnalysis");
	static const FString FeatureDailyUrbanLife = TEXT("enableDailyUrbanLife");
	static const FString FeatureValidationStats = TEXT("enableValidationStats");
}

static bool ParseBoolAttribute(const FXmlNode* Node, const FString& AttrName, bool DefaultValue)
{
	if (!Node)
	{
		return DefaultValue;
	}
	const FString Value = Node->GetAttribute(AttrName);
	if (Value.IsEmpty())
	{
		return DefaultValue;
	}
	return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
}

static float ParseFloatAttribute(const FXmlNode* Node, const FString& AttrName, float DefaultValue)
{
	if (!Node)
	{
		return DefaultValue;
	}
	const FString Value = Node->GetAttribute(AttrName);
	return Value.IsEmpty() ? DefaultValue : FCString::Atof(*Value);
}

static int32 ParseIntAttribute(const FXmlNode* Node, const FString& AttrName, int32 DefaultValue)
{
	if (!Node)
	{
		return DefaultValue;
	}
	const FString Value = Node->GetAttribute(AttrName);
	return Value.IsEmpty() ? DefaultValue : FCString::Atoi(*Value);
}

bool FSimulationConfigLoader::InspectClockModeSections(const FString& AbsoluteFilePath, FClockModeSectionPresence& OutPresence)
{
	OutPresence = FClockModeSectionPresence();
	FXmlFile XmlDocument;
	if (!XmlDocument.LoadFile(AbsoluteFilePath))
	{
		return false;
	}

	const FXmlNode* Root = XmlDocument.GetRootNode();
	if (!Root)
	{
		return false;
	}

	OutPresence.bHasSimulationClock = Root->FindChildNode(TEXT("simulationClock")) != nullptr;
	OutPresence.bHasSimulationMode = Root->FindChildNode(TEXT("simulationMode")) != nullptr;
	OutPresence.bHasFastSimulation = Root->FindChildNode(TEXT("fastSimulation")) != nullptr;
	return true;
}

bool FSimulationConfigLoader::LoadIntoRegistry(USimulationFeatureRegistry* Registry, const FString& AbsoluteFilePath)
{
	if (!Registry)
	{
		return false;
	}

	FXmlFile XmlDocument;
	if (!XmlDocument.LoadFile(AbsoluteFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SimulationConfig] Could not load %s — using defaults"), *AbsoluteFilePath);
		return false;
	}

	const FXmlNode* Root = XmlDocument.GetRootNode();
	if (!Root)
	{
		return false;
	}

	const FXmlNode* FeaturesNode = Root->FindChildNode(TEXT("features"));
	if (FeaturesNode)
	{
		for (const FXmlNode* FeatureNode = FeaturesNode->GetFirstChildNode(); FeatureNode; FeatureNode = FeatureNode->GetNextNode())
		{
			const FString Name = FeatureNode->GetAttribute(TEXT("name"));
			const bool bValue = ParseBoolAttribute(FeatureNode, TEXT("value"), false);

			if (Name.Equals(SimulationConfigKeys::FeatureEvacuation, ESearchCase::IgnoreCase))
			{
				Registry->SetFeatureEnabled(ESimulationFeature::Evacuation, bValue);
			}
			else if (Name.Equals(SimulationConfigKeys::FeatureLeadership, ESearchCase::IgnoreCase))
			{
				Registry->SetFeatureEnabled(ESimulationFeature::Leadership, bValue);
			}
			else if (Name.Equals(SimulationConfigKeys::FeatureCopsAndRobbers, ESearchCase::IgnoreCase))
			{
				Registry->SetFeatureEnabled(ESimulationFeature::CopsAndRobbers, bValue);
			}
			else if (Name.Equals(SimulationConfigKeys::FeatureInfrastructure, ESearchCase::IgnoreCase))
			{
				Registry->SetFeatureEnabled(ESimulationFeature::InfrastructureAnalysis, bValue);
			}
			else if (Name.Equals(SimulationConfigKeys::FeatureDailyUrbanLife, ESearchCase::IgnoreCase))
			{
				Registry->SetFeatureEnabled(ESimulationFeature::DailyUrbanLife, bValue);
			}
			else if (Name.Equals(SimulationConfigKeys::FeatureValidationStats, ESearchCase::IgnoreCase))
			{
				Registry->SetFeatureEnabled(ESimulationFeature::ValidationStats, bValue);
			}
		}
	}

	FEvacuationRoutingSettings EvacSettings = Registry->GetEvacuationSettings();
	const FXmlNode* EvacNode = Root->FindChildNode(TEXT("evacuation"));
	if (EvacNode)
	{
		for (const FXmlNode* Child = EvacNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("globalRadius"), ESearchCase::IgnoreCase))
			{
				EvacSettings.GlobalRadius = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.GlobalRadius);
			}
			else if (Tag.Equals(TEXT("distanceWeight"), ESearchCase::IgnoreCase))
			{
				EvacSettings.DistanceWeight = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.DistanceWeight);
			}
			else if (Tag.Equals(TEXT("congestionWeight"), ESearchCase::IgnoreCase))
			{
				EvacSettings.CongestionWeight = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.CongestionWeight);
			}
			else if (Tag.Equals(TEXT("reevaluationInterval"), ESearchCase::IgnoreCase))
			{
				EvacSettings.CongestionReevaluationIntervalSeconds = ParseFloatAttribute(Child, TEXT("value"),
				                                                                         EvacSettings.CongestionReevaluationIntervalSeconds);
			}
			else if (Tag.Equals(TEXT("switchThreshold"), ESearchCase::IgnoreCase))
			{
				EvacSettings.CongestionSwitchThreshold = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.CongestionSwitchThreshold);
			}
			else if (Tag.Equals(TEXT("maxReassignmentAttempts"), ESearchCase::IgnoreCase))
			{
				EvacSettings.MaxReassignmentAttempts = ParseIntAttribute(Child, TEXT("value"), EvacSettings.MaxReassignmentAttempts);
			}
			else if (Tag.Equals(TEXT("preferNearestExitInitially"), ESearchCase::IgnoreCase))
			{
				EvacSettings.bPreferNearestExitInitially = ParseBoolAttribute(Child, TEXT("value"), EvacSettings.bPreferNearestExitInitially);
			}
			else if (Tag.Equals(TEXT("reactionDelayMin"), ESearchCase::IgnoreCase))
			{
				EvacSettings.ReactionDelayMin = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.ReactionDelayMin);
			}
			else if (Tag.Equals(TEXT("reactionDelayMax"), ESearchCase::IgnoreCase))
			{
				EvacSettings.ReactionDelayMax = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.ReactionDelayMax);
			}
			else if (Tag.Equals(TEXT("evacuationDuration"), ESearchCase::IgnoreCase))
			{
				EvacSettings.EvacuationDuration = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.EvacuationDuration);
			}
			else if (Tag.Equals(TEXT("evacuationTimerStartDelay"), ESearchCase::IgnoreCase))
			{
				EvacSettings.EvacuationTimerStartDelay = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.EvacuationTimerStartDelay);
			}
			else if (Tag.Equals(TEXT("speedVariationMin"), ESearchCase::IgnoreCase))
			{
				EvacSettings.SpeedVariationMin = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.SpeedVariationMin);
			}
			else if (Tag.Equals(TEXT("speedVariationMax"), ESearchCase::IgnoreCase))
			{
				EvacSettings.SpeedVariationMax = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.SpeedVariationMax);
			}
			else if (Tag.Equals(TEXT("congestionToleranceVariation"), ESearchCase::IgnoreCase))
			{
				EvacSettings.CongestionToleranceVariation = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.CongestionToleranceVariation);
			}
			else if (Tag.Equals(TEXT("nearestExitBiasVariation"), ESearchCase::IgnoreCase))
			{
				EvacSettings.NearestExitBiasVariation = ParseFloatAttribute(Child, TEXT("value"), EvacSettings.NearestExitBiasVariation);
			}
			else if (Tag.Equals(TEXT("enableCasualties"), ESearchCase::IgnoreCase))
			{
				EvacSettings.bEnableCasualties = ParseBoolAttribute(Child, TEXT("value"), EvacSettings.bEnableCasualties);
			}
			else if (Tag.Equals(TEXT("enableCelebrationState"), ESearchCase::IgnoreCase))
			{
				EvacSettings.bEnableCelebrationState = ParseBoolAttribute(Child, TEXT("value"), EvacSettings.bEnableCelebrationState);
			}
		}
	}

	Registry->SetEvacuationSettings(EvacSettings);

	FDailyUrbanLifeSettings DailySettings = Registry->GetDailyUrbanLifeSettings();
	const FXmlNode* DailyNode = Root->FindChildNode(TEXT("dailyUrbanLife"));
	if (DailyNode)
	{
		for (const FXmlNode* Child = DailyNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("enableNeeds"), ESearchCase::IgnoreCase))
			{
				DailySettings.bEnableNeedsDuringDailyLife = ParseBoolAttribute(Child, TEXT("value"), DailySettings.bEnableNeedsDuringDailyLife);
			}
			else if (Tag.Equals(TEXT("needsRateScale"), ESearchCase::IgnoreCase))
			{
				DailySettings.NeedsRateScale = ParseFloatAttribute(Child, TEXT("value"), DailySettings.NeedsRateScale);
			}
			else if (Tag.Equals(TEXT("startAgentsAtHome"), ESearchCase::IgnoreCase))
			{
				DailySettings.bStartAgentsAtHome = ParseBoolAttribute(Child, TEXT("value"), DailySettings.bStartAgentsAtHome);
			}
			else if (Tag.Equals(TEXT("maxScheduleMinuteOffset"), ESearchCase::IgnoreCase))
			{
				DailySettings.MaxScheduleMinuteOffset = ParseIntAttribute(Child, TEXT("value"), DailySettings.MaxScheduleMinuteOffset);
			}
			else if (Tag.Equals(TEXT("walkingSpeedMin"), ESearchCase::IgnoreCase))
			{
				DailySettings.WalkingSpeedMin = ParseFloatAttribute(Child, TEXT("value"), DailySettings.WalkingSpeedMin);
			}
			else if (Tag.Equals(TEXT("walkingSpeedMax"), ESearchCase::IgnoreCase))
			{
				DailySettings.WalkingSpeedMax = ParseFloatAttribute(Child, TEXT("value"), DailySettings.WalkingSpeedMax);
			}
			else if (Tag.Equals(TEXT("interactionPauseMin"), ESearchCase::IgnoreCase))
			{
				DailySettings.InteractionPauseMinSeconds = ParseFloatAttribute(Child, TEXT("value"), DailySettings.InteractionPauseMinSeconds);
			}
			else if (Tag.Equals(TEXT("interactionPauseMax"), ESearchCase::IgnoreCase))
			{
				DailySettings.InteractionPauseMaxSeconds = ParseFloatAttribute(Child, TEXT("value"), DailySettings.InteractionPauseMaxSeconds);
			}
			else if (Tag.Equals(TEXT("scheduleFile"), ESearchCase::IgnoreCase))
			{
				const FString File = Child->GetAttribute(TEXT("value"));
				if (!File.IsEmpty())
				{
					DailySettings.DefaultScheduleFile = File;
				}
			}
			else if (Tag.Equals(TEXT("breakAreaTag"), ESearchCase::IgnoreCase))
			{
				const FString TagValue = Child->GetAttribute(TEXT("value"));
				if (!TagValue.IsEmpty())
				{
					DailySettings.BreakAreaTag = TagValue;
				}
			}
			else if (Tag.Equals(TEXT("minStationsPerWorkArea"), ESearchCase::IgnoreCase))
			{
				DailySettings.MinStationsPerWorkArea = ParseIntAttribute(
					Child, TEXT("value"), DailySettings.MinStationsPerWorkArea);
			}
			else if (Tag.Equals(TEXT("homeReturnOverridesNeeds"), ESearchCase::IgnoreCase))
			{
				DailySettings.bScheduledHomeReturnOverridesNeeds = ParseBoolAttribute(
					Child, TEXT("value"), DailySettings.bScheduledHomeReturnOverridesNeeds);
			}
		}
	}
	Registry->SetDailyUrbanLifeSettings(DailySettings);

	FInfrastructurePlanningSettings InfraSettings = Registry->GetInfrastructurePlanningSettings();
	const FXmlNode* InfraNode = Root->FindChildNode(TEXT("infrastructurePlanning"));
	if (InfraNode)
	{
		for (const FXmlNode* Child = InfraNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("layoutVariant"), ESearchCase::IgnoreCase))
			{
				const FString Value = Child->GetAttribute(TEXT("value"));
				if (!Value.IsEmpty())
				{
					InfraSettings.LayoutVariantName = Value;
				}
			}
			else if (Tag.Equals(TEXT("comparisonBaseline"), ESearchCase::IgnoreCase))
			{
				const FString Value = Child->GetAttribute(TEXT("value"));
				if (!Value.IsEmpty())
				{
					InfraSettings.ComparisonBaselineVariant = Value;
				}
			}
			else if (Tag.Equals(TEXT("scheduleFile"), ESearchCase::IgnoreCase))
			{
				const FString Value = Child->GetAttribute(TEXT("value"));
				if (!Value.IsEmpty())
				{
					InfraSettings.ScheduleFile = Value;
				}
			}
			else if (Tag.Equals(TEXT("autoSelectFromMap"), ESearchCase::IgnoreCase))
			{
				InfraSettings.bAutoSelectFromMap = ParseBoolAttribute(
					Child, TEXT("value"), InfraSettings.bAutoSelectFromMap);
			}
			else if (Tag.Equals(TEXT("samplingIntervalSeconds"), ESearchCase::IgnoreCase))
			{
				InfraSettings.SamplingIntervalSeconds = ParseFloatAttribute(
					Child, TEXT("value"), InfraSettings.SamplingIntervalSeconds);
			}
			else if (Tag.Equals(TEXT("runDurationSimMinutes"), ESearchCase::IgnoreCase))
			{
				InfraSettings.RunDurationSimMinutes = ParseFloatAttribute(
					Child, TEXT("value"), InfraSettings.RunDurationSimMinutes);
			}
			else if (Tag.Equals(TEXT("enableVisualization"), ESearchCase::IgnoreCase))
			{
				InfraSettings.bEnableVisualization = ParseBoolAttribute(
					Child, TEXT("value"), InfraSettings.bEnableVisualization);
			}
			else if (Tag.Equals(TEXT("reuseDailyUrbanLifeBootstrap"), ESearchCase::IgnoreCase))
			{
				InfraSettings.bReuseDailyUrbanLifeBootstrap = ParseBoolAttribute(
					Child, TEXT("value"), InfraSettings.bReuseDailyUrbanLifeBootstrap);
			}
			else if (Tag.Equals(TEXT("serviceStationCapacity"), ESearchCase::IgnoreCase))
			{
				InfraSettings.ServiceStationCapacity = ParseIntAttribute(
					Child, TEXT("value"), InfraSettings.ServiceStationCapacity);
			}
			else if (Tag.Equals(TEXT("serviceStationTag"), ESearchCase::IgnoreCase))
			{
				const FString Value = Child->GetAttribute(TEXT("value"));
				if (!Value.IsEmpty())
				{
					InfraSettings.ServiceStationTag = FName(*Value);
				}
			}
			else if (Tag.Equals(TEXT("autoExportOnEnd"), ESearchCase::IgnoreCase))
			{
				InfraSettings.bAutoExportOnEnd = ParseBoolAttribute(
					Child, TEXT("value"), InfraSettings.bAutoExportOnEnd);
			}
			else if (Tag.Equals(TEXT("autoExportComparisonOnEnd"), ESearchCase::IgnoreCase))
			{
				InfraSettings.bAutoExportComparisonOnEnd = ParseBoolAttribute(
					Child, TEXT("value"), InfraSettings.bAutoExportComparisonOnEnd);
			}
			else if (Tag.Equals(TEXT("comparisonVariant"), ESearchCase::IgnoreCase))
			{
				const FString Value = Child->GetAttribute(TEXT("value"));
				if (!Value.IsEmpty())
				{
					InfraSettings.ComparisonVariantName = Value;
				}
			}
		}
	}
	Registry->SetInfrastructurePlanningSettings(InfraSettings);

	FLeadershipSettings LeadershipSettings = Registry->GetLeadershipSettings();
	const FXmlNode* LeadershipNode = Root->FindChildNode(TEXT("leadership"));
	if (LeadershipNode)
	{
		for (const FXmlNode* Child = LeadershipNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("defaultFollowDistance"), ESearchCase::IgnoreCase))
			{
				LeadershipSettings.DefaultFollowDistance = ParseFloatAttribute(
					Child, TEXT("value"), LeadershipSettings.DefaultFollowDistance);
			}
			else if (Tag.Equals(TEXT("formationUpdateInterval"), ESearchCase::IgnoreCase))
			{
				LeadershipSettings.FormationUpdateInterval = ParseFloatAttribute(
					Child, TEXT("value"), LeadershipSettings.FormationUpdateInterval);
			}
			else if (Tag.Equals(TEXT("leadershipOverridesSchedule"), ESearchCase::IgnoreCase))
			{
				LeadershipSettings.bLeadershipOverridesSchedule = ParseBoolAttribute(
					Child, TEXT("value"), LeadershipSettings.bLeadershipOverridesSchedule);
			}
		}
	}
	Registry->SetLeadershipSettings(LeadershipSettings);

	FCopsAndRobbersSettings CrimeSettings = Registry->GetCopsAndRobbersSettings();
	const FXmlNode* CrimeNode = Root->FindChildNode(TEXT("copsAndRobbers"));
	if (CrimeNode)
	{
		for (const FXmlNode* Child = CrimeNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("policeDetectionRadius"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.PoliceDetectionRadius = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.PoliceDetectionRadius);
			}
			else if (Tag.Equals(TEXT("robberDetectionRadius"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.RobberDetectionRadius = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.RobberDetectionRadius);
			}
			else if (Tag.Equals(TEXT("crimeProbability"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.CrimeProbability = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.CrimeProbability);
			}
			else if (Tag.Equals(TEXT("crimeCheckInterval"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.CrimeCheckInterval = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.CrimeCheckInterval);
			}
			else if (Tag.Equals(TEXT("crimeDuration"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.CrimeDuration = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.CrimeDuration);
			}
			else if (Tag.Equals(TEXT("captureRadius"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.CaptureRadius = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.CaptureRadius);
			}
			else if (Tag.Equals(TEXT("pursuitUpdateInterval"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.PursuitUpdateInterval = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.PursuitUpdateInterval);
			}
			else if (Tag.Equals(TEXT("fleeDistance"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.FleeDistance = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.FleeDistance);
			}
			else if (Tag.Equals(TEXT("chaseRunSpeed"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.ChaseRunSpeed = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.ChaseRunSpeed);
			}
			else if (Tag.Equals(TEXT("civiliansAvoidRobbers"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.bCiviliansAvoidRobbers = ParseBoolAttribute(
					Child, TEXT("value"), CrimeSettings.bCiviliansAvoidRobbers);
			}
			else if (Tag.Equals(TEXT("civilianAvoidRadius"), ESearchCase::IgnoreCase))
			{
				CrimeSettings.CivilianAvoidRadius = ParseFloatAttribute(
					Child, TEXT("value"), CrimeSettings.CivilianAvoidRadius);
			}
		}
	}
	Registry->SetCopsAndRobbersSettings(CrimeSettings);

	FSimulationClockSettings ClockSettings = Registry->GetSimulationClockSettings();
	const FXmlNode* ClockNode = Root->FindChildNode(TEXT("simulationClock"));
	if (ClockNode)
	{
		for (const FXmlNode* Child = ClockNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("simulationSpeed"), ESearchCase::IgnoreCase))
			{
				ClockSettings.SimulationSpeed = ParseFloatAttribute(
					Child, TEXT("value"), ClockSettings.SimulationSpeed);
			}
			else if (Tag.Equals(TEXT("startHour"), ESearchCase::IgnoreCase))
			{
				ClockSettings.StartHour = FMath::Clamp(
					ParseIntAttribute(Child, TEXT("value"), ClockSettings.StartHour), 0, 23);
			}
			else if (Tag.Equals(TEXT("startMinute"), ESearchCase::IgnoreCase))
			{
				ClockSettings.StartMinute = FMath::Clamp(
					ParseIntAttribute(Child, TEXT("value"), ClockSettings.StartMinute), 0, 59);
			}
		}
	}
	Registry->SetSimulationClockSettings(ClockSettings);

	ESimulationMode ParsedMode = Registry->GetSimulationMode();
	const FXmlNode* ModeNode = Root->FindChildNode(TEXT("simulationMode"));
	if (ModeNode)
	{
		const FString ModeAttr = ModeNode->GetAttribute(TEXT("mode"));
		if (ModeAttr.Equals(TEXT("Fast"), ESearchCase::IgnoreCase))
		{
			ParsedMode = ESimulationMode::Fast;
		}
		else if (ModeAttr.Equals(TEXT("AccurateTime"), ESearchCase::IgnoreCase)
			|| ModeAttr.Equals(TEXT("Accurate"), ESearchCase::IgnoreCase))
		{
			ParsedMode = ESimulationMode::AccurateTime;
		}
		else if (ModeAttr.Equals(TEXT("Visual"), ESearchCase::IgnoreCase))
		{
			ParsedMode = ESimulationMode::Visual;
		}
		else if (!ModeAttr.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[SimulationConfig] Unknown simulationMode mode='%s' in %s — keeping %s"),
				*ModeAttr,
				*AbsoluteFilePath,
				*Registry->GetSimulationModeLabel());
		}

		UE_LOG(LogTemp, Log,
			TEXT("[SimulationConfig] Parsed simulationMode mode='%s' → %s (from %s)"),
			*ModeAttr,
			ParsedMode == ESimulationMode::Fast ? TEXT("Fast")
				: (ParsedMode == ESimulationMode::AccurateTime ? TEXT("AccurateTime") : TEXT("Visual")),
			*AbsoluteFilePath);
	}
	Registry->SetSimulationMode(ParsedMode);

	FFastSimulationSettings FastSettings = Registry->GetFastSimulationSettings();
	const FXmlNode* FastNode = Root->FindChildNode(TEXT("fastSimulation"));
	if (FastNode)
	{
		for (const FXmlNode* Child = FastNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("simulationSpeed"), ESearchCase::IgnoreCase))
			{
				FastSettings.SimulationSpeed = ParseFloatAttribute(Child, TEXT("value"), FastSettings.SimulationSpeed);
			}
			else if (Tag.Equals(TEXT("runDurationSimMinutes"), ESearchCase::IgnoreCase))
			{
				FastSettings.RunDurationSimMinutes = ParseFloatAttribute(Child, TEXT("value"), FastSettings.RunDurationSimMinutes);
			}
			else if (Tag.Equals(TEXT("randomSeed"), ESearchCase::IgnoreCase))
			{
				FastSettings.RandomSeed = ParseIntAttribute(Child, TEXT("value"), FastSettings.RandomSeed);
			}
			else if (Tag.Equals(TEXT("autoExportOnEnd"), ESearchCase::IgnoreCase))
			{
				FastSettings.bAutoExportOnEnd = ParseBoolAttribute(Child, TEXT("value"), FastSettings.bAutoExportOnEnd);
			}
			else if (Tag.Equals(TEXT("maxScheduleMinuteStepsPerFrame"), ESearchCase::IgnoreCase))
			{
				FastSettings.MaxScheduleMinuteStepsPerFrame = ParseIntAttribute(
					Child, TEXT("value"), FastSettings.MaxScheduleMinuteStepsPerFrame);
			}
			else if (Tag.Equals(TEXT("movementSpeedMultiplier"), ESearchCase::IgnoreCase))
			{
				FastSettings.MovementSpeedMultiplier = ParseFloatAttribute(
					Child, TEXT("value"), FastSettings.MovementSpeedMultiplier);
			}
		}
	}
	Registry->SetFastSimulationSettings(FastSettings);

	FAccurateTimeSettings AccurateSettings = Registry->GetAccurateTimeSettings();
	const FXmlNode* AccurateNode = Root->FindChildNode(TEXT("accurateTime"));
	if (AccurateNode)
	{
		for (const FXmlNode* Child = AccurateNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("simulationSpeed"), ESearchCase::IgnoreCase))
			{
				AccurateSettings.SimulationSpeed = ParseFloatAttribute(
					Child, TEXT("value"), AccurateSettings.SimulationSpeed);
			}
			else if (Tag.Equals(TEXT("runDurationSimMinutes"), ESearchCase::IgnoreCase))
			{
				AccurateSettings.RunDurationSimMinutes = ParseFloatAttribute(
					Child, TEXT("value"), AccurateSettings.RunDurationSimMinutes);
			}
			else if (Tag.Equals(TEXT("randomSeed"), ESearchCase::IgnoreCase))
			{
				AccurateSettings.RandomSeed = ParseIntAttribute(Child, TEXT("value"), AccurateSettings.RandomSeed);
			}
			else if (Tag.Equals(TEXT("autoExportOnEnd"), ESearchCase::IgnoreCase))
			{
				AccurateSettings.bAutoExportOnEnd = ParseBoolAttribute(
					Child, TEXT("value"), AccurateSettings.bAutoExportOnEnd);
			}
			else if (Tag.Equals(TEXT("maxScheduleMinuteStepsPerFrame"), ESearchCase::IgnoreCase))
			{
				AccurateSettings.MaxScheduleMinuteStepsPerFrame = ParseIntAttribute(
					Child, TEXT("value"), AccurateSettings.MaxScheduleMinuteStepsPerFrame);
			}
		}
	}
	Registry->SetAccurateTimeSettings(AccurateSettings);

	FValidationAnalyticsSettings ValidationSettings = Registry->GetValidationAnalyticsSettings();
	const FXmlNode* ValidationNode = Root->FindChildNode(TEXT("validationStats"));
	if (ValidationNode)
	{
		for (const FXmlNode* Child = ValidationNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
		{
			const FString Tag = Child->GetTag();
			if (Tag.Equals(TEXT("showOnScreenPanel"), ESearchCase::IgnoreCase))
			{
				ValidationSettings.bShowOnScreenPanel = ParseBoolAttribute(
					Child, TEXT("value"), ValidationSettings.bShowOnScreenPanel);
			}
			else if (Tag.Equals(TEXT("sampleIntervalSeconds"), ESearchCase::IgnoreCase))
			{
				ValidationSettings.SampleIntervalSeconds = ParseFloatAttribute(
					Child, TEXT("value"), ValidationSettings.SampleIntervalSeconds);
			}
			else if (Tag.Equals(TEXT("waitingSpeedThreshold"), ESearchCase::IgnoreCase))
			{
				ValidationSettings.WaitingSpeedThreshold = ParseFloatAttribute(
					Child, TEXT("value"), ValidationSettings.WaitingSpeedThreshold);
			}
			else if (Tag.Equals(TEXT("congestionSpeedThreshold"), ESearchCase::IgnoreCase))
			{
				ValidationSettings.CongestionSpeedThreshold = ParseFloatAttribute(
					Child, TEXT("value"), ValidationSettings.CongestionSpeedThreshold);
			}
		}
	}
	Registry->SetValidationAnalyticsSettings(ValidationSettings);

	UE_LOG(LogTemp, Log, TEXT("[SimulationConfig] Loaded from %s"), *AbsoluteFilePath);
	return true;
}
