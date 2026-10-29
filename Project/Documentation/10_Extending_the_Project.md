# Extending the Project

Most new scenarios are a map + XML schedule + tags + existing managers. Touch `ABoidsAB` or the schedule executor only when you need a command the current action set cannot express.

## Adding a map

Duplicate the closest existing use-case or validation map so that its world settings and basic actor setup are retained. A scheduled pedestrian map normally needs:

- one `AFlockController`;
- one `AScheduleManager`;
- placed `ABoidsAB` actors or a Blueprint subclass, unless a spawner creates them;
- `AAgentStation` and `AAgentArea` actors where interactions are required;
- the manager actor for the selected use case, such as `AEmergencyManager`, `ADailyUrbanLifeManager`, or `AInfrastructurePlanningManager`.

Add a NavMesh Bounds Volume, build navigation, and inspect the result in the editor. Sidewalk-only routes use `UNavArea_Sidewalk` and `UNavFilter_SidewalkOnly`, so the area assignment must be consistent with the route expected by the agents.

Destination actors need tags that exactly match the XML, for example `Home*`, `Work*`, `Exit*`, or `FoodCourt`. Set `AScheduleManager::RelativeSchedulePath` to the new schedule unless a use-case manager selects it at run time.

Map names participate in feature selection. Existing gates recognise Daily Urban Life names, `MallA` and `MallB`, and names containing `Validation` or the `V0*_` form. A map outside those conventions needs the corresponding feature enabled in `SimulationConfig.xml`, or a new gate added to `ApplyMapDrivenFeatureGates`.

Blueprint defaults and UMG widget setup are binary assets and must be edited in Unreal Editor.

## Writing a schedule

Place schedule files under `Schedules/`; validation schedules are kept under `Schedules/Validation/`. An `<agent id="Agent_N">` entry must use the same ID as the placed or spawned character. Rules, conditions, and `<then>` actions follow the structures used in the existing schedule files and in `04_XML_Guide.md`.

After creating the file, assign it through `AScheduleManager`, or set the relevant use-case manager's `scheduleFile` value. For dynamically spawned agents, a small single-agent template can be assigned with `AssignScheduleOverrideToAgent`.

Action names go through `ParseActionString`. A new string does nothing until you wire the enum/parser and the execution path in `AScheduleManager` / `ABoidsAB`.

When a schedule fails to run, check the agent ID first, followed by the file path, destination tags, station identifiers, and simulation time. `thesis.VerboseSchedule 1` provides rule and condition logs during Play in Editor.

## Adding stations

Place an `AAgentStation` or one of its Blueprint children at each interaction point. Set `StationIdentifier` to the value used by the XML `stationType`. `MaxConcurrentOccupants` controls capacity and `InteractionDuration` controls the default occupation time.

Assign the station to an `AAgentArea`. The area's tag must match the XML `area` name. The area can contain several stations, allowing schedule execution to select a matching available station.

For a posed interaction, choose `InteractionPositionMode` and set the location and rotation offsets. A montage or sequence can be assigned through the animation properties, with looping and blend values where required.

Infrastructure planning can resize service stations at run time. Tag those stations with `ServicePoint`, or with the value configured by `serviceStationTag`. A `serviceStationCapacity` value of `0` leaves the editor capacity unchanged.

Multi-stage interactions use several `<stage>` elements inside a `UseStation` action. Each stage must resolve to a valid area and station identifier.

## Building a validation scenario

Create the level under `Content/UseCases/Validation/` and use a name recognised by the existing gate, such as `V07_Example` or a name containing `Validation`. Place goal actors with unique destination tags, then add a schedule under `Schedules/Validation/` with the required idle and movement rules.

Build the NavMesh, start Play in Editor, and read the Validation Statistics panel. Keep population and configuration values fixed when comparing runs. Validation values are live and in memory, so they must be recorded manually; there is no validation CSV exporter.

Automated assertions would require additional Unreal Automation or Functional Test code, which the project does not currently contain.

## Extending agent behaviour

A new scheduled command requires an `EScheduledAction` entry, parsing in `ParseActionString`, and an execution branch in `AScheduleManager` or `ABoidsAB`. Keep the XML name stable once schedules depend on it.

A new schedule condition belongs in `EvaluateCondition`. If it introduces new state, make sure the manager or agent that owns that state is available when the condition is evaluated.

Priority behaviour is handled by `UAgentPrioritySelectorComponent`. Add a priority enum value and place its evaluation at the intended point in the existing order. Needs belong in `UAgentNeedsComponent`, together with the critical-state handling used by the priority selector.

Stuck, path, or queue recovery should remain in `UAgentFallbackHandlerComponent`. Leadership changes belong in `UAgentLeadershipComponent`, and police, robber, or civilian state changes belong in `UAgentCrimeRoleComponent`.

For a new analytics measurement, add the field to the appropriate snapshot structure and populate it in `USimulationAnalytics`. Add it to the relevant CSV writer only if that use case has an export path.

A new configurable feature needs an `ESimulationFeature` value, an XML attribute handled by `FSimulationConfigLoader`, storage in `USimulationFeatureRegistry`, and registry checks at the call sites. Centralising the check in the registry avoids separate local feature booleans drifting out of sync.

## Adding a use case

Start with the map and identify the actor that will own the run lifecycle. Existing examples are the Daily Urban Life, Infrastructure Planning, Leadership, and Cops and Robbers managers.

If the use case needs tunable values, add a settings structure in `SimulationTypes.h` and load a matching XML section in `FSimulationConfigLoader`. Add an `ESimulationScenario` value when analytics or shared systems need to distinguish the scenario. Automatic selection by map name is added through `ApplyMapDrivenFeatureGates`.

The map must contain the required manager, destinations, schedule manager, flock controller, agents or spawner, and navigation data. Supply a schedule and record every tag or station identifier on which it depends. If the use case records metrics, connect the manager's start and finish points to `USimulationAnalytics` and add an exporter only for the required output format.

## Extending the Mall comparison

Each layout must first produce an infrastructure CSV through `autoExportOnEnd`. The `comparisonBaseline` and `comparisonVariant` values must match the `LayoutVariant` values written into those files. Enable `autoExportComparisonOnEnd`, or use the placed `AVerificationValidationManager`, to generate the comparison CSV and summary.

A new layout name also needs a matching schedule. If it should be selected automatically from the map name, extend the current map-selection logic alongside the Mall A and Mall B cases.
