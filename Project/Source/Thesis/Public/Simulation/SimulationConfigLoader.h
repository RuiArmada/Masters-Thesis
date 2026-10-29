#pragma once

#include "CoreMinimal.h"
#include "Simulation/SimulationTypes.h"

class USimulationFeatureRegistry;

/** Loads Saved/Config/SimulationConfig.xml into USimulationFeatureRegistry. */
class THESIS_API FSimulationConfigLoader
{
public:
	static bool LoadIntoRegistry(USimulationFeatureRegistry* Registry, const FString& AbsoluteFilePath);

	/** Which Phase 2/3 sections are present in an XML file (for Project→Saved merge). */
	struct FClockModeSectionPresence
	{
		bool bHasSimulationClock = false;
		bool bHasSimulationMode = false;
		bool bHasFastSimulation = false;
	};

	static bool InspectClockModeSections(const FString& AbsoluteFilePath, FClockModeSectionPresence& OutPresence);
};
