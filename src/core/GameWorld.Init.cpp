#include "core/GameWorldInternal.h"
#include "core/Log.h"

#include <limits>
#include <set>

using namespace GameWorldInternal;

namespace
{
    constexpr int MultiplayerHumanSlots = 2;

    // B5 (docs/work_plan_2026-07-13.md): deterministic reseed for a
    // regeneration retry — same inputs always produce the same sequence of
    // attempts, so a retried world is still fully reproducible from the
    // original seed.
    unsigned int PerturbSeedForRetry(unsigned int seed, int attempt)
    {
        return seed ^ (0xB5297A4Du * (static_cast<unsigned int>(attempt) + 1));
    }

    // Validates that the military road ring connects every player exactly
    // once in a single cycle — mirrors the check
    // tests/MilitaryRoadNetworkTests.cpp already exercises for
    // MilitaryRoadNetwork::Generate's output. Used to decide whether a
    // generation attempt needs to be retried (B5).
    bool ValidateMilitaryRing(const MilitaryRoadNetwork& roads, int playerCount)
    {
        if (playerCount < 2)
            return true;
        // A 2-player "ring" is a single mutual edge, not a closed cycle — one
        // route, not `playerCount` (bug found 2026-07-14: this off-by-one
        // made validation fail on EVERY attempt for every 2-player world,
        // silently wasting all 8 retries and falling through to "proceeding
        // with the last attempt" every single time. The actual generated
        // rings were fine throughout — MilitaryRoadNetworkTests.
        // TwoPlayersGetExactlyOneMutualRoute already asserts exactly 1 route
        // for 2 players — so no test caught it; only the retry log spam did.
        size_t expectedRoutes = playerCount == 2 ? 1u : static_cast<size_t>(playerCount);
        if (roads.GetRoutes().size() != expectedRoutes)
            return false;

        std::set<int> visited;
        int current = 0;
        int previous = -1;
        for (int step = 0; step < playerCount; step++)
        {
            visited.insert(current);
            std::vector<int> neighbors = roads.GetNeighbors(current);
            if (neighbors.size() != (playerCount == 2 ? 1u : 2u))
                return false;

            int next = -1;
            for (int n : neighbors)
                if (n != previous) { next = n; break; }
            if (next == -1)
                next = neighbors.front();
            previous = current;
            current = next;
        }
        return visited.size() == static_cast<size_t>(playerCount);
    }

    Color PlayerSlotColor(int id)
    {
        static const std::array<Color, 7> colors{
            Color{66, 154, 255, 255},
            Color{220, 72, 72, 255},
            Color{230, 151, 62, 255},
            Color{176, 86, 216, 255},
            Color{73, 181, 126, 255},
            Color{217, 210, 82, 255},
            Color{88, 196, 210, 255}
        };
        return colors[static_cast<size_t>(std::clamp(id, 0, static_cast<int>(colors.size()) - 1))];
    }

    Vec2i DirectionFromHqToTile(Vec2i hqAnchor, Vec2i hqFootprint, Vec2i tile)
    {
        if (tile.x < hqAnchor.x)
            return {-1, 0};
        if (tile.x >= hqAnchor.x + hqFootprint.x)
            return {1, 0};
        if (tile.y < hqAnchor.y)
            return {0, -1};
        if (tile.y >= hqAnchor.y + hqFootprint.y)
            return {0, 1};
        return {};
    }

    std::vector<Vec2i> GetMilitaryExitDirections(const MilitaryRoadNetwork& roads,
                                                 const TileMap& tilemap,
                                                 int playerId,
                                                 Vec2i hqAnchor,
                                                 Vec2i hqFootprint)
    {
        std::vector<Vec2i> directions;
        for (const MilitaryRoute& route : roads.GetRoutes())
        {
            int gateTile = -1;
            if (route.playerA == playerId && !route.tiles.empty())
                gateTile = route.tiles.front();
            else if (route.playerB == playerId && !route.tiles.empty())
                gateTile = route.tiles.back();

            if (gateTile < 0)
                continue;

            Vec2i direction = DirectionFromHqToTile(
                hqAnchor, hqFootprint, tilemap.GetCoordsFromId(gateTile));
            if (direction.x != 0 || direction.y != 0)
                directions.push_back(direction);
        }
        return directions;
    }

    int CountMatchingDirections(Vec2i candidateDirection,
                                const std::vector<Vec2i>& exitDirections,
                                bool opposite)
    {
        int matches = 0;
        for (Vec2i exitDirection : exitDirections)
        {
            if (opposite)
                exitDirection = {-exitDirection.x, -exitDirection.y};
            if (candidateDirection == exitDirection)
                matches++;
        }
        return matches;
    }

    // Adds a debug resource package to the player's headquarters.
    void GrantDebugResourcesToHeadquarters(Player* player, int amount)
    {
        if (player == nullptr || amount <= 0)
            return;

        for (auto* building : player->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->buildingType != BuildingType::Headquarters)
                continue;

            for (ResourceType type : resourceTypes)
            {
                auto& buffer = storage->buffers[type];
                if (buffer.type == ResourceType::Null)
                    buffer = ResourceBuffer{type, amount};
                buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
                for (int i = 0; i < amount; i++)
                    buffer.GenerateResource(type);
            }
            Log::Msg("[Debug]", "starting HQ received ", amount, " of every resource");
            return;
        }
    }

    void GrantDebugManpower(Player* player)
    {
        if (player == nullptr)
            return;
        int cap = player->GetPopulationCap();
        int gift = static_cast<int>(cap * 0.7);
        if (gift > 0)
        {
            player->AddManpower(static_cast<double>(gift));
            Log::Msg("[Debug]", player->name, " received ", gift, " manpower (70% of cap ", cap, ")");
        }
    }

    // AI rework etap 4 (TODO #2): difficulty is a head start, not different
    // logic — one deterministic model for every level, higher levels start
    // with more resources and manpower (and lower levels additionally take
    // noisier decisions, see UtilityAIModel). Indexed by
    // MapParameters::aiDifficulty (0 Primitive .. 3 Hard). Deliberately
    // grants ECONOMIC basics only — no swords/ammo, the AI raises its
    // military chains like everyone else. Init-only and purely
    // params-driven, so it is identical on host and client mirrors
    // (lockstep-safe) and needs no save-format change. Extra starting
    // BUILDINGS (TODO mentions them) are deferred: placement helpers are
    // entangled with patch generation, and the resource+manpower head start
    // buys the same economic lead without new placement code.
    void GrantDifficultyStartingBonus(Player* aiPlayer, int aiDifficulty)
    {
        if (aiPlayer == nullptr)
            return;

        // Starting grant is a LIGHT head start, not a crutch (user 2026-07-20:
        // "kilka sztuk a nie 200 ironu"). Two tiers, because granting a lot of
        // the WEAPON-chain goods is exactly what masked the iron chain the AI is
        // supposed to build: 200 IRON at HQ meant DiagnoseResourceNeed(IRON)
        // never went urgent, so it never descended to build a Foundry/IRON_ORE+
        // COAL mine (see AIActions::TryBuildProducerFor).
        //   - build materials (WOOD, STONE, PLANKS, FOOD_PROVISIONS): a modest
        //     buffer — on TOP of the ~120/120/60 every HQ already starts with —
        //     so the opening buildout and first road stubs don't stall before
        //     the base's own producers ramp. These are raw construction inputs;
        //     granting them masks nothing the AI ought to be building itself.
        //   - IRON: enough on Hard for Smith plus the first siege unit, still
        //     far below a self-sustaining weapon economy;
        //   - TOOLS: up to exactly one Barracks cost on Hard. This unlocks an
        //     early militia foothold but grants no weapons, so stronger unit
        //     profiles still require the real smelting/forging chain.
        static constexpr std::array<int, 4> materialGrant{0, 10, 20, 30};
        static constexpr std::array<int, 4> militaryStoneReserve{0, 0, 10, 20};
        static constexpr std::array<int, 4> militaryPlankReserve{0, 0, 5, 10};
        static constexpr std::array<int, 4> ironGrant{0, 3, 10, 30};
        // Hard starts with exactly one Barracks' worth of tools. This creates
        // an early military foothold without gifting weapons or replacing the
        // Smith chain needed for stronger units.
        static constexpr std::array<int, 4> toolsGrant{0, 3, 6, 10};
        static constexpr std::array<double, 4> manpowerCapFraction{0.0, 0.10, 0.25, 0.50};
        int level = std::clamp(aiDifficulty, 0, 3);

        auto grantResource = [](StorageComponent* storage, ResourceType type, int amount)
        {
            if (amount <= 0)
                return;
            auto& buffer = storage->buffers[type];
            if (buffer.type == ResourceType::Null)
                buffer = ResourceBuffer{type, amount};
            buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
            for (int i = 0; i < amount; i++)
                buffer.GenerateResource(type);
        };

        if (materialGrant[level] > 0 || ironGrant[level] > 0 || toolsGrant[level] > 0)
        {
            for (auto* building : aiPlayer->GetTrackedBuildingsWithComponent<StorageComponent>())
            {
                auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
                if (storage == nullptr || building->buildingType != BuildingType::Headquarters)
                    continue;

                for (ResourceType type : {ResourceType::WOOD, ResourceType::STONE,
                                          ResourceType::PLANKS, ResourceType::FOOD_PROVISIONS})
                    grantResource(storage, type, materialGrant[level]);
                grantResource(storage, ResourceType::STONE, militaryStoneReserve[level]);
                grantResource(storage, ResourceType::PLANKS, militaryPlankReserve[level]);
                grantResource(storage, ResourceType::IRON, ironGrant[level]);
                grantResource(storage, ResourceType::TOOLS, toolsGrant[level]);
                break;  // a player owns at most one HQ
            }
        }

        double manpowerGift = aiPlayer->GetPopulationCap() * manpowerCapFraction[level];
        if (manpowerGift > 0.0)
            aiPlayer->AddManpower(manpowerGift);
    }

}

// B5 (docs/work_plan_2026-07-13.md): see the declaration comment in
// GameWorld.h for the retry rationale (safe because nothing player-visible
// exists yet at this point in InitWorld/InitMultiplayerWorld).
Vec2i GameWorld::GenerateWorldLayout(MapParameters& params, int playerCount, std::vector<Vec2i>& outAnchors)
{
    constexpr int kMaxAttempts = 8;
    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    unsigned int baseSeed = params.seed;

    for (int attempt = 0; attempt < kMaxAttempts; attempt++)
    {
        params.seed = attempt == 0 ? baseSeed : PerturbSeedForRetry(baseSeed, attempt);

        tilemap.generator.GenerateTileMap(tilemap, params);
        outAnchors = MapGenerator::PickHeadquartersAnchors(params, playerCount);

        std::map<int, Vec2i> hqAnchorsByPlayer;
        for (int playerId = 0; playerId < playerCount; playerId++)
            hqAnchorsByPlayer[playerId] = outAnchors[playerId];

        militaryRoads = MilitaryRoadNetwork{};
        militaryRoads.Generate(tilemap, hqAnchorsByPlayer, hqFootprint, MapGenerator::HeadquartersTerritorySize(), params.seed);

        if (ValidateMilitaryRing(militaryRoads, playerCount))
        {
            if (attempt > 0)
                Log::Msg("[MapGenerator]", "world generation succeeded on retry attempt ", attempt,
                          " (seed ", params.seed, ")");
            return hqFootprint;
        }

        Log::Msg("[MapGenerator]", "world generation validation failed on attempt ", attempt,
                  " (seed ", params.seed, "), retrying");
    }

    Log::Msg("[MapGenerator]", "world generation validation still failing after ", kMaxAttempts,
              " attempts — proceeding with the last attempt (seed ", params.seed, ")");
    return hqFootprint;
}

// Creates and registers the requested runtime object.
Player* GameWorld::CreatePlayer(int id, PlayerControllerType controllerType, const std::string& name, Color color)
{
    auto player = std::make_unique<Player>(id, tilemap);
    player->name = name;
    player->controllerType = controllerType;
    player->color = color;

    Player* ptr = player.get();
    playerHandler.players[id] = std::move(player);
    AttachControllerForPlayer(ptr);
    return ptr;
}

// Creates and registers the requested runtime object.
Vec2i GameWorld::CreateStartingHq(Player* player, Vec2i hqAnchor, unsigned int seed)
{
    if (player == nullptr)
        return hqAnchor;

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);
    MapGenerator::PrepareStartingArea(tilemap, hqAnchor, resourceRng);
    SetFootprintTerrain(tilemap, hqAnchor, hqFootprint, TileType::GRASS, resourceRng, 3);

    player->Build<Headquarters>(hqAnchor, false);
    return hqAnchor;
}

// Creates and registers the requested runtime object.
void GameWorld::CreateStartingVillageAndResources(Player* player, Vec2i hqAnchor, unsigned int seed)
{
    if (player == nullptr)
        return;

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);

    Village villagePreview{0};
    Vec2i villageFootprint = villagePreview.GetFootprint();
    std::mt19937 startRng(seed);
    std::uniform_int_distribution<int> sideDist(0, 3);
    std::uniform_int_distribution<int> offsetDist(-2, 2);
    // Widened from 3 (2026-07-13), then from 6 (2026-07-17, user request):
    // the starting village now sits 14 tiles out — past the 10-tile HQ build
    // clearance (TileMap::CanBuildFootprint), leaving the HQ apron free for
    // logistics. With the military road generated BEFORE the village (see
    // CreateStartingHq/InitWorld ordering comment), overlap is structurally
    // impossible regardless of distance.
    int gap = 14;

    // Playtest report (2026-07-20): a straight-line-legal candidate can still
    // end up much farther than `gap` by actual ROAD path once BuildStartRoad
    // detours around the military track. Sample a wide pool of candidates (up
    // from 12) and measure each one's real road length up front — via
    // FindRoadPathBetweenFootprints, read-only, nothing committed yet — so we
    // never build a Village the road ends up dragging out past kMaxVillageRoadTiles.
    // The budget counts only actual Road tiles (perimeter-to-perimeter, not
    // the HQ/Village footprints themselves) — matches CountOwnedBuildings(Road).
    constexpr int kMaxVillageRoadTiles = 15;
    constexpr int kCandidateAttempts = 30;

    struct VillageCandidate
    {
        Vec2i anchor{};
        Vec2i direction{};
    };

    std::vector<VillageCandidate> villageCandidates;
    auto addCandidate = [&](int side, int offset)
    {
        switch (side)
        {
            case 0:
                villageCandidates.push_back({
                    {hqAnchor.x - gap - villageFootprint.x, hqAnchor.y + offset}, {-1, 0}});
                break;
            case 1:
                villageCandidates.push_back({
                    {hqAnchor.x + hqFootprint.x + gap, hqAnchor.y + offset}, {1, 0}});
                break;
            case 2:
                villageCandidates.push_back({
                    {hqAnchor.x + offset, hqAnchor.y - gap - villageFootprint.y}, {0, -1}});
                break;
            default:
                villageCandidates.push_back({
                    {hqAnchor.x + offset, hqAnchor.y + hqFootprint.y + gap}, {0, 1}});
                break;
        }
    };

    // Always include one centered candidate on every side. Random sampling
    // below still varies the final layout, while these four guarantee that
    // the side opposite the military exits is actually considered.
    for (int side = 0; side < 4; side++)
        addCandidate(side, 0);

    for (int attempt = 0; attempt < kCandidateAttempts; attempt++)
    {
        int side = sideDist(startRng);
        int offset = offsetDist(startRng);
        addCandidate(side, offset);
    }

    // Two fallback layers, matching the original guarantee that a legally
    // buildable candidate is always used if one exists: prefer the shortest
    // ROUTABLE candidate (ideally within budget), but if none of the
    // buildable candidates can find a road at all (the road-net BFS's own
    // "shouldn't happen short of fully boxed in" case), fall back to the
    // first buildable candidate rather than placing nothing.
    bool haveBuildable = false;
    Vec2i bestBuildableAnchor{};
    int bestBuildableExitMatches = std::numeric_limits<int>::max();
    int bestBuildableOppositeMatches = -1;
    bool bestBuildableDetached = false;
    bool haveRoutable = false;
    Vec2i bestRoutableAnchor{};
    int bestRoadClearance = 0;
    std::size_t bestPathLength = std::numeric_limits<std::size_t>::max();
    bool bestWithinBudget = false;
    bool bestDetached = false;
    int bestExitMatches = std::numeric_limits<int>::max();
    int bestOppositeMatches = -1;
    const std::vector<Vec2i> exitDirections = GetMilitaryExitDirections(
        militaryRoads, tilemap, player->id, hqAnchor, hqFootprint);

    for (VillageCandidate candidate : villageCandidates)
    {
        candidate.anchor = ClampAnchor(candidate.anchor, villageFootprint, tilemap.params);
        if (!tilemap.CanBuildFootprint(candidate.anchor, villageFootprint, player))
            continue;

        const int exitMatches = CountMatchingDirections(candidate.direction, exitDirections, false);
        const int oppositeMatches = CountMatchingDirections(candidate.direction, exitDirections, true);
        const bool footprintDetached = HasMilitaryRoadClearance(
            tilemap, candidate.anchor, villageFootprint, 1);
        const bool betterBuildable =
            !haveBuildable ||
            (footprintDetached != bestBuildableDetached && footprintDetached) ||
            (footprintDetached == bestBuildableDetached &&
             exitMatches != bestBuildableExitMatches && exitMatches < bestBuildableExitMatches) ||
            (footprintDetached == bestBuildableDetached &&
             exitMatches == bestBuildableExitMatches &&
             oppositeMatches > bestBuildableOppositeMatches);
        if (betterBuildable)
        {
            haveBuildable = true;
            bestBuildableAnchor = candidate.anchor;
            bestBuildableDetached = footprintDetached;
            bestBuildableExitMatches = exitMatches;
            bestBuildableOppositeMatches = oppositeMatches;
        }

        std::vector<int> path;
        bool detached = false;
        if (footprintDetached)
        {
            path = FindRoadPathBetweenFootprints(
                tilemap, player, candidate.anchor, villageFootprint,
                hqAnchor, hqFootprint, 1);
            detached = !path.empty();
        }
        if (path.empty())
        {
            path = FindRoadPathBetweenFootprints(
                tilemap, player, candidate.anchor, villageFootprint,
                hqAnchor, hqFootprint);
        }
        if (path.empty())
            continue;

        const bool withinBudget =
            path.size() <= static_cast<std::size_t>(kMaxVillageRoadTiles);
        const bool betterRoutable =
            !haveRoutable ||
            (detached != bestDetached && detached) ||
            (detached == bestDetached && withinBudget != bestWithinBudget && withinBudget) ||
            (detached == bestDetached && withinBudget == bestWithinBudget &&
             exitMatches != bestExitMatches && exitMatches < bestExitMatches) ||
            (detached == bestDetached && withinBudget == bestWithinBudget &&
             exitMatches == bestExitMatches && oppositeMatches != bestOppositeMatches &&
             oppositeMatches > bestOppositeMatches) ||
            (detached == bestDetached && withinBudget == bestWithinBudget &&
             exitMatches == bestExitMatches && oppositeMatches == bestOppositeMatches &&
             path.size() < bestPathLength);
        if (betterRoutable)
        {
            haveRoutable = true;
            bestPathLength = path.size();
            bestRoutableAnchor = candidate.anchor;
            bestRoadClearance = detached ? 1 : 0;
            bestWithinBudget = withinBudget;
            bestDetached = detached;
            bestExitMatches = exitMatches;
            bestOppositeMatches = oppositeMatches;
        }
    }

    if (!haveBuildable)
        return;
    if (haveRoutable && bestPathLength > static_cast<std::size_t>(kMaxVillageRoadTiles))
        Log::Msg("[MapGenerator]", "Starting village: no candidate found within ",
                  kMaxVillageRoadTiles, " road tiles of HQ — using shortest found (",
                  bestPathLength, " tiles)");

    Vec2i villageAnchor = haveRoutable ? bestRoutableAnchor : bestBuildableAnchor;
    SetFootprintTerrain(tilemap, villageAnchor, villageFootprint, TileType::GRASS, resourceRng, 3);
    Building* village = player->Build<Village>(villageAnchor, false);

    if (village != nullptr)
        BuildStartRoad(player, villageAnchor, villageFootprint, hqAnchor, hqFootprint,
                       haveRoutable ? bestRoadClearance : 0);

    // WOOD/STONE stay on the original ring (17..23); COAL/IRON_ORE (user
    // request 2026-07-19: iron is often missing near spawn) sit on a wider
    // ring (26..32) so all four patches fit around the HQ without collisions
    // — four spread directions, one per patch.
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::WOOD, resourceRng, 17, 23, Vec2i{-1, 0});
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::STONE, resourceRng, 17, 23, Vec2i{1, 0});
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::COAL, resourceRng, 26, 32, Vec2i{0, -1});
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::IRON_ORE, resourceRng, 26, 32, Vec2i{0, 1});
}

// Initializes runtime state for this object.
//
// Generation order (fixed 2026-07-13 — user report: military road sometimes
// cut through the starting village or its resource-road network):
//   1. Terrain, HQ anchors (B1) and the military road ring (B2) are
//      generated together by GenerateWorldLayout, retrying on a perturbed
//      seed if the ring fails validation (B5) — entirely before any player
//      or building exists, so a retry never needs to undo anything.
//   2. Every player is created and its Headquarters is placed
//      (CreateStartingHq) at its now-final validated anchor.
//   3. Only THEN is each player's village, start road and starting resource
//      patches placed (CreateStartingVillageAndResources) —
//      TileMap::CanBuildFootprint already refuses any tile with
//      isMilitaryRoad set, so this step automatically steers clear of the
//      road with no extra bookkeeping.
// Previously the military road was generated LAST, after everything
// (including village + resources) was already built, so its pathfinder
// could only route around bases by treating each one as a big blocked
// rectangle — a real box, but the fallback above could still land on a
// village or a resource-patch access road placed near that rectangle's edge.
void GameWorld::InitWorld(std::string name, Renderer* r, AudioSystem* a, MapParameters params)
{
    combatTelemetry.Clear();
    worldName = name;
    render = r;
    audio  = a;

    int opponentCount = std::clamp(params.aiOpponentCount, 0, 5);
    int playerCount = opponentCount + 1;
    // B1 (docs/work_plan_2026-07-13.md): every HQ — including the human
    // player's — sits on the same deterministic n-gon; no player is
    // special-cased to the exact map center anymore.
    std::vector<Vec2i> anchors;
    Vec2i hqFootprint = GenerateWorldLayout(params, playerCount, anchors);

    localPlayerId = 0;
    auto* human = CreatePlayer(0, PlayerControllerType::LocalHuman, "Player", PlayerSlotColor(0));

    std::map<int, Vec2i> hqAnchorsByPlayer{{0, anchors[0]}};
    std::map<int, Player*> playersById{{0, human}};
    std::map<int, unsigned int> baseSeedByPlayer{{0, params.seed ^ 0x9E3779B9u}};

    for (int i = 0; i < opponentCount; i++)
    {
        int playerId = i + 1;
        auto* enemy = CreatePlayer(playerId, PlayerControllerType::AI, "AI Opponent " + std::to_string(playerId), PlayerSlotColor(playerId));
        hqAnchorsByPlayer[playerId] = anchors[playerId];
        playersById[playerId] = enemy;
        baseSeedByPlayer[playerId] = params.seed ^ (0x85EBCA6Bu + static_cast<unsigned int>(i * 104729));
    }

    // Military road ring already generated (and validated/retried, B5) by
    // GenerateWorldLayout above, before any of these players/HQs existed.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingHq(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId));

    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
    {
        Player* p = playersById.at(playerId);
        CreateStartingVillageAndResources(p, anchor, baseSeedByPlayer.at(playerId));
        // Preserves the original (asymmetric) debug behavior: only the human
        // player gets a resource/manpower grant here, AI opponents only get
        // the debugMode flag — matches pre-reorder InitWorld exactly.
        if (params.debugMode)
        {
            p->debugMode = true;
            if (playerId == 0)
            {
                GrantDebugResourcesToHeadquarters(p, 50);
                GrantDebugManpower(p);
            }
        }
        // Keyed on the slot id, NOT controllerType — playerId 0 is always
        // the human here, and slot identity is what stays identical between
        // a host world and a client mirror.
        if (playerId != 0)
            GrantDifficultyStartingBonus(p, params.aiDifficulty);
    }

    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(anchors[0].x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(anchors[0].y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {tilemap.params.sizeX, tilemap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
    UpdateFogOfWar();
}

// Initializes deterministic multiplayer runtime state with server-assigned slots.
void GameWorld::InitMultiplayerWorld(std::string name, Renderer* r, AudioSystem* a, MapParameters params, int localId, bool authoritativeHost)
{
    combatTelemetry.Clear();
    worldName = name;
    render = r;
    audio  = a;

    localPlayerId = std::clamp(localId, 0, MultiplayerHumanSlots - 1);

    int opponentCount = std::clamp(params.aiOpponentCount, 0, 5);
    int playerCount = MultiplayerHumanSlots + opponentCount;
    // B1 (docs/work_plan_2026-07-13.md): every HQ sits on the same
    // deterministic n-gon, same as InitWorld. B5: terrain + anchors + the
    // military road ring are generated (and retried on validation failure)
    // together, before any player/building exists.
    std::vector<Vec2i> anchors;
    Vec2i hqFootprint = GenerateWorldLayout(params, playerCount, anchors);

    std::map<int, Vec2i> hqAnchorsByPlayer;
    std::map<int, Player*> playersById;
    std::map<int, unsigned int> baseSeedByPlayer;
    Vec2i cameraAnchor = anchors[0];

    // Pass 1: create every player and pick every HQ anchor — no building yet
    // (see InitWorld's comment for why the military road must be generated
    // before any base is placed).
    for (int playerId = 0; playerId < MultiplayerHumanSlots; playerId++)
    {
        PlayerControllerType controllerType = PlayerControllerType::Remote;
        if (playerId == localPlayerId)
            controllerType = PlayerControllerType::LocalHuman;

        std::string playerName = playerId == 0 ? "Host" : "Client";
        auto* player = CreatePlayer(playerId, controllerType, playerName, PlayerSlotColor(playerId));
        Vec2i anchor = anchors[playerId];
        hqAnchorsByPlayer[playerId] = anchor;
        playersById[playerId] = player;
        baseSeedByPlayer[playerId] = params.seed ^ (0x9E3779B9u + static_cast<unsigned int>(playerId * 104729));
        if (playerId == localPlayerId)
            cameraAnchor = anchor;
    }

    for (int i = 0; i < opponentCount; i++)
    {
        int playerId = MultiplayerHumanSlots + i;
        PlayerControllerType controllerType = authoritativeHost ? PlayerControllerType::AI : PlayerControllerType::Remote;
        auto* enemy = CreatePlayer(playerId, controllerType, "AI Opponent " + std::to_string(i + 1), PlayerSlotColor(playerId));
        hqAnchorsByPlayer[playerId] = anchors[playerId];
        playersById[playerId] = enemy;
        baseSeedByPlayer[playerId] = params.seed ^ (0x85EBCA6Bu + static_cast<unsigned int>(i * 104729));
    }

    // Pass 2: place every Headquarters. Military road ring already generated
    // (and validated/retried, B5) by GenerateWorldLayout above.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingHq(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId));

    // Pass 3: village + start road + resource patches — CanBuildFootprint
    // already refuses isMilitaryRoad tiles, so this automatically avoids the
    // road baked in above.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingVillageAndResources(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId));

    // Keyed on the slot id, NOT controllerType — AI slots are Remote on a
    // client mirror, and both sides must build the identical starting state.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        if (playerId >= MultiplayerHumanSlots)
            GrantDifficultyStartingBonus(playersById.at(playerId), params.aiDifficulty);

    if (params.debugMode)
    {
        for (auto& [id, player] : playerHandler.players)
        {
            if (player == nullptr) continue;
            player->debugMode = true;
            GrantDebugResourcesToHeadquarters(player.get(), 50);
            GrantDebugManpower(player.get());
        }
    }

    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(cameraAnchor.x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(cameraAnchor.y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {tilemap.params.sizeX, tilemap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
    UpdateFogOfWar();
}

