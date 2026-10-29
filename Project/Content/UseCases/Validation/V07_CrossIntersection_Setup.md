# V07 Cross-Intersection — Editor Setup

Build this level in Unreal Editor. Binary `.umap` assets are not generated from C++.

## 1. Create the level

1. Save as `Content/UseCases/Validation/V07_CrossIntersection.umap` (name must contain `V07_` for feature gates).
2. Add a **Nav Mesh Bounds Volume** covering the walkable area and **Build** navigation.

## 2. Required actors

| Actor | Purpose |
|-------|---------|
| `AFlockController` | One flock for spawned agents |
| `AScheduleManager` | Set `RelativeSchedulePath` to `Schedules/Validation/Validation_V07_CrossIntersection.xml` **or** rely on spawner overrides only |
| `ADirectionalSpawner` × 2 | Spawn agents with diagonal flow |
| Empty actors (targets) | Tagged `Exit_SE` and `Exit_ESE` |

## 3. Spawner layout

**Spawner_NW** (north-west corner):

- Arrow points toward south-east (intersection center, then `Exit_SE`).
- `NumBoids` = 10, `SpawnType` = Burst (or Flow with staggered rate).
- `ScheduleOverride` = `Schedules/Validation/Validation_MoveNW_SE.xml`
- Agents receive IDs `Agent_0` … `Agent_9` from spawn order (match flock registration).

**Spawner_WNW** (west-north-west, slightly offset from NW spawner):

- Arrow points toward east-south-east.
- `NumBoids` = 10.
- `ScheduleOverride` = `Schedules/Validation/Validation_MoveWNW_ESE.xml`
- Agents `Agent_10` … `Agent_19`.

## 4. Exit targets

Place two empty actors at the far ends of each diagonal:

- Tag one `Exit_SE` (south-east).
- Tag one `Exit_ESE` (east-south-east, offset from the first exit).

Paths should cross near the map center.

## 5. Play settings

- Enable validation stats in `SimulationConfig.xml` (`validationStats/showOnScreenPanel`).
- Map name `V07_CrossIntersection` enables Daily Urban Life + ValidationStats via `ApplyMapDrivenFeatureGates`.
- Confirm log: `[SimulationFeatureRegistry] Map '...V07...' → DailyUrbanLife ON (validation schedule pipeline + ValidationStats)`.

## 6. Expected behaviour

- Group 1 moves NW → SE; group 2 moves WNW → ESE.
- Streams intersect at center; watch waiting/congestion on the validation panel.
- Agents complete when reaching the tagged exit actors.

## Alternative: global schedule only

If spawners do not use `ScheduleOverride`, set `ScheduleManager.RelativeSchedulePath` to `Validation_V07_CrossIntersection.xml` and place 20 `ABoidsAB` actors with matching IDs instead of spawners.
