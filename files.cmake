set(SRC_FILES
    App.cpp

    # Phase 2 game-layer data model
    data/GameData.cpp

    # Phase 2 combat logic
    combat/WeaponInstance.cpp
    combat/EnemyAI.cpp

    # Phase 2 entities + scenes
    entities/Player.cpp
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
    scenes/GameScene.hpp
)

set(TEST_FILES
    GameDataTest.cpp

    # Phase 2 combat logic
    CombatStatsTest.cpp
    WeaponInstanceTest.cpp
    EnemyAITest.cpp
)
