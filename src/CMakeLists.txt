cmake_minimum_required(VERSION 3.16)

include_directories(../inc)
        target_sources(game
                       PRIVATE
                       GameWorld.cpp
                       Building.cpp
                       Game.cpp
                       Resource.cpp
                       RoadNetwork.cpp
                       MapGenerator.cpp
                       GameWindow.cpp
                       Scenes.cpp
                       Gui.cpp
                       Renderer.cpp
                       GuiController.cpp)