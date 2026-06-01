set(SRC_FILES
    App.cpp

    # Phase 2 game-layer data model
    data/GameData.cpp

    # Phase 2 combat logic
    combat/WeaponInstance.cpp
    combat/EnemyAI.cpp

    # Phase 2 entities + scenes
    entities/Player.cpp
    entities/Bullet.cpp
    entities/Enemy.cpp
    world/Room.cpp
    ui/Hud.cpp
    scenes/GameScene.cpp
)

set(INCLUDE_FILES
    App.hpp

    # Phase 2 game-layer data model
    data/GameData.hpp

    # Phase 2 combat logic
    combat/CombatStats.hpp
    combat/WeaponInstance.hpp
    combat/EnemyAI.hpp

    # Phase 2 entities + scenes
    entities/Player.hpp
    entities/Bullet.hpp
    entities/Enemy.hpp
    world/Room.hpp
    ui/Hud.hpp
    scenes/GameScene.hpp
)

set(TEST_FILES
    GameDataTest.cpp

    # Phase 2 combat logic
    CombatStatsTest.cpp
    WeaponInstanceTest.cpp
    EnemyAITest.cpp
)
