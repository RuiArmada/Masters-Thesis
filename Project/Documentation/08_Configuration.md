# Configuration

## SimulationConfig.xml

At run time, the feature registry first looks for `Saved/Config/SimulationConfig.xml`. If that file is absent, it loads `Config/SimulationConfig.xml` and copies the baseline into `Saved/Config`.

Use the Saved copy for local experiments. Changes to the Config copy alter the project baseline. In that baseline, Evacuation and Infrastructure Analysis are enabled; the other main features are disabled. Daily Urban Life and Validation Statistics can still be enabled by the map-name gates. `USimulationFeatureRegistry::ReloadConfig()` reloads the XML from disk.

## Feature flags

The `<features>` section maps XML attributes to `ESimulationFeature` values.

| XML attribute | Feature | Baseline |
|---|---|---:|
| `enableEvacuation` | `Evacuation` | `true` |
| `enableLeadership` | `Leadership` | `false` |
| `enableCopsAndRobbers` | `CopsAndRobbers` | `false` |
| `enableInfrastructureAnalysis` | `InfrastructureAnalysis` | `true` |
| `enableDailyUrbanLife` | `DailyUrbanLife` | `false` |
| `enableValidationStats` | `ValidationStats` | `false` |

Daily Urban Life maps, Mall A/B maps, and Validation maps may override these values after configuration loading.

## Daily Urban Life

Settings are read from `<dailyUrbanLife>`.

| XML attribute | C++ setting | Baseline |
|---|---|---|
| `enableNeeds` | `bEnableNeedsDuringDailyLife` | `true` |
| `needsRateScale` | `NeedsRateScale` | `0.1` |
| `startAgentsAtHome` | `bStartAgentsAtHome` | `true` |
| `maxScheduleMinuteOffset` | `MaxScheduleMinuteOffset` | `8` |
| `walkingSpeedMin`, `walkingSpeedMax` | Walking-speed range, cm/s | `170`, `230` |
| `interactionPauseMin`, `interactionPauseMax` | Greeting-pause range, seconds | `2`, `5` |
| `scheduleFile` | `DefaultScheduleFile` | `Schedules/DailyUrbanLifeSchedule.xml` |
| `breakAreaTag` | `BreakAreaTag` | `Plaza` |
| `homeReturnOverridesNeeds` | `bScheduledHomeReturnOverridesNeeds` | `true` |
| `minStationsPerWorkArea` | `MinStationsPerWorkArea` | `4` |

## Evacuation

The `<evacuation>` section controls exit selection, timing, per-agent variation, and optional casualty and celebration states.

| XML attribute | Use | Baseline |
|---|---|---:|
| `globalRadius` | Exit-search radius | `999999` |
| `distanceWeight` | Distance term in exit scoring | `1.0` |
| `congestionWeight` | Congestion term in exit scoring | `2.0` |
| `reevaluationInterval` | Seconds between congestion checks | `5.0` |
| `switchThreshold` | Congestion threshold for switching exits | `0.35` |
| `maxReassignmentAttempts` | Maximum number of exit changes | `3` |
| `preferNearestExitInitially` | Applies an initial nearest-exit preference | `true` |
| `reactionDelayMin`, `reactionDelayMax` | Panic reaction delay, seconds | `0`, `1` |
| `evacuationDuration` | Countdown duration; `0` disables the automatic timer | `120` |
| `evacuationTimerStartDelay` | Delay before the countdown starts | `0` |
| `speedVariationMin`, `speedVariationMax` | Per-agent flee-speed multiplier range | `0.85`, `1.15` |
| `congestionToleranceVariation` | Per-agent congestion-threshold variation | `0.15` |
| `nearestExitBiasVariation` | Per-agent nearest-exit bias scale | `0.35` |
| `enableCasualties` | Enables casualty logic | `true` |
| `enableCelebrationState` | Enables the success celebration state | `true` |

The evacuation settings structure also contains `NearestExitBias`, with a C++ default of `0.5`. It is separate from the XML attributes listed above.

## Infrastructure planning

Settings under `<infrastructurePlanning>` control Mall A/B selection, run timing, service capacity, visualisation, and export.

| XML attribute | Use | Baseline |
|---|---|---|
| `autoSelectFromMap` | Selects the Mall A or Mall B schedule and capacity from the map name | `true` |
| `layoutVariant` | Label written for the active layout | `MallA` |
| `comparisonBaseline` | Baseline layout name used by V&V | `MallA` |
| `scheduleFile` | Infrastructure schedule path | Mall A schedule |
| `samplingIntervalSeconds` | Analytics sampling interval | `5` |
| `runDurationSimMinutes` | Run duration in simulated minutes | `10` |
| `enableVisualization` | Enables `AInfrastructureVisualizationActor` use | `true` |
| `reuseDailyUrbanLifeBootstrap` | Reuses tag-based destination setup | `true` |
| `serviceStationCapacity` | Capacity applied to tagged service stations; `0` keeps editor values | `1` |
| `serviceStationTag` | Tag used to select stations for the capacity override | `ServicePoint` |
| `autoExportOnEnd` | Writes the infrastructure CSV when the run ends | `true` |
| `autoExportComparisonOnEnd` | Builds the paired-layout comparison | `true` |
| `comparisonVariant` | Variant layout name | `MallB` |

## Leadership

The `<leadership>` section contains:

| XML attribute | Baseline |
|---|---:|
| `defaultFollowDistance` | `200` |
| `formationUpdateInterval` | `0.5` |
| `leadershipOverridesSchedule` | `true` |

## Cops and Robbers

The `<copsAndRobbers>` section contains:

| XML attribute | Baseline |
|---|---:|
| `policeDetectionRadius` | `1200` |
| `robberDetectionRadius` | `900` |
| `crimeProbability` | `0.08` |
| `crimeCheckInterval` | `5` |
| `crimeDuration` | `8` |
| `captureRadius` | `150` |
| `pursuitUpdateInterval` | `0.4` |
| `fleeDistance` | `800` |
| `civiliansAvoidRobbers` | `true` |
| `civilianAvoidRadius` | `700` |

## Validation Statistics

The `<validationStats>` settings control the on-screen panel and its classification thresholds.

| XML attribute | Baseline |
|---|---:|
| `showOnScreenPanel` | `true` |
| `sampleIntervalSeconds` | `0.5` |
| `waitingSpeedThreshold` | `20` |
| `congestionSpeedThreshold` | `80` |

The speed thresholds are in cm/s.

## AFlockController properties

An `AFlockController` placed in a level supplies shared movement values to the agents that register with it. Level-specific editor values take precedence over constructor defaults.

`MaxSpeed` and `MinSpeed` define the basic speed range. `AlignmentWeight`, `SeparationWeight`, `CohesionWeight`, and `AvoidanceWeight` set the relative strengths of flock alignment, personal spacing, group cohesion, and obstacle or enemy avoidance.

The following properties affect the blend between path following and flock steering during `MoveTo`:

| Property | Role |
|---|---|
| `bEnableSteeringWhileMoving` | Allows flock steering while an agent follows a path |
| `NavProgressWeight` | Legacy weight retained for editor compatibility; path progress uses a lateral-only blend basis |
| `MovingSteeringWeight` | Strength of lateral flock steering during movement |
| `AntiProgressDamp` | Legacy editor-compatible property |
| `MaxSteerDeviationDegrees` | Maximum heading deviation from the path |
| `NavMeshSnapDistance` | Distance in centimetres used to snap an off-mesh agent |
| `NavMeshSnapCooldownSeconds` | Minimum time between NavMesh snap attempts |
| `PathLookAheadDistance` | Distance used to look ahead on the current path segment |
| `WallProbeDistance` | Length of the wall probe |
| `WallClearanceDistance` | Preferred side clearance from a wall |
| `WallClearanceStrength` | Strength of the side-clearance correction |

Perception and general steering use `SeparationFOV`, `AlignmentFOV`, and `CohesionFOV` for the three force cones. `NumSonar` and `SonarRange` control obstacle probes. `GoToWeight` controls seeking, and `Acceleration` provides movement acceleration tuning.

Constructor speeds are in the 100–150 range and the default separation and avoidance weights are comparatively high. Placed controller actors may have different values and are authoritative for their map.

## Schedule manager

`AScheduleManager::RelativeSchedulePath` is the editor-facing default XML path. A use-case manager can replace it at run time. The main run-time functions are `SetScheduleFilePath`, `ReloadSchedules`, and `AssignScheduleOverrideToAgent`.

## Stations and areas

`AAgentStation` exposes the following editor properties:

| Property | Role |
|---|---|
| `InteractionDuration` | Default time that an agent occupies the station |
| `MaxConcurrentOccupants` | Number of agents admitted at once |
| `StationIdentifier` | Identifier matched against XML `stationType` |
| `InteractionPositionMode` | Selects snap, interpolation, or no position adjustment |
| `InteractionLocationOffset` | Positional offset for the interaction pose |
| `InteractionRotationOffset` | Rotational offset for the interaction pose |
| `InteractionInterpolateSeconds` | Time used to interpolate into the pose |
| `bAlignInteractionRotation` | Aligns the agent with the interaction transform |
| `InteractionAnimType` | Chooses no animation, a montage, or a sequence |
| `InteractionMontage` | Montage used for the interaction |
| `InteractionSequence` | Sequence used for the interaction |
| `bLoopInteractionAnimation` | Loops the selected interaction animation |
| `InteractionAnimSlotName` | Animation slot name |
| `InteractionAnimBlendIn`, `InteractionAnimBlendOut` | Blend times for the animation |

`AAgentArea` groups stations. `bAreaClosed` prevents the area from being used, `DestinationCategory` classifies it as Residential, Business, Public, or None, and `Stations` holds the assigned station actors.

## Navigation

Navigation is configured in the level rather than in `SimulationConfig.xml`. Each navigable map needs a Recast NavMesh. Sidewalk regions can use `UNavArea_Sidewalk`, and path queries that should remain on those regions use `UNavFilter_SidewalkOnly`.

## Analytics files

| Domain | Output |
|---|---|
| Evacuation | `Saved/Analytics/Evacuation_<UnixTimestamp>.csv` |
| Infrastructure | `Saved/Analytics/Infrastructure_<Layout>_<Timestamp>.csv` |
| Mall V&V | `VnV_Comparison_MallA_vs_MallB_<Timestamp>.csv` and `VnV_Summary_MallA_vs_MallB_<Timestamp>.txt` |
| Cops and Robbers | `Saved/Analytics/CopsAndRobbers_<Timestamp>.csv` |
| Daily Urban Life | An export API is available, but the manager does not export automatically |
| Validation | No CSV exporter |
