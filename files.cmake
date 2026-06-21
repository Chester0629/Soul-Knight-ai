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

    # Phase 4 per-content port (#5): faithful per-enemy/boss/character brains
    combat/EnemyAI03.cpp
    combat/BossAI02.cpp
    combat/CharSkillC01.cpp

    # Phase 4 Wave A (enemies): faithful per-enemy brains, adversarially verified
    combat/EnemyAI01.cpp
    combat/EnemyAI02.cpp
    combat/EnemyAI04.cpp
    combat/EnemyAI06.cpp
    combat/EnemyAI07.cpp
    combat/EnemyAI08.cpp
    combat/EnemyAI09.cpp
    combat/EnemyAI10.cpp
    combat/EnemyAI11.cpp
    combat/EnemyAI12.cpp
    combat/EnemyAI13.cpp
    combat/EnemyAI14.cpp
    combat/EnemyAI15.cpp
    combat/EnemyAIShark.cpp
    combat/WolfController.cpp
    combat/SnowmanController.cpp

    # Phase 4 Wave B (bosses): faithful per-boss brains, adversarially verified
    combat/BossAI03.cpp
    combat/BossAI05.cpp
    combat/BossAI06.cpp
    combat/BossAI06Child.cpp
    combat/BossAI07.cpp
    combat/BossAI08.cpp
    combat/BossAI09.cpp
    combat/BossAI10.cpp
    combat/BossAI11.cpp
    combat/BossAI12.cpp
    combat/BossAI12Parent.cpp
    combat/BossAI13.cpp
    combat/BossAI14.cpp
    combat/BossAINianLantern.cpp

    # Phase 4 Wave C (characters): faithful per-hero skill brains, adversarially verified
    combat/CharSkillC02.cpp
    combat/CharSkillC03.cpp
    combat/CharSkillC04.cpp
    combat/CharSkillC05.cpp
    combat/CharSkillC06.cpp
    combat/CharSkillC07.cpp
    combat/CharSkillC08.cpp
    combat/CharSkillC09.cpp
    combat/CharSkillC10.cpp
    combat/CharSkillC11.cpp
    combat/CharSkillC12.cpp
    combat/CharSkillC13.cpp

    # Phase 4 Wave D (weapons): faithful recoverable weapon mechanics, adversarially verified
    combat/GunThrow.cpp
    combat/Gun005.cpp
    combat/Gun008.cpp
    combat/BulletRoundabout.cpp

    # Phase 4 Wave E (pet/NPC/summon allies): faithful ally brains, adversarially verified
    combat/NpcSummon01.cpp
    combat/RGBatteryController.cpp
    combat/NpcMercenaryController.cpp

    # Phase 4 Wave F (new gun fire-pattern shapes): faithful scatter/heat/charge math
    combat/Gun016.cpp
    combat/Gun019.cpp
    combat/Gun007.cpp
    combat/Gun004.cpp
    combat/Gun002.cpp
    combat/Gun012.cpp
    combat/Gun014.cpp
    combat/Gun018.cpp

    # Phase 4 Wave G (single-shot / secondary spread guns + drone parent)
    combat/Gun001.cpp
    combat/Gun009.cpp
    combat/Gun013.cpp
    combat/Gun011.cpp
    combat/Gun017.cpp
    combat/Gun006Paw.cpp

    # Phase 4 Wave H (spawn-pattern bullet-trigger math)
    combat/RGBDelayDivision.cpp
    combat/RGBulletTrigger.cpp
    combat/RGBTDivision.cpp

    # Phase 4 Wave J (world grid-gen + interactables)
    world/RGRoomX.cpp
    world/RGAisle.cpp
    world/RGBox.cpp
    world/ItemWishingWell.cpp

    # Phase 5 Wave K (bullet motion/timing accumulators), adversarially verified
    combat/Bullet03.cpp
    combat/BulletParabola.cpp
    combat/BulletBoom.cpp
    combat/BulletColor.cpp
    combat/BulletLaterFixedTarget.cpp

    # Phase 5 Wave L (recoverable logic-gap closure), adversarially verified
    combat/RGPetController.cpp
    combat/GunStaffWizard.cpp
    combat/GunWaken.cpp
    combat/GunMagicBow.cpp
    combat/GunMultiBullet.cpp
    combat/GunHeroBow.cpp
    world/RGRoomXEndless.cpp

    # Engine port -- sim infrastructure (Plan 1)
    sim/FixedClock.cpp
    sim/Scheduler.cpp
    sim/FireSystem.cpp
    sim/EnemyController.cpp
    sim/BrainFactory.cpp
    sim/BossController.cpp
    sim/Simulation.cpp
    sim/WeaponController.cpp

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
    world/FloorBlock.cpp
    ui/Hud.cpp
    ui/HudLayout.cpp
    scenes/GameScene.cpp
    scenes/EndScene.cpp

    # Engine port -- run loop (Line 2, Phase 1): run-level state + controller
    game/RunController.cpp
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

    # Phase 4 per-content port (#5): faithful per-enemy/boss/character brains
    combat/EnemyAI03.hpp
    combat/BossAI02.hpp
    combat/CharSkillC01.hpp

    # Phase 4 Wave A (enemies): faithful per-enemy brains, adversarially verified
    combat/EnemyAI01.hpp
    combat/EnemyAI02.hpp
    combat/EnemyAI04.hpp
    combat/EnemyAI06.hpp
    combat/EnemyAI07.hpp
    combat/EnemyAI08.hpp
    combat/EnemyAI09.hpp
    combat/EnemyAI10.hpp
    combat/EnemyAI11.hpp
    combat/EnemyAI12.hpp
    combat/EnemyAI13.hpp
    combat/EnemyAI14.hpp
    combat/EnemyAI15.hpp
    combat/EnemyAIShark.hpp
    combat/WolfController.hpp
    combat/SnowmanController.hpp

    # Phase 4 Wave B (bosses): faithful per-boss brains, adversarially verified
    combat/BossAI03.hpp
    combat/BossAI05.hpp
    combat/BossAI06.hpp
    combat/BossAI06Child.hpp
    combat/BossAI07.hpp
    combat/BossAI08.hpp
    combat/BossAI09.hpp
    combat/BossAI10.hpp
    combat/BossAI11.hpp
    combat/BossAI12.hpp
    combat/BossAI12Parent.hpp
    combat/BossAI13.hpp
    combat/BossAI14.hpp
    combat/BossAINianLantern.hpp

    # Phase 4 Wave C (characters): faithful per-hero skill brains, adversarially verified
    combat/CharSkillC02.hpp
    combat/CharSkillC03.hpp
    combat/CharSkillC04.hpp
    combat/CharSkillC05.hpp
    combat/CharSkillC06.hpp
    combat/CharSkillC07.hpp
    combat/CharSkillC08.hpp
    combat/CharSkillC09.hpp
    combat/CharSkillC10.hpp
    combat/CharSkillC11.hpp
    combat/CharSkillC12.hpp
    combat/CharSkillC13.hpp

    # Phase 4 Wave D (weapons): faithful recoverable weapon mechanics, adversarially verified
    combat/GunThrow.hpp
    combat/Gun005.hpp
    combat/Gun008.hpp
    combat/BulletRoundabout.hpp

    # Phase 4 Wave E (pet/NPC/summon allies): faithful ally brains, adversarially verified
    combat/NpcSummon01.hpp
    combat/RGBatteryController.hpp
    combat/NpcMercenaryController.hpp

    # Phase 4 Wave F (new gun fire-pattern shapes): faithful scatter/heat/charge math
    combat/Gun016.hpp
    combat/Gun019.hpp
    combat/Gun007.hpp
    combat/Gun004.hpp
    combat/Gun002.hpp
    combat/Gun012.hpp
    combat/Gun014.hpp
    combat/Gun018.hpp

    # Phase 4 Wave G (single-shot / secondary spread guns + drone parent)
    combat/Gun001.hpp
    combat/Gun009.hpp
    combat/Gun013.hpp
    combat/Gun011.hpp
    combat/Gun017.hpp
    combat/Gun006Paw.hpp

    # Phase 4 Wave H (spawn-pattern bullet-trigger math)
    combat/RGBDelayDivision.hpp
    combat/RGBulletTrigger.hpp
    combat/RGBTDivision.hpp

    # Phase 4 Wave J (world grid-gen + interactables)
    world/RGRoomX.hpp
    world/RGAisle.hpp
    world/RGBox.hpp
    world/ItemWishingWell.hpp

    # Phase 5 Wave K (bullet motion/timing accumulators), adversarially verified
    combat/Bullet03.hpp
    combat/BulletParabola.hpp
    combat/BulletBoom.hpp
    combat/BulletColor.hpp
    combat/BulletLaterFixedTarget.hpp

    # Phase 5 Wave L (recoverable logic-gap closure), adversarially verified
    combat/RGPetController.hpp
    combat/GunStaffWizard.hpp
    combat/GunWaken.hpp
    combat/GunMagicBow.hpp
    combat/GunMultiBullet.hpp
    combat/GunHeroBow.hpp
    world/RGRoomXEndless.hpp

    # Engine port -- sim infrastructure (Plan 1)
    sim/SimConfig.hpp
    sim/SimMath.hpp
    sim/FixedClock.hpp
    sim/Scheduler.hpp
    sim/BulletState.hpp
    sim/EntityState.hpp
    sim/FireIntent.hpp
    sim/FireSystem.hpp
    sim/SimEvent.hpp
    sim/WorldCollision.hpp
    sim/EnemyController.hpp
    sim/BrainFactory.hpp
    sim/BossController.hpp
    sim/IBossBrain.hpp
    sim/BossBrainAdapters.hpp
    sim/WorldInputs.hpp
    sim/Simulation.hpp
    sim/WeaponController.hpp

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
    world/FloorBlock.hpp
    ui/Hud.hpp
    ui/HudLayout.hpp
    scenes/GameScene.hpp
    scenes/EndScene.hpp

    # Engine port -- run loop (Line 2, Phase 1)
    game/RunState.hpp
    game/RunController.hpp
    game/FloorClear.hpp
)

set(TEST_FILES
    GameDataTest.cpp

    # Phase 3 faithful core logic (ported from 1.7.10 RE)
    RGRandomTest.cpp
    RGMazeTest.cpp
    RoomGenTest.cpp
    MapManagerTest.cpp
    FloorBlockTest.cpp
    FloorConnectivityTest.cpp
    CorridorNeutralityTest.cpp
    DoorSealTest.cpp
    DoorTransitionTest.cpp
    DamageTest.cpp
    LootTableTest.cpp
    LootIntegrationTest.cpp
    BossAI01Test.cpp
    BossAI04Test.cpp
    BossAINianTest.cpp
    PlayerDashTest.cpp

    # Phase 4 per-content port (#5) + RoomGen golden-grid regression locks (#4)
    EnemyAI03Test.cpp
    BossAI02Test.cpp
    CharSkillC01Test.cpp
    RoomGenGoldenTest.cpp
    RoomGenDesignTest.cpp
    DesignRoomConnectivityTest.cpp
    BoxDestructionTest.cpp

    # Phase 4 Wave A (enemies): faithful per-enemy brains, adversarially verified
    EnemyAI01Test.cpp
    EnemyAI02Test.cpp
    EnemyAI04Test.cpp
    EnemyAI06Test.cpp
    EnemyAI07Test.cpp
    EnemyAI08Test.cpp
    EnemyAI09Test.cpp
    EnemyAI10Test.cpp
    EnemyAI11Test.cpp
    EnemyAI12Test.cpp
    EnemyAI13Test.cpp
    EnemyAI14Test.cpp
    EnemyAI15Test.cpp
    EnemyAISharkTest.cpp
    WolfControllerTest.cpp
    SnowmanControllerTest.cpp

    # Phase 4 Wave B (bosses): faithful per-boss brains, adversarially verified
    BossAI03Test.cpp
    BossAI05Test.cpp
    BossAI06Test.cpp
    BossAI06ChildTest.cpp
    BossAI07Test.cpp
    BossAI08Test.cpp
    BossAI09Test.cpp
    BossAI10Test.cpp
    BossAI11Test.cpp
    BossAI12Test.cpp
    BossAI12ParentTest.cpp
    BossAI13Test.cpp
    BossAI14Test.cpp
    BossAINianLanternTest.cpp

    # Phase 4 Wave C (characters): faithful per-hero skill brains, adversarially verified
    CharSkillC02Test.cpp
    CharSkillC03Test.cpp
    CharSkillC04Test.cpp
    CharSkillC05Test.cpp
    CharSkillC06Test.cpp
    CharSkillC07Test.cpp
    CharSkillC08Test.cpp
    CharSkillC09Test.cpp
    CharSkillC10Test.cpp
    CharSkillC11Test.cpp
    CharSkillC12Test.cpp
    CharSkillC13Test.cpp

    # Phase 4 Wave D (weapons): faithful recoverable weapon mechanics, adversarially verified
    GunThrowTest.cpp
    Gun005Test.cpp
    Gun008Test.cpp
    BulletRoundaboutTest.cpp

    # Phase 4 Wave E (pet/NPC/summon allies): faithful ally brains, adversarially verified
    NpcSummon01Test.cpp
    RGBatteryControllerTest.cpp
    NpcMercenaryControllerTest.cpp

    # Phase 4 Wave F (new gun fire-pattern shapes): faithful scatter/heat/charge math
    Gun016Test.cpp
    Gun019Test.cpp
    Gun007Test.cpp
    Gun004Test.cpp
    Gun002Test.cpp
    Gun012Test.cpp
    Gun014Test.cpp
    Gun018Test.cpp

    # Phase 4 Wave G (single-shot / secondary spread guns + drone parent)
    Gun001Test.cpp
    Gun009Test.cpp
    Gun013Test.cpp
    Gun011Test.cpp
    Gun017Test.cpp
    Gun006PawTest.cpp

    # Phase 4 Wave H (spawn-pattern bullet-trigger math)
    RGBDelayDivisionTest.cpp
    RGBulletTriggerTest.cpp
    RGBTDivisionTest.cpp

    # Phase 4 Wave J (world grid-gen + interactables)
    RGRoomXTest.cpp
    RGAisleTest.cpp
    RGBoxTest.cpp
    ItemWishingWellTest.cpp

    # Phase 5 Wave K (bullet motion/timing accumulators), adversarially verified
    Bullet03Test.cpp
    BulletParabolaTest.cpp
    BulletBoomTest.cpp
    BulletColorTest.cpp
    BulletLaterFixedTargetTest.cpp

    # Phase 5 Wave L (recoverable logic-gap closure), adversarially verified
    RGPetControllerTest.cpp
    GunStaffWizardTest.cpp
    GunWakenTest.cpp
    GunMagicBowTest.cpp
    GunMultiBulletTest.cpp
    GunHeroBowTest.cpp
    RGRoomXEndlessTest.cpp

    # Phase 2 combat logic
    CombatStatsTest.cpp
    WeaponInstanceTest.cpp
    EnemyAITest.cpp

    # HUD landing Phase 1 -- pure coordinate logic (converter + loader + parked)
    HudLayoutTest.cpp

    # Engine port -- sim infrastructure (Plan 1)
    SimMathTest.cpp
    FixedClockTest.cpp
    SchedulerTest.cpp
    SimPodsTest.cpp
    FireSystemTest.cpp
    EnemyControllerTest.cpp
    EnemyBrainDispatchTest.cpp
    BrainFactoryTest.cpp
    BossControllerTest.cpp
    BossBrainDispatchTest.cpp
    SimulationTest.cpp
    WeaponControllerTest.cpp

    # Engine port -- run loop (Line 2, Phase 1): RunState/continuation + perFloorSeed
    RunLoopTest.cpp

    # Milestone 1 A3: CharSkill -> sim pipeline (skill cast + cooldown -> HUD)
    SkillPipelineTest.cpp

    # Milestone 1 A4: Vertical/Bottom Filled (UV-crop) cooldown-mask geometry
    FilledImageTest.cpp
)
