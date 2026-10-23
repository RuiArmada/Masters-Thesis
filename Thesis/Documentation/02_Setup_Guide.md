# Setup Guide

## Engine

Open the project with **Unreal Engine 5.4** (`Thesis.uproject`). The runtime module is `Thesis`. These notes assume Win64 Development Editor.

## Plugins and modules

The only plugin listed in the `.uproject` is `ModelingToolsEditorMode` (editor targets). Module dependencies in `Thesis.Build.cs` include Core, Engine, XmlParser, NavigationSystem, AIModule, EnhancedInput, and related engine modules. UMG is pulled in for the use-case widgets.

## Maps and config worth knowing

The repository does not ship engine `.ini` files, so the editor uses its own default startup and game maps. Open the use-case map you care about from the Content Browser.

Simulation features are in `Config/SimulationConfig.xml`. At runtime the game prefers `Saved/Config/SimulationConfig.xml` and will copy the project file there the first time Saved is missing. Edit Saved for local experiments.

## Building

Generate project files from the `.uproject` if needed, then build **ThesisEditor** (Development Editor, Win64).

Command-line example:

```bat
"C:\Program Files\Epic Games\UE_5.4\Engine\Build\BatchFiles\Build.bat" ThesisEditor Win64 Development -Project="…\Thesis\Thesis.uproject" -WaitMutex
```

If the editor holds Live Coding / UBT, an external build can sit on the mutex until PIE or Live Coding releases it. Compiling inside the editor is often easier while iterating.

## Running

1. Open a use-case or validation map.
2. Confirm the level has the actors that scenario needs (ScheduleManager, FlockController, agents, stations, managers). Placement is in the `.umap`.
3. Play (PIE) or standalone.

Useful maps:

| Scenario | Asset |
|----------|--------|
| Evacuation | `/Game/UseCases/Evacuation` |
| Daily urban life | `/Game/UseCases/DailyUrbanLife` |
| Mall A / B | `/Game/UseCases/UrbanPlanning/MallA` or `MallB` |
| Validation | `/Game/UseCases/Validation/V0*_…` |

Map-name gates flip some features automatically (Daily Urban Life, malls, validation). Schedules are XML under `Schedules/` (runtime often uses the copy under `Saved/Schedules/`). Rebuild NavMesh after layout changes; sidewalk preference needs sidewalk areas consistent with `UNavArea_Sidewalk`.

## Typical problems

| What you see | Usual cause | What to check |
|--------------|-------------|----------------|
| Schedules ignored | Wrong path, IDs mismatch, agent not registered | ScheduleManager path vs `Agent_N` IDs |
| No path / empty waypoints | Off-mesh start/goal or filter | NavMesh, sidewalk connectivity |
| Station never starts | Arrival XY/Z not met | Path actually reaches station root |
| Wrong features | Config vs map gate | `Saved/Config/SimulationConfig.xml` |
| Build hangs | Mutex / Live Coding | Close PIE or compile in-editor |
| RuleParser path spam | Legacy absolute paths | Use ScheduleManager schedules |
| Empty Analytics folder | Export never ran | Finish a UC1/UC3 session with auto-export |
| No validation panel | Feature / CVar off | Validation map + `thesis.ValidationDebugPanel 1` |
