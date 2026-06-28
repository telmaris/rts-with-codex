#ifndef GAME_COMMAND_H
#define GAME_COMMAND_H

#include "Building.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class GameCommandType
{
    BuildBuilding,
    DestroyBuilding,
    SetReceiver,
    AttackBuilding,
    IssueMilitaryOrder,
    RecruitUnit,
    StartFocus,
    StartTechnologyResearch
};

struct GameCommand
{
    static constexpr int WireVersion = 4;

    static GameCommand BuildBuilding(int playerId, BuildingType buildingType, Vec2i tilePos, bool chargeCost = true)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::BuildBuilding;
        command.buildingType = buildingType;
        command.tilePos = tilePos;
        command.chargeCost = chargeCost;
        return command;
    }

    static GameCommand DestroyBuilding(int playerId, int tileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::DestroyBuilding;
        command.sourceTileId = tileId;
        return command;
    }

    static GameCommand SetReceiver(int playerId, int sourceTileId, int targetTileId, bool alternativeReceiver = false)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::SetReceiver;
        command.sourceTileId = sourceTileId;
        command.targetTileId = targetTileId;
        command.alternativeReceiver = alternativeReceiver;
        return command;
    }

    static GameCommand AttackBuilding(int playerId, int sourceTileId, int targetTileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::AttackBuilding;
        command.sourceTileId = sourceTileId;
        command.targetTileId = targetTileId;
        return command;
    }

    static GameCommand IssueMilitaryOrder(int playerId, MilitaryOrderType orderType, int sourceTileId, int targetTileId, int divisionId = -1)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::IssueMilitaryOrder;
        command.militaryOrderType = orderType;
        command.sourceTileId = sourceTileId;
        command.targetTileId = targetTileId;
        command.divisionId = divisionId;
        return command;
    }

    static GameCommand RecruitUnit(int playerId, int barracksTileId, MilitaryUnitType unitType)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::RecruitUnit;
        command.sourceTileId = barracksTileId;
        command.militaryUnitType = unitType;
        return command;
    }

    static GameCommand StartFocus(int playerId, std::string focusId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartFocus;
        command.researchId = std::move(focusId);
        return command;
    }

    static GameCommand StartTechnologyResearch(int playerId, std::string technologyId, int universityTileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartTechnologyResearch;
        command.researchId = std::move(technologyId);
        command.sourceTileId = universityTileId;
        return command;
    }

    std::string Serialize() const
    {
        std::ostringstream stream;
        stream << WireVersion << ' '
               << commandId << ' '
               << targetTick << ' '
               << static_cast<int>(type) << ' '
               << playerId << ' '
               << static_cast<int>(buildingType) << ' '
               << tilePos.x << ' '
               << tilePos.y << ' '
               << sourceTileId << ' '
               << targetTileId << ' '
               << (chargeCost ? 1 : 0) << ' '
               << static_cast<int>(militaryOrderType) << ' '
               << static_cast<int>(militaryUnitType) << ' '
               << divisionId << ' '
               << (alternativeReceiver ? 1 : 0) << ' '
               << std::quoted(researchId);
        return stream.str();
    }

    static bool TryDeserialize(const std::string& payload, GameCommand& command)
    {
        std::istringstream stream(payload);
        int version = 0;
        std::uint64_t commandId = 0;
        std::uint64_t targetTick = 0;
        int type = 0;
        int buildingType = 0;
        int chargeCost = 0;
        int militaryOrderType = 0;
        int militaryUnitType = 0;
        int alternativeReceiver = 0;
        std::string researchId;

        GameCommand parsed;
        stream >> version
               >> commandId
               >> targetTick
               >> type
               >> parsed.playerId
               >> buildingType
               >> parsed.tilePos.x
               >> parsed.tilePos.y
               >> parsed.sourceTileId
               >> parsed.targetTileId
               >> chargeCost
               >> militaryOrderType
               >> militaryUnitType
               >> parsed.divisionId
               >> alternativeReceiver;

        stream >> std::quoted(researchId);

        if (!stream || version != WireVersion || !IsValidType(type))
            return false;

        parsed.type = static_cast<GameCommandType>(type);
        parsed.commandId = commandId;
        parsed.targetTick = targetTick;
        parsed.buildingType = static_cast<BuildingType>(buildingType);
        parsed.chargeCost = chargeCost != 0;
        parsed.militaryOrderType = static_cast<MilitaryOrderType>(militaryOrderType);
        parsed.militaryUnitType = static_cast<MilitaryUnitType>(militaryUnitType);
        parsed.alternativeReceiver = alternativeReceiver != 0;
        parsed.researchId = std::move(researchId);
        command = std::move(parsed);
        return true;
    }

    std::uint64_t commandId{0};
    std::uint64_t targetTick{0};
    int playerId{0};
    GameCommandType type{GameCommandType::BuildBuilding};
    BuildingType buildingType{BuildingType::Building};
    Vec2i tilePos{0, 0};
    int sourceTileId{-1};
    int targetTileId{-1};
    bool chargeCost{true};
    MilitaryOrderType militaryOrderType{MilitaryOrderType::None};
    MilitaryUnitType militaryUnitType{MilitaryUnitType::Militia};
    int divisionId{-1};
    bool alternativeReceiver{false};
    std::string researchId;

    static bool IsValidType(int type)
    {
        switch (static_cast<GameCommandType>(type))
        {
            case GameCommandType::BuildBuilding:
            case GameCommandType::DestroyBuilding:
            case GameCommandType::SetReceiver:
            case GameCommandType::AttackBuilding:
            case GameCommandType::IssueMilitaryOrder:
            case GameCommandType::RecruitUnit:
            case GameCommandType::StartFocus:
            case GameCommandType::StartTechnologyResearch:
                return true;
        }
        return false;
    }
};

struct GameCommandResult
{
    static constexpr int WireVersion = 3;

    std::uint64_t commandId{0};
    std::uint64_t simulationTick{0};
    std::uint64_t targetTick{0};
    int playerId{0};
    GameCommandType type{GameCommandType::BuildBuilding};
    bool accepted{false};
    std::string reason;
    std::string commandPayload;

    std::string Serialize() const
    {
        std::ostringstream stream;
        stream << WireVersion << ' '
               << commandId << ' '
               << simulationTick << ' '
               << targetTick << ' '
               << playerId << ' '
               << static_cast<int>(type) << ' '
               << (accepted ? 1 : 0) << ' '
               << std::quoted(reason) << ' '
               << std::quoted(commandPayload);
        return stream.str();
    }

    static bool TryDeserialize(const std::string& payload, GameCommandResult& result)
    {
        std::istringstream stream(payload);
        int version = 0;
        int type = 0;
        int accepted = 0;
        std::string reason;
        std::string commandPayload;

        GameCommandResult parsed;
        stream >> version
               >> parsed.commandId
               >> parsed.simulationTick
               >> parsed.targetTick
               >> parsed.playerId
               >> type
               >> accepted
               >> std::quoted(reason)
               >> std::quoted(commandPayload);

        if (!stream || version != WireVersion || !GameCommand::IsValidType(type))
            return false;

        parsed.type = static_cast<GameCommandType>(type);
        parsed.accepted = accepted != 0;
        parsed.reason = std::move(reason);
        parsed.commandPayload = std::move(commandPayload);
        result = std::move(parsed);
        return true;
    }
};

struct GameServerFrame
{
    static constexpr int WireVersion = 1;

    std::uint64_t tick{0};
    std::uint64_t checksum{0};
    bool hasChecksum{false};
    std::vector<GameCommandResult> results;

    std::string Serialize() const
    {
        std::ostringstream stream;
        stream << WireVersion << ' '
               << tick << ' '
               << (hasChecksum ? 1 : 0) << ' '
               << checksum << ' '
               << results.size();
        for (const auto& result : results)
            stream << ' ' << std::quoted(result.Serialize());
        return stream.str();
    }

    static bool TryDeserialize(const std::string& payload, GameServerFrame& frame)
    {
        std::istringstream stream(payload);
        int version = 0;
        int hasChecksum = 0;
        size_t resultCount = 0;
        GameServerFrame parsed;
        stream >> version >> parsed.tick >> hasChecksum >> parsed.checksum >> resultCount;
        if (!stream || version != WireVersion)
            return false;

        parsed.hasChecksum = hasChecksum != 0;
        parsed.results.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++)
        {
            std::string serializedResult;
            stream >> std::quoted(serializedResult);
            if (!stream)
                return false;

            GameCommandResult result;
            if (!GameCommandResult::TryDeserialize(serializedResult, result))
                return false;
            parsed.results.push_back(std::move(result));
        }

        frame = std::move(parsed);
        return true;
    }
};

#endif
