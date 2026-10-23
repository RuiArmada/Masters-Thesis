# API and Classes

The simulation code belongs to the `Thesis` module. The classes below form the main run-time path from XML schedules and feature configuration to agent movement, interactions, use-case control, and analytics.

## ABoidsAB

`ABoidsAB` is the pedestrian character and derives from `ACharacter`. It combines local flock steering with navigation, schedule execution, station interactions, evacuation states, and the agent-side AI components.

An agent can compute and follow waypoint paths, start a `MoveTo`, enter a station interaction, flee to an exit or safe zone, and enter evacuation casualty or celebration states. It registers with `AScheduleManager` and `AFlockController`. Feature-dependent work is checked through `USimulationFeatureRegistry`.

The movement path centres on `TargetL`, `StartMoveTo`, `ComputePath`, and `MoveAlongWaypoints`. Station movement uses `MoveToStation`, `TargetStation`, and the interaction pipeline fields. `CurrentBehavior` and the flee and exit helpers track higher-level behaviour. `DEBUG` is exposed for agent debugging.

Each character may host `UAgentNeedsComponent`, `UAgentPrioritySelectorComponent`, `UAgentFallbackHandlerComponent`, `UAgentLeadershipComponent`, `UAgentCrimeRoleComponent`, and `UPedestrianInteractionComponent`.

## AFlockController

`AFlockController` holds the flock membership and shared steering parameters. It does not tick agents on their behalf. Agents register through `AddBoidToTheFlock` and leave through `RemoveBoidFromTheFlock`.

Its getters expose the editor-configured speed limits, alignment, separation, cohesion and avoidance weights, force FOV values, sonar settings, and path/flock movement blend. These values are read by `ABoidsAB` during movement.

## AScheduleManager

`AScheduleManager` owns the simulation clock and executes the XML schedules. It loads agent rules and tasks, registers agents by ID, checks when work is due, evaluates schedule conditions, and sends the resulting commands to `ABoidsAB`.

The supported condition path includes `emergency` and location checks for `Home`, `Work`, and `Food`. Individual agents can receive schedule-file overrides.

`RelativeSchedulePath` is the default editor path. The main API includes `SetScheduleFilePath`, `ReloadSchedules`, `RegisterBoid`, `GetBoidById`, and `AssignScheduleOverrideToAgent`. Daily Urban Life and Infrastructure managers use these functions when they choose a schedule for the current map.

## AAgentStation and AAgentArea

`AAgentStation` is a capacity-limited interaction point with a FIFO queue. `RequestEnter` requests a place, `NotifyOccupantReached` completes arrival at the station, and `LeaveStation` releases the place. `HasAvailableSlot`, `GetQueueLength`, and `GetUtilization` expose its current state. `MatchesIdentifier` compares the station with the identifier requested by the schedule.

The station also supplies getters for its interaction pose and animation settings. Queue and occupancy changes are available through `OnAgentEnqueued` and the admit and release delegates.

`AAgentArea` groups stations and adds an open or closed state and a destination category. Schedule execution uses it to find an available station or a station with a particular identifier for a `UseStation` action.

## Layered agent components

| Class | Responsibility |
|---|---|
| `UAgentNeedsComponent` | Tracks hunger and energy rates and exposes critical-need flags |
| `UAgentPrioritySelectorComponent` | Evaluates priority layers and may issue movement or defer scheduled work |
| `UAgentFallbackHandlerComponent` | Handles stuck agents and path or queue failures |
| `UAgentLeadershipComponent` | Stores leader or follower roles and calculates the follow point |
| `UAgentCrimeRoleComponent` | Runs police, robber, and civilian states |
| `UPedestrianInteractionComponent` | Handles short social pauses and greetings |

## Emergency and world actors

| Class | Responsibility |
|---|---|
| `AEmergencyManager` | Controls the global evacuation lifecycle and timer |
| `AIncidentManager` | Creates incident instances and broadcasts incident changes |
| `AExitPoint` | Provides exit capacity, queue slots, and agent processing |
| `AEvacuationSafeZone` | Marks a safe destination and its arrival radius |
| `UCrowdRoutingService` | Scores evacuation exits; other routing modes use stubs or fallback behaviour |
| `UCongestionAnalyzer` | Samples congestion for route decisions and analytics |

## Use-case managers

`ADailyUrbanLifeManager` starts UC2. It selects the schedule, resolves tagged destinations, assigns walking speeds and schedule offsets, and starts analytics.

`AInfrastructurePlanningManager` runs UC3: layout settings, service-station capacity overrides, run timing, and infrastructure CSV export. `AVerificationValidationManager` reads Mall A and Mall B results and writes the comparison CSV plus text summary. `AInfrastructureVisualizationActor` is the optional UC3 overlay.

`ALeadershipDemoManager` assigns leadership groups when the feature is enabled. `ACopsAndRobbersDemoManager` assigns crime roles and exports crime analytics. `ADirectionalSpawner` creates agents and may give them schedule overrides.

## Simulation subsystems and configuration

| Type | Responsibility |
|---|---|
| `USimulationFeatureRegistry` | Stores feature flags and typed settings loaded from XML |
| `USimulationAnalytics` | Samples simulation state and writes supported CSV formats |
| `UValidationDebugPanelSubsystem` | Draws the on-screen validation statistics |
| `FSimulationConfigLoader` | Reads `SimulationConfig.xml` into the feature registry settings |

## Navigation helpers

`UNavArea_Sidewalk` marks sidewalk navigation areas. `UNavFilter_SidewalkOnly` is used for queries that should prefer sidewalk regions and penalise or exclude other areas.

## Analytics data

The selected snapshot structures in `SimulationTypes.h` are:

- `FEvacuationAnalyticsSnapshot`
- `FDailyUrbanLifeAnalyticsSnapshot`
- `FInfrastructureAnalyticsSnapshot`, including station and path entries
- `FCopsAndRobbersAnalyticsSnapshot`
- `FValidationAnalyticsSnapshot`

The same header contains settings structures for evacuation, daily life, infrastructure, leadership, cops and robbers, and validation.

## Legacy and peripheral code

`ARuleParser` is an older B.A.R.S-style parser with hard-coded paths. It is not the active XML schedule path. `APredatorBoid` and `APredatorSpawner` are marked as legacy and are not used by UC1–UC3. `MapProcessing` in Toolbelt is utility and legacy map-XML tooling.
