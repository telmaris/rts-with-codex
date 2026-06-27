# Strategic AI Design

This document defines the target architecture for the strategic AI controller. The model is data-oriented: game state is converted into pressures, pressures select a plan, and the plan generates concrete tasks. Personality changes thresholds and utility scores, but it never replaces situation assessment.

## Pipeline

Every AI tick runs four stages:

1. Situation assessment
   - Read economy telemetry, storage, physical production chains, road network state, territory, armies, known enemies, research and focus state.
   - Produce normalized axis pressures in the `0.0-1.0` range.
   - Attach reasons to pressures, so later stages know whether a shortage is caused by missing buildings, missing inputs, transport failure, storage limits or overconsumption.

2. Need calculation
   - Convert pressures into actionable needs.
   - Example: `WOOD` shortage becomes either `BuildWoodcutter`, `ConnectWoodcutterToStorage`, `BuildStorage`, `ChangeRecipe`, or `ReduceWoodConsumption`.
   - Needs carry resource/building/territory targets and a severity score.

3. Strategic plan selection
   - Score candidate plans against current pressures, active plan inertia and personality.
   - The selected plan biases action utility, but does not hard-lock behavior. Urgent logistics or survival actions may override an offensive plan.

4. Task generation
   - Convert the plan and top needs into concrete `GameCommand`s or queued AI tasks.
   - Tasks should remain small and verifiable: build one building, connect two buildings, start one research, recruit one unit batch, attack one target, expand toward one resource patch.

## Analysis Frequency

Axis analysis is cached and refreshed independently. This keeps strategic reasoning cheap while still matching the pace of the game.

- `Resources`: every 6 seconds. Resource flow and shortages change quickly enough to need frequent checks.
- `Logistics`: every 8 seconds. Stalls, road gaps and storage bottlenecks should be noticed quickly.
- `Military`: every 10 seconds. Army strength, supply and garrison pressure are medium-frequency decisions.
- `Risk`: every 12 seconds. Reserve risk and hostile border exposure should react faster than expansion planning.
- `InternalDevelopment`: every 15 seconds. Population, storage, villages and universities move slower.
- `Technology`: every 20 seconds. Research/focus decisions are strategic and should not churn.
- `Expansion`: every 30 seconds. Territory value and overextension are slower, map-level evaluations.
- `Diplomacy`: every 45 seconds. Until diplomacy is richer, rival/threat analysis can be slow.

The pipeline should always read the latest cached axis pressures when selecting plans. A plan may therefore be reconsidered every AI tick, but expensive axis scans only run when their interval expires.

## Strategy Axes

Each axis returns pressure `0.0-1.0` plus explanations.

- `Resources`: shortages, surpluses, recipe inputs, reserves, production/consumption balance.
- `Logistics`: stalled buildings, transport time, road capacity, storage pressure, disconnected producers/consumers, supply line length.
- `Military`: army size, garrison capacity, offensive readiness, defensive readiness, manpower, weapon supply, food supply.
- `Expansion`: terrain value, nearby resource access, defense cost, border length, overextension.
- `InternalDevelopment`: population, manpower growth, villages, roads, storage, universities, government progression.
- `Technology`: research opportunities selected by current problems and strategic plan.
- `Diplomacy`: threats, relations, rivalry, shared enemies. Initially this can be neutral until diplomacy exists.
- `Risk`: reserves, rebuild capacity, number of fronts, economic safety, exposed borders.

## Personality

Personality traits modify utility and thresholds only. They do not directly issue orders.

- `Aggression`: raises value of offensive plans and acceptable attack losses.
- `Planning`: raises required preparation, reserves and prerequisite completion.
- `RiskTolerance`: lowers reserve requirements and accepts uncertain attacks.
- `Expansionism`: raises utility of territory and resource expansion.
- `EconomicFocus`: raises value of reserves, stable production and recovery.
- `Militarism`: raises target army share and military tech/building preference.
- `DefensiveBias`: raises fortification, garrison and border defense preference.
- `LogisticsAwareness`: raises penalty for long transport, road bottlenecks and weak supply.
- `Adaptability`: controls how quickly AI can switch plans.
- `Opportunism`: raises value of attacking weak targets or claiming undefended resources.
- `Persistence`: increases active-plan inertia.
- `GovernmentPreference`: biases focus/government choices toward Tribal, Chiefdom, Kingdom or Aristocracy.

## Strategic Plans

Initial plan set:

- `RecoverEconomy`: restore basic resource flow and reserves.
- `FixLogistics`: connect stalled chains, add roads, add storage, shorten transport.
- `BuildArmy`: increase troop count, supply, equipment and military buildings.
- `DefendBorder`: fortify exposed borders and reinforce garrisons.
- `PrepareOffensive`: stockpile supply, gather force, improve roads to the front.
- `ExpandForResources`: claim territory that unlocks critical resources.
- `DevelopPopulation`: improve villages, food, manpower and population cap.
- `ResearchSpecialization`: select tech/focuses that solve current bottlenecks.
- `ConsolidateTerritory`: reduce overextension, connect holdings, secure supply lines.

## Utility Formula

Every concrete action candidate is scored with:

```text
utility =
baseValue
* need
* personalityModifier
* feasibility
* urgency
* planModifier
```

Where:

- `baseValue`: static usefulness of action type.
- `need`: pressure-derived demand for this action.
- `personalityModifier`: trait-based multiplier.
- `feasibility`: whether the AI can pay, build, reach or defend the action.
- `urgency`: how quickly the problem hurts the player.
- `planModifier`: active strategic plan bias.

Actions with impossible prerequisites should get low feasibility, not special-case branches.

## Shortage Diagnosis

A resource shortage must be diagnosed by cause:

1. Missing producer
   - No completed building can produce the resource.
   - Candidate actions: build producer, expand to required terrain, research unlock.

2. Missing input
   - Producers exist but lack recipe ingredients.
   - Candidate actions: produce input, connect supplier, switch recipe.

3. Logistics failure
   - Inputs/outputs exist but buildings are stalled or disconnected.
   - Candidate actions: build road, increase road capacity, build intermediate storage.

4. Storage bottleneck
   - Output buffers are full or storage is missing/too far.
   - Candidate actions: build storage, connect producer to storage, expand road capacity.

5. Overconsumption
   - Consumption rate exceeds sustainable production.
   - Candidate actions: increase production, reduce consumers, choose efficiency tech/focus.

This diagnosis is the main guard against “AI sees low resource, blindly builds another building”.

## Attack Evaluation

Offensive action score:

```text
attackScore =
forceRatio
* supplyReadiness
* targetValue
* reinforcementAccess
* aggression
* riskTolerance
- exposedBorderPenalty
- longSupplyLinePenalty
- multiFrontPenalty
```

Definitions:

- `forceRatio`: local effective strength vs target and nearby defenders.
- `supplyReadiness`: food/weapon supply and ability to keep it supplied.
- `targetValue`: military/economic/territorial value of the target.
- `reinforcementAccess`: distance and roads from core army/storage.
- `exposedBorderPenalty`: risk created elsewhere by moving troops.
- `longSupplyLinePenalty`: transport distance and weak road capacity to target.
- `multiFrontPenalty`: number of enemies/fronts that can punish the attack.

Aggression and risk tolerance can raise the final score, but cannot make a starving, disconnected army sensible.

## Implementation Notes

- `PlayerEconomyTelemetry` is the base data source for resource production and consumption rates.
- `AIStrategyAxis`, `AIPersonality`, `AIStrategicPlan`, `AIStrategySignal`, `AIStrategySnapshot` and `AIActionUtility` are the code-level vocabulary for this design.
- The next implementation step should add typed need objects, for example `AIResourceNeed`, `AILogisticsNeed`, `AIMilitaryNeed`, and then generate action candidates from those needs.
