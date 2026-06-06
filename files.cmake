set(SRC_FILES
    App.cpp

    # Phase 2 game-layer data model
    data/GameData.cpp

    # Phase 3 faithful core logic (ported from 1.7.10 RE)
    data/RGRandom.cpp
    data/LootTable.cpp
    world/RGMaze.cpp
    world/RoomGen.cpp
    world/MapManager.cpp
    combat/Damage.cpp
    combat/BossAI01.cpp
    combat/BossAI04.cpp
    combat/BossAINian.cpp
    combat/PlayerDash.cpp

    # Phase 2 combat logic
    combat/WeaponInstance.cpp
    combat/EnemyAI.cpp

    # Phase 2 entities + scenes
    entities/Player.cpp
    entities/Bullet.cpp
    entities/Enemy.cpp
    entities/Boss.cpp
    entities/Chest.cpp
    entities/WeaponPickup.cpp
    world/Room.cpp
    ui/Hud.cpp
    scenes/GameScene.cpp
)

set(INCLUDE_FILES
    App.hpp

    # Phase 2 game-layer data model
    data/GameData.hpp

    # Phase 3 faithful core logic (ported from 1.7.10 RE)
    data/RGRandom.hpp
    data/LootTable.hpp
    world/RGMaze.hpp
    world/RoomGen.hpp
    world/MapManager.hpp
    combat/Damage.hpp
    combat/BossAI01.hpp
    combat/BossAI04.hpp
    combat/BossAINian.hpp
    combat/PlayerDash.hpp

    # Phase 2 combat logic
    combat/CombatStats.hpp
    combat/WeaponInstance.hpp
    combat/EnemyAI.hpp

    # Phase 2 entities + scenes
    entities/Player.hpp
    entities/Bullet.hpp
    entities/Enemy.hpp
    entities/Boss.hpp
    entities/Chest.hpp
    entities/WeaponPickup.hpp
    world/Room.hpp
    ui/Hud.hpp
    scenes/GameScene.hpp
)

set(TEST_FILES
    GameDataTest.cpp

    # Phase 3 faithful core logic (ported from 1.7.10 RE)
    RGRandomTest.cpp
    RGMazeTest.cpp
    RoomGenTest.cpp
    MapManagerTest.cpp
    DamageTest.cpp
    LootTableTest.cpp
    LootIntegrationTest.cpp
    BossAI01Test.cpp
    BossAI04Test.cpp
    BossAINianTest.cpp
    PlayerDashTest.cpp

    # Phase 2 combat logic
    CombatStatsTest.cpp
    WeaponInstanceTest.cpp
    EnemyAITest.cpp
)
