# Validation and Verification

Validation uses authored Unreal maps, XML schedules, and live analytics. The project has no Unreal Automation or Functional Test C++ classes, and the validation statistics are not exported to CSV.

The available pedestrian-flow maps are `V01` to `V06` in `Content/UseCases/Validation/`. Separate crosswalk, merge, and split maps do not exist. There is also no separate corridor map. Names matching `Validation` or `V01_`–`V09_` are recognised by the map-driven feature gates, although the project contains no V07, V08, or V09 maps.

## Common validation setup

Each map uses a schedule from `Schedules/Validation/`. Agents are normally identified as `Agent_0`, `Agent_1`, and so on, matching the IDs in the XML. Depending on the map, agents may be placed directly in the level or created by `ADirectionalSpawner`. The spawner can assign per-agent schedule overrides using `Validation_MoveLeft.xml` and `Validation_MoveRight.xml` as templates.

Time comes from the same `AScheduleManager` simulation clock used by the other use cases. `USimulationAnalytics` samples an `FValidationAnalyticsSnapshot` for the on-screen panel. The snapshot contains the scenario name, simulation time, spawned, active, completed, and failed agent counts, travel-time statistics, average speed, average path length, throughput, flow rate, queue length, density, waiting time, congestion time, FPS, moving and waiting counts, and current average velocity.

Two fields use negative sentinel values. `AgentsFailed` is `-1` because failed-agent tracking is not supported, and `AveragePathLength` is `-1` because path length is not tracked. The panel renders these values as `n/a`.

## V01 — Unidirectional

`V01_Unidirectional.umap` uses `Validation_Unidirectional.xml`. Agents idle briefly and then move to the actor tagged `Exit_Right`. The run should form a one-way stream, with the completion count and throughput increasing as agents reach the exit. Check that `Exit_Right` resolves correctly and that opposing collisions are uncommon.

## V02 — Bidirectional

`V02_Bidirectional.umap` uses `Validation_Bidirectional.xml`. According to the schedule comments, `Agent_0`–`Agent_9` move to `Exit_Right`, while `Agent_10`–`Agent_19` move to `Exit_Left`.

The two streams share the same space. Under comparable density, this should produce more interference, waiting, and congestion than V01. Confirm that both exit tags are used and watch the waiting and congestion values where paths conflict.

## V03 — Corner

`V03_Corner.umap` uses `Validation_Corner.xml`. Every agent moves to `CornerExit`. The NavMesh route bends around the corner, so agents may slow near the turn and travel times should include the detour. A successful run ends with the agents reaching the tagged exit.

## V04 — Bottleneck

`V04_Bottleneck.umap` uses `Validation_Bottleneck.xml`. All agents target `BottleExit`.

The narrow section is expected to restrict capacity. Queue length and density should rise near the constriction, and completion should be delayed relative to an open route. The most useful panel values are maximum queue length, peak density, travel time, and throughput.

## V05 — T-junction

`V05_TJunction.umap` uses `Validation_TJunction.xml`. Agent groups are assigned to the left, right, or straight exits, as noted in the schedule comments. This creates branching flow and contention around the junction. Check the destination tag for each group and observe current or peak density at the junction.

## V06 — Functional area

`V06_FunctionalArea.umap` uses `Validation_FunctionalArea.xml`. All agents move from the layout-specific functional zone to `FunctionalExit`. Completion against that tag confirms that agents can leave the area through the intended destination.

## Directional schedule templates

`Validation_MoveLeft.xml` and `Validation_MoveRight.xml` contain template agent blocks for leftward and rightward movement. They are intended for directional spawners and `AScheduleManager::AssignScheduleOverrideToAgent`, rather than as complete multi-agent scenario schedules.

## Running a validation map

Open one of the V01–V06 maps and confirm that its `AScheduleManager` points to the matching file in `Schedules/Validation/`, unless the path is already authored in the map. Build the NavMesh and check that destination actors carry exactly the tags referenced by the XML.

Start Play in Editor with `validationStats/showOnScreenPanel` on and `thesis.ValidationDebugPanel` set to `1`. The panel is live, in-memory only—note numbers down yourself if you need a run-to-run comparison; this path has no CSV export.

## Mall A/B infrastructure verification

The infrastructure comparison belongs to UC3 and does not use a `V0*` validation map. It compares `MallA.umap` and `MallB.umap` using `InfrastructurePlanningSchedule_MallA.xml` and `InfrastructurePlanningSchedule_MallB.xml`.

Each run records infrastructure data such as travel, queues, congestion, stations, paths, popularity, and throughput. With automatic export enabled, the run files follow the pattern `Infrastructure_<Layout>_*.csv`. The comparison produces `VnV_Comparison_MallA_vs_MallB_*.csv` and a text summary named `VnV_Summary_MallA_vs_MallB_*.txt` in `Saved/Analytics`.

The generated checklist covers:

- whether both layout CSV files can be loaded;
- whether travel time was recorded;
- whether queue and station metrics are present;
- whether path utilisation was recorded;
- whether throughput was recorded;
- whether variant waiting time is less than or equal to the baseline;
- whether variant peak congestion is less than or equal to the baseline.

Run both layouts with `autoExportOnEnd` and comparison export enabled. The `comparisonBaseline` and `comparisonVariant` names must match the layout labels in the CSV data. The final CSV and summary files provide the Mall A versus Mall B verification record.
