cmake_minimum_required(VERSION 3.16)

include_directories(../inc)
        target_sources(game
                       PRIVATE
                       GameWorld.cpp
                       GameWorld.TileMap.cpp
                       GameWorld.Init.cpp
                       GameWorld.Persistence.cpp
                       GameWorld.Commands.cpp
                       GameWorld.Checksum.cpp
                       GameWorld.Render.cpp
                       Controller.cpp
                       Technology.cpp
                       Building.cpp
                       BuildingComponents.cpp
                       BuildingConfig.cpp
                       Game.cpp
                       Resource.cpp
                       RoadNetwork.cpp
                       MapGenerator.cpp
                       TcpGameTransport.cpp
                       GameWindow.cpp
                       Scenes.cpp
                       Gui.cpp
                       ResearchCatalog.cpp
                       Renderer.cpp
                       GuiController.cpp)
