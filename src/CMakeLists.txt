cmake_minimum_required(VERSION 3.16)

include_directories(../inc)

target_sources(game
               PRIVATE
               core/GameSession.cpp
               core/GameWorld.cpp
               core/GameWorld.TileMap.cpp
               core/GameWorld.Init.cpp
               core/GameWorld.Persistence.cpp
               core/GameWorld.Commands.cpp
               core/GameWorld.Checksum.cpp
               core/GameWorld.Render.cpp
               core/GameWorld.Units.cpp
               core/GameWorld.Elimination.cpp
               ai/Controller.cpp
               research/Technology.cpp
               economy/Building.cpp
               economy/BuildingComponentsInternal.cpp
               economy/ProductionComponent.cpp
               economy/LogisticsComponent.cpp
               economy/WorkersComponent.cpp
               economy/StorageComponent.cpp
               economy/PopulationComponent.cpp
               economy/BuildingConfig.cpp
               economy/ConstructionQueue.cpp
               economy/RecruitmentComponent.cpp
               economy/HqComponent.cpp
               economy/ConqueredEconomy.cpp
               economy/Player.cpp
               scenes/Game.cpp
               data/Resource.cpp
               data/Equipment.cpp
               simulation/RoadNetwork.cpp
               simulation/PathingService.cpp
               simulation/MapGenerator.cpp
               simulation/MilitaryRoadNetwork.cpp
               data/RtsDataFile.cpp
               warfare/UnitDefinition.cpp
               warfare/BattleUnit.cpp
               warfare/UnitMarchSystem.cpp
               warfare/CombatPipeline.cpp
               warfare/UnitCombatSystem.cpp
               warfare/HqCombatSystem.cpp
               multiplayer/TcpGameTransport.cpp
               scenes/GameWindow.cpp
               scenes/SceneUtils.cpp
               scenes/MainMenuScene.cpp
               scenes/OptionsScene.cpp
               scenes/NewGameScene.cpp
               scenes/MultiplayerScene.cpp
               scenes/ControlsScene.cpp
               scenes/LoadGameScene.cpp
               scenes/SaveGameScene.cpp
               scenes/GameScene.cpp
               scenes/GameMenuScene.cpp
               ui/Gui.cpp
               ui/InputManager.cpp
               research/ResearchCatalog.cpp
               ui/Renderer.cpp
               ui/GuiController.cpp
               ui/GuiCommon.cpp
               ui/GuiMapWidgets.cpp
               ui/GuiHudPanels.cpp
               ui/GuiResearchTree.cpp
               ui/GuiBuildModes.cpp
               ai/DiplomaticState.cpp
               ui/AudioSystem.cpp
               WinmmStubs.cpp)

# Organize folders in IDE
source_group("core" FILES
             core/GameWorld.cpp core/GameWorld.TileMap.cpp core/GameWorld.Init.cpp
             core/GameWorld.Persistence.cpp core/GameWorld.Commands.cpp
             core/GameWorld.Checksum.cpp core/GameWorld.Render.cpp core/GameWorld.Units.cpp
             core/GameWorld.Elimination.cpp)

source_group("ai" FILES ai/Controller.cpp ai/DiplomaticState.cpp)
source_group("research" FILES research/Technology.cpp research/ResearchCatalog.cpp)
source_group("economy" FILES economy/Building.cpp economy/BuildingComponentsInternal.cpp
             economy/ProductionComponent.cpp economy/LogisticsComponent.cpp economy/WorkersComponent.cpp
             economy/StorageComponent.cpp economy/PopulationComponent.cpp
             economy/BuildingConfig.cpp economy/ConstructionQueue.cpp economy/RecruitmentComponent.cpp
             economy/HqComponent.cpp economy/ConqueredEconomy.cpp economy/Player.cpp)
source_group("simulation" FILES simulation/RoadNetwork.cpp simulation/MapGenerator.cpp
             simulation/MilitaryRoadNetwork.cpp)
source_group("data" FILES data/Resource.cpp data/Equipment.cpp data/RtsDataFile.cpp)
source_group("warfare" FILES warfare/UnitDefinition.cpp warfare/BattleUnit.cpp warfare/UnitMarchSystem.cpp
             warfare/CombatPipeline.cpp warfare/UnitCombatSystem.cpp warfare/HqCombatSystem.cpp)
source_group("multiplayer" FILES multiplayer/TcpGameTransport.cpp)
source_group("scenes" FILES scenes/Game.cpp scenes/GameWindow.cpp scenes/SceneUtils.cpp
             scenes/MainMenuScene.cpp scenes/OptionsScene.cpp scenes/NewGameScene.cpp
             scenes/MultiplayerScene.cpp scenes/ControlsScene.cpp scenes/LoadGameScene.cpp
             scenes/SaveGameScene.cpp scenes/GameScene.cpp scenes/GameMenuScene.cpp)
source_group("ui" FILES ui/Gui.cpp ui/Renderer.cpp ui/GuiController.cpp ui/GuiCommon.cpp
             ui/GuiMapWidgets.cpp ui/GuiHudPanels.cpp ui/GuiResearchTree.cpp
             ui/GuiBuildModes.cpp ui/AudioSystem.cpp)
