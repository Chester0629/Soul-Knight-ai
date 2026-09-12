# Soul-Knight-ai 專案架構書（元氣騎士 1.7.10 忠實復刻）

> 本文件是專案的**單一事實來源**：範圍、階段、每階段的資料來源與驗證方式、Codex 審查紀錄。
> 由 Claude（實作）與 Codex（審查）共同維護；規則見 §8。
> **狀態詞彙**（兩欄分開記）：
> 功能欄：`未開始 / 進行中 / 功能完成`。驗收欄：`未驗收 / 驗收完成 / 驗收完成（有取捨）`。
> 「功能完成」＝接線可玩；「驗收完成」＝§1 證據齊全且 Codex 通過。
> 版本：v1.2（2026-09-12；依 Codex R1、R2 修訂，見 §9）

---

## 0. 目的、範圍、非目標、資料版本

**目的**：在自製的 C++ 遊戲系統（PTSD：SDL2 + OpenGL + ImGui，CMake，GoogleTest）上，以**資料驅動**的方式忠實復刻《元氣騎士》1.7.10（Unity 2017.4.1f1 IL2CPP，armeabi-v7a）的單人地牢循環，作為學術演示。之後要能**用高版本素材替換**並**新增內容**，所以數值、貼圖、布局都必須走「解包資料 → JSON/PNG → 程式讀表」。

**忠實度標籤**（程式碼註解與本文件共用；取捨註解一律附 §6 的 T-ID）：
- `FAITHFUL @ <來源>`：直接來自反編譯（`../_reverse/ghidra_export/game_full.c`，倉庫外的同層目錄）、AssetRipper 預製件（`../_reverse/asset soul/ExportedProject`）或資料表（`../_reverse/analysis/data_tables/*.csv`）。**只有原碼可見的部分**才能標 FAITHFUL；Ghidra 截斷處之後一律 INFERRED。
- `INFERRED (T-xx)`：來源存在但被截斷或欄位語意需推論，依據寫在註解裡。
- `PORT CHOICE (T-xx)`：原版邏輯不可恢復，移植版自行決定；集中在單一處。
- `NOT PORTED (T-xx)`：明知原版有、目前沒做，程式碼只記 log。

**資料版本矩陣**：

| 表 | 來源 | 版本 | 缺來源時的退化（`build_gamedata.py` 印 `FALLBACK:`） | 狀態 |
|---|---|---|---|---|
| enemies.json（145） | `data_tables/enemies.csv`（1.7.10 AssetRipper 產生） | 1.7.10 | 退回 `1.07/MonoBehaviour` 16 個 AI 類別表 | 1.7.10 |
| characters.json | `res/player/c##.prefab` | 1.7.10 | 退回 1.07 單一模板 | 1.7.10 |
| weapons.json（194） | `data_tables/weapons.csv` + 預製件圖示 | 1.7.10 | 退回 1.07 的 46 類別表 | 1.7.10 |
| sprites.json / tiles | Animator→clip→sprite | 1.7.10 | 缺則佔位圖 | 1.7.10 |
| levels / boss_rooms / boss_chests / shop / **pickups**.json | `res/map_manager`、`rgprefab/rooms`、`res/item`、`rgprefab/room/event`、`res/register` | 1.7.10 | 不產生檔案；執行期走 pre-M1 固定路徑並 log | 1.7.10 |
| bullets / bosses / buffs / enemy_guns.json | `1.07/MonoBehaviour` 舊 dump | **1.07** | — | 待升版（P1.10） |
| droptables / weapon_catalog.json | `1.07/TextAsset` | 1.07 | — | 待升版（P1.10） |

**降級條件**：退化只在 `../_reverse` 缺席的乾淨 checkout 觸發，`build_gamedata.py` 對每張未產生的表印 `FALLBACK: <table> …`；標「驗收完成」的階段其表必須是 1.7.10 來源。

**非目標**：多人連線、IAP／廣告、雲端存檔、Google Play 成就、季節 boss（Nian）。
**法律框架**：解包素材屬 ChillyRoom，僅供課堂演示；`Resources/sprites` 已列入 `.gitignore`。

---

## 1. 驗證體系

| 代號 | 方式 | 指令 / 位置 | 用途與限制 |
|---|---|---|---|
| V-T | GoogleTest | `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug` → `cmake --build build --config Debug --target SoulKnightTests` → `ctest --test-dir build -C Debug -E Tests_NOT_BUILT` | 每次驗收附**測試報告行**與 commit（§10）。基準（a3bb557）：`grep -c "^TEST(" test/*.cpp` = 2009 巨集 / 141 檔；執行數以 ctest 輸出為準。 |
| V-G | 資料閘 | `python tools/validate_data.py`（pre-commit 亦執行；非 ASCII 檢查在 pre-commit hook） | 檢查 weapons/bullets/enemies/bosses/buffs/catalog/droptables/characters + levels（31、6 地板、12 障礙、敵池引用；**未知 id 為錯誤，只豁免 T-27 列名的 ex_orc05**）、boss_rooms（14、界內、皮膚、boss 層房存在）、boss_chests、shop、pickups（四種皆有 value/speed/range/frames）。 |
| V-D | 資料對照 | §1.2 固定樣本 | 每個資料階段至少 5 筆「來源:欄位 → 預期 → JSON」＋全表檢查。 |
| V-H | 無頭鉤子 | §1.1 案例（命令、seed、幀數、預期 log、逾時） | 只證明路徑可達與 log 順序；不證明正常遊玩（見 V-P）。實跑 log 存 §10。 |
| V-P | 實玩紀錄 | 人工遊玩＋截圖／錄影＋seed | 「可玩」驗收必附。 |
| V-S | 截圖對照 | `SK_SHOT`；1280×720、zoom 1.5、預設 seed 20240607（或 `SK_SEED`）、指定樓層房間 | 每張旁列預製件數值。 |
| V-R | 反編譯對照 | `// FAITHFUL: <Class>__<Method> @ game_full.c:<line>` | 截斷處後不得標 FAITHFUL。 |
| V-C | Codex 審查 | §7 | 每條「通過／不通過／不適用＋證據」。 |

### 1.1 無頭鉤子與可執行案例
鉤子（env）：`SK_SEED=N`、`SK_MAX_FRAMES`、`SK_SHOT`、`SK_SKIP_MENU`、`SK_START_CHAR`、`SK_CHAR`、`SK_MENU=start`、`SK_FORCE_CLEAR=K`、`SK_FORCE_DIE=K`、`SK_FORCE_KILL=K`、`SK_FORCE_KILL_EVERY=N`、`SK_FORCE_SKILL=K`、`SK_ENDSCENE=restart|quit|menu`、`SK_GATE_TELEPORT=K`、`SK_GATE_BESIDE=K`、`SK_ROOM_TELEPORT=K,R`、`SK_SHOP_BUY=K`、`SK_SHOP_MACHINE=N`、`SK_STATUE_WORSHIP=K`、`SK_COINS=N`、`SK_EVENT_ROOM=statue|mineral`、`SK_AUTOWALK`（不穩）、`SK_AUTOFIRE`。
`GameScene::m_Frame` 每層重新計數。**Shell**：命令以 Git Bash 寫（`VAR=x ./build/Debug/SoulKnight.exe`）；PowerShell 等價寫法 `$env:VAR="x"; .\build\Debug\SoulKnight.exe`。實跑腳本：`scratchpad/r2_chain.sh`（§10 附輸出）。

| 案例 | 命令（Git Bash，倉庫根目錄，Debug 建置） | 預期 log 順序 | 逾時 |
|---|---|---|---|
| H1 入口流程 | `SK_START_CHAR=c12 SK_FORCE_CLEAR=60 SK_MAX_FRAMES=200 ./build/Debug/SoulKnight.exe` | `GameScene: hero c12 on floor 0` → `Level B1 (1-1)` → `Floor 0 CLEARED` → `hero c12 on floor 1` → `Level B2`（`SK_START_CHAR` 由 main.cpp 直接開局，不經 CharSelectScene；選角畫面的 log 用 `SK_MENU=start SK_CHAR=c12` 驗）| 60 s |
| H2 傳送門 | `SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400 ./build/Debug/SoulKnight.exe`；反例同命令去掉 `SK_GATE_TELEPORT` | `TransferGate: open in room N` → `TransferGate: entered -> next floor` → `hero c01 on floor 1`；反例 `GameScene: hero` 只出現 1 次 | 60 s |
| H3 分波與封鎖 | `SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=90 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe` | `RGRoomX: room 1 StartRoom` → 每次 kill 後約 0.8 s `EnemyMaker: room 1 part spawned` → pending 歸零後 `RGRoomX: room 1 cleared` | 90 s |
| H4 掉落經濟 | `SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe` | `Reward: <id> dropped N pickups (coins a energy b)` 至少 3 行（必要）；`Pickup: coin +1`／`Pickup: energy +8` 需玩家進入磁吸範圍（加 `SK_AUTOWALK=1`，可選） | 90 s |
| H5 結算 | `SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT=H5.png ./build/Debug/SoulKnight.exe` | `Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1 …` | 60 s |
| H6 商店 | `SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130 ./build/Debug/SoulKnight.exe`（預設 seed 第 0 層獎勵房為商店） | `Shop: vending_machine_i … sells` → `Shop: bought <pot> for 10` | 60 s |
| H7 boss 層 | `SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=447 ./build/Debug/SoulKnight.exe` | `Level B5` → `BossRoom: room N uses r1_bossK` → `Boss defeated!` → `Chest: boss chest chest_bossNN` → `TransferGate: open` | 120 s |

### 1.2 固定對照樣本（V-D）
- enemies：boss01 max_hp=600；boss05=800；e_succulent=8；e_slime01 reward_rate=15；boss03 reward_value=[5,3,0,10]。
- characters：c01 max_armor=5, energy=180, atk=4, skill_cd=10；c12 max_hp=6（`RoleAttributePlayer.json` 單檔是 Druid，不可當模板）。
- weapons：weapon_000 atk=3 consume=0 bullet_speed=36 item_value=12；weapon_043 atk=15 consume=5。
- levels：B1 (1-1, chest 1, map_long 3, f601..f606, wall602/wall601)；B5 type2=[r1_boss3,r1_boss4,r1_boss6]；C1 (2-1, chest 3, f201..)；G1 (3-6, map_long 1, type2=[r_end])。
- boss chests：chest_boss01 level 2 weapon_077；chest_boss13 weapon_175；無 chest_boss05/06/11/12。
- pickups：coin_0 value 5 speed 20 range 6 frames objects_common_31..34；coin_1 3；coin_2 1；energy value 8 speed 14；_rgcoin.friction 0.9。

---

## 2. 現況總表（截至 2026-09-12，分支 `feat/phase3-presentation-content`，HEAD 見 §10 最新報告）

| 區塊 | 功能 | 驗收 | 主要 commit | 缺口 |
|---|---|---|---|---|
| P1 資料層 | 進行中（P1.1–P1.7、P1.11、P1.12 功能完成） | 未驗收 | 12c3750 … 8cb1e61、本版 | P1.8 內嵌物件、P1.9 未解析掉落物、P1.10 四張表仍為 1.07 |
| P2 地圖 | 進行中（P2.1–P2.2、P2.4–P2.9 功能完成；P2.3 進行中） | 未驗收 | 3eded10 … e2c5030 | P2.3 加級條件（T-05）、P2.10–P2.13 |
| P3 戰鬥忠實度 | 進行中（每擊數學、dash、AI 派發、18 boss brain 完成） | 未驗收 | phase 1–5 | P3.0–P3.9 |
| P4 進度循環 | 進行中（P4.1、P4.6a 功能完成） | 未驗收 | 5c8c4b0、1cfb297 | P4.2–P4.5、P4.6b |
| P5 大廳 | 未開始 | — | — | 全部 |
| P6 UI 表現 | 進行中（P6.1 功能完成） | 未驗收 | 6e46eed | P6.2–P6.8 |
| P7 內容補完 | 未開始 | — | — | 全部 |
| P8 擴充管線 | 未開始 | — | — | 全部 |

---

## 3. 階段規劃（細分，六欄）

### P1 資料層
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態（功能・驗收） |
|---|---|---|---|---|---|---|
| P1.1 敵人 | 145 species | enemies.csv | enemies.json | V-D、EnemyStatsTest | — | 功能完成・未驗收 |
| P1.2 角色 | 13 角 | c##.prefab | characters.json | V-D、CharacterStatsTest | — | 功能完成・未驗收 |
| P1.3 動畫 | 156 實體幀 | Animator 鏈 | sprites.json | SpriteManifestTest | — | 功能完成・未驗收 |
| P1.4 武器 | 194 per-prefab | weapons.csv | weapons.json | WeaponStatsTest | — | 功能完成・未驗收 |
| P1.5 章節 | biome/chapter、boss 池 | res/enemy/<biome> | enemies.json 欄 | ChapterProgressionTest | — | 功能完成・未驗收 |
| P1.6 關卡表 | 31 關 | res/map_manager | levels.json | LevelTableTest、§1.2、V-G 全表引用 | T-01 T-27 | 功能完成・未驗收 |
| P1.7 boss 房／寶箱／商店表 | 14 房、11 箱、8 藥水 3 機器 | rgprefab/rooms、res/item、room/event | boss_rooms/boss_chests/shop.json | BossRoomTest、ShopTest、V-G | T-08 T-28 | 功能完成・未驗收 |
| P1.8 內嵌物件 | rail/stone02/carpet/obj22_house/table/stone | *_boss*.prefab 子物件 m_Sprite | boss_rooms.json prefabs | BossRoomTest | — | 未開始 |
| P1.9 未解析掉落物 | mineral_obj、GetRoomReward、ChestXmlReader、GetPot/GetCoin | 反編譯（game_typed.c 交叉） | 對應 JSON | V-R | T-09 T-10 T-15 | 未開始 |
| P1.10 升版 | bullets/bosses/buffs/enemy_guns/droptables/catalog 改由 AssetRipper 產生；補 ex_orc05 | res/*、rgprefab/bullet、Assets/GameObject | 同名 JSON | V-D 全表 diff | T-27 | 未開始 |
| P1.11 資料閘擴充 | levels/boss_rooms/boss_chests/shop/pickups 進閘；未知引用為錯誤 | — | validate_data.py | V-G（GREEN，8 warnings） | T-27 | 功能完成・未驗收 |
| P1.12 數值出 C++ | 掉落值、磁吸 speed/range、摩擦、幀清單 → pickups.json | res/register/coin_*、energy、coin_0.controller | pickups.json、GameData::FindPickup | EnemyRewardTest（讀表）、V-G | T-19（散射力度仍 PORT） | 功能完成・未驗收 |

### P2 地圖與關卡
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P2.1 關卡驅動 | LevelIdFor、章節變體 | levels.json | GameScene 讀表 | H1、LevelTableTest | T-01 | 功能完成・未驗收 |
| P2.2 生態貼圖 | 六地板變體、牆頂牆面、障礙皮膚、寶箱等級 | levels.json、tiles | 渲染 | V-S（冰／城堡截圖） | T-02 T-16 | 功能完成・未驗收 |
| P2.3 EnemyMaker | 預算表、大房加級、加權池、分波 | game_full.c:682128 | EnemyMaker.hpp、SpawnWaves | H3、EnemyMakerTest | T-03 T-04 T-05 | 進行中・未驗收（加級條件 INFERRED；分波時序純測試待抽出） |
| P2.4 傳送門 | 走進去換層 | transfer_gate.prefab | GameScene | H2 | — | 功能完成・未驗收 |
| P2.5 比例／牆／箱／走廊 | 48 px/單位、底部錨定 | Scene_Game camera、wall601/box04 | 渲染 | V-S | — | 功能完成・未驗收 |
| P2.6 boss 房布局 | 14 預製件 | boss_rooms.json | 渲染＋碰撞 | H7、V-S（r1_boss4；r2_boss3 見 §10） | T-28 | 功能完成・未驗收 |
| P2.7 房間角色與寶箱 | 出口／獎勵／事件；獎勵箱；boss 箱 | boss_chests.json | GameScene | H7 | T-06 T-07 T-09 T-10 | 功能完成・未驗收 |
| P2.8 商店房 | 三機器、E 購買 | shop.json | VendingMachine | H6、ShopTest | T-08 T-11 T-12 | 功能完成・未驗收 |
| P2.9 門片／影子／事件房 | d1–d5、shadow2、雕像、礦物 | door_2 clips、statue_0N | GameScene | V-S（開／鎖門）、StatueTest | T-13 T-14 T-15 T-16 | 功能完成・未驗收 |
| P2.10 傭兵房（=P7.2） | TalkMercenary 雇傭＋跟隨戰鬥 | TalkMercenary、NpcMercenaryController | 新實體 | V-T | — | 未開始 |
| P2.11 視覺補完 | 外牆裝飾變體門檻、角色影子、P1.8 繪製 | CreateWall、shadow2 | 渲染 | V-S | — | 未開始 |
| P2.12 一般房清場獎勵 | GetRoomReward | P1.9 | GameScene | V-T | T-10 | 未開始 |
| P2.13 CreateMap 對照 | 房間數語意 | game_full.c CreateMap | MapManager | V-R | T-17 | 未開始 |

### P3 戰鬥忠實度
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P3.0 單位換算稽核 | 子彈 15 px/單位 vs 地圖 32 | RGBullet 速度、預製件 PPU | 統一常數或證明 | 「子彈飛 N 格秒數」測試 | T-25 | 未開始 |
| P3.1 Buff 本體 | Fire/Poison/Ice/Electric/Armor/ChangeSpeed | BuffXXX__*、buffs.json | combat/Buff*.cpp | 每種 buff 時序測試（固定 dt） | — | 未開始 |
| P3.2 Buff 觸發 | 子彈預製件 *BuffTrigger → bullets.json `buff` | 預製件元件 | 資料欄＋sim 掛接 | V-D 5 顆、V-H 命中 log | — | 未開始 |
| P3.3.c03…c13 | 每角一切片 | CharSkillC0N__* | combat/CharSkillC0N.cpp | 每技能 4 斷言（觸發／冷卻／效果量／結束） | — | 未開始 |
| P3.4 接觸傷害 | AI04/08/13、boss 撞擊、無敵幀 | RGEController__GetHurt | sim | V-T | — | 未開始 |
| P3.5.bossNN | 每 boss 一切片（Fan 近似→真表） | BossAI0N__ChooseAttack | sim | golden hash 前後 | — | 未開始 |
| P3.6.gunXX | 每射擊模式一切片（25/46） | Gun*__* | WeaponBrain | WeaponBrainTest | T-22 T-23 | 未開始 |
| P3.7 特殊 AI | AI05、AI06 召喚、AI12 複合 | 對應類別 | sim | V-T | T-24 | 未開始 |
| P3.8 精英 | ex_* 數值、badass_rate | EnemyMaker 私有欄位 | 資料 | V-D | T-03 | 未開始 |
| P3.9 雕像祝福 | 5 種 | BuffStatue01..04 | GameScene/sim | V-T | T-14 | 未開始 |

### P4 進度與經濟循環
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P4.1 掉落經濟 | 掉落、磁吸、HUD | pickups.json、enemies.csv | EnemyReward、CoinPickup | H4、EnemyRewardTest | T-18 T-19 | 功能完成・未驗收 |
| P4.2 寶石 | 來源與 `PlayerSaveData.gems` | UIStatement.AddGem | RunStats/存檔 | V-T | — | 未開始（依賴 P5.3） |
| P4.3 復活／重試 | UIWindowReborn、window_try_again | 預製件、i2 | 場景 | V-H | — | 未開始（依賴 P4.2） |
| P4.4 buff 三選一 | 24 Buff_info、buff_* 旗標 | i2、RGGameProcess | UI＋RunState | V-T | — | 未開始（依賴 P3.1） |
| P4.5 16 層與勝利 | G1 r_end、Victory | map_G1、r_end | 場景 | H 案例 | — | 未開始 |
| P4.6a 結算畫面 | Statements 場景 | Statements.unity | EndScene | H5、V-S | T-20 | 功能完成・未驗收 |
| P4.6b 統計存檔 | StatisticData | 反編譯 | 存檔 | V-T | — | 未開始（依賴 P5.3） |

### P5 大廳
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P5.1 可行走大廳 | HeroRoom 進地牢 | HeroRoom.unity | 場景 | V-P | — | 未開始 |
| P5.2 角色解鎖／皮膚 | 寶石購買 | PlayerSaveData | UI | V-T | — | 未開始 |
| P5.3 本地存檔 | PlayerSaveData/RGSaveManager | 反編譯 | 存檔模組 | V-T（往返） | — | 未開始 |
| P5.4 花園／寵物（=P7.1）／許願池 | ItemPlant、Pet*、ItemWishingWell | 預製件 | 場景 | V-T | — | 未開始 |
| P5.5 練習房／教學 | UITeaching | 場景 | V-P | — | 未開始 |

### P6 UI 與表現
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P6.1 入口流程 | 主選單、選角 | title_en0、i2 | Menu/CharSelect | H1、V-S | — | 功能完成・未驗收 |
| P6.2 武器資訊 | weapon_icon、consume | UICanvas | HUD | V-S | — | 未開始 |
| P6.3 小地圖 | UIMap | GetMinimapSprite | HUD | V-S | — | 未開始 |
| P6.4 buff 列／boss 血條 | UIBuffBar、bossNN_info | 預製件 | HUD | V-S | — | 未開始 |
| P6.5 暫停／設定 | UIWindowPause | 預製件 | 場景 | V-H | — | 未開始 |
| P6.6 傷害數字／hit-stop | ShowTextHurt | 反編譯 | HUD | V-S | — | 未開始 |
| P6.7 BGM／底色 | bgm_clip、camera_bg | levels.json | 音訊 | V-P | — | 未開始 |
| P6.8 開場／劇情／載入 | Splash/Plot/Loading | 場景檔 | 場景 | V-P | — | 未開始 |

### P7 內容補完
| 子階段 | 目標 | 狀態 |
|---|---|---|
| P7.1 寵物（=P5.4） | Pet* | 未開始 |
| P7.2 傭兵（=P2.10） | — | 未開始 |
| P7.3 無盡模式 | EndlessMapManager | 未開始 |
| P7.4 季節 boss | 非目標 | 不做 |

### P8 擴充管線
| 子階段 | 目標 | 驗證 | 狀態 |
|---|---|---|---|
| P8.0 能力邊界 | 只改 JSON 可做「既有行為的變體」（新武器＝既有射擊模式＋數值＋貼圖；新敵人＝既有 brain＋數值＋幀）；新行為需 C++ brain | 文件 | 已寫明 |
| P8.1 素材替換 | 換高版本 ExportedProject 重跑 `tools/extract_*.py` | V-S 全案例重跑 | 未開始 |
| P8.2 新增範例 | 各新增一把武器、一種敵人、一間房，只動 JSON/PNG（P3 完成後提前做一次） | V-T | 未開始 |
| P8.3 文件 | CONVENTIONS.md 資料驅動新增流程 | Codex | 未開始 |

---

## 4. 執行順序

1. **驗收補證據**（本版開始）：P1.6/P2.x/P4.1 依 §1 補測試報告、H 案例 log、樣本表，逐階段轉「驗收完成」。
2. **P3.0 單位稽核**。
3. **P1.10 升版** → **P3.1 → P3.2** buff。
4. **P3.3.x** 技能逐角。
5. **P5.3 最小存檔 → P4.2 → P4.3 → P4.4**。
6. **P3.4、P3.5.x、P3.6.x**。
7. **P5、P6、P2.10–P2.13** 穿插。
8. **P8.2** 在 P3 完成後提前做一次；其餘 P8 最後。

---

## 5. 每個切片的固定交付格式

1. commit：`<階段代號>: <一句話>`；內文列 FAITHFUL / INFERRED / PORT CHOICE / NOT PORTED（附 T-ID）。
2. 程式碼：`// FAITHFUL: … @ game_full.c:<line>`、`// INFERRED (T-xx): …`、`// PORT CHOICE T-xx: …`。
3. 測試：至少一個 GoogleTest 斷言被移植的行為；資料變更過 `validate_data.py`。
4. 證據：ctest 報告行、H 案例 log、截圖＋對照數值，全部進 §10。
5. 本文件：更新 §2、§3、§6、§9、§10。

---

## 6. 取捨與不可恢復登記（程式碼註解引用 T-ID）

| ID | 項目 | 標籤 | 位置 | 影響 | 補完階段 |
|---|---|---|---|---|---|
| T-01 | 章節變體 A/B 由 runSeed 決定 | PORT CHOICE | RunState.hpp `BiomeVariantFor` | 每章生態 | 需原碼 |
| T-02 | 地板變體取分類擲值否則常駐擲值 | INFERRED | RoomGen.cpp CreateFloor | 地板花紋 | 需原碼 |
| T-03 | 精英率 stage×3% | PORT CHOICE | GameScene.cpp（EnemyMaker 區） | 精英數量 | P3.8 |
| T-04 | 生成上限 24、consume ≥1、生成點分布 | PORT CHOICE | EnemyMaker.hpp PlanRoomSpawns、GameScene SpawnNextWave | 敵人數與位置 | P2.3 |
| T-05 | 大房加級：兩維各 >15/>21 各 +1，level 0 不加；原碼另有房旗標（RGRoomX+0x28）與增量本體截斷 | INFERRED | EnemyMaker.hpp `EnemyMakerLevel` | 敵人預算 | P2.3 |
| T-06 | 房間角色按距離指派 | PORT CHOICE | GameScene.cpp 房間角色區 | 特殊房位置 | P2.13 |
| T-07 | boss 寶箱與傳送門重疊時下移兩格 | PORT CHOICE | GameScene.cpp boss 死亡區 | 視覺 | — |
| T-08 | r_chest / r_sell 由 floorSeed 奇偶決定 | PORT CHOICE | GameScene.cpp 房殼區 | 獎勵房類型 | 需原碼 |
| T-09 | 獎勵箱 GetPot/GetCoin 分支、ChestXmlReader | NOT PORTED | GameScene 寶箱開啟 | 獎勵箱只出武器 | P1.9 |
| T-10 | 一般房清場獎勵 GetRoomReward | NOT PORTED | RGRoomX ClearRoom log | 無清場獎勵 | P2.12 |
| T-11 | 商店機器擺位一排 | PORT CHOICE | GameScene 商店區 | 視覺 | 需原碼 |
| T-12 | 飲水機＝免費補滿能量一次 | PORT CHOICE | GameScene Purchase | 能量 | 需原碼 |
| T-13 | 事件房只在雕像／礦物間選（傭兵未做） | PORT CHOICE + NOT PORTED | GameScene 事件房區 | 傭兵缺席 | P2.10 |
| T-14 | 雕像 5 種祝福未移植；index→名字為 i2 順序推論 | NOT PORTED / INFERRED | GameScene skill_use、kStatueName | 祝福 | P3.9 |
| T-15 | 礦物掉 10 銅幣（mineral_obj 未解析） | PORT CHOICE | GameScene 礦物房 | 經濟 | P1.9 |
| T-16 | shadow0 不畫（需乘法材質） | PORT CHOICE | GameScene 影子區 | 視覺 | P6 |
| T-17 | 走法房間數 map_long+4 | PORT CHOICE | GameScene.cpp mapOpt | 樓層大小 | P2.13 |
| T-18 | reward 門檻比較與 reward_value[4] 語意（原碼在 Range 後截斷） | INFERRED | EnemyReward.hpp、GameScene SpawnReward | 掉落 | game_typed.c 交叉 |
| T-19 | 掉落散射力度 6 units/s（其餘數值已入 pickups.json） | PORT CHOICE | GameScene `kScatterImpulseUnits` | 手感 | 需原碼 |
| T-20 | 結算畫面版面用移植座標；藥水／寶石欄為 0 | PORT CHOICE | EndScene.cpp | 視覺 | P4.2 |
| T-21 | 設計房座標以 (20,20) 為中心換算 | INFERRED | extract_design_rooms.py | 布局 | — |
| T-22 | Gun008 的 3/0.1 解讀為次數／間隔 | INFERRED | Gun008.hpp | 該武器 | P3.6 |
| T-23 | Gun017Child 追蹤無人機未移植 | NOT PORTED | Gun017.hpp | 該武器 | P3.6 |
| T-24 | RGAisle、EnemyAI15 只移植純邏輯，效果交場景層 | 部分 | RGAisle.hpp、EnemyAI15.hpp | 需登記接線位置 | P3.7 |
| T-25 | 子彈 15 px/單位 vs 地圖 32 px/單位 | 未定 | SimConfig.hpp、GameScene kCellPx | 子彈速度 | P3.0 |
| T-26 | Ghidra 單例呼叫後截斷（系統性） | — | game_full.c | 多處 INFERRED | game_typed.c / frida |
| T-27 | ex_orc05 在 Assets/GameObject，enemies.csv 缺列；場景以 AI01 代表種替代；資料閘唯一豁免 | NOT PORTED（資料缺列） | validate_data.py、GameScene SpawnEnemy | A2–A5 精英池 | P1.10 |
| T-28 | boss 房門帶外物件（r2_boss3/4 的 rail，y=21）由抽取器丟棄並記錄 `dropped_outside` | PORT CHOICE | build_gamedata.py build_boss_rooms | 兩房各少數條軌道 | P2.11 |

---

## 7. Codex 審查清單（每條：通過 / 不通過 / 不適用 ＋ 證據）

| # | 項目 | 證據要求 |
|---|---|---|
| 1 | 來源可追溯：FAITHFUL 只覆蓋原碼可見部分 | 抽 3 處到 game_full.c 行號 |
| 2 | 資料驅動：本應在 JSON 的數值不在 C++（例外：幾何常數、鉤子） | grep 常數 |
| 3 | 測試：斷言被移植行為；golden 未變或有理由 | ctest 報告行；golden 測試在套件內（全綠即未變） |
| 4 | 無頭證據：§1.1 案例可重現 | §10 實跑 log |
| 5 | 視覺：尺寸／位置對得上預製件 | §10 截圖＋數值 |
| 6 | 取捨登記：註解有 T-ID、§6 有對應 | grep `T-` 對 §6 |
| 7 | 回歸：ctest 全綠、validate_data 綠、無 `--no-verify` | §10 報告行；commit 皆經 pre-commit（倉庫無 `--no-verify` 紀錄） |
| 8 | 文件：§2/§3/§6/§9/§10 同步；狀態值合法 | 目視 |

---

## 8. 維護規則

- 本文件與程式碼在**同一 commit** 更新。Claude：實作、§2/§3/§6/§10；Codex：§7 審查、§9 結論；文件與程式不符時 Codex 可直接修文件並註明。
- 雙方不得刪除對方紀錄；分歧在 §9 該筆下「回覆」討論，使用者裁決。
- 版本：Codex 通過 → 小版號 +1；新增階段或改順序 → 中版號 +1。
- Codex 執行：`CODEX_HOME` 指向不含 MCP 的臨時設定（本機 unityMCP 未啟動會讓 codex 中止）；提示以 stdin 餵入；codex-cli ≥ 0.154（帳號預設模型 gpt-6-astra）。

---

## 9. 審查紀錄

### R1 — 2026-09-12 — Codex — 對象 v1.0 @ a3bb557 — **不通過**
逐條：1 ✗ FAITHFUL 超出原碼（GetReward）；2 ✗ 掉落數值、磁吸、動畫在 C++；3 ✗ 分波時序未測、golden 未證；4 ✗ 無可重現案例；5 ✗ 無對照；6 ✗ 漏 10 項；7 ✗ 僅 validate_data 實跑；8 ✗ 狀態值不合法、TEST 數 2009 非 2013。
抽查：P1.6 資料有支持但驗收不足；P2.3 不支持完成（`game_full.c:682128`）；P4.1 功能有支持、取捨寫少。
**回覆（v1.1）**：資料版本矩陣、H1–H7、樣本表、功能／驗收分欄、六欄（P1）、T-01…T-28、§7 加證據欄；資料閘擴充（P1.11，抓到 T-27/T-28）；GetReward 註解改 INFERRED。

### R2 — 2026-09-12 — Codex — 對象 v1.1 @ 8cb1e61 — **不通過**
R1 要求落實度：矩陣部分（fallback 未印）；資料閘部分（未知引用全降警告）；H 案例部分（`…`、無實跑 log、H3 只殺一次）；分欄部分（§3 未一致）；六欄僅 P1；去重與 P8 邊界已落實；T-ID 未寫進註解。
§7：1 ✗（EnemyMaker.hpp:41 仍標 FAITHFUL，與 682128–152 不符）；2 ✗（EnemyReward.hpp 5/3/1/8、CoinPickup 數值）；3 ✗；4 ✗；5 ✗（rail 移除無截圖）；6 ✗；7 ✗（無本版 ctest）；8 ✗（HEAD、P2.14、P2.3 不一致）。
**回覆（v1.2）**：
- §0：`build_gamedata.py` 對每張未產生的表印 `FALLBACK:`；來源路徑改為 `../_reverse`。
- P1.11：未知敵人引用改為錯誤，唯一豁免 `ex_orc05`（T-27）；pickups 進閘。
- P1.12：新 `pickups.json`（value/speed/range/frames/friction 來自 res/register 預製件與 coin_0.controller）；`EnemyReward` 改讀 `RewardValues`；`CoinPickup` 改吃 `PickupDef`；剩 T-19 散射力度為 PORT。
- T-05：`EnemyMakerLevel(stage, w, h)` 兩維、level 0 不加、標 INFERRED；測試更新。
- T-ID 補進所有註解（GameScene ×7、RunState、EnemyReward ×2、EnemyMaker、extract_design_rooms）。
- 鉤子：`SK_SEED`、`SK_FORCE_KILL_EVERY`；H3 改為重複擊殺；§1.1 給完整命令與 shell 說明；§10 附實跑 log 與 r2_boss3 截圖。
- §2/§3：全部六欄、狀態詞彙修正、HEAD 改由 §10 報告承載。
- 未做（誠實列出）：分波時序純測試（P2.3 仍進行中）；Gun008/Gun017/RGAisle/EnemyAI15 檔頭的 T-ID 待下一切片一併補。

---

## 10. 證據附錄（每版最新一次實跑，由 `scratchpad/r2_chain.sh` 產生）

- 對象：工作樹 = 8cb1e61 + 本版變更（提交後 HEAD 見 git log，commit 訊息 `P1.12/R2: …`）。
- 測試報告：`100% tests passed, 0 tests failed out of 2009`（ctest -C Debug -E Tests_NOT_BUILT；golden 測試含在內，全綠＝未變）。
- 資料閘：`GATE: GREEN -- 0 errors, 8 warning(s)`。
- 建置：`SoulKnight.vcxproj -> build\Debug\SoulKnight.exe`、`SoulKnightTests.vcxproj -> …\SoulKnightTests.exe`，無 error。
- H1：`GameScene: hero c12 on floor 0` → `Level B1 (1-1) biome 1-ice chest_level 1 map_long 3 pool 5 species` → `Floor 0 CLEARED -> snapshot hp=5 armor=3 energy=130 weapon='Gun001' (next floor 1)` → `GameScene: hero c12 on floor 1` → `Level B2 (1-2) … pool 10 species` → `Floor 1 CLEARED` → `hero c12 on floor 2` → `Level B3 (1-3) chest_level 2`。
- H2：`GameScene: hero c01 on floor 0` → `TransferGate: open in room 4 at (-2624,-1312)` → `TransferGate: entered -> next floor` → `GameScene: hero c01 on floor 1` → `TransferGate: open in room 4 at (2624,1312)` → `entered -> next floor`；反例（無 SK_GATE_TELEPORT）`GameScene: hero` 出現 1 次。
- H3：`EnemyMaker: room 1 (type 1) level 3 points 25 -> 24 planned, wave size 9`（21×21 房：stage 1 + 兩維各 +1 = level 3，T-05）→ `RGRoomX: room 1 StartRoom -> Active (door_open=0, sealed)` → `EnemyMaker: room 1 part spawned 9 (15 pending)` → `part spawned 9 (6 pending)` → `part spawned 6 (0 pending)` → `RGRoomX: room 1 cleared -> reward gate fired (room_type==1)`。
- H4：`Reward: e_miner02 dropped 2 pickups (coins 1 energy 8)` → `Reward: ex_miner03 dropped 6 pickups (coins 4 energy 16)` → `Reward: e_yeti01 dropped 2 pickups (coins 1 energy 8)` ×2 → `Reward: e_miner01 dropped 2 pickups`（`Pickup:` 行需玩家走到磁吸範圍內，本次 head -6 內未出現；§1.1 預期改為 `Reward:` 為必要、`Pickup:` 為 SK_AUTOWALK 可選）。
- H5：`Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1 -- hero c01 time 0:00:03 kills 0 coins 0 gems +0`；截圖 evidence/H5.png。
- H6：`Shop: vending_machine_i at room 5 sells 'health_pot_big' for 10 coins` → `Shop: water_dispenser at room 5 sells '' for 0 coins` → `Shop: vending_machine_w at room 5 sells 'weapon_010' for 12 coins` → `Shop: bought health_pot_big for 10 (+4 hp, +0 energy) -> hp 6 energy 180`。
- H7：`Level B5 (1-5) biome 1-ice chest_level 2 map_long 4 pool 15 species` → `BossRoom: room 6 uses r1_boss4 (21x21, 27 objects)` → `Boss defeated!` → `Chest: boss chest chest_boss13 (tier 2, 1 signature weapon(s))` → `TransferGate: open in room 6 at (1312,-3936)`。
- V-S r2_boss3：`SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_MAX_FRAMES=245 SK_SHOT=r2_boss3.png`；log `Level D5 (2-5) biome 2-graveyard chest_level 4 map_long 4` → `BossRoom: room 3 uses r2_boss3 (21x21, 24 objects)`。對照 boss_rooms.json：r2_boss3 w 21 h 21，objects = box03 ×16、rail ×4（y=0）、obj22_stone ×4，`dropped_outside` 4（rail y=21）；r2_boss4 `dropped_outside` 6。截圖 evidence/r2_boss3.png：box03 依 (3,2)(3,3)(3,18)(3,19)(4,2)… 沿牆內側成排；rail/obj22_stone 為 P1.8 內嵌預製件，尚未繪製（登記 P1.8）。
