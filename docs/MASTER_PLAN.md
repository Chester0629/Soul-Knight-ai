# Soul-Knight-ai 專案架構書（元氣騎士 1.7.10 忠實復刻）

> 本文件是專案的**單一事實來源**：範圍、階段、每階段的資料來源與驗證方式、Codex 審查紀錄。
> 由 Claude（實作）與 Codex（審查）共同維護；規則見 §8。
> **狀態詞彙**（兩欄分開記）：
> 功能欄：`未開始 / 進行中 / 功能完成`。驗收欄：`未驗收 / 驗收完成 / 驗收完成（有取捨）`。
> 「功能完成」＝接線可玩；「驗收完成」＝§1 證據齊全且 Codex 通過。
> 版本：v1.20（2026-09-14；R17 通過 P6.7，小版號 +1）

---

## 0. 目的、範圍、非目標、資料版本

**目的**：在自製的 C++ 遊戲系統（PTSD：SDL2 + OpenGL + ImGui，CMake，GoogleTest）上，以**資料驅動**的方式忠實復刻《元氣騎士》1.7.10（Unity 2017.4.1f1 IL2CPP，armeabi-v7a）的單人地牢循環，作為學術演示。之後要能**用高版本素材替換**並**新增內容**，所以數值、貼圖、布局都必須走「解包資料 → JSON/PNG → 程式讀表」。

**忠實度標籤**（程式碼註解與本文件共用；取捨註解一律附 §6 的 T-ID）：
- `FAITHFUL @ <來源>`：直接來自反編譯（`../_reverse/ghidra_export/game_full.c`；**`game_typed.c` 為同一二進位的帶型別版本，欄位以名字出現，較可讀，引用時寫明檔名**；皆在倉庫外的同層目錄）、AssetRipper 預製件（`../_reverse/asset soul/ExportedProject`）或資料表（`../_reverse/analysis/data_tables/*.csv`）。**只有原碼可見的部分**才能標 FAITHFUL；Ghidra 截斷處之後一律 INFERRED。
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
| levels / boss_rooms / boss_chests / shop（pots/machines/**statues**）/ pickups.json | `res/map_manager`、`rgprefab/rooms`、`res/item`、`rgprefab/room/event`、`rgprefab/room/statue`、`res/register` | 1.7.10 | 不產生檔案；執行期：levels 缺→固定 ice 路徑；boss_rooms 缺→借 r1_* 布局；boss_chests/shop 缺→該房無內容並 log；**pickups 缺→掉落值全 0＝不掉落**（GameData log `pickups.json absent`） | 1.7.10 |
| bullets（25 類別列＋178 預製件列）/ bosses（16）/ buffs（12）/ enemy_guns（10）.json | 類別列：`<Class>.cs.meta` guid → 路徑序第一個帶該元件的 1.7.10 預製件（T-30；bullet 類別列於 v1.9 起同樣解析：18/25 解析、7 筆（BulletBuleLight/CoinExplode/EPoly/Fire/Gas/GreenLight/Polymerization）無預製件保留 1.07 並以 `_source` 標明）；bullets 另有每預製件列（178/256，`rgprefab/bullet`） | 1.7.10／少數 1.07（每筆 `_source`） | 退回 `1.07/MonoBehaviour` 類別表並印 FALLBACK | 1.7.10（P1.10） |
| droptables / weapon_catalog.json | `EP/Resources/chest_data.txt`、`weapon_introduce.txt`（與 1.07 逐位元相同） | 1.7.10 | 退回 `1.07/TextAsset` | 1.7.10（P1.10） |

**降級條件**：整表退化只在 `../_reverse` 缺席的乾淨 checkout 觸發；另有「來源存在但該類別無任何 1.7.10 預製件」的逐筆例外（bullets 7 筆類別列：BulletBuleLight/CoinExplode/EPoly/Fire/Gas/GreenLight/Polymerization，以 `_source: "1.07"` 標明，補完屬 P1.13），`build_gamedata.py` 對每張退化或未產生的表印 `FALLBACK: <table> …`（weapons、enemies、characters、sprites、levels、boss_rooms、boss_chests、shop、statues、pickups、bullets、bosses、buffs、enemy_guns、droptables、weapon_catalog 十六處）；標「驗收完成」的階段其表必須是 1.7.10 來源。

**非目標**（不列入階段表）：多人連線、IAP／廣告、雲端存檔、Google Play 成就、季節 boss（Nian）。
**法律框架**：解包素材屬 ChillyRoom，僅供課堂演示；`Resources/sprites` 已列入 `.gitignore`。

---

## 1. 驗證體系

| 代號 | 方式 | 指令 / 位置 | 用途與限制 |
|---|---|---|---|
| V-T | GoogleTest | `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug` → `cmake --build build --config Debug --target SoulKnightTests` → `ctest --test-dir build -C Debug -E Tests_NOT_BUILT` | 每次驗收附**測試報告行**與 commit（§10）。基準（a3bb557）：`grep -c "^TEST(" test/*.cpp` = 2009 巨集 / 141 檔；執行數以 ctest 輸出為準。 |
| V-G | 資料閘 | `python tools/validate_data.py`（pre-commit 亦執行；非 ASCII 檢查在 pre-commit hook） | 檢查 weapons/bullets/enemies/bosses/buffs/catalog/droptables/characters + levels（31、6 地板、12 障礙、敵池引用；**未知 id 為錯誤，無豁免**（T-27 已關）、boss_rooms（14、界內、皮膚、boss 層房存在）、boss_chests、shop（pots/machines/statues：7 座、cost>0、index 唯一、貼圖存在）、pickups（四種皆有 value/speed/range/frames）、六張升版表的 `_source` 標記與 weapon→bullet／boss→bullet／enemy_class→bosses 交叉引用（**只有 `KNOWN_UNRESOLVED_BULLET_REFS` 清單內的引用降為 warning，清單外一律錯誤**；邊界由 `tools/tests/test_validate_data.py` 以注入假引用驗證，見 T-31）。 |
| V-D | 資料對照 | §1.2 固定樣本 | 每個資料階段至少 5 筆「來源:欄位 → 預期 → JSON」＋全表檢查。 |
| V-H | 無頭鉤子 | §1.1 案例（命令、seed、幀數、預期 log、逾時） | 只證明路徑可達與 log 順序；不證明正常遊玩（見 V-P）。實跑 log 存 §10。 |
| V-P | 實玩紀錄 | 人工遊玩＋截圖／錄影＋seed | 「可玩」驗收必附。 |
| V-S | 截圖對照 | `SK_SHOT`；1280×720、zoom 1.5、預設 seed 20240607（或 `SK_SEED`）、指定樓層房間 | 每張旁列預製件數值。 |
| V-R | 反編譯對照 | `// FAITHFUL: <Class>__<Method> @ game_full.c:<line>` | 截斷處後不得標 FAITHFUL。 |
| V-C | Codex 審查 | §7 | 每條「通過／不通過／不適用＋證據」。 |

### 1.1 無頭鉤子與可執行案例
鉤子（env）：`SK_SEED=N`、`SK_MAX_FRAMES`、`SK_SHOT`、`SK_SKIP_MENU`、`SK_START_CHAR`、`SK_CHAR`、`SK_MENU=start`、`SK_FORCE_CLEAR=K`、`SK_FORCE_DIE=K`、`SK_FORCE_KILL=K`、`SK_FORCE_KILL_EVERY=N`、`SK_FORCE_SKILL=K`、`SK_ENDSCENE=restart|quit|menu`、`SK_GATE_TELEPORT=K`、`SK_GATE_BESIDE=K`、`SK_ROOM_TELEPORT=K,R`（R 無生成點且為出口房時傳到房中央）、`SK_SHOP_BUY=K`、`SK_SHOP_MACHINE=N`、`SK_STATUE_WORSHIP=K`、`SK_COINS=N`、`SK_EVENT_ROOM=statue|mineral`、`SK_ZOOM=<float>`（整房截圖用，覆蓋鏡頭 1.5）、`SK_FORCE_BUFF=K,<kind>`（第 K 幀對玩家上 buff）、`SK_FORCE_WEAPON=<weapon id>`（開局裝備）、`SK_FORCE_HIT=K`（自第 K 幀起持續朝最近敵人開火）、`SK_MUTE=1`（靜音但仍印 `Audio:` log）、`SK_AUTOWALK`（不穩）、`SK_AUTOFIRE`。
`GameScene::m_Frame` 每層重新計數。**Shell**：命令以 Git Bash 寫（`VAR=x ./build/Debug/SoulKnight.exe`）；PowerShell 等價寫法 `$env:VAR="x"; .\build\Debug\SoulKnight.exe`。實跑腳本：`tools/evidence_chain.sh <版本>`（輸出入 `docs/evidence/<版本>/`，§10 引用；每案逐一檢查退出碼、逾時與 PNG 大小，任一失敗腳本以非零退出）。

| 案例 | 命令（Git Bash，倉庫根目錄，Debug 建置） | 預期 log 順序 | 逾時 |
|---|---|---|---|
| H1 入口流程 | `SK_START_CHAR=c12 SK_FORCE_CLEAR=60 SK_MAX_FRAMES=200 ./build/Debug/SoulKnight.exe` | `GameScene: hero c12 on floor 0` → `Level B1 (1-1)` → `Floor 0 CLEARED` → `hero c12 on floor 1` → `Level B2`（`SK_START_CHAR` 由 main.cpp 直接開局，不經 CharSelectScene；選角畫面的 log 用 `SK_MENU=start SK_CHAR=c12` 驗）| 60 s |
| H2 傳送門 | `SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400 ./build/Debug/SoulKnight.exe`；反例同命令去掉 `SK_GATE_TELEPORT` | `TransferGate: open in room N` → `TransferGate: entered -> next floor` → `hero c01 on floor 1`；反例 `GameScene: hero` 只出現 1 次 | 60 s |
| H3 分波與封鎖 | `SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=90 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe` | 實際順序：`EnemyMaker: room 1 part spawned 9 (15 pending)`（首波在 StartRoom 內先生成）→ `EnemyMaker: room 1 (type 1) level 3 points P -> 24 planned, wave size 9` → `RGRoomX: room 1 StartRoom -> Active` → 每次整波死亡後**立即**下一波 `part spawned 9 (6 pending)` → `part spawned 6 (0 pending)` → `RGRoomX: room 1 cleared`（FAITHFUL：EnemyDead → CreatePart 無延遲，見 T-29） | 90 s |
| H4 掉落經濟 | `SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe` | `Reward: <id> dropped N pickups (coins a energy b)` 至少 3 行（必要）；`Pickup: coin +1`／`Pickup: energy +8` 需玩家進入磁吸範圍（加 `SK_AUTOWALK=1`，可選） | 90 s |
| H5 結算 | `SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT=H5.png ./build/Debug/SoulKnight.exe` | `Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1 …` | 60 s |
| H6 商店 | `SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130 ./build/Debug/SoulKnight.exe`（預設 seed 第 0 層獎勵房為商店） | `Shop: vending_machine_i … sells` → `Shop: bought <pot> for 10` | 60 s |
| H7 boss 層 | `SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=447 ./build/Debug/SoulKnight.exe` | `Level B5` → `BossRoom: room N uses r1_bossK` → `Boss defeated!` → `Chest: boss chest chest_bossNN` → `TransferGate: open` | 120 s |
| H8 雕像讀表 | `SK_SKIP_MENU=1 SK_EVENT_ROOM=statue SK_COINS=30 SK_STATUE_WORSHIP=100 SK_MAX_FRAMES=130 ./build/Debug/SoulKnight.exe` | `Statue: worshipped statue_0N (<name>) -> statue_index N`（貼圖／價格來自 shop.json statues） | 60 s |
| H9 buff 本體 | `SK_SKIP_MENU=1 SK_FORCE_BUFF=30,fire SK_MAX_FRAMES=200 ./build/Debug/SoulKnight.exe`；護甲例 `SK_FORCE_BUFF=30,armor SK_MAX_FRAMES=600`（6 s 需 600 幀） | `Buff: Fire applied to 61453 for 2.000000s` → `Buff: Fire ended on 61453`（61453 = 玩家 view id；持續 2 s 來自 buff_fire.prefab buff_time） | 60 s |
| H10 buff 觸發 | `SK_SKIP_MENU=1 SK_FORCE_WEAPON=weapon_013 SK_AUTOWALK=1 SK_FORCE_HIT=0 SK_MAX_FRAMES=600 ./build/Debug/SoulKnight.exe`（`SK_FORCE_HIT=K`＝從第 K 幀起持續瞄準最近敵人開火） | 火焰子彈命中 → `Buff: Fire applied to <enemy id> for 2.000000s` → `Buff: Fire ended on <id>`（腳本斷言 ended 必出現；weapon_013 的 bullet 帶 RGBulletBuffTrigger→buff_fire） | 90 s |
| H11 音效 | `SK_SKIP_MENU=1 SK_MUTE=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=300 ./build/Debug/SoulKnight.exe` | `Audio: bgm bgm_<biome>Low.ogg` → 擊殺 `Audio: sfx enermy_die*.wav`；加 `SK_AUTOWALK=1 SK_AUTOFIRE=1 SK_FORCE_HIT=30` 另見 `Audio: sfx fx_gun_*.wav`（開火）；`fx_hit.wav`（effect 5）於玩家、敵人或 boss 受擊時播放 | 60 s |
| V-S 整房 | `SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_ZOOM=0.6 SK_MAX_FRAMES=245 SK_SHOT=r2_boss3_full.png ./build/Debug/SoulKnight.exe` | 21×21 boss 房整房入鏡；對照 boss_rooms.json 物件座標 | 120 s |

### 1.2 固定對照樣本（V-D）
- enemies：boss01 max_hp=600；boss05=800；e_succulent=8；e_slime01 reward_rate=15；boss03 reward_value=[5,3,0,10]。
- characters：c01 max_armor=5, energy=180, atk=4, skill_cd=10；c12 max_hp=6（`RoleAttributePlayer.json` 單檔是 Druid，不可當模板）。
- weapons：weapon_000 atk=3 consume=0 bullet_speed=36 item_value=12；weapon_043 atk=15 consume=5。
- levels：B1 (1-1, chest 1, map_long 3, f601..f606, wall602/wall601)；B5 type2=[r1_boss3,r1_boss4,r1_boss6]；C1 (2-1, chest 3, f201..)；G1 (3-6, map_long 1, type2=[r_end])。
- boss chests：chest_boss01 level 2 weapon_077；chest_boss13 weapon_175；無 chest_boss05/06/11/12。
- pickups：coin_0 value 5 speed 20 range 6 frames objects_common_31..34；coin_1 3；coin_2 1；energy value 8 speed 14；_rgcoin.friction 0.9。
- P1.10（詳 `docs/evidence/p1_10_diff.md`）：`GameObject/ex_orc05.prefab` RoleAttribute.max_hp=50、EGun001.bullet_speed=16（1.07 為 10）；`res/effect/buff_statue_1.prefab` BuffStatue01.speed=12、count=4（1.07 為 24、1）；`res/enemy/1-forest/boss07.prefab` BossAI07.bullet01=bullet_e_24；`rgprefab/bullet/bullet_0.prefab` RGBullet.destroy_time=5；`res/enemy/e_battery01.prefab` EGun008.atk=5。
- statues：7 座 statue_01..07，item_value 皆 15；底座 objects3_6、光 objects3_7 @ y 0.69375；身體 1→objects3_0、2→objects3_3、3→objects3_2、4→objects3_1、5→objects3_4、6→objects3_5、7→objects3_3 @ y 1.1。

---

## 2. 現況總表（截至 2026-09-12，分支 `feat/phase3-presentation-content`，HEAD 見 §10 最新報告）

| 區塊 | 功能 | 驗收 | 主要 commit | 缺口 |
|---|---|---|---|---|
| P1 資料層 | 進行中（P1.1–P1.7、P1.10–P1.12 功能完成） | P1.10 驗收完成（有取捨）；其餘未驗收 | 12c3750 … 本版 | P1.8 內嵌物件、P1.9 未解析掉落物、P1.13 每實例子彈／敵槍表（T-30、T-31） |
| P2 地圖 | 進行中（P2.1–P2.9 功能完成） | 未驗收 | 3eded10 … 本版 | P2.10–P2.13；T-05 加級本體待原碼 |
| P3 戰鬥忠實度 | 進行中（每擊數學、dash、AI 派發、18 boss brain、P3.1 buff 本體、P3.2 buff 觸發功能完成；P3.0 稽核完成待裁決） | P3.2 驗收完成（有取捨）；其餘未驗收 | phase 1–5、本版 | P3.0 裁決、P3.3–P3.9 |
| P4 進度循環 | 進行中（P4.1、P4.6a 功能完成） | 未驗收 | 5c8c4b0、1cfb297 | P4.2–P4.5、P4.6b |
| P5 大廳 | 未開始 | 未驗收 | — | 全部 |
| P6 UI 表現 | 進行中（P6.1、P6.7 功能完成） | P6.7 驗收完成（有取捨）；P6.1 未驗收 | 6e46eed、本版 | P6.2–P6.6、P6.8 |
| P7 內容補完 | 未開始 | 未驗收 | — | 全部 |
| P8 擴充管線 | 進行中（P8.0 文字完成；P8.1–P8.3 未開始） | 未驗收 | — | P8.1–P8.3 |

---

## 3. 階段規劃（細分；七欄＝目標／來源／交付／驗證／取捨＋狀態）

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
| P1.8 內嵌物件 | rail/stone02/carpet/obj22_house/table/stone | *_boss*.prefab 子物件 m_Sprite | boss_rooms.json prefabs | BossRoomTest | — | 未開始・未驗收 |
| P1.9 未解析掉落物 | mineral_obj、GetRoomReward、ChestXmlReader、GetPot/GetCoin | 反編譯（game_typed.c 交叉） | 對應 JSON | V-R | T-09 T-10 T-15 | 未開始・未驗收 |
| P1.10 升版 | 六表改由 AssetRipper 產生；補 ex_orc05（146 種）、撤資料閘豁免 | `<Class>.cs.meta` guid → 預製件 m_Script；rgprefab/bullet、res/enemy/**、rgprefab/buff、res/effect、GameObject/ex_orc05、Resources/*.txt | 同名 JSON（每筆 `_source`）、`docs/evidence/p1_10_diff.md` | V-D 五筆樣本＋全表計數 diff；GameDataTest（bullets 203、enemies 146、EGun001 speed 16）；V-G GREEN 6 warnings | T-27 已關；T-30 類別列取路徑序第一個實例；T-31 78 顆非 RGBullet 家族子彈；59 個武器與 18 筆 boss 子彈引用未解析（清單化） | 功能完成・**驗收完成（有取捨 T-30/T-31）**（R10 @ 41c9172） |
| P1.13 每實例表 | 敵人各自的槍／子彈值（不再以類別代表）、非 RGBullet 家族子彈 | 各敵人預製件的槍子物件 | enemy_guns/bullets 每實例列＋ C++ 查表改鍵 | V-D、GameDataTest | T-30 T-31 | 未開始・未驗收 |
| P1.11 資料閘擴充 | levels/boss_rooms/boss_chests/shop/pickups 進閘；未知引用為錯誤 | — | validate_data.py | V-G（GREEN，8 warnings） | T-27 | 功能完成・未驗收 |
| P1.12 數值出 C++ | 掉落值、磁吸 speed/range、摩擦、幀清單 → pickups.json；雕像 cost／貼圖／偏移 → shop.json statues | res/register/coin_*、energy、coin_0.controller；rgprefab/room/statue/statue_01..07 | pickups.json、shop.json statues、GameData::FindPickup/FindStatue | EnemyRewardTest、StatueTest（讀表）、V-G | T-19（散射力度仍 PORT）；剩餘 C++ 常數＝幾何／鉤子 | 功能完成・未驗收 |

### P2 地圖與關卡
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P2.1 關卡驅動 | LevelIdFor、章節變體 | levels.json | GameScene 讀表 | H1、LevelTableTest | T-01 | 功能完成・未驗收 |
| P2.2 生態貼圖 | 六地板變體、牆頂牆面、障礙皮膚、寶箱等級 | levels.json、tiles | 渲染 | V-S（冰／城堡截圖） | T-02 T-16 | 功能完成・未驗收 |
| P2.3 EnemyMaker | 預算表、大房加級、加權池、分波 | game_typed.c StartEnemyMaker 77871–77995、EnemyDead 73138–73185、CreatePart 78030–78084 | EnemyMaker.hpp、WaveScheduler.hpp、GameScene SpawnWaveBatch | H3、EnemyMakerTest、WaveSchedulerTest（9/9/6 分波、整波死亡後立即下一波、房未空不放波、boss 房空計畫即 Done） | T-03 T-04（選敵／扣 consume／生成點：CreateEnemy 截斷）T-05（加級本體截斷）T-29（ShowUpEffect 0.8 s 音效已於 v1.17 接線；字串為推論） | 功能完成・未驗收 |
| P2.4 傳送門 | 走進去換層 | transfer_gate.prefab | GameScene | H2 | — | 功能完成・未驗收 |
| P2.5 比例／牆／箱／走廊 | 48 px/單位、底部錨定 | Scene_Game camera、wall601/box04 | 渲染 | V-S | — | 功能完成・未驗收 |
| P2.6 boss 房布局 | 14 預製件 | boss_rooms.json | 渲染＋碰撞 | H7、V-S（r1_boss4；r2_boss3 見 §10） | T-28 | 功能完成・未驗收 |
| P2.7 房間角色與寶箱 | 出口／獎勵／事件；獎勵箱；boss 箱 | boss_chests.json | GameScene | H7 | T-06 T-07 T-09 T-10 | 功能完成・未驗收 |
| P2.8 商店房 | 三機器、E 購買 | shop.json | VendingMachine | H6、ShopTest | T-08 T-11 T-12 | 功能完成・未驗收 |
| P2.9 門片／影子／事件房 | d1–d5、shadow2、雕像、礦物 | door_2 clips、statue_0N | GameScene | V-S（開／鎖門）、StatueTest | T-13 T-14 T-15 T-16 | 功能完成・未驗收 |
| P2.10 傭兵房（=P7.2） | TalkMercenary 雇傭＋跟隨戰鬥 | TalkMercenary、NpcMercenaryController | 新實體 | V-T | — | 未開始・未驗收 |
| P2.11 視覺補完 | 外牆裝飾變體門檻、角色影子、P1.8 繪製 | CreateWall、shadow2 | 渲染 | V-S | — | 未開始・未驗收 |
| P2.12 一般房清場獎勵 | GetRoomReward | P1.9 | GameScene | V-T | T-10 | 未開始・未驗收 |
| P2.13 CreateMap 對照 | 房間數語意 | game_full.c CreateMap | MapManager | V-R | T-17 | 未開始・未驗收 |

### P3 戰鬥忠實度
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P3.0 單位換算稽核 | 子彈 15 px/單位 vs 地圖 32 | 武器圖集 `spritePixelsToUnits: 16`；Scene_Game ortho 7.5；RGBullet__SetBulletVelocity（stub，T-26） | 稽核結論（§9 R3/R4）：15 無可追溯依據；**32 = 待驗證假設**（圖集 PPU 16 × 2 只證明空間換算，未證明 JSON speed 的單位）；改值影響：子彈／敵人／角色速度、磁吸 range、散射、兩個釘 15 的測試（WeaponBrainTest:248、BrainFactoryTest:66）與 golden | 「子彈飛 N 格秒數」測試 | T-25 | 進行中（稽核完成，待使用者裁決改 32）・未驗收 |
| P3.1 Buff 本體 | Fire/Poison/Ice/Electric/Armor/ChangeSpeed 的附加／計時／生效 | 可見：六個 ctor 寫 is_enemy/buff_time；BuffChangeSpeed__Start → RoleAttribute__ChangeSpeed(changeValue) 呼叫（game_typed.c 86553–86555）；BuffIce__BuffEnd 呼叫 CancelInvoke（86943–86945，只證明取消某排程）；BuffArmor__GetArmor 存在（86492–86503）；BuffFire__GetHurt 86867–86886 無傷害常數；BuffElectric__Start 86760 起。不可見／推論（T-32）：tick 間隔／傷害、電擊長度、護甲量與暫時護甲語意、changeValue 加法性 | include/combat/Buff.hpp（BuffList：Apply 刷新不疊加、Tick 固定 dt）、Simulation::ApplyBuff/TickBuffs、buffs.json 每列 `_inferred{tick_sec,tick_damage,speed_mul,armor_delta,_why}`、SK_FORCE_BUFF | BuffTest 33 例（六種各覆蓋 Has／到期＋ended／重上刷新不疊加／到期後完全還原；Fire 非整除時長 2.75 s 恰 2 tick、0.99 s 零 tick；Ice／Electric 重上只 ended 一次；兩種 buff 共存）＋ SimulationTest 3 例（火焰扣血走 Damage 路徑；滿甲 5/5 上護甲到期仍 5；3/5 上護甲受傷後到期只扣仍存在的量）；H9；V-G（六種必有 `_inferred` 與 `_why`） | T-32（數值 INFERRED；changeValue 加法→乘法換算） | 功能完成・未驗收 |
| P3.2 Buff 觸發 | 帶 *BuffTrigger 的子彈命中時上 buff | FAITHFUL：BuffEffectTrigger__TriggerWith game_typed.c 86608–86645：`probability <= 0` 不擲骰，**只在非暴擊且 camp==0 時返回**（暴擊或其他陣營仍上 buff）；`probability > 0` 擲 `Random.Range(0,100)`，`probability > roll` 才上。INFERRED（T-33）：三種無機率欄位的觸發器在截斷後無法排除隱藏判定；RGLaserBuff 4% | bullets.json 每列 `buff{kind,buff_prefab,trigger,probability,_probability_inferred}`（26 顆：ice 10／poison 6／fire 6／electric 3／changespeed 1；含 8 筆 sword/spear/laser 新列，全表 211）；`WeaponDef.bulletPrefab`、`GameData::BulletForWeapon`；FireIntent/BulletState 攜帶 buff 與 camp；`Simulation::ShouldApplyBulletBuff(probability, isCrit, camp, rng)` 由 `RollBulletBuff` 於命中後呼叫 | BuffTriggerTest 17 例（5 筆資料樣本；命中上 buff＋DoT；機率 0 非暴擊不上、暴擊上；零機率×暴擊×陣營矩陣；`>` 邊界；100% 必上；固定 seed 可重放；無 buff 武器 golden 不變）；H10（含 `Buff: Fire ended`）；V-G | T-33；NOT PORTED：敵彈→玩家 buff（EnemyGunDef 無 bullet 連結） | 功能完成・**驗收完成（有取捨 T-33）**（R16 @ 8a16e95） |
| P3.3.c03…c13 | 每角一切片 | CharSkillC0N__* | combat/CharSkillC0N.cpp | 每技能 4 斷言（觸發／冷卻／效果量／結束） | — | 未開始・未驗收 |
| P3.4 接觸傷害 | AI04/08/13、boss 撞擊、無敵幀 | RGEController__GetHurt | sim | V-T | — | 未開始・未驗收 |
| P3.5.bossNN | 每 boss 一切片（Fan 近似→真表） | BossAI0N__ChooseAttack | sim | golden hash 前後 | — | 未開始・未驗收 |
| P3.6.gunXX | 每射擊模式一切片（25/46） | Gun*__* | WeaponBrain | WeaponBrainTest | T-22 T-23 | 未開始・未驗收 |
| P3.7 特殊 AI | AI05、AI06 召喚、AI12 複合 | 對應類別 | sim | V-T | T-24 | 未開始・未驗收 |
| P3.8 精英 | ex_* 數值、badass_rate | EnemyMaker 私有欄位 | 資料 | V-D | T-03 | 未開始・未驗收 |
| P3.9 雕像祝福 | 5 種 | BuffStatue01..04 | GameScene/sim | V-T | T-14 | 未開始・未驗收 |

### P4 進度與經濟循環
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P4.1 掉落經濟 | 掉落、磁吸、HUD | pickups.json、enemies.csv | EnemyReward、CoinPickup | H4、EnemyRewardTest | T-18 T-19 | 功能完成・未驗收 |
| P4.2 寶石 | 來源與 `PlayerSaveData.gems` | UIStatement.AddGem | RunStats/存檔 | V-T | — | 未開始（依賴 P5.3）・未驗收 |
| P4.3 復活／重試 | UIWindowReborn、window_try_again | 預製件、i2 | 場景 | V-H | — | 未開始（依賴 P4.2）・未驗收 |
| P4.4 buff 三選一 | 24 Buff_info、buff_* 旗標 | i2、RGGameProcess | UI＋RunState | V-T | — | 未開始（依賴 P3.1）・未驗收 |
| P4.5 16 層與勝利 | G1 r_end、Victory | map_G1、r_end | 場景 | H 案例 | — | 未開始・未驗收 |
| P4.6a 結算畫面 | Statements 場景 | Statements.unity | EndScene | H5、V-S | T-20 | 功能完成・未驗收 |
| P4.6b 統計存檔 | StatisticData | 反編譯 | 存檔 | V-T | — | 未開始（依賴 P5.3）・未驗收 |

### P5 大廳
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P5.1 可行走大廳 | HeroRoom 進地牢 | HeroRoom.unity | 場景 | V-P | — | 未開始・未驗收 |
| P5.2 角色解鎖／皮膚 | 寶石購買 | PlayerSaveData | UI | V-T | — | 未開始・未驗收 |
| P5.3 本地存檔 | PlayerSaveData/RGSaveManager | 反編譯 | 存檔模組 | V-T（往返） | — | 未開始・未驗收 |
| P5.4 花園／寵物（=P7.1）／許願池 | ItemPlant、Pet*、ItemWishingWell | 預製件 | 場景 | V-T | — | 未開始・未驗收 |
| P5.5 練習房／教學 | UITeaching | 場景 | V-P | — | 未開始・未驗收 |

### P6 UI 與表現
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P6.1 入口流程 | 主選單、選角 | title_en0、i2 | Menu/CharSelect | H1、V-S | — | 功能完成・未驗收 |
| P6.2 武器資訊 | weapon_icon、consume | UICanvas | HUD | V-S | — | 未開始・未驗收 |
| P6.3 小地圖 | UIMap | GetMinimapSprite | HUD | V-S | — | 未開始・未驗收 |
| P6.4 buff 列／boss 血條 | UIBuffBar、bossNN_info | 預製件 | HUD | V-S | — | 未開始・未驗收 |
| P6.5 暫停／設定 | UIWindowPause | 預製件 | 場景 | V-H | — | 未開始・未驗收 |
| P6.6 傷害數字／hit-stop | ShowTextHurt | 反編譯 | HUD | V-S | — | 未開始・未驗收 |
| P6.7 音效／BGM | 183 顆原版 clip、效果表、開火／命中／死亡／拾取／寶箱／傳送門／門／boss 出場音、選單與依關卡 biome 的 BGM 高低版 | rgsound/{effect,bgm,test}（`tools/extract_audio.py`，gitignored，183/183 逐位元相同）；RGMusicManager.prefab `effect_list[0..10]`（typed 19258–19282；7＝fx_show_up）；UITitle.bgm_clip／btn_clip；MapManager.bgm_clip 六生態（typed 25098–25119）；RGWeapon.audio_clip 193/194；EGun 9/10；ui_effects 六項中五項有 `_evidence` 預製件連結（player_hurt 只有音檔來源說明），只有 chest_open 的使用語意標 FAITHFUL，其餘五項的播放時機為 PORT CHOICE（T-35） | audio.json（`build_audio_table`：effects／bgm／weapon_shoot／enemy_gun_shoot／ui_effects）、`GameData::BgmFor(biome, high)`、`Game::AudioPlayer`（SK_MUTE、Mix 失敗自動靜音、每次播放印 `Audio:`）、SDL2_mixer 開 stb_vorbis 供 ogg；C++ 無 clip 字面（除缺表退回 fx_hit.wav） | AudioTableTest（effect 7、武器 clip、選單 bgm、六生態 low/high 與退回、逐一檢查 audio.json 每個引用的檔案存在且 >200 筆）；H11；實跑 `SK_SEED=1 SK_FORCE_CLEAR=25` 見 bgm 隨 A1→D1→E1 換為 bgm_1/2/3Low；V-G | T-34、T-35、T-29（出場音 0.8 s 延遲已接）；NOT PORTED：敵人／boss 攻擊音、buff 音 | 功能完成・**驗收完成（有取捨 T-34／T-35）**（R17 @ 2d2ca97） |
| P6.8 開場／劇情／載入 | Splash/Plot/Loading | 場景檔 | 場景 | V-P | — | 未開始・未驗收 |

### P7 內容補完
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P7.1 寵物（=P5.4） | Pet* 跟隨／攻擊 | res/pet 預製件、Pet*__* | 新實體 | V-T | — | 未開始・未驗收 |
| P7.2 傭兵（=P2.10） | 見 P2.10 | — | — | — | T-13 | 未開始・未驗收 |
| P7.3 無盡模式 | EndlessMapManager、level -1 預算 9999 | 反編譯、map_manager | 模式旗標 | V-H | — | 未開始・未驗收 |

### P8 擴充管線
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P8.0 能力邊界 | 只改 JSON 可做「既有行為的變體」（新武器＝既有射擊模式＋數值＋貼圖；新敵人＝既有 brain＋數值＋幀）；新行為需 C++ brain | — | 本節文字 | Codex 目視 | — | 功能完成（文字）・未驗收 |
| P8.1 素材替換 | 換高版本 ExportedProject 重跑 `tools/extract_*.py` | 高版本 AssetRipper 輸出 | 腳本參數化 | V-S 全案例重跑 | — | 未開始・未驗收 |
| P8.2 新增範例 | 各新增一把武器、一種敵人、一間房，只動 JSON/PNG（P3 完成後提前做一次） | — | 範例資料 | V-T | — | 未開始・未驗收 |
| P8.3 文件 | CONVENTIONS.md 資料驅動新增流程 | — | 文件 | Codex | — | 未開始・未驗收 |

---

## 4. 執行順序

1. **驗收補證據**（本版開始）：P1.6/P2.x/P4.1 依 §1 補測試報告、H 案例 log、樣本表，逐階段轉「驗收完成」。
2. **P3.0 單位稽核**。
3. ~~P1.10 升版~~（v1.8 完成）→ **P3.1 → P3.2** buff。
4. ~~P3.1 → P3.2~~（v1.16 完成，待驗收）；**P6.7 音效**依使用者要求提前完成（v1.16）。**P3.3.x** 技能逐角。
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
| T-04 | 生成上限 24、consume ≥1、選敵與扣點迴圈、生成點分布（CreateEnemy 在 Range(0,200) 後截斷；R4 前生成點索引恆為 0 的 bug 已修） | INFERRED / PORT CHOICE | EnemyMaker.hpp、WaveScheduler.hpp、GameScene SpawnWaveBatch | 敵人數與位置 | P2.3 |
| T-05 | 大房加級：兩維各 >15/>21 各 +1，level 0 不加；原碼旗標為 `the_room.random_room`，增量本體截斷（game_typed.c:77871–77995） | INFERRED | EnemyMaker.hpp `EnemyMakerLevel` | 敵人預算 | P2.3 |
| T-06 | 房間角色按距離指派 | PORT CHOICE | GameScene.cpp 房間角色區 | 特殊房位置 | P2.13 |
| T-07 | boss 寶箱與傳送門重疊時下移兩格 | PORT CHOICE | GameScene.cpp boss 死亡區 | 視覺 | — |
| T-08 | r_chest / r_sell 由 floorSeed 奇偶決定 | PORT CHOICE | GameScene.cpp 房殼區 | 獎勵房類型 | 需原碼 |
| T-09 | 獎勵箱 GetPot/GetCoin 分支、ChestXmlReader | NOT PORTED | GameScene 寶箱開啟 | 獎勵箱只出武器 | P1.9 |
| T-10 | 一般房清場獎勵 GetRoomReward | NOT PORTED | RGRoomX ClearRoom log | 無清場獎勵 | P2.12 |
| T-11 | 商店機器擺位一排 | PORT CHOICE | GameScene 商店區 | 視覺 | 需原碼 |
| T-12 | 飲水機＝免費補滿能量一次 | PORT CHOICE | GameScene Purchase | 能量 | 需原碼 |
| T-13 | 事件房只在雕像／礦物間選（傭兵未做） | PORT CHOICE + NOT PORTED | GameScene 事件房區 | 傭兵缺席 | P2.10 |
| T-14 | 雕像 7 座中 5 種祝福未移植；index→名字為 i2 順序推論（cost／貼圖／偏移已入 shop.json statues） | NOT PORTED / PORT CHOICE | GameScene kStatueName、skill_use | 祝福 | P3.9 |
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
| T-26 | Ghidra 單例呼叫後截斷（系統性） | — | EnemyMaker.hpp 檔頭、SimConfig.hpp（引用處） | 多處 INFERRED | game_typed.c / frida |
| T-27 | **已關（v1.8）**：ex_orc05 改由 `GameObject/ex_orc05.prefab` 直讀（max_hp 50、EnemyAI01），資料閘豁免撤除 | 已解 | build_gamedata.py build_ex_orc05_row | A2–A5 精英池 | — |
| T-28 | boss 房門帶外物件（r2_boss3/4 的 rail，y=21）由抽取器丟棄並記錄 `dropped_outside` | PORT CHOICE | build_gamedata.py build_boss_rooms | 兩房各少數條軌道 | P2.11 |
| T-30 | 類別鍵列（Bullet01／BossAI07／BuffArmor／EGun001…）的數值取自路徑序第一個使用該類別的 1.7.10 預製件；同類別其他實例可能不同（如 EGun001 取自 ex_orc05） | PORT CHOICE | build_gamedata.py `_first_prefab_by_guid` | 敵槍／子彈數值 | P1.13 |
| T-31 | 78 顆非 RGBullet 家族子彈預製件（BulletFire/BulletGas…）未入表（bullets.json 現 211 列＝25 類別列＋178 預製件列＋8 筆 buff-only 列）；51 個武器與 18 **筆** boss 子彈引用（42 個不同 id）指向表外預製件，列入 `KNOWN_UNRESOLVED_BULLET_REFS` 後降為 warning，清單外為錯誤 | NOT PORTED（資料） | validate_data.py `KNOWN_UNRESOLVED_BULLET_REFS`（# T-31）、tools/tests/test_validate_data.py；C++ 自 v1.16 起載入 `WeaponDef::bulletPrefab`（`GameData::BulletForWeapon`） | 對應武器／boss 的子彈行為 | P1.13 / P3.6 |
| T-32 | 六種 buff 的 tick 間隔（1 s）、tick 傷害（1）、冰凍／電擊＝speed_mul 0、護甲 +1 與「暫時護甲：只記實際加到的量、到期扣 min(已給, 現有)」、changeValue 加法性與 `max(0,1+changeValue)`（只有下限）皆為推論；buff_time／changeValue／is_enemy 為預製件實值 | INFERRED | combat/Buff.hpp、buffs.json `_inferred._why`、build_gamedata.py `_buff_inferred` | 燃燒／中毒 DPS、冰凍手感 | P3.2 之後以實玩對照修正 |
| T-33 | (a) RGBulletBuffTrigger／RGSwordBuffTrigger／RGArrowBuffTrigger 無序列化機率欄位，且 OnTriggerEnter2D 在 GameObject 呼叫後截斷（Arrow 18779–18793、Sword 31953–31967、Bullet 47177–47183 只委派基底）→ 「必中」為推論（probability 1.0，`_probability_inferred`）；(b) RGLaserBuff `buff_probability` 4 無可見比較式，依 >1 視為百分比 → 4%；(c) ReboundBuffEffector 無序列化欄位、BuffRandomEffectTrigger（bullet_96 多 buff）未移植 | INFERRED / NOT PORTED | build_gamedata.py attach_bullet_buffs、Simulation.cpp ShouldApplyBulletBuff、GameData.hpp | 上 buff 機率 | P3.6 |
| T-34 | BGM High/Low 切換條件不可見（PlayBGM 只收現成 clip）；移植版：房鎖定→High、清房→Low；生態無 High 版時退 Low；曲目依關卡實際 biome（MapManager.bgm_clip）選取 | INFERRED | AudioPlayer／GameScene 房狀態處、GameData::BgmFor(biome, high) | 音樂層次 | 需原碼／實玩對照 |
| T-35 | audio.json `ui_effects` 六項中五項附 `_evidence` 預製件連結（player_hurt 無，僅音檔來源說明）；只有 chest_open 的使用語意可證（FAITHFUL），enemy_hurt／enemy_death／boss_death／player_death／player_hurt 的播放時機為移植版選擇（`_port_choice` + `_why`）；`fx_hit.wav` 為缺表時的唯一字面退回 | PORT CHOICE | build_gamedata.py ui_effects、AudioPlayer／GameScene 播放點 | 命中／死亡音 | 需原碼（RGMusicManager 呼叫點）對照 |
| T-29 | CreatePart 在 part ≥ 1 時 `Invoke("ShowUpEffect", 0.8f)`（字串表有 ShowUpEffect、無 CreatePart；ShowUpEffect 只播音效 7）；分波本身無延遲。**v1.17 起音效已接線**：第 2 波以後生成後 0.8 s 播 effect 7（GameScene 計時器，用 kShowUpEffectDelayMs） | INFERRED（字串） | WaveScheduler.hpp kShowUpEffectDelayMs、GameScene 分波處 | 音效 | 已接線；字串仍為推論 |

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
- §2/§3：全部七欄、狀態詞彙修正、HEAD 改由 §10 報告承載。
- 未做（誠實列出）：分波時序純測試（P2.3 仍進行中）；Gun008/Gun017/RGAisle/EnemyAI15 檔頭的 T-ID 待下一切片一併補。

### R3 — 2026-09-12 — Codex — 對象 v1.2 @ 1097c86 — **不通過**
R2 落實度：資料閘、pickups 讀表、鉤子、§1.1 命令＝已落實；FALLBACK（enemies/characters/sprites 缺）、EnemyMaker 檔頭（大房加級仍在 FAITHFUL 段）、§10 附件（截圖與腳本不在倉庫）、§2/§3（P7/P8 缺欄、未開始項無驗收欄）、T-ID（T-14 只在 log 字串、T-26 無引用）＝部分。
抽查：Gun008.hpp:45 → 317449–450 通過；CombatStats.hpp:269 → 432415–438 通過；EnemyMaker.hpp:17–24 → 682128–234 不通過（加級增量不可見）。
§7：1 ✗ 2 ✗（GameScene 雕像貼圖／價格 15 硬編）3 ✗（分波時序測試未做）4 ✓（靜態核對；重跑不適用）5 ✗（缺截圖）6 ✗ 7 ✓ 8 ✗。
**回覆（v1.3）**：
- FALLBACK 補齊 enemies／characters／sprites／statues 四處（`build_gamedata.py`）；§0 改寫 pickups 缺檔行為（不掉落）。
- EnemyMaker.hpp 檔頭：FAITHFUL 段只留可見的預算表，加級本體移到「NOT visible（T-26）」段。
- 雕像：新 `build_statue_table` → shop.json `statues`（7 座 statue_01..07，非文件先前寫的 5 座；item_value 15、body/light 貼圖與 y 偏移全由預製件讀出）；`GameData::FindStatue`；GameScene 刪除 `kStatueBodySprite`/`kStatueCost`；`kStatueName` 標 PORT CHOICE T-14；StatueTest 讀表斷言；資料閘加 statues 檢查。
- 分波時序：抽出 `include/game/WaveScheduler.hpp`（Plan/TakeWave/NotifyRoomEmpty/Tick/Done，kPartDelayMs 800）；GameScene 改用；WaveSchedulerTest 5 例（24→9/9/6；房未空不放；790 ms 不放、800 ms 放；1 隻；parts 0→1）。log 字串逐字不變。
- T-26 引用補進 EnemyMaker.hpp 與 SimConfig.hpp；T-14 補進 kStatueName 註解。
- §2/§3：P7/P8 改七欄；所有未開始項加「未驗收」；非目標移出階段表。
- §10 附件：`docs/evidence/v1.3/`（H1–H7 log、ctest、gate、H5.png、r2_boss3.png、head.txt）與 `tools/evidence_chain.sh <tag>` 入倉庫。
- P3.0 稽核（scout 報告）：`kDataSpeedToPxPerSec = 15` 無來源依據；武器圖集 PPU 16、渲染 2×→32 px/單位＝kCellPx；15 可能誤取自 ortho 7.5×2；反編譯 RGBullet__SetBulletVelocity 為 stub（T-26）。改 32 會讓所有子彈／敵人／磁吸速度變 2.13 倍並動到兩個釘 15 的測試——**待使用者裁決**後做 P3.0 收尾。
- 未做：分波 waveSize＝level×3 的語意（this_count 預製件）仍為 INFERRED（沿用既有 GameScene 解讀，H3 log 相符）。

### R4 — 2026-09-12 — Codex — 對象 v1.3 @ 458c595 — **不通過**
R3 落實度：FALLBACK 十項、雕像讀表、分波純測試、T-14/T-26、§10 附件入庫＝已落實；EnemyMaker 檔頭（FAITHFUL 段仍含選敵／consume）、§2 P8 狀態、腳本無 statue 步驟＝部分。
新發現：WaveScheduler 聲稱的 `EnemyDead → Invoke("CreatePart", 0.8f)` 在匯出中找不到，可見的 0.8 s 呼叫在 CreatePart（682285–682290）；GameScene.cpp:1925 生成點公式 `(N×(serial×7+3)) % N` 恆為 0；H3 真實順序與文件不符；boss 截圖未涵蓋整房。
§7：1 ✗ 2 ✓ 3 ✓（靜態）4 ✓（靜態）5 ✗ 6 ✗ 7 ✓ 8 ✗。P3.0 意見：不同意「已證實必須改 32」，同意 15 無依據；改值也動磁吸範圍與散射。
**回覆（v1.4）**：
- 改讀 `game_typed.c`（帶型別）後分波語意重寫為 FAITHFUL：`EnemyDead`（73138–73185）this_count-- → 為 0 且 enemy_point>0 時**立即** CreatePart，否則 OnGameStateChange(2)＝清房；`CreatePart`（78030–78084）每波 max_room_size = level×3 隻，part≥1 時 Invoke 0.8 s＝ShowUpEffect 音效（T-29，NOT PORTED）。WaveScheduler 去掉 800 ms 閘門，測試改為「房空的第一個 tick 放波」；boss 房 `level 0, this_count 1` 語意寫入註解。
- 生成點索引修為 `(serial×7+3) % N`（PORT CHOICE T-04）；EnemyMaker.hpp 檔頭只留可見段，選敵／扣點移到 INFERRED (T-04)，T-05 旗標命名 `random_room`。
- §1.1 H3 依真實順序改寫；新增 H8 雕像案例與整房截圖案例；`SK_ZOOM` 鉤子；`tools/evidence_chain.sh` 加 statue 步驟、`pipefail`、每案 `timeout 120`、整房截圖 `r2_boss3_full.png`。
- §2 P8 改「進行中」；「六欄」改「七欄」。
- 整房截圖過程發現 v1.3 的 r2_boss3 截圖拍錯房（見 §10 更正）；`SK_ROOM_TELEPORT` 補 level-0 房的中央傳送與鏡頭對齊。
- P3.0：32 改寫為待驗證假設並列完整影響範圍（§3 P3.0）；裁決仍待使用者。

### R5 — 2026-09-12 — Codex — 對象 v1.4 @ 9536194 — **不通過**
R4 落實度：分波立即放波（game_typed.c 73158–73184、78049–78075 核對）、生成點公式、H3 順序、H8／整房案例、SK_ZOOM、§2 P8、T-29＝已落實；EnemyMaker／WaveScheduler 引用止於 77990 但公式在 77995、GameScene:916 仍寫 `this_count`、870–872 大房加級仍在 FAITHFUL 段＝部分；evidence_chain.sh 有 pipefail/timeout 但不檢查退出碼、`head` 截流、第 26 行濾掉傳送中心 log＝部分。
實查：資料閘 GREEN；T-01～T-29 齊；13 個 txt 讀完、3 張 PNG 非零；整房圖與 JSON 相符（四角 2×2 箱、下右門、四石／界內 rail 未畫＝P1.8、界外 rail＝T-28）。抽查三處 FAITHFUL 皆通過。
§7：1 ✗ 2 ✓ 3 ✓ 4 ✗ 5 ✓ 6 ✓ 7 ✓ 8 ✗。
**回覆（v1.5）**：
- GameScene:916 改 `max_room_size = level*3 + max_room_size`（game_typed.c:77995）並註明 CreateEnemy 扣減截斷（T-04）；870–872 大房加級改標 INFERRED (T-05)；三處引用範圍改為 77871–77995。
- `tools/evidence_chain.sh` 重寫：每案先把完整 log 寫檔再過濾（不再 `head` 截流）、檢查退出碼／逾時（124）／空摘錄／PNG 非零、H2neg 斷言＝1、最後 `STATUS: OK|FAILED(n)` 且失敗非零退出；r2boss3 兩案的過濾式都保留 `SK_ROOM_TELEPORT -> room 3 centre`，full 案另保留 `SK_ZOOM override`，附件可由腳本原樣產生。
- 首次實跑時 H1–H4 逾時（各 120 s；同命令單獨重跑只需 7 s，原因未確定，疑為新建 exe 首次啟動的防毒掃描）：加入 WARM-UP 步驟（`SK_MAX_FRAMES=1`、600 s 上限）；第二次整鏈 `STATUS: OK`（§10 v1.5）。
- §1.1 腳本路徑改為 `tools/evidence_chain.sh`。

### R6 — 2026-09-13 — Codex — 對象 v1.5 @ 0f8ac9f — **不通過**
R5 三項：註解＝已落實（GameScene:916 → typed:77995；WaveScheduler:17 → 73158–73184；:22 → 78045–78075）；腳本＝部分（WARM-UP 失敗只印 rc、ctest／gate 空摘錄未檢查、PNG 未排除舊產物）；§1.1 路徑＝已落實。§10「與 v1.4 逐字相同」不成立（H7 多四行）；附件未保存 STATUS。
§7：1 ✓ 2 ✓ 3 ✓ 4 ✗ 5 ✓ 6 ✓ 7 ✓ 8 ✗。
**回覆（v1.6）**：腳本開頭清除舊 png/txt/log；ctest 摘錄空或含失敗、gate 非 GREEN、WARM-UP 非零退出皆入 FAILS；結尾 `STATUS … <UTC>` 寫入 `status.txt`。§9 R5 與 §10 措辭依實際修正（兩案皆保留中心傳送、full 案含 zoom；H7 摘錄差異說明）。

### R7 — 2026-09-13 — Codex — 對象 v1.6 @ e1507b0 — **通過**（架構書與證據體系；不代表各功能階段已驗收完成）
R6 三項皆已補齊；資料閘 GREEN；T-01～T-29 齊；14 個 txt 與 §10 一致；三張 PNG 非零；§7 八條全部通過（4/7 為附件／靜態核對，exe 重跑不適用）。
非阻擋改善：腳本 ctest 判斷 `0 tests failed` 會命中 `10 tests failed`（退出碼仍攔得住）；缺 exe 時未寫 status.txt。
Codex 對下一步的建議：P3.0 裁決後收尾（記錄選值依據；驗證飛行 N 格秒數、角色／敵人速度、磁吸範圍與散射；golden 變動逐項說明）→ P1.10 升版（六表 1.7.10 來源、全表 diff＋固定樣本、補 ex_orc05 撤豁免）→ P3.1 buff 本體（固定 dt 驗證觸發／週期／持續／疊加刷新／解除還原）→ P3.2 buff 觸發（≥5 顆預製件對照、命中→生效→結束 log）。
**回覆（v1.7）**：兩個非阻擋項已修（ctest 判斷改 `^100% tests passed, 0 tests failed`；缺 exe 亦寫 status.txt）。§4 順序依建議維持；P3.0 的 15→32 裁決交使用者。

### R8 — 2026-09-13 — Codex — 對象 P1.10 切片 @ 4d0a786 — **不通過**
五筆樣本數值皆核對通過（BuffStatue01 實檔在 `res/effect/buff_statue_1.prefab`，文件路徑錯）；GameDataTest 三項變更有依據（203＝25 筆 1.07 類別列＋178 筆 1.7.10；146＝145＋ex_orc05；EGun001 speed 16）。
阻擋：bullets 的 25 筆類別列仍為 1.07，與矩陣「六表皆 1.7.10」不符；T-31 的 warning 無界限（注入不存在引用仍 GREEN）；程式缺 T-31；§1 仍寫 ex_orc05 豁免；「18 個 boss」應為 18 筆引用；§9 未補。T-30 可接受為暫時 PORT CHOICE（同為 EGun001：e_orc01 speed 7、ex_orc01 8、代表列取 16），每實例接線留 P1.13。
§7：1 ✗ 2 ✓ 3 ✓ 4 ✓ 5 不適用 6 ✗ 7 ✓ 8 ✗。
**回覆（v1.9）**：bullet 類別列改走同一 guid→首個預製件解析並記 `_prefab`；`KNOWN_UNRESOLVED_BULLET_REFS` 清單化、清單外為錯誤、`tools/tests/test_validate_data.py` 注入假引用驗證邊界並納入證據鏈；GameData.cpp 查表加 T-30 註解（T-31 位置在 Python 閘門與邊界測試，C++ 尚無 bullet_prefab 讀取）；§1 V-G 刪豁免敘述；§1.2 路徑更正；T-31 措辭改「18 筆引用」。

### R9 — 2026-09-13 — Codex — 對象 P1.10 切片 v1.9 @ 4d4d49b — **不通過（僅文件一致性）**
25 筆 bullet 類別列獨立核對全部相符（18 解析、7 保留 1.07）；T-31 清單 47 個 id、清單外報錯、邊界測試 3/3 並入證據鏈；三筆抽查（Bullet04 `_bullet_71` destroy_time 0.75、Bullet05 `_bullet_8` rotate_angle 570、BulletParabola `bullet_90` camp 0）通過；T-01～T-31 齊；附件一致。§7：1–4、6、7 通過，5 不適用，**8 不通過**：T-31 位置寫成 GameData.cpp（實際在閘門）；diff 報告仍說 Bullet01 原封保留 1.07、「14 distinct entries」應為 14 個不同目標（18 筆引用、涉及 12 個 boss）；build_gamedata.py 註解「none observed」應為 7 筆；§0 退化條件未涵蓋「來源存在但類別無預製件」的例外。
**回覆（v1.10）**：四處文字全部改正（本節上方 T-31 列、§9 R8 回覆、§0 降級條件、diff 報告兩處、build_gamedata.py 註解）。無程式行為變更，附件沿用 v1.9。

### R10 — 2026-09-13 — Codex — 對象 P1.10 切片 v1.10 @ 41c9172 — **驗收完成（有取捨 T-30／T-31）**
R9 四項全數修正；`git diff 4d4d49b..41c9172 --stat` 3 檔、僅 Markdown 與 docstring，AST 相同無行為變更。§7：1–4、6–8 通過，5 不適用。P1.10 為第一個進入「驗收完成」的切片；補完留 P1.13／P3.6。

### R11 — 2026-09-13 — Codex — 對象 P3.1 切片 v1.12 @ f28ac26 — **不通過**
資料驅動通過（`_buff_inferred` 記憶體重算與 JSON 全符，C++ 無硬編 DPS）；T-01～T-32 齊；附件一致。阻擋：(1) 來源標示過度：CancelInvoke 不能證明「到期由排程觸發」、GetArmor 存在不能證明「持續期間加甲」、changeValue 加法性不能以移植版 `+=` 自證；Buff.hpp 寫「夾至 [0,1]」但程式只有下限；引用行號不準（Fire GetHurt 86867–86886、Electric Start 86760）。(2) 護甲 bug：滿甲 5/5 上 +1 被上限裁掉，到期仍扣 1 → 4。(3) tick 溢算：最後一步累加整個 dt，非整除時長會多一 tick。(4) 測試：Electric／ChangeSpeed 各僅 3 斷言，刷新與還原覆蓋不足，「MultipleActiveBuffs」只上一種。(5) §10 寫 2016 應為 2036。T-32 獨立意見：推論值保留為可調參數即可，不阻擋。
**回覆（v1.13）**：護甲改記實際加到的量、到期扣 min(已給, 現有)（INFERRED T-32），SimulationTest 加滿甲與部分甲受傷兩例；Tick 以 `effectiveDt = min(dt, remaining)` 累計，加 2.75 s／1.0 s 恰 2 tick 與整除兩例；六種各補 Has／到期＋ended／刷新不疊加／到期後完全還原，兩種 buff 共存改為真的上兩種（BuffTest 19→31，ctest 2036→2050）；Buff.hpp 與 `_why` 改為「只證明取消排程」「存在不證語意」「加法性為推論」「只有下限」，行號更正並重生 buffs.json（R12 發現檔頭第 18 行仍寫「加法性已確認」，v1.14 補改）；§10 數字改正。由獨立 verifier agent 實跑 ctest 2050/2050、gate GREEN、H9 armor 案 applied/ended 確認。

### R12 — 2026-09-14 — Codex — 對象 P3.1 切片 v1.13 @ a6f50e1 — **不通過（僅一處來源措辭）**
護甲、tick、六種測試矩陣、§10 2050 皆通過；gate GREEN；T-01～T-32 齊；附件一致。阻擋：Buff.hpp:18 仍寫「ADDITIVE … decompile-confirmed」，與同檔第 58 行矛盾；§9 R11 回覆「全部改正」不實。建議：補 0.99 s 零 tick 例；Ice／Electric 以 ended 次數區分刷新與疊加；armor 無頭案補附件。
**回覆（v1.14）**：Buff.hpp:18 改為「只有呼叫可確認，加法性為 INFERRED (T-32)」；§9 R11 措辭更正；BuffTest 加 `DurationShorterThanTickSecNeverTicks`（0.99/1.0 → 0 tick）與 `ReapplyingIceOrElectricEndsExactlyOnce`；證據鏈加 H9armor 案（600 幀，applied→ended）。

### R13 — 2026-09-14 — Codex — 對象 P3.1 切片 v1.14 @ 48d091e — **不通過（僅 §3 測試數 31→33）**
R12 阻擋已清；diff 30 檔 +60/−11 無行為變更；gate GREEN；T-01～T-32 齊；ctest 2052、H9／H9armor 一致。§7：1–4、6、7 通過，5 不適用，8 不通過（§3 P3.1 寫 31 例，實為 33）。
**回覆（v1.15）**：§3 改 33。純文件變更。

### R14 — 2026-09-14 — Codex — 對象 P3.2＋P6.7 @ 750702b — **P3.2 不通過；P6.7 不通過**
實查：五顆子彈預製件→JSON 相符；183 顆音檔逐位元相同；effect 7、weapon_000→fx_gun_1、weapon_013→fx_gun_4 GUID 吻合；gate GREEN、unittest 3/3、新增 19 測試通過；H10／H11 重跑正常（H10 附件因只截 6 行漏掉 ended）。
阻擋（P3.2）：`game_typed.c:86622–86645` 顯示 `probability <= 0` 不擲骰且**只在非暴擊且 camp==0 時返回**，暴擊仍上 buff，實作未傳暴擊；Arrow／Sword／Bullet 觸發器在截斷後不能宣稱必中（應 INFERRED）；T-31 數量與「C++ 未載入 bullet_prefab」過期；`SK_FORCE_HIT` 實為持續開火。阻擋（P6.7）：`BgmFor` 以章節固定冰原／城堡／外星，森林／墓地／火山播錯曲；寶箱／死亡／傳送門 clip 硬編且無 T-ID；T-29 未同步（出場音改為房鎖定即播）；`EveryReferenced` 測試只查部分引用。
**回覆（v1.17）**：`Simulation::ShouldApplyBulletBuff(probability, isCrit, camp, rng)` 依 86608–86645 實作（零機率：暴擊或 camp≠0 才上；>0 擲骰 `probability > roll`；≥100 必上），HitResult.isCrit 與 BulletState.camp 穿線，BuffTriggerTest 新增 4 例＋改寫 1 例（零機率非暴擊不上／暴擊上／矩陣／邊界／100%）；三種無機率觸發器全部改標 INFERRED (T-33)；T-31 更新為 211 列、51 武器＋18 筆 boss 引用（42 id）、C++ 已載入 bullet_prefab；`SK_FORCE_HIT` 文字改正。`GameData::BgmFor(biome, high)` 依關卡 biome 選曲並加六生態測試（實跑 A1→D1→E1 見 bgm_1/2/3Low）；clip 字面移入 audio.json `ui_effects`（五項有預製件連結、player_hurt 僅音檔來源說明；chest_open 語意 FAITHFUL，其餘五項播放時機 PORT CHOICE T-35）；boss 出場音改為第 2 波後 0.8 s（T-29 同步）；`EveryReferencedClipFileExists` 走遍整表（>200 筆）；證據鏈 H10 截 40 行並斷言 `Buff: Fire ended`。ctest 2079。

### R15 — 2026-09-14 — Codex — 對象 P3.2＋P6.7 v1.17 @ 82673b5 — **兩者皆不通過（僅文件一致性）**
R14 程式阻擋全部修正：機率判定與 86608–86645 一致、穿線正確；T-33／T-31／T-29 接線／BGM 六生態／ui_effects／全表音檔／H10 皆通過；實跑 BuffTrigger 17＋AudioTable 10＝27/27、gate GREEN、unittest 3/3；T-01～T-35 齊。§7：1–4、6、7 通過，5 不適用，8 不通過：BuffTriggerTest 實為 17 例（新增 4＋改寫 1，非 18／新增 5）；§3 P2.3 仍寫 T-29「未移植」；H11 的 `fx_hit.wav` 敘述不對（敵人／boss 受擊亦用 effect 5）；T-35 措辭應為「五項有預製件連結（player_hurt 無），只有 chest_open 語意可證」（R15 原要求「六項皆有」為誤，R16 更正）；FireIntent.hpp:43 註解與舊測試名 `…ZeroNeverApplies` 過期。
**回覆（v1.18）**：以上六處全部改正（含 FireIntent.hpp 註解、測試改名 `DataBulletProbabilityZeroIsCritOnly`）。無程式行為變更；附件沿用 v1.17。

### R16 — 2026-09-14 — Codex — 對象 P3.2＋P6.7 v1.18 @ 8a16e95 — **P3.2 驗收完成（有取捨 T-33）；P6.7 不通過（一處事實）**
R15 六項中五項通過；diff 3 檔 +15/−10 無行為變更。P6.7 唯一剩餘：`ui_effects` 實際只有五項帶 `_evidence`（player_hurt 無預製件連結，產表程式刻意不產生），R15 的「六項皆有」要求本身有誤，本輪更正。§7：P3.2 全過；P6.7 只有 8 不通過。
**回覆（v1.19）**：§3 P6.7、§6 T-35、§9 R14／R15 措辭改為「五項有預製件連結，player_hurt 僅音檔來源說明，只有 chest_open 語意可證」。P3.2 狀態改為驗收完成（有取捨）。純文件變更。

### R17 — 2026-09-14 — Codex — 對象 P6.7 v1.19 @ 2d2ca97 — **驗收完成（有取捨 T-34／T-35）**
diff 1 檔 +10/−6 純文件；T-35 措辭與 audio.json `_evidence` 相符。§7 全過（5 不適用）。至此已驗收切片：P1.10、P3.2、P6.7；架構書本身 R7 通過。

---

## 10. 證據附錄（每版最新一次實跑；檔案在 `docs/evidence/<版本>/`，由 `tools/evidence_chain.sh <版本>` 產生；腳本把結尾 `STATUS: OK|FAILED` 與 UTC 時戳寫入 `status.txt`，只有 OK 的附件有效）

v1.17（`docs/evidence/v1.17/`；`head.txt` = 建置時的基底 commit 750702b，提交後 HEAD 見 git log）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2079`（BuffTriggerTest 17、AudioTableTest 10 含六生態；golden 含在內）。
- 資料閘：`gate.txt` = `GATE: GREEN -- 0 errors, 8 warning(s)`；`gate_tests.txt` = 邊界 unittest（3 例，注入假引用必須報錯）。
- H1–H9：`H1.txt` … `H9.txt`、`H2neg.txt`（=1）；截圖 `H5.png`；`status.txt`。H9 = `Buff: Fire applied to 61453 for 2.000000s` → `Buff: Fire ended on 61453`；`H9armor.txt` = `Buff: Armor applied to 61453 for 6.000000s` → `Buff: Armor ended on 61453`；`H10.txt`（40 行）= `Buff: Fire applied to <id>` … `Buff: Fire ended on <id>`（腳本斷言 ended）；`H11.txt`（40 行）= `sfx fx_gun_1.wav` x22、`sfx enermy_die3.wav` x16、`bgm bgm_5Low.ogg` x1、`sfx fx_door.wav` x1。關鍵事件與 v1.4 相同（H7 摘錄上限改為 12 行，前面多出前幾層的 `TransferGate: open` 四行）：H1 `hero c12 on floor 0` → `Level B1 (1-1)` → `Floor 0 CLEARED` → `floor 1` → `Level B2`；H2 `TransferGate: open in room 4` → `entered -> next floor` → `hero c01 on floor 1`；H3 `part spawned 9 (15 pending)` → `level 3 points 25 -> 24 planned, wave size 9` → `StartRoom -> Active` → `9 (6 pending)` → `6 (0 pending)` → `room 1 cleared`；H4 `Reward: e_miner02 dropped 2 pickups (coins 1 energy 8)` …；H5 `Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1`；H6 `bought health_pot_big for 10`；H7 `BossRoom: room 6 uses r1_boss4 (21x21, 27 objects)` → `Boss defeated!` → `Chest: boss chest chest_boss13` → `TransferGate: open in room 6`；H8 `Statue: worshipped statue_05 (Elf) -> statue_index 5`。
- V-S r2_boss3：`r2boss3_seed.txt`（含 `SK_ROOM_TELEPORT -> room 3 centre (no spawn points) at (-2624,1312)`）、`r2_boss3.png`（zoom 1.5 局部）、`r2boss3_full.txt`（含 `SK_ZOOM override 0.6`）、`r2_boss3_full.png`（整房）。對照 boss_rooms.json r2_boss3 21×21：box03 ×16 = 四角 2×2 叢（(3–4,2–3)、(3–4,18–19)、(16–17,2–3)、(16–17,18–19)）→ 截圖四角各見 2×2 箱體；obj22_stone ×4 與 rail ×4（y=0）未繪製＝P1.8；dropped_outside 4（rail y=21）＝T-28；boss 在房中央、門在下與右。**更正（v1.4 起）**：v1.3 的 `r2_boss3.png` 拍到的是起始房（當時 `SK_ROOM_TELEPORT` 對 level-0 房靜默跳過）。

v1.6（e1507b0，R7 通過的附件，已由 v1.8 取代）／v1.5（0f8ac9f）／v1.4（9536194）：ctest 2016/2016、gate GREEN、H1–H8、整房截圖（附件已由 v1.6 取代）。v1.3（458c595）：ctest 2015/2015。v1.2（1097c86）：ctest 2009/2009。
