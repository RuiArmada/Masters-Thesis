# Debug Panel

## Validation Statistics

The Validation Statistics panel is managed by `UValidationDebugPanelSubsystem`. Its values come from the `FValidationAnalyticsSnapshot` maintained by `USimulationAnalytics`.

The panel appears when all three of the following conditions are met:

1. the `ValidationStats` feature is enabled;
2. `showOnScreenPanel` is `true` under `<validationStats>` in `SimulationConfig.xml`;
3. `thesis.ValidationDebugPanel` is set to `1`.

Validation and `V0*_` map names enable the feature through the map-driven gates. The console variable is enabled by default and can be changed while the simulation is running:

```
thesis.ValidationDebugPanel 0
thesis.ValidationDebugPanel 1
```

The same configuration section controls the sample interval and the speed thresholds. Their baseline values are 0.5 seconds per sample, 20 cm/s for waiting, and 80 cm/s for congestion.

## Panel fields

Negative values are sentinels for unsupported or untracked data and are displayed as `n/a`.

| Panel label | Snapshot field | Meaning |
|---|---|---|
| Scenario Name | `ScenarioName` | Normally the loaded map name |
| Simulation Time | `SimulationTimeSeconds` | Elapsed session time in seconds |
| Agents Spawned | `AgentsSpawned` | Agents seen or spawned during the session |
| Agents Active | `AgentsActive` | Agents currently active |
| Agents Completed | `AgentsCompleted` | Agents reported through destination or completion hooks |
| Agents Failed | `AgentsFailed` | Unsupported; `-1` is displayed as `n/a` |
| Average Travel Time | `AverageTravelTimeSeconds` | Mean duration of completed trips |
| Minimum Travel Time | `MinTravelTimeSeconds` | Fastest completed trip |
| Maximum Travel Time | `MaxTravelTimeSeconds` | Slowest completed trip |
| Average Speed | `AverageSpeed` | Mean sampled speed |
| Average Path Length | `AveragePathLength` | Not tracked; `-1` is displayed as `n/a` |
| Throughput (agents/sec) | `ThroughputAgentsPerSecond` | Completions per unit of elapsed time |
| Flow Rate | `FlowRate` | Flow value calculated during analytics sampling |
| Average Queue Length | `AverageQueueLength` | Mean queue value from congestion or station samples |
| Maximum Queue Length | `MaximumQueueLength` | Largest observed queue |
| Peak Density | `PeakDensity` | Highest density sample |
| Current Density | `CurrentDensity` | Most recent density sample |
| Average Waiting Time | `AverageWaitingTimeSeconds` | Mean waiting time |
| Maximum Waiting Time | `MaximumWaitingTimeSeconds` | Longest observed wait |
| Average Congestion Time | `AverageCongestionTimeSeconds` | Mean time spent at or below the congestion-speed threshold |
| FPS | `FPS` | Current frame-rate sample |
| Current Agent Count | `CurrentAgentCount` | Agents counted in the latest sample |
| Current Moving Agents | `CurrentMovingAgents` | Agents above the waiting-speed threshold |
| Current Waiting Agents | `CurrentWaitingAgents` | Agents at or below the waiting-speed threshold |
| Current Average Velocity | `CurrentAverageVelocity` | Mean velocity of the sampled agents |

## Reading a run

Open a V01–V06 validation map and start Play in Editor. The panel is drawn as a cyan on-screen debug block. For a fair comparison, keep spawn counts and other configuration values the same between runs and record the values manually; the validation panel has no CSV export path.

Large maximum queues together with high peak density usually indicate a saturated bottleneck or junction. A high average congestion time means many agents remained below the configured congestion-speed threshold. If throughput is low relative to the spawned count, inspect the level for blocked routes, unresolved destinations, or agents that have not completed. Long travel times with low density more often indicate a long route or detour than crowding.

`n/a` for failed agents and average path length is normal. Those two measurements are not collected.

Mall A/B infrastructure verification uses infrastructure CSV files and the V&V summary rather than this panel.

## Other debug controls

| Control | Effect |
|---|---|
| `thesis.AgentAIDebug 0\|1` | Enables periodic priority-layer and agent AI logging from `AgentAIDebug.h` |
| `thesis.VerboseSchedule 0\|1` | Enables detailed schedule, rule, and condition logging |
| `ABoidsAB::DEBUG` | Editor-exposed per-agent debug flag; defaults to `true` |

The `WBP_Evacuation` and `WBP_DailyLife` UMG widgets may display additional run-time information. Their fields are authored in Blueprint rather than enumerated by the C++ panel.
