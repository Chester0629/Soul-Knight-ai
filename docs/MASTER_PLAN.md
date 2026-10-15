# Soul-Knight-ai 專案架構書（元氣騎士 1.7.10 忠實復刻）

> 本文件是專案的**單一事實來源**：範圍、階段、每階段的資料來源與驗證方式、Codex 審查紀錄。
> 由 Claude（實作）與 Codex（審查）共同維護；規則見 §8。
> **狀態詞彙**（兩欄分開記）：
> 功能欄：`未開始 / 進行中 / 功能完成`。驗收欄：`未驗收 / 驗收完成 / 驗收完成（有取捨）`。
> 「功能完成」＝接線可玩；「驗收完成」＝§1 證據齊全且 Codex 通過。
> 版本：v1.53（2026-10-15；P2.18 碰撞與移動第二刀 R41 驗收完成（有取捨 T-61／T-62／T-63）；v1.53 只套用 R41 非阻擋建議：文件措辭與 A/B 腳本的最後狀態行，無程式行為改動。前一版 v1.47：P2.17 渲染版面第一刀，R36 驗收完成（有取捨 T-59／T-60））

---

## 0. 目的、範圍、非目標、資料版本

**目的**：在自製的 C++ 遊戲系統（PTSD：SDL2 + OpenGL + ImGui，CMake，GoogleTest）上，以**資料驅動**的方式忠實復刻《元氣騎士》1.7.10（Unity 2017.4.1f1 IL2CPP，armeabi-v7a）的單人地牢循環，作為學術演示。之後要能**用高版本素材替換**並**新增內容**，所以數值、貼圖、布局都必須走「解包資料 → JSON/PNG → 程式讀表」。

**忠實度標籤**（程式碼註解與本文件共用；取捨註解一律附 §6 的 T-ID）：
- `FAITHFUL @ <來源>`：直接來自反編譯（`../_reverse/ghidra_export/game_full.c`；**`game_typed.c` 為同一二進位的帶型別版本，欄位以名字出現，較可讀，引用時寫明檔名**；皆在倉庫外的同層目錄）、AssetRipper 預製件（`../_reverse/asset soul/ExportedProject`）或資料表（`../_reverse/analysis/data_tables/*.csv`）。**只有原碼可見的部分**才能標 FAITHFUL；Ghidra 截斷處之後一律 INFERRED。
- `INFERRED (T-xx)`：來源存在但被截斷或欄位語意需推論，依據寫在註解裡。
- `PORT CHOICE (T-xx)`：原版邏輯不可恢復，移植版自行決定；集中在單一處。
- `NOT PORTED (T-xx)`：明知原版有、目前沒做，程式碼只記 log。
- `DISASM (T-39)`：game_typed.c 在截斷處之後看不到、但從 `libil2cpp.so` ARM 反組譯讀出的邏輯或常數。註解必附位址（`DISASM 0x26446c`），可用 `tools/arm_disasm.py <位址>` 重現（需 Python capstone；Ghidra 位址 − 0x10000 ＝ 檔案偏移）。可信度介於 FAITHFUL 與 INFERRED 之間：指令可見，但語意（呼叫目標、欄位偏移對名）由人判讀。

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
鉤子（env）：`SK_SEED=N`、`SK_MAX_FRAMES`、`SK_SHOT`、`SK_SKIP_MENU`、`SK_START_CHAR`、`SK_CHAR`、`SK_MENU=start`、`SK_FORCE_CLEAR=K`、`SK_FORCE_DIE=K`、`SK_FORCE_KILL=K`、`SK_FORCE_KILL_EVERY=N`、`SK_FORCE_SKILL=K`、`SK_ENDSCENE=restart|quit|menu`、`SK_GATE_TELEPORT=K`、`SK_GATE_BESIDE=K`、`SK_ROOM_TELEPORT=K,R[,dx,dy]`（R 無生成點且為出口房時傳到房中央；v1.42 起可加像素偏移 dx,dy，只作用於房中央落點，預設 0,0 與舊兩欄行為相同，非零時多印一行 `SK_ROOM_TELEPORT offset (dx,dy)`；v1.43 起由 `Game::ParseRoomTeleport`（include/game/TestHooks.hpp）嚴格解析：恰 2 或 4 欄、K／R 整欄整數且不溢位、dx／dy 整欄浮點且有限、|v| ≤ 4096，任一不符整個鉤子作廢並印 `SK_ROOM_TELEPORT rejected: <原因>`；用來避免玩家站在 boss 出生點上把 boss 擋住）、`SK_SHOP_BUY=K`、`SK_SHOP_MACHINE=N`、`SK_STATUE_WORSHIP=K`、`SK_COINS=N`、`SK_EVENT_ROOM=statue|mineral`、`SK_ZOOM=<float>`（整房截圖用，覆蓋鏡頭 1.5）、`SK_FORCE_BUFF=K,<kind>`（第 K 幀對玩家上 buff）、`SK_FORCE_WEAPON=<weapon id>`（開局裝備）、`SK_FORCE_HIT=K`（自第 K 幀起持續朝最近敵人開火）、`SK_FORCE_SKILL_PULSE=K1,K2`（第 K1、K2 幀各按一次技能鍵）、`SK_DT_MS=N`（每幀固定 N ms，量測用）、`SK_TRACE_BULLETS=1`（印子彈座標）、`SK_MUTE=1`（靜音但仍印 `Audio:` log）、`SK_AUTOWALK`（不穩）、`SK_AUTOFIRE`；P2.18 證據鉤子（皆嚴格解析於 `include/game/TestHooks.hpp`，不合法整個作廢並印 `<鉤子> rejected: <原因>`，未設即無效）：`SK_FORCE_DASH=K,dx,dy`（第 K 幀沿 (dx,dy) 衝刺，走與 C02 大招同一個 `Sim::ApplyDash`，不需英雄／滑鼠／技能鍵；印 `Dash: SK_FORCE_DASH frame K from (x,y) to (x,y) blocked=0|1`）、`SK_WALK_DIR=dx,dy`（玩家每幀固定朝此方向走，忽略鍵盤）、`SK_DT_SPIKE=K,ms`（主迴圈第 K 幀〔0 起算，僅 `SK_MAX_FRAMES` 設定時計數〕的 dt 改為 ms，模擬卡頓；場景在 dt 超過 160 ms 時印前三次 `Frame dt X ms clamped to 160 ms`）、`SK_TRACE_POS=F1,F2,…`（場景第 F 幀印玩家與每隻敵人／boss 的座標與 `blocked=0|1`，`blocked` 是以該物種身體圓問 `BlocksAny` 的結果）。
`GameScene::m_Frame` 每層重新計數。**Shell**：命令以 Git Bash 寫（`VAR=x ./build/Debug/SoulKnight.exe`）；PowerShell 等價寫法 `$env:VAR="x"; .\build\Debug\SoulKnight.exe`。實跑腳本：`tools/evidence_chain.sh <版本>`（輸出入 `docs/evidence/<版本>/`，§10 引用；每案逐一檢查退出碼、逾時與 PNG 大小，並以 `require_order`（事件依序出現）、`require_count`（最少筆數）與 `require`（存在）斷言上表「預期 log」的必要事件，缺一或順序錯即失敗；任一失敗腳本以非零退出）。

| 案例 | 命令（Git Bash，倉庫根目錄，Debug 建置） | 預期 log 順序 | 逾時 |
|---|---|---|---|
| H1 入口流程 | `SK_START_CHAR=c12 SK_FORCE_CLEAR=60 SK_MAX_FRAMES=200 ./build/Debug/SoulKnight.exe` | `GameScene: hero c12 on floor 0` → `Level B1 (1-1)` → `Floor 0 CLEARED` → `hero c12 on floor 1` → `Level B2`（`SK_START_CHAR` 由 main.cpp 直接開局，不經 CharSelectScene；選角畫面的 log 用 `SK_MENU=start SK_CHAR=c12` 驗）| 60 s |
| H2 傳送門 | `SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400 ./build/Debug/SoulKnight.exe`；反例同命令去掉 `SK_GATE_TELEPORT` | `TransferGate: open in room N` → `TransferGate: entered -> next floor` → `hero c01 on floor 1`；反例 `GameScene: hero` 只出現 1 次 | 60 s |
| H3 分波與封鎖 | `SK_DT_MS=20 SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=45 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe`（v1.50 起加 `SK_DT_MS=20`：H3 的強殺以幀數計、戰鬥以時間計，結果隨 dt 而變；base 0912b66 對現況的 A/B 見 `docs/evidence/p2.18/h3_ab.txt`——dt 20 兩者皆清房，dt 30 base 清房、現況陣亡，dt 40／80 兩者皆陣亡且現況早 7 幀，牆鐘下 base 3／3 清房、現況 1／3；故釘在兩者都清房的 20 ms；v1.38 起每 45 幀強殺：敵人改用真實走速後會走近射擊，原本每 90 幀的節奏下靜止的英雄在第 3 波前陣亡） | 實際順序：`EnemyMaker: room 1 part spawned 9 (15 pending)`（首波在 StartRoom 內先生成）→ `EnemyMaker: room 1 (type 1) level 3 points P -> 24 planned, wave size 9` → `RGRoomX: room 1 StartRoom -> Active` → 每次整波死亡後**立即**下一波 `part spawned 9 (6 pending)` → `part spawned 6 (0 pending)` → `RGRoomX: room 1 cleared`（FAITHFUL：EnemyDead → CreatePart 無延遲，見 T-29） | 90 s |
| H4 掉落經濟 | `SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe` | `Reward: <id> dropped N pickups (coins a energy b)` 至少 3 行（必要）；`Pickup: coin +1`／`Pickup: energy +8` 需玩家進入磁吸範圍（加 `SK_AUTOWALK=1`，可選） | 90 s |
| H5 結算 | `SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT=H5.png ./build/Debug/SoulKnight.exe` | `Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1 …` | 60 s |
| H6 商店 | `SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130 ./build/Debug/SoulKnight.exe`（預設 seed 第 0 層獎勵房為商店） | `Shop: vending_machine_i … sells` → `Shop: bought <pot> for 10` | 60 s |
| H7 boss 層 | `SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=700 ./build/Debug/SoulKnight.exe` | `Level B5` → `BossRoom: room N uses r1_bossK` → `Boss defeated!` → `Chest: boss chest chest_bossNN` → `TransferGate: open` | 120 s |
| H8 雕像讀表 | `SK_SKIP_MENU=1 SK_EVENT_ROOM=statue SK_COINS=30 SK_STATUE_WORSHIP=100 SK_MAX_FRAMES=130 ./build/Debug/SoulKnight.exe` | `Statue: worshipped statue_0N (<name>) -> statue_index N`（貼圖／價格來自 shop.json statues） | 60 s |
| H9 buff 本體 | `SK_SKIP_MENU=1 SK_FORCE_BUFF=30,fire SK_MAX_FRAMES=200 ./build/Debug/SoulKnight.exe`；護甲例 `SK_FORCE_BUFF=30,armor SK_MAX_FRAMES=600`（6 s 需 600 幀） | `Buff: Fire applied to 61453 for 2.000000s` → `Buff: Fire ended on 61453`（61453 = 玩家 view id；持續 2 s 來自 buff_fire.prefab buff_time） | 60 s |
| H10 buff 觸發 | `SK_SKIP_MENU=1 SK_FORCE_WEAPON=weapon_013 SK_AUTOWALK=1 SK_FORCE_HIT=0 SK_MAX_FRAMES=600 ./build/Debug/SoulKnight.exe`（`SK_FORCE_HIT=K`＝從第 K 幀起持續瞄準最近敵人開火） | 火焰子彈命中 → `Buff: Fire applied to <enemy id> for 2.000000s` → `Buff: Fire ended on <id>`（腳本斷言 ended 必出現；weapon_013 的 bullet 帶 RGBulletBuffTrigger→buff_fire） | 90 s |
| H12 技能 | `SK_SKIP_MENU=1 SK_MUTE=1 SK_START_CHAR=<c> SK_FORCE_SKILL_PULSE=60,300 SK_MAX_FRAMES=600 ./build/Debug/SoulKnight.exe`，c 為 c03 c04 c05 c07 c08 c10 c11 c12 c13 | 全部有 `Skill: <c> activated`；c03／c04／c07／c08／c10／c13 另需 `Skill: <c> ended`；c05／c11 為瞬發（RoleSkillEnd 在 RoleSkill 尾端，DISASM 0x2661e8／0x26af90），改斷言 `Skill: <c> effect …`；c12 RoleSkillEnd 不清 in_skill（T-49），斷言 effect 行 | 60 s／角 |
| H11 音效 | `SK_SKIP_MENU=1 SK_MUTE=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=300 ./build/Debug/SoulKnight.exe` | `Audio: bgm bgm_<biome>Low.ogg` → 擊殺 `Audio: sfx enermy_die*.wav`；加 `SK_AUTOWALK=1 SK_AUTOFIRE=1 SK_FORCE_HIT=30` 另見 `Audio: sfx fx_gun_*.wav`（開火）；`fx_hit.wav`（effect 5）於玩家、敵人或 boss 受擊時播放 | 60 s |
| V-S 整房 | `SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_ZOOM=0.6 SK_MAX_FRAMES=245 SK_SHOT=r2_boss3_full.png ./build/Debug/SoulKnight.exe` | 21×21 boss 房整房入鏡；對照 boss_rooms.json 物件座標 | 120 s |
| V-S P2.18 碰撞 | AC1：`SK_SEED=14 SK_SKIP_MENU=1 SK_MUTE=1 SK_ROOM_TELEPORT=1,1 SK_ZOOM=1.4 SK_DT_MS=20 SK_MAX_FRAMES=251 SK_TRACE_POS=3,250 SK_SHOT=p218_s14_repro.png ./build/Debug/SoulKnight.exe`；AC4：`SK_SEED=1 SK_SKIP_MENU=1 SK_MUTE=1 SK_ROOM_TELEPORT=5,4,120,0 SK_FORCE_DASH=10,1,0 SK_DT_MS=20 SK_MAX_FRAMES=20 SK_ZOOM=3 SK_TRACE_POS=4,12`；AC5：`SK_ROOM_TELEPORT=5,4,100,0 SK_WALK_DIR=1,0 SK_DT_SPIKE=5,5000 SK_DT_MS=20 SK_MAX_FRAMES=30 SK_TRACE_POS=5,6,7,29`；斷言 `python tools/p218_check.py <證據目錄>` | `SpawnFilter: N of M connected floor cells removed`；第 3 幀房 1 每隻敵人 `blocked=0` 且第 3→250 幀位移 > 1 px；`Dash: … from (2744.0,-1312.0) to (2755.2,-1312.0) blocked=0`（牆內面 +144 px、身體半徑 12.8，貼牆停下）；`Frame dt 5000 ms clamped to 160 ms` 且各追蹤幀玩家都在牆內面之前；`P218 CHECK: OK`。另有壞值案 `p218_hooks_bad`（四個鉤子皆被拒絕且不生效） | 120 s |
| V-S P2.17 渲染 | `SK_SEED=1 SK_SKIP_MENU=1 SK_MUTE=1 SK_ROOM_TELEPORT=60,1 SK_MAX_FRAMES=130 SK_SHOT=p217_a1_room1.png ./build/Debug/SoulKnight.exe`；牆後／牆前＝同環境加 `SK_ROOM_TELEPORT=30,4,0,-128`／`30,4,0,128 SK_ZOOM=3 SK_MAX_FRAMES=60`；冰原＝`SK_SEED=5 SK_MAX_FRAMES=60`；斷言 `python tools/p217_pixel_check.py <證據目錄>` | `Level A1 (1-1) biome 1-forest` → `SK_ROOM_TELEPORT -> room 1 spawn point`；牆案另有 `SK_ROOM_TELEPORT offset (0,-128)`／`(0,128)`；像素斷言 `P217 PIXELS: OK`（灌木列頂 y=555、虛空＝`camera_bg`、無地板縫、牆後頂蓋蓋住腳） | 120 s |

### 1.2 固定對照樣本（V-D）
- enemies：boss01 max_hp=600；boss05=800；e_succulent=8；e_slime01 reward_rate=15；boss03 reward_value=[5,3,0,10]。
- characters：c01 max_armor=5, energy=180, atk=4, skill_cd=10；c12 max_hp=6（`RoleAttributePlayer.json` 單檔是 Druid，不可當模板）。
- weapons：weapon_000 atk=3 consume=0 bullet_speed=36 item_value=12；weapon_043 atk=15 consume=5。
- levels：B1 (1-1, chest 1, map_long 3, f601..f606, wall602/wall601)；B5 type2=[r1_boss3,r1_boss4,r1_boss6]；C1 (2-1, chest 3, f201..)；G1 (3-6, map_long 1, type2=[r_end])。
- boss chests：chest_boss01 level 2 weapon_077；chest_boss13 weapon_175；無 chest_boss05/06/11/12。
- pickups：coin_0 value 5 speed 20 range 6 frames objects_common_31..34；coin_1 3；coin_2 1；energy value 8 speed 14；_rgcoin.friction 0.9。
- P1.10（詳 `docs/evidence/p1_10_diff.md`）：`GameObject/ex_orc05.prefab` RoleAttribute.max_hp=50、EGun001.bullet_speed=16（1.07 為 10）；`res/effect/buff_statue_1.prefab` BuffStatue01.speed=12、count=4（1.07 為 24、1）；`res/enemy/1-forest/boss07.prefab` BossAI07.bullet01=bullet_e_24；`rgprefab/bullet/bullet_0.prefab` RGBullet.destroy_time=5；`res/enemy/e_battery01.prefab` EGun008.atk=5。
- colliders（P2.14）：c01 身體圓 r 0.4 @ (0,0.5)、受擊框 0.5×0.5 @ (0,0.7)；e_slime01 r 0.4 @ (0,0.5)、框 1×1 @ (0,0.5)；boss01 r 0.6 @ (0,1.0)、框 1.2×1.8 @ (0,1.3)；bullet_0 框 0.5×0.4 @ (0.25,0)；e_shark body_scale 1.5。
- statues：7 座 statue_01..07，item_value 皆 15；底座 objects3_6、光 objects3_7 @ y 0.69375；身體 1→objects3_0、2→objects3_3、3→objects3_2、4→objects3_1、5→objects3_4、6→objects3_5、7→objects3_3 @ y 1.1。

---

## 2. 現況總表（截至 2026-09-12，分支 `feat/phase3-presentation-content`，HEAD 見 §10 最新報告）

| 區塊 | 功能 | 驗收 | 主要 commit | 缺口 |
|---|---|---|---|---|
| P1 資料層 | 進行中（P1.1–P1.7、P1.10–P1.12 功能完成） | P1.10 驗收完成（有取捨）；其餘未驗收 | 12c3750 … 本版 | P1.8 內嵌物件、P1.9 未解析掉落物、P1.13 每實例子彈／敵槍表（T-30、T-31） |
| P2 地圖 | 進行中（P2.1–P2.9、P2.14、P2.15、P2.17、P2.18 功能完成） | P2.14、P2.15、P2.17、P2.18 驗收完成（有取捨）；其餘未驗收 | 3eded10 … 本版 | P2.10–P2.13、P2.16 boss 圖缺；T-05 加級本體待原碼；P2.17 之外的畫面差異（手持武器、陰影、朝向翻轉、地板變體 RNG、裝飾牆、門開關圖、尖刺開合）見該列「未做」；P2.18 之外的碰撞差異（Box2D 的質量／推擠、外牆外虛空無碰撞、原版出生點取法）見該列「未做」 |
| P3 戰鬥忠實度 | 進行中（每擊數學、dash、AI 派發、18 boss brain、P3.0、P3.0b、P3.1、P3.2、P3.3.a/b/c 功能完成） | P3.0、P3.0b、P3.2、P3.3.a、P3.3.b、P3.3.c 驗收完成；其餘未驗收 | phase 1–5、本版 | P3.4–P3.9、P3.3.c 效果未移植部分（T-43–T-48）、boss 與敵彈速度讀資料（T-54／T-55）、子彈掃掠碰撞（T-57） |
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
| P2.14 人物比例與碰撞框 | 使用者回報人物比原版小：地圖與物件畫 2 倍，玩家／敵人／子彈／地上武器卻是 1 倍、boss 1.5 倍。改為一律與地圖同比例，碰撞改讀預製件 | FAITHFUL：人物與敵人圖集 `spritePixelsToUnits: 16`（SpriteAtlasTexture-character01）；c01／e_slime01／e_yeti01／boss01 身體 localScale 1（boss01 的 1.5 只在 shadow／shadow_lock／point_1）；58 種 ex_* 精英與 e_shark 身體 1.5／2.0 由資料 `body_scale` 帶入；每幀 sprite pivot（c01_4 為 0.48643634,−0.0017742697，`Assets/rgtexture/character/c01_4.asset`，底部置中）寫入 sprites.json；子物件 Transform 位移逐層合成到根空間（`q' = localPosition + localScale·q`，根物件只貢獻縮放）：`sprite_offset`（boss05 (0.2,0.3)、boss11 (−0.074,−1.1)、boss12_1/2 (0,−0.2)、e_creeper_fly／e_ufo (0,−0.7)、ex_ 版 (0,−1.1)、e_mosquito (0,−0.2)、ex_mosquito (0,−0.35)，其餘為 0）與碰撞 offset（mineral_gold／mineral_crystal 身體圓 y 0.5→1.3）；碰撞：子物件 `collider` 圓（Body 層，擋牆不擋子彈）＋根物件 IsTrigger 受擊框（子彈打這個），依 Physics2DSettings 碰撞矩陣確認 | `include/game/WorldScale.hpp`（kSpriteScale＝kCellPx/16＝2，全物件共用）、`include/entities/SpriteAnchor.hpp`（腳底對齊 pivot）、`include/sim/BodyGeom.hpp`（單位→px、子彈圓對受擊圓／框）；characters／enemies／bullets.json 的 `collider`／`hurtbox`／`body_scale`；c01 身體圓 r 0.4@(0,0.5)、受擊框 0.5×0.5@(0,0.7)；boss01 r 0.6@(0,1.0)、框 1.2×1.8@(0,1.3)；bullet_0 框 0.5×0.4→內切圓 6.4 px；玩家碰撞半徑 16→12.8 px，門鎖內縮 49→45.8 px | test/ScaleColliderTest.cpp 20 例（ScaleTest 2、ColliderDataTest 10、BodyGeomTest 3、SimColliderTest 2、SpriteAnchorTest 3：縮放常數、四種實體與 bullet_0 碰撞值、e_shark／ex_slime01 body_scale、全英雄全敵人有碰撞、邊界內 0.5 px 命中／外 0.5 px 不中且舊 20 px 邊界兩者皆不中、pivot 腳底在身體中心下 16 px、礦物身體圓 y 1.3、boss11 繪製 pivot 下移 35.2 px、boss05 偏移、非零 sprite_offset 清單鎖定）；BuffTriggerTest 驗資料半徑與 4 px 退回值兩次執行的首擊 tick 各自等於由紀錄位置獨立算出的掃掠接觸 tick（v1.39 起；「恰早一 tick」原為步長對齊的巧合）；tools/tests 8 例；V-S `docs/evidence/scale/`：scale_before／scale_after（騎士 0.8×0.9 格→1.54×1.69 格，原版約 1.6×1.8）、enemies_after、boss_after（雪人 boss）、boss05_after（sprite_offset (0.2,0.3)；boss11 在現行 boss 池抽不到，只由單元測試覆蓋，見 P2.15） | T-52（子彈框取內切圓且忽略偏移；敵彈無預製件沿用 4 px；e_snowman_temp 無身體碰撞，取受擊框內切圓與其根空間 offset，目前算得 16 px）| 功能完成・**驗收完成（有取捨 T-52）**（R29 @ 455789a） |
| P2.15 boss 池依當層生態 | `GameData::ChapterBossPool` 以章節固定生態（冰原／城堡／外星）選 boss，忽略當層實際變體（A/B、C/D、E/F），故森林／墓地／火山的 boss（如 boss11「3-volcano」）永遠不會出現；與 P6.7 BGM 原本的錯誤同型（小怪池早已讀 `levels.json` 當層 `enemies`，不受影響） | levels.json 每層 biome；enemies.json 每隻 boss 的 biome（取自 res/enemy/<biome>/boss*.prefab 資料夾）：1-forest boss07/08、1-ice boss09/10/13、2-castle boss01/02、2-graveyard boss03/04、3-alien boss05/06、3-volcano boss11（boss12_1/12_2 見 T-58） | `GameData::BiomeBossPool(biome)`（篩選與依 id 排序同舊碼），`ChapterBossPool(c)` 改為 `BiomeBossPool(ChapterBiome(c))`；GameScene 有當層資料時用 `BiomeBossPool(LevelBiome())`，否則退回章節生態；選取仍是 (slot, bossRoom) 的純函數、不多抽亂數（golden 不變）；新增 log `BossPool: biome <b> -> <id> (pool N)`；`SK_ROOM_TELEPORT` 加可選偏移，v1.43 起嚴格解析（見 §1.1） | ChapterProgressionTest 新增 2 例：六生態 boss 清單逐一等式、未知生態為空、`ChapterBossPool(1..6)` 等於對應生態池；A1–F5 全 30 層池非空，池內每隻 maxHp>0、有 enemyClass、有 sprite 幀、`MakeBossBrain` 有對應 brain（不退回 BossAI01）；V-S `p215_boss11`（seed 5 → F5 火山、`BossPool: biome 3-volcano -> boss11 (pool 1)`，玩家下移 160 px，boss 在上方呈扁平岩漿生物）、`p215_boss11_full`（zoom 0.5）、`p215_forest`（seed 1 → A5、boss08 清楚可見）、`p215_graveyard`（seed 1 → D5、boss04 只畫出頭頂藍寶石幀，見 P2.16）；四案皆斷言 `SK_ROOM_TELEPORT offset (0,-160)`；TestHooksTest 9 例（K／R 整數溢位、dx 合法 dy 非法時兩者皆歸零、兩欄、四欄含負數小數、欄數 0/1/3/5、空欄與尾隨雜字 `12x,4`／`12,4,0,5px`／`12,4.5`、nan／inf／-inf／±1e999、4096 收而 4096.5 拒、拒絕時偏移歸零並帶原因）；證據案 `teleport_nan`（`12,4,nan,0` 印拒絕行且 0 次傳送）；ctest 2167 | T-58（boss12 兩半體不生成） | 功能完成・**驗收完成（有取捨 T-58）**（R34 @ c1f8642） |
| P2.16 boss 圖補完 | P2.15 讓森林／墓地 boss 可抽到後發現圖缺：boss04 本體動畫只引用寶石幀 boss04_5–8（實際 9×7、9×6、9×5、9×5，另有表情幀），沒有本體；boss06、boss07 沒有 `ide`／`run` 狀態（boss06 只有表情 biaoqing_*，boss07 只有表情與 x_ide／x_dead），`SpriteFrames` 退回其他狀態；現有測試只驗幀數非空 | 各 boss 預製件的 SpriteRenderer／Animator 與 AnimationClip | sprites.json 補 boss04 本體與 boss06／07 正確待機／移動狀態 | 每隻 boss 待機狀態存在且尺寸合理的測試；V-S 三隻 boss 截圖 | — | 未開始・未驗收 |
| P2.17 渲染版面第一刀（pivot／排序層／背景色／地板縫） | 使用者回報畫面仍不像原版、人物像「卡在牆裡」。唯讀診斷（diag_C_assets 項 1／4／6、diag_A_char_collision RC2）找到四個版面錯誤：(a) `addObjSprite` 把所有房間 sprite 一律底部對齊，但原版約 26 種 16×16 sprite 的 pivot 是 (0.5,0.5)（灌木牆、木箱、桶、樹、火盆等，美術只在上半），30／31 層受影響（G1 除外），整體高出 0.5 格（1280×720 下 24 螢幕 px），牆／箱下留一條裸地板；(b) 玩家／敵人 z=5、boss 6，牆與箱的 w2 頂蓋 z≈1.2–1.3，角色站在牆／箱後面時腳和下半身蓋在頂蓋上，看起來嵌進去；原版頂蓋在 sorting layer 6 高於角色 layer 5；(c) `camera_bg` 解析後從未套用，格外虛空純黑；(d) PTSD 預設雙線性＋CLAMP_TO_BORDER，相鄰地板間出現深色格線 | FAITHFUL：`rgtexture/**/<stem>.asset` 的 `m_Pivot`（objects2_30/31/32/33/34/48/49/50/55/56/57/62/63/68、objects1_5/14/20/26/30/32/34/85、objects4_5/20 為 (0.5,0.5)，另 objects1_2／objects1_22（boss 房 box_weapon／box_spell 頂蓋）與 texiao_01 為 (0.5,0.5)、shadow0／shadow1 為 (0.5,0.3)；tile_pivots.json 共 36 筆非預設＝26 筆中心 pivot 道具＋texiao_01＋statue 7 筆＋shadow 2 筆；statue 的 objects3_0–6 為 (0.6,0)…(0.5,0.1)；牆 w*／地板 f*／door01–03／shadow2 為 (0.5,0)；sprite 的 `m_Offset` 恆等於 (pivot−0.5)×尺寸〔w601 y＝−12；quad 中心＝pivot 點−m_Offset，與 `PivotCentre` 的 (0.5−pivot)×尺寸同值〕，交叉驗證）；預製件 SpriteRenderer 的 `m_SortingLayer`／`m_SortingOrder`＋`ProjectSettings/TagManager.asset`（2 Floor、3 Shadow、4 Wall、5 Character、6 WallFont、7 Effect、8 UI）：wall601／wall_bush1／box02／door_n 底圖 layer 4、w2／door01 bar layer 6（order 2／3）、statue_01 底座 4、神像 6、光 5、sting01 底盤 2 order 1、`ding` 4 order 1；`MapManager.camera_bg`（map_A1／B1.prefab）；`Texture2D/*.png.meta` 圖集 `filterMode: 0`。實測參考影格（冰原 1-1、1280×720，`f0000`／`f1500`）虛空 (7,25,49) 對 port B1 的 (7,26,49)，差 1／255 | 資料：`tile_pivots.json`（162 個 stem、36 個非預設；`build_gamedata.py build_tile_pivots`）、levels.json 每面牆 `top_sort`／`face_sort`與每個障礙 `sort`／`alt_sort`、boss_rooms.json prefabs `sort`／`alt_sort`、shop.json statues `base_sort`／`body_sort`／`light_sort`（`_prefab_sorts`，與 `_prefab_sprites` 逐項長度比對）；`include/game/RenderLayout.hpp`（`PivotCentre`／`CellAnchor`／`PlaceRoomSprite`／`SortZ`／`RoomSortZ`／`ClearColor`、非房間物件 z 常數）；GameScene 的房間 sprite（地板、牆、箱、障礙、門底圖、門影、門閂、boss 房、雕像三件、礦石、走廊）全走同一條 `PlaceRoomSprite` 路徑；不經它的只有自有座標公式的 VendingMachine、Chest、傳送門與雕像名牌，translation＝錨點＋(0.5−pivot)×尺寸×比例、z＝layer＋order×0.1−y×1e-6（y 取格底錨點，同一物件的底圖與頂蓋共用）；Player／Enemy／Boss／Bullet／Chest／CoinPickup／WeaponPickup／VendingMachine／傳送門改用同一組常數（相對順序沿用舊碼：地板＜影＜牆底圖＜傳送門光＜寶箱＜傳送門圖＜拾取物＜子彈＜角色＜boss；原版 transfer_gate 的 `light` 為 layer 4 order 0、寶箱 box05／box06 為 layer 4 order 0／2，與舊碼光 1.5＜寶箱 2.0 同向，故光 4.45、寶箱 4.5；舊碼寶箱與傳送門圖同為 2.0 平手，現為寶箱 4.5＜門圖 4.6；依原版 sorting layer 刻意上移的只有 w2 頂蓋／神像升到角色之上；另有一處平手拆解：寶箱＜傳送門圖，登記於 T-59）；破掉的箱連頂蓋一併隱藏（原本只藏底圖）；`GameScene::Render` 每幀以當層 `camera_bg` 清除畫面（`OnExit` 還原黑色）；房間 sprite 以 `Util::Image(path,false)` 點取樣（不改 PTSD 原始碼） | RenderLayoutTest 11 例（pivot 數學、`tile_pivots` 含 (0.5,0.5) 清單與 (0.5,0)／未知 stem 預設、A1 灌木牆底圖落在錨點而非高 16 px、冰原 wall601 位置不變、tile_pivots.json 全 162 筆逐筆斷言（相對錨點偏移＝(0.5−pivot)×尺寸×比例；測試實際比對的是相對修前底部對齊位置的 y 位移＝−pivot.y×高度×比例，兩式等價）（36 筆非預設全部移動、其餘不動；列名 26 個移動例與 8 個不動例）、全 31 層全牆與障礙：layer 6 的頂蓋高於玩家／敵人／boss（layer 7 光暈與 layer 5 房物由另一例斷言壓在角色之下）、底圖低於玩家（含 y=±20000）、A1 box02 頂蓋高於玩家而底圖低於、同層 y 排序與 order 勝過 y、非房間物件相對順序鏈、光暈與 layer 5 房物不與角色爭序、A1／B1 `camera_bg` 與 31 層皆非黑；忽略 pivot 或把頂蓋排在角色下方會使其失敗）；tools/tests 11 例（+3：pivot 值、缺 pivot 報錯、頂蓋層須高於底圖）；`validate_data.py` 新增 pivot／sort 檢查；`tools/p217_pixel_check.py` 對證據截圖斷言（對修前建置的同案截圖 5／6 條 FAIL，對修後 6／6 OK）；V-S `docs/evidence/p2.17/`（成對 before／after：A1 房 1、牆後、牆前、B1 起始房、森林 boss 房、地板縫 6 倍放大；單張：B1 參考影格對 port、雕像房 `statue_after`、A1 zoom 0.6 `a1_room1_zoom06_after`）與 `docs/evidence/v1.46/p217_*`；量測：灌木列頂 y 531→555（24 px＝16 world px）、虛空 (0,0,0)→A1 (5,31,40)／B1 (7,26,49)、地板縫暗線 5 條垂直＋1 條水平（各寬 2 px、深 13–30 亮度；偵測器取樣座標為 10＋2 個，相鄰合併後為此數）→0；ctest 2167→2178 | T-59（z 軸對應與 y 排序、非房間物件維持舊順序〔傳送門光＜寶箱＝原版 layer 4 order 0／0–2〕、光暈／layer 5 房物下壓）、T-60（`camera_bg`→相機賦值 INFERRED；點取樣只套房間 sprite）。**未做（屬後續切片，非本刀）**：碰撞與移動（出生在箱內、衝刺穿牆、碰撞形狀）、子彈（旋轉、碰撞、圖、死亡特效、原版在 layer 6）、手持武器、角色與牆／箱影子（shadow0@0.39 α、shadow3）、朝向翻轉、run／dead 動畫、地板變體 RNG（f505／f506 不出現）、裝飾牆 wall504 30%、門開關圖（door03）、尖刺開合、寶箱四層、各 sprite 的顏色（m_Color，例如 wall101 的 0.735 灰、box_spell 偏藍）、角色／子彈／特效／HUD 的點取樣、商店機器與寶箱的 pivot、光暈（texiao_01）的位置（原版 `light` 子物件 dy 0.3 且金色 α0.22）、`SK_ROOM_TELEPORT` 偏移只在無出生點的房（出口／boss 房）生效 | 功能完成・**驗收完成（有取捨 T-59／T-60）**（R36，工作樹基底 d83f80a） |
| P2.18 碰撞與移動第二刀（脫困／出生點過濾／掃掠移動與衝刺／dt 上限／碰撞形狀讀預製件） | 使用者回報角色仍會卡進箱子與牆（繪製順序已由 P2.17 修）。唯讀診斷（diag_A_char_collision RC1／RC3／RC4）找到三個碰撞成因：(RC1) 敵人出生點取自 `FloorBlock::ConnectedFloorCells`，它只看 RoomGen 格，不含箱／火盆疊加層（RoomGen 刻意不把箱寫進格網），而移動只測目的地圓、沒有「已重疊就推出」，所以出生在箱內的敵人每個目標位置也都重疊、被永久釘住（`SK_SEED=14 … SK_ROOM_TELEPORT=1,1` 房 1 三隻實跑 5 秒不動）；(RC3) `ApplyDash` 80 px 只測終點，可越過 1 格（32 px）牆；玩家 dtMs 無上限（卡頓 ≥ ~277 ms 可跨 1 格牆）；外牆外是無碰撞虛空；(RC4) 障礙碰撞一律簡化為整格 32×32：雕像原版 2×2 單位、obj22_xx 1.5×1.5、obj11_03／05 1×1.2、obj11_01／signs01 為圓，設計房 obj_index 8／9／10 完全沒有碰撞 | FAITHFUL（預製件資料）：13 個英雄與 147 個敵人／boss 預製件中的 134 個，根物件 `Rigidbody2D` 為 Dynamic（`res/player/c01.prefab` m_BodyType 0、m_CollisionDetection 1 Continuous）；6 個 Kinematic（e_battery01、ex_battery01、e_chest1、e_chest2、boss07、boss06_c），7 個沒有 Rigidbody2D（mineral_crystal／gold、e_succulent、ex_succulent、e_tentacle、e_tentacle_temp、boss12_parent）。INFERRED（物理結果）：Dynamic 剛體與靜態 Wall／Obstacle 碰撞器重疊時，Box2D 沿接觸法線把它推出、也不穿牆，所以原版推論不會永久卡在箱子裡；推出的位置與擦邊行為不是逐點還原（T-61／T-62），port 對 Kinematic 的 6 個預製件也一併推出（原版不會）；身體碰撞圓 r 0.4 @(0,0.5)（c01..c13，與 port 一致，未改）；障礙碰撞器取自各預製件的 `BoxCollider2D`／`CircleCollider2D`（root＝格底中心，中心＝root＋m_Offset 經 Transform 鏈）：box01..05／cask_*／brazier01／03／obj11_04／06／07／obj22_05..08 皆 1×1 @(0,0.5)；`statue_01..07` 的 `collider` 子物件 Box 2×2 @(0,1)（layer 0，tag Wall）；obj22_01 Box 2×1.5 @(0,0.75)（layer 0）；obj22_02／03／10 Box 1.5×1.5 @(0,0.75)（layer 13）；obj11_03／05 Box 1×1.2 @(0,0.6)（layer 13）；obj11_01 Circle r 0.5 @(0,1.4)（layer 0）；signs01 Circle r 0.5 @(0,1.25)（layer 0）；box_weapon Box 1.7×1 @(-0.15,0.5)；vending_machine_i Box 2×1、vending_machine_w Box 2.5×1、water_dispenser Box 1×1（皆 @(0,0.5)）；obj11_02、sting01／speed_up／speed_down 沒有實心碰撞器（裝飾或僅 trigger）；mineral_crystal／gold 的身體圓在 layer 8（Body），不算阻擋；阻擋層＝Default 0／Obstacle 12／Wall 13（`Physics2DSettings.asset` 為壓縮格式，解碼為 INFERRED，T-61）。診斷實跑與數字見 `scratchpad/diag_A_char_collision.md`（§2.2 為原版對 port 碰撞器表） | 資料：`tools/build_gamedata.py _prefab_blockers`（複用 `_parse_prefab`／`_to_root`／`_collider_json`，只取 enabled、非 trigger、GameObject 在阻擋層的 Box／Circle）→ `levels.json` obstacles[].blockers（360 格中 265 有實心碰撞器）、`boss_rooms.json` prefabs[].blockers（32 個中 23 個）、`shop.json` statues[]／machines[].blockers（7＋3）；`GameData` 的 `LevelObstacleDef`／`PrefabSkin`／`StatueDef`／`MachineDef` 新增 `std::vector<ColliderDef> blockers`；`validate_data.py` 新增檢查（形狀、有限、≤ 8 單位、每物件至多 1 個；box／cask／brazier／雕像／機器至少 1 個）。程式：`include/sim/Depenetration.hpp`（圓對 AABB／圓的最小位移推出：候選＝精確 MTV 法線出口＋四個面出口，取「不與任何阻擋重疊」中位移最小者，必要時迭代 ≤ 8 輪，仍出不來則取樣最近空位搜尋；`kPushOutEpsPx` 0.05 因 `Util::Overlap` 視相切為重疊）；`WorldCollision::Depenetrate`（預設＝取樣搜尋，`NullWorldCollision` 恆等）；`GameScene::Depenetrate`（收集半徑＋3 格內的牆／走廊／箱／已封的門後精確求解，驗證後仍重疊則退回通用搜尋）；`include/sim/BodyMove.hpp`（`MoveBody`：與走路相同的分軸滑牆，切成 ≤ 一個身體半徑的子步，每個軸向子步以 `WorldCollision::MoveBlocked` 測整段掃掠而非只測終點；開闊世界逐位元等於 pos＋delta；`flushToContact` 供衝刺貼牆）；`WorldCollision::MoveBlocked`（預設＝終點加每 max(r/4,1) px 取樣，僅測試替身使用；`GameScene` 覆寫為精確解：`Sim::SegmentBlockedBy`＝線段對圓／AABB 的 Minkowski 和，沿用 `SweptCollision.hpp` 的 `SegmentCircleEntry`／`SegmentRoundedBoxEntry`）；`SkillDash.hpp ApplyDash` 改為先脫困再 `MoveBody`（貼牆停下，不再整段取消）；`Simulation::MoveControllers` 先對每個存活敵人／boss 脫困（在步初快照之前）、再 `MoveBody`；`SimConfig.hpp kMaxFrameDtMs`＝`kFixedStepMs × kMaxStepsPerAdvance`＝160 ms 與 `ClampFrameDtMs`；`GameScene::Update` 在 `RecordTime` 之後夾 dt，玩家每幀先脫困再 `MoveBody`；`include/world/RoomBlockers.hpp`（`BlockerFromDef`／`FirstBlocker`／`DesignObstacleCollider`／`FilterFreeCells`／`ResolveSpawnPoint`）；GameScene 出生格＝只留身體圓不與任何牆／走廊／箱／已存在碰撞器重疊的格（順序不變、不抽亂數、全被擋才退回原清單），閘門位置、玩家起點、boss 出生點、獎勵箱都取過濾後的格；`SpawnEnemy` 以該物種身體半徑就座（`ResolveSpawnPoint`→`Depenetrate`，改位時印 `Spawn: … moved …`）；boss 同樣依其身體半徑就座；寶箱／商店掉落的武器經 `FreeDropPoint`；設計房 obj_index 1–4、8、9、10、11 的碰撞器、boss 房 box／solid、雕像、商店機器都改用預製件資料（沒有關卡資料時退回舊 32×32，8／9／10 維持無碰撞）；四個證據鉤子（見 §1.1） | DepenetrationTest 16 例（邊緣／角落最小位移、中心在方塊內、中心正中、兩阻擋相鄰不來回、窄縫夾住、1 格走廊重疊牆壁、1 格走廊內牆格正中、圓阻擋、雕像大小方塊與 32 px 身體、窄於身體的口袋由搜尋解開且確定性、4000 點屬性掃描：凡 resolved 結果必自由且位移 < 64＋r＋4 px〔斷言上界，不是「一格＋身體」〕、通用搜尋與精確解差 < 3 px、Null／自由不動）；BodyMoveTest 15 例（開闊地精確、開闊世界 3000 個隨機位移〔含多子步〕逐位元等於 pos＋delta、≤ 一子步＝逐軸掃掠滑牆 2000 點、**R37 反例**〔圓障礙 r16 於原點、身體 12.8、(-6,28.5)→(6,28.5)：兩端自由、中點重疊，精確世界與取樣世界都拒絕，身體留在原地〕、箱角擦邊〔對角越角與軸向越過頂面，兩端皆自由〕、精確掃掠對 4000 條隨機線段與 0.05 px 密取樣一致〔取樣命中必精確命中；以 r＋0.06 取樣仍無命中則精確必無命中〕、**舊的只測終點衝刺保留為回歸對照並實際穿牆**、衝刺起點距牆 0–80 px 步長 0.5 共 162 個起點全停在牆近側且貼牆 ≤ 0.06 px〔舊碼在其中 45 個起點穿牆，測試鎖定為 45〕、斜衝沿牆滑、起點在阻擋內先推出再衝、位移 50 ～ 1e12 px 與 64 個方向都不穿 1 格牆、NaN／Inf 位移不動、走路滑牆、`ClampFrameDtMs` 邊界值）；RoomBlockersTest 10 例（預製件碰撞器放在格底根上的數學、C1／D1／E1／F1／A1 關卡的 8／9／10 形狀逐值對預製件、boss 預製件 obj22_10／obj11_05／box_weapon、7 座雕像 2×2@(0,1)、三台機器、31 層 8／9／10 有資料才擋且身體站在其上必重疊、設計房 3 處 obj_index 8 ×4 關卡全數有碰撞器、**出生池掃描**：A1／A4／B1／C1／C4／D1／E1／F1 八層 × 108 間設計房 × 2 個 seed＝1728 次建房，連通地板格 337328 個中 27570 個〔8.2%〕身體圓中心被擋，**場景實際的波次取點（serial 0–7，公式 `(serial×7+3) % size`）13824 次中 1168 次〔8.4%〕落在阻擋內，過濾後兩者皆為 0**，且沒有任何一間房被濾空；大身體〔r 19.2／25.6／32〕2592 次取點中 1187 次被改到同房較寬的格、16 次需靠脫困；14 間 boss 房連通格 344 個被擋→0）；BodyCollisionSimTest 5 例（出生在 3×3 箱塊中央的敵人在第一個 tick 內被推出且睡著時不動、醒著後 250 tick 都不重疊並走離、boss 出生在雕像大小方塊內被推出、dt 100000／1e9／7 交替下在封閉小房的敵人 400 次 Advance 都不出房、單次 `Advance` 步數上限＝8＝夾制值）；鉤子測試套件 `RoomTeleportHookTest`（9，原有）＋`ForceDashHookTest`／`WalkDirHookTest`／`DtSpikeHookTest`／`TraceFramesHookTest`（各 1 例，四個新鉤子的嚴格解析；檔案為 `TestHooksTest.cpp`，但套件名不是 `TestHooksTest`，過濾請用 `--gtest_filter=*HookTest.*`）；RoomConnectivityDiffTest 4 例（差分比較本身的 1 個正例與 2 個反例：裸房四門互通（正例）；一個新規則完全蓋住單一門口而其餘三門仍互通→比較必須失敗〔舊的整房布林值會放行〕；既有已封閉的房再失去一對門→比較必須失敗；外加真實資料掃描，見下）；SkillDashTest 的「撞近牆」例改為貼牆停下（舊斷言要求整段取消）；tools/tests 11→22 例（碰撞形狀逐值、壞形狀／缺形狀／機器缺碰撞器必報錯；`test_p218_sweep.py` 9 例：以倉庫內的假遊戲（`tools/tests/fixtures/`）驗兩支腳本——掃描腳本：健康輸出通過；**某個 boss seed 零列而 boss 總列數仍達 8 時必須失敗**、某個敵人 seed 零列、blocked＝1、遊戲非零退出皆必須失敗；H3 A/B 腳本：健康通過、**遊戲非零退出必須失敗**、**NEITHER〔既無死亡也無清房〕必須失敗**、`unset` 案確實沒有 `SK_DT_MS` 且呼叫端的 `SK_DT_MS=999`／`SK_FOO` 都到不了遊戲、seed 固定為 20240607；輸出目錄建不出來〔唯讀沙盒〕時整組 SKIP 而非錯誤，假遊戲是已入庫的 fixture，只有輸出需要可寫）；`validate_data.py` GREEN；**ctest 2178→2232／2232，golden／replay／戰鬥雜湊套件全部未改且全過**（NullWorldCollision 下脫困與掃掠測試恆為「不擋」、開闊世界移動逐位元等於 pos＋delta，故無須更新任何 golden；v1.49 的精確掃掠改變的是有牆世界的擦邊行為，全套 ctest 通過、golden 檔案未動）；實跑（`docs/evidence/v1.48/`、`docs/evidence/p2.18/`）：AC1 seed 14 房 1 第 3 幀三隻敵人（e_boar01、e_orc02、e_boar01）`blocked=0`、第 250 幀各移動 169.7／134.4／107.5 px；該層過濾移除 91／822 個格；另掃 seed 1–30 第 0 層共 456 隻敵人 `blocked=1` 為 0，boss 層（seed 1／2／3／5／7 的第五關）10 筆 boss `blocked=0`（`docs/evidence/p2.18/seed_sweep.txt`、`boss_sweep.txt`，腳本 `tools/p218_seed_sweep.sh`：檢查每次遊戲退出碼與逾時、每個敵人 seed 與每個 boss seed 各至少一列、敵人列 ≥ 100 與 boss 列 ≥ 8、blocked 計數須為 0，任一不符即非零退出；結尾 `P218 SWEEP: OK` 存於 `sweep_status.txt`）；AC4 `Dash` 從 x＝2744.0 停在 2755.2（牆內面 2768、半徑 12.8，距牆 < 0.3 px〔斷言值；二分法精度約 0.05 px〕；舊的只測終點會落在 2824，圓邊 2811.2 超出牆外緣 2800）；AC5 5000 ms 尖峰被夾成 160 ms，該幀位移被牆截在 22.1 px（無牆時為 208 px/s × 0.16 s ＝ 33.3 px），隨後停在 2754.5，不穿牆；修前（`s14r1_f3_before.png`、`s14r1_f250_before.png`）與修後（`s14r1_f3_after.png`、`s14r1_f250_after.png`）截圖成對 | T-61（脫困＝Box2D 的幾何近似、阻擋層解碼 INFERRED、碰撞器取預製件資料但每物件只取第一個、無資料時退回整格）、T-62（掃掠子步＝身體半徑、衝刺貼牆停下且距離仍為 T-56 的手調 80 px、dt 夾制 160 ms）、T-63（出生／起點／boss／掉落點過濾與就座，沿用 T-04 的取點步幅）。**未做**：不移植 Box2D（無質量／摩擦／動量推擠；原版 Body 層彼此不碰撞 INFERRED，故角色之間仍不互推）；外圍牆外的虛空仍無碰撞（身體已不可能走進去，只剩鉤子傳送可傳出）；原版挑出生格的邏輯仍在 Ghidra 截斷處（T-04），過濾是保險而非還原；礦石仍用整格（mineral 預製件的身體圓在 Body 層）；`obj11_01`／`signs01` 的圓碰撞器中心在格上方 0.9／0.75 單位處，已照預製件放置，未與原版畫面逐點比對；設計房 8／9／10 與 1.5 寬方塊等新阻擋對房內連通的影響，以 `RoomConnectivityDiffTest` 做差分回歸（以 4 px 格點把每個阻擋膨脹 16 px 身體，對 W／E／S／N 四扇門各取「有效入口」＝本身未被擋的門口格點，洪填後逐**門對**比較：舊規則〔箱／火盆整格、8／9／10 不擋〕下連通的門對，新規則下必須仍連通；入口被完全擋死的門視為失去其所有門對；所有門開著；預設每個「障礙碰撞器簽名」取一層：31 層中 30 層有障礙表、收斂為 7 種簽名 × 108 間設計房 × 3 個 seed＝2268 組＋14 間 boss 房 × 3 seed＝42 組，約 9 秒；`SK_CONN_FULL=1` 跑全 30 層 9720 組＋boss 42 組，約 35 秒；G1 無障礙表，場景對它退回舊規則，等同舊規則、不另跑；verifier 的 10044 組是 31 層〔含 G1〕，本測試全量是 30 層 9720 組）：**失去的門對＝0、失去的門＝0**（預設 12978 個舊規則連通門對、全量 55620 個，設計房與 boss 房皆然）；有 6 間房〔r1_50／52／55／74／75／78〕在新舊規則下**都**「封閉」（有效門口不全互通，540 組＝6 房 × 30 層 × 3 seed），原因是**可破壞的箱被當成永久牆**（遊戲中子彈可打破箱，故實際可通；本測試不把這 6 間算失敗，只斷言「不新增」），這是此測試的已知限制；門（door_e／w 1×5）碰撞仍沿用整格封門；出生點過濾把門封印帶（即使門開著）也算阻擋，所以離門一格內放不下大身體的物種會被改位 | 功能完成・**驗收完成（有取捨 T-61／T-62／T-63）**（R41，工作樹基底 0912b66） |

### P3 戰鬥忠實度
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P3.0 單位換算 | 資料速度／距離（Unity 單位）統一為 1 單位＝32 世界 px（＝一格 kCellPx，與 P2.14 人物比例、技能幾何同一來源） | 使用者 2026-10-09 裁決；圖集 `spritePixelsToUnits: 16` × kSpriteScale 2；Rigidbody2D 速度、RGCoin speed/range 為單位／秒、單位 | `Sim::kDataSpeedToPxPerSec = Game::Scale::kCellPx`（不再有獨立 15）；稽核所有速度路徑：武器子彈 ×32（weapon_000 36 u/s：540→1152 px/s）、敵人走速原本**把單位當 px 用**（3–12 px/s，幾乎不動）改 ×32（96–384 px/s）、玩家走速原本私有 ×30 改 ×32（c01 6.5：195→208）、金幣磁吸 speed/range/散射 ×32、BossAI01 撤退門檻原本拿 px 比單位改成除以 32、擊退 repel 改為單位制（T-53）；`SK_DT_MS`（固定幀時長）與 `SK_TRACE_BULLETS`（印子彈座標）兩個量測鉤子 | SpeedScaleTest 5 例（常數＝32 且同源；weapon_000 子彈（weapons.json bullet_speed 36；bullets.json bullet_0.speed 10 不用於飛行）0.5 s 飛 18 格、1.0 s 飛 36 格，誤差 0.01 格；e_slime01／e_yeti01 每秒走 v 格，控制器與 Simulation 各驗；boss 腦拿到的距離為單位）；改動期望值的既有測試逐項記於 §9「P3.0 實作紀錄」；golden 未變；V-S `docs/evidence/speed/`：子彈三幀截圖量得 35.9 格／秒（資料 36） | T-53 T-54 T-55 T-56；T-57 為掃掠碰撞剩餘近似；T-25 關閉 | 功能完成・**驗收完成（有取捨 T-53–T-57）**（R32 @ f55f2dd） |
| P3.0b 子彈掃掠碰撞 | P3.0 後子彈最快每步約 41 px，逐步離散檢查連正面都會穿過迎面敵人（Codex R30 反例：weapon_033 半徑 8、64 u/s 對 e_alien01 半寬 12.8、5 u/s，相對步長 44.16 px > 命中區間 41.6 px） | — | `include/sim/SweptCollision.hpp`：以相對運動檢查整步軌跡，圓形受擊框解線段對圓、方形受擊框用精確圓角矩形（兩個加寬矩形＋四個角圓）；多目標取最早 t，平手照舊掃描順序（敵人索引→boss）；`BodyGeom::HurtSweepBeforeWall`：子彈走完整一步，與牆的接觸比例 tw 在**同一個 t 軸**比較，只接受 t_hit ≤ tw（平手目標優先）；牆查詢與 `DamageObstacle` 依子彈 id 逐顆處理，同一步前一顆子彈打破的箱子不再擋後一顆；`WorldCollision::SweepBlocked` 取樣間距 max(r,1)、二分 8 次；`kMaxPlayerSweepPx` 64 | SweptCollisionTest.cpp 17 例（SweptCollisionTest 13、SweptPrimitiveTest 2、SweptOracleTest 2）；獨立預言 `test/SweptOracle.hpp` 不呼叫任何正式碰撞程式（每步切 2000 子步、自寫雙精度重疊判定）：Codex 反例整模擬 2816 次對齊，命中 2774、其中舊規則會穿過的 **226 次**皆釘成等式，且每次命中步與預言相同；24000 次隨機抽樣 HurtSweep 與預言一致（預言命中 7967、起點重疊 106、錯誤 0）；20667 次牆序抽樣 0 不符；R31 反例（tw＝0.48828125，接觸時中心 y 23.4375）不中（原反例無牆亦不中）；目標 y 終點改為 20 的變體：無牆 t＝0.52 命中、有牆不中；終點改為 18：牆前 t＝0.3714 命中且牆未受傷；移動目標加牆 4224 次整模擬（目標先 1719 次牆未受傷、牆先 2483 次恰受傷一次、22 次近似平手跳過比較）；平手 0.5 中、0.4999 不中；1-hp 箱一步兩彈：第一彈破箱、第二彈同一步通過破箱位置並在後續步命中箱後敵人；2-hp 箱兩彈皆消耗、敵人未中（以還原舊前置查詢驗證會失敗）；golden 未變 | T-57（目標步內直線移動；牆取樣近似；玩家位移 >64 px 或首步視為靜止；起點重疊 t＝0 命中，同時卡在牆與目標內時目標優先；接近平手約 0.03–0.1 px 內目標優先） | 功能完成・**驗收完成（有取捨 T-57、T-52）**（R32 @ f55f2dd） |
| P3.1 Buff 本體 | Fire/Poison/Ice/Electric/Armor/ChangeSpeed 的附加／計時／生效 | 可見：六個 ctor 寫 is_enemy/buff_time；BuffChangeSpeed__Start → RoleAttribute__ChangeSpeed(changeValue) 呼叫（game_typed.c 86553–86555）；BuffIce__BuffEnd 呼叫 CancelInvoke（86943–86945，只證明取消某排程）；BuffArmor__GetArmor 存在（86492–86503）；BuffFire__GetHurt 86867–86886 無傷害常數；BuffElectric__Start 86760 起。不可見／推論（T-32）：tick 間隔／傷害、電擊長度、護甲量與暫時護甲語意、changeValue 加法性 | include/combat/Buff.hpp（BuffList：Apply 刷新不疊加、Tick 固定 dt）、Simulation::ApplyBuff/TickBuffs、buffs.json 每列 `_inferred{tick_sec,tick_damage,speed_mul,armor_delta,_why}`、SK_FORCE_BUFF | BuffTest 33 例（六種各覆蓋 Has／到期＋ended／重上刷新不疊加／到期後完全還原；Fire 非整除時長 2.75 s 恰 2 tick、0.99 s 零 tick；Ice／Electric 重上只 ended 一次；兩種 buff 共存）＋ SimulationTest 3 例（火焰扣血走 Damage 路徑；滿甲 5/5 上護甲到期仍 5；3/5 上護甲受傷後到期只扣仍存在的量）；H9；V-G（六種必有 `_inferred` 與 `_why`） | T-32（數值 INFERRED；changeValue 加法→乘法換算） | 功能完成・未驗收 |
| P3.2 Buff 觸發 | 帶 *BuffTrigger 的子彈命中時上 buff | FAITHFUL：BuffEffectTrigger__TriggerWith game_typed.c 86608–86645：`probability <= 0` 不擲骰，**只在非暴擊且 camp==0 時返回**（暴擊或其他陣營仍上 buff）；`probability > 0` 擲 `Random.Range(0,100)`，`probability > roll` 才上。INFERRED（T-33）：三種無機率欄位的觸發器在截斷後無法排除隱藏判定；RGLaserBuff 4% | bullets.json 每列 `buff{kind,buff_prefab,trigger,probability,_probability_inferred}`（26 顆：ice 10／poison 6／fire 6／electric 3／changespeed 1；含 8 筆 sword/spear/laser 新列，全表 211）；`WeaponDef.bulletPrefab`、`GameData::BulletForWeapon`；FireIntent/BulletState 攜帶 buff 與 camp；`Simulation::ShouldApplyBulletBuff(probability, isCrit, camp, rng)` 由 `RollBulletBuff` 於命中後呼叫 | BuffTriggerTest 17 例（5 筆資料樣本；命中上 buff＋DoT；機率 0 非暴擊不上、暴擊上；零機率×暴擊×陣營矩陣；`>` 邊界；100% 必上；固定 seed 可重放；無 buff 武器 golden 不變）；H10（含 `Buff: Fire ended`）；V-G | T-33；NOT PORTED：敵彈→玩家 buff（EnemyGunDef 無 bullet 連結） | 功能完成・**驗收完成（有取捨 T-33）**（R16 @ 8a16e95） |
| P3.3.a 技能稽核（C04/C08/C10/C13） | 已移植四角技能的反編譯稽核＋可驗證 | game_typed.c：C04 RoleAtk 1303–1399、KillSomeOne 1399–1439、RoleSkillEnd 1214–1263（完整可見）；C08 ctor 2136–2152、GetHurt 2235–2261、Update 2204–2211；C10 HurtSomeOne 3360–3395、Update 3083–3110；C13 3827–3854／3988–4020／4104–4139；characters.json skill_cd／in_skill_time 來自 c0N.prefab RoleAttributePlayer | 稽核表（§9 R18 前附於 commit 訊息／本列）：全部 FAITHFUL 相符，兩處修正：C04 EndSkill 補 `has_cut=false`（RoleSkillEnd 可見）、C04 EndSkillEffect 註解由「energy−1」改為 `speed_rate +=`；GameScene 加 `Skill: <char> activated/ended` log | CharSkillC04/08/10/13Test（既有 4 檔＋C04 `EndSkillClearsHasCut`）；H12 四角 `SK_FORCE_SKILL=60` 各見 `Skill: c0N activated` | — | 功能完成・**驗收完成**（R18 @ fccff53） |
| P3.3.b 技能接線 | C04/C08/C10/C13 真正接進 Simulation（原 `StubSkillAdapter` 同幀 EndSkill，效果為空） | ICharSkill 新鉤子（`OnPlayerHurt`／`RoleAtkMelee`／`OnEnemyKilled`／`OnPlayerBulletHit`，預設 no-op）；技能鍵**邊緣觸發**（Advance 收到 false→true 即鎖存待處理按鍵，零子步也不丟，由下一個固定步消費一次；按住只算一次，放開後再按才算第二次）；C04 `RoleAtkMelee`→斬擊取代開火（cutDamage atk+3／+7）、`KillSomeOne` 於玩家擊殺；C08 `OnPlayerHurt`→`GetHurt` 盾吸收先於 ApplyToPlayer（盾值無下限，2231–2261 可見，T-37b）；C10/C13 開火時 arm（T-38）、玩家子彈命中時回甲，in_skill_time 自結；擊殺歸因：EntityState.deathSource（PlayerBullet／PlayerCut／PlayerDot／Other／Forced）在**第一次致死傷害**時鎖定（`deathLocked`，同一固定步內後續斬擊／DoT／子彈皆不可改寫）；混合 DoT 依來源分兩次結算（非玩家先）；Forced（KillAllHostiles／SK_FORCE_KILL）優先且不可改寫 | include/sim/ICharSkill.hpp、CharSkillAdapters.hpp、Simulation.cpp（`ApplyMeleeCut`、`ApplyDamageToPlayer`、Cull 歸因、`m_SkillHeldPrev`）、combat/CharSkillC04（`m_RefreshedThisWindow`）、SimConfig.hpp、`SK_FORCE_SKILL_PULSE=K1,K2` | CharSkillSimTest 26 例（邊緣觸發：單次 Advance 多子步只啟動一次、按住跨幀不切換、放開再按才結束、零子步 Advance 不丟按鍵、按住 100 ms 一次；C04 六殺序列的冷卻進度與刷新後再啟動、無擊殺結束正常扣冷卻、結束後不再斬擊；C08 盾吸收與關閉後致死；C10/C13 六次命中：第 3、6 次各回甲 +1，第 4、5 次不回；擊殺歸因九例：強殺不算、非玩家 DoT 不算、玩家 DoT 算、混合 DoT 由致死 tick 來源決定（兩例）、同步內非玩家 DoT 致死後玩家子彈不改寫、玩家斬擊致死後非玩家 DoT 不改寫、強殺後在飛子彈同步落地不改寫（先開技能窗、以 Combo 為可翻轉觀測值）、強殺後同步斬擊不改寫；C01 golden）；H12 四角 `SK_FORCE_SKILL_PULSE=60,300` → `activated` → `ended` | T-36（斬擊 48 px／120°）；T-37（C04/C08 結束時機不可見→再按切換；C04 擊殺刷新後 EndSkill 不清冷卻）；T-37b（盾值無下限為可見原碼）；T-38（C10/C13 普通開火即 arm、skillStrengthen=false 未接） | 功能完成・**驗收完成（有取捨 T-36／T-37／T-38）**（R22 @ 7d86655） |
| P3.3.c 五角技能（C03/C05/C07/C11/C12） | 五個 stub 改為真本體並接進 Simulation | game_typed.c 可見段：各角 Update／Gate／RoleSkillEnd（C03 841–850／872–901／924–936、DeadChild 943–956；C05 1517–1526／1549–1577／1584–1638；C07 1937–1946／1969–1997／2004–2026、miss 退冷卻 2085–2097、第 6 隻蝙蝠結束 2098–2113；C11 3521–3530／3552–3580／3587–3601；C12 3674–3683／3706–3729／3736–3749）；截斷後以 DISASM（T-39）補：C03 0.4 s 延遲雷擊（14 u 最近目標、Linecast 牆、8 傷、10 u 連鎖 0.15 s）、C05 瓶數 1／強化 3、C07 6／10 隻蝙蝠與 BulletBat 飛行、C11 治療光（0.8 s 一跳、4 u、每跳 +1）、C12 召喚／斬擊分支；c03/c05/c07/c11/c12.prefab 的 skill_refs／skill_params（BulletBat.damage 5 等）經 build_character_roster → characters.json → CharacterDef | `include/combat/SkillWorld.hpp`、五角 brain、`CharSkillAdapters.hpp` 五個真 adapter（ICharSkill 加 `NeedsWorld`／`TickEffects`，預設 no-op）、`Simulation::BuildSkillWorld`／`ApplySkillEffects`、`DeathSource::PlayerSkill`、`tools/arm_disasm.py`；GameScene 印 `Skill: <char> effect <cue>` | 五角 brain 測試 61 例（C03 17、C05 8、C07 18、C11 10、C12 8）＋ CharSkillSimTest 15 例（敵人 50／100 HP 可觀測：C03 第 20 步 50→42、連鎖第 27 步、牆擋不中、無目標仍進冷卻；C07 敵 100→70、玩家 hp 3→5、滿血給 10 能量；C11 第 1／40／80 步各 +1、走出 4 u 不再補；C12 召喚後斬擊 50→45→40；C03 雷擊致死記 PlayerSkill、C12 斬擊致死記 PlayerCut，各配一個強殺不記的反例，以 `PlayerKillCount`／`LastDeathSourceForTest` 為可翻轉觀測）；tools/tests 5 例；H12 九角 | T-39 DISASM；T-40 技能幾何 1 u＝32 px；T-41 牆線段取樣與敵集合；T-42 強化／RGGameProcess buff 旗標未接；T-43–T-48 各角效果未移植部分；T-49 C12 RoleSkillEnd 不清 in_skill（FAITHFUL），未找到其他清除點故視為常駐（INFERRED）；T-50 C03 DeadChild 未接；T-51 表現層 | 功能完成・**驗收完成（有取捨 T-39–T-51、T-36）**（R26 @ c908354） |
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
| T-25 | **已關（v1.38，P3.0）**：資料速度原以 15 px/單位換算、無依據；使用者裁決統一為 32 px/單位（＝kCellPx） | 已解 | SimConfig.hpp `kDataSpeedToPxPerSec = Game::Scale::kCellPx` | — | — |
| T-26 | Ghidra 單例呼叫後截斷（系統性） | — | EnemyMaker.hpp 檔頭、SimConfig.hpp（引用處） | 多處 INFERRED | game_typed.c / frida |
| T-27 | **已關（v1.8）**：ex_orc05 改由 `GameObject/ex_orc05.prefab` 直讀（max_hp 50、EnemyAI01），資料閘豁免撤除 | 已解 | build_gamedata.py build_ex_orc05_row | A2–A5 精英池 | — |
| T-28 | boss 房門帶外物件（r2_boss3/4 的 rail，y=21）由抽取器丟棄並記錄 `dropped_outside` | PORT CHOICE | build_gamedata.py build_boss_rooms | 兩房各少數條軌道 | P2.11 |
| T-30 | 類別鍵列（Bullet01／BossAI07／BuffArmor／EGun001…）的數值取自路徑序第一個使用該類別的 1.7.10 預製件；同類別其他實例可能不同（如 EGun001 取自 ex_orc05） | PORT CHOICE | build_gamedata.py `_first_prefab_by_guid` | 敵槍／子彈數值 | P1.13 |
| T-31 | 78 顆非 RGBullet 家族子彈預製件（BulletFire/BulletGas…）未入表（bullets.json 現 211 列＝25 類別列＋178 預製件列＋8 筆 buff-only 列）；51 個武器與 18 **筆** boss 子彈引用（42 個不同 id）指向表外預製件，列入 `KNOWN_UNRESOLVED_BULLET_REFS` 後降為 warning，清單外為錯誤 | NOT PORTED（資料） | validate_data.py `KNOWN_UNRESOLVED_BULLET_REFS`（# T-31）、tools/tests/test_validate_data.py；C++ 自 v1.16 起載入 `WeaponDef::bulletPrefab`（`GameData::BulletForWeapon`） | 對應武器／boss 的子彈行為 | P1.13 / P3.6 |
| T-32 | 六種 buff 的 tick 間隔（1 s）、tick 傷害（1）、冰凍／電擊＝speed_mul 0、護甲 +1 與「暫時護甲：只記實際加到的量、到期扣 min(已給, 現有)」、changeValue 加法性與 `max(0,1+changeValue)`（只有下限）皆為推論；buff_time／changeValue／is_enemy 為預製件實值 | INFERRED | combat/Buff.hpp、buffs.json `_inferred._why`、build_gamedata.py `_buff_inferred` | 燃燒／中毒 DPS、冰凍手感 | P3.2 之後以實玩對照修正 |
| T-33 | (a) RGBulletBuffTrigger／RGSwordBuffTrigger／RGArrowBuffTrigger 無序列化機率欄位，且 OnTriggerEnter2D 在 GameObject 呼叫後截斷（Arrow 18779–18793、Sword 31953–31967、Bullet 47177–47183 只委派基底）→ 「必中」為推論（probability 1.0，`_probability_inferred`）；(b) RGLaserBuff `buff_probability` 4 無可見比較式，依 >1 視為百分比 → 4%；(c) ReboundBuffEffector 無序列化欄位、BuffRandomEffectTrigger（bullet_96 多 buff）未移植 | INFERRED / NOT PORTED | build_gamedata.py attach_bullet_buffs、Simulation.cpp ShouldApplyBulletBuff、GameData.hpp | 上 buff 機率 | P3.6 |
| T-34 | BGM High/Low 切換條件不可見（PlayBGM 只收現成 clip）；移植版：房鎖定→High、清房→Low；生態無 High 版時退 Low；曲目依關卡實際 biome（MapManager.bgm_clip）選取 | INFERRED | AudioPlayer／GameScene 房狀態處、GameData::BgmFor(biome, high) | 音樂層次 | 需原碼／實玩對照 |
| T-35 | audio.json `ui_effects` 六項中五項附 `_evidence` 預製件連結（player_hurt 無，僅音檔來源說明）；只有 chest_open 的使用語意可證（FAITHFUL），enemy_hurt／enemy_death／boss_death／player_death／player_hurt 的播放時機為移植版選擇（`_port_choice` + `_why`）；`fx_hit.wav` 為缺表時的唯一字面退回 | PORT CHOICE | build_gamedata.py ui_effects、AudioPlayer／GameScene 播放點 | 命中／死亡音 | 需原碼（RGMusicManager 呼叫點）對照 |
| T-36 | C04 斬擊命中範圍：48 px 半徑、前方 120° 弧（原版 RGSword collider 未恢復） | PORT CHOICE | SimConfig.hpp、Simulation::ApplyMeleeCut | 斬擊命中 | 需原碼／預製件 collider |
| T-37 | C04／C08 技能的結束時機不可見（RoleSkillEnd 1214–1263 可見但無呼叫點；C08 無 in_skill_time）：移植版以再按技能鍵切換結束；C04 在本窗擊殺刷新（KillSomeOne 1399–1439 非 wrap 分支）後 EndSkill 不再清 this_skill_time，且結束一律清 in_skill_effect／has_cut。T-37b：C08 盾值 `shield_value -= damage` 無下限、不溢出到 HP（2231–2261 可見） | PORT CHOICE / FAITHFUL(b) | CharSkillC04.hpp/.cpp `m_RefreshedThisWindow`、CharSkillC08、CharSkillAdapters.hpp | 技能持續 | 需原碼 |
| T-38 | C10/C13 於普通開火時 arm `skill_atk`（C13 3988–4020 只見 RoleAtk 開頭）；`skillStrengthen=false` 路徑未接（強化系統未移植） | PORT CHOICE | CharSkillAdapters.hpp C10/C13、Simulation.cpp TickWeapon | 回甲觸發、斬擊加值 | P3.3 後續 |
| T-39 | 證據類別 DISASM：game_typed.c 截斷後由 libil2cpp.so ARM 反組譯補回的邏輯與常數，註解附位址 | 證據類別 | 各 CharSkillC03/05/07/11/12、tools/arm_disasm.py | 可信度低於 FAITHFUL | 可由 frida 實機驗證 |
| T-40 | 技能幾何（半徑、蝙蝠速度等 Unity 單位）以 `kWorldUnitPx = 32`（1 u＝1 格＝32 px）換算；v1.38 起子彈與移動速度同用此值，不再不一致 | PORT CHOICE → 與 P3.0 裁決一致 | SimConfig.hpp、WorldScale.hpp | — | — |
| T-41 | 世界快照近似：Physics2D.Linecast("Wall") 以每 8 px 取樣 WorldCollision::Blocks（半徑 1）；敵集合只含醒著的活敵＋boss | PORT CHOICE | Simulation::BuildSkillWorld、SkillWorld.hpp | 雷擊／治療目標選取 | 需完整物理 |
| T-42 | 未接線、預設 false：skill_strengthen（`c_level > 4`，DISASM 0x52612c）、RGGameProcess 的 buff_reflection／buff_vampire_hp／buff_digestive／buff_energy；brain 有 setter 與測試 | PORT CHOICE | 各 brain、adapter | 強化版技能不會出現 | P4.4 buff 三選一 |
| T-43 | C03 電擊暈眩擲骰（dizzy_rate 70，強化 +20，buff_ele）、雷電／光環視覺、鏡頭震動 | NOT PORTED | CharSkillC03 | 暈眩效果 | P3.2 之後接 buff 觸發 |
| T-44 | C05 BulletBottle 飛行、爆炸與毒氣區（Gas.damage 1）；sim 只發 `bottle_throw n= variant=i/N` | NOT PORTED | CharSkillC05 | 鍊金術士技能無傷害 | 需投擲物系統 |
| T-45 | C07 蝙蝠擊退 `GetForce(vel, 9.0)`、命中特效與音效未移植；衝刺目標消失則無命中返回；蝙蝠不受牆阻擋 | NOT PORTED / PORT CHOICE | CharSkillC07 | 擊退與牆 | 需物理 |
| T-46 | C07 回能量由 `Simulation::PlayerEnergyGrantedLastAdvance()` 回報、GameScene `AddEnergy` 套用（能量屬場景） | PORT CHOICE | Simulation、GameScene | 無 | — |
| T-47 | C11 寵物治療（每隻 5）與強化版 BuffArmor 子物件；sim 無寵物 | NOT PORTED | CharSkillC11 | 寵物 | P7.1 |
| T-48 | C12 雙狼召喚只發提示；斬擊階段走 T-36 近戰，傷害＝role_attribute.atk，暴擊參數丟棄 | NOT PORTED / PORT CHOICE | CharSkillC12 | 德魯伊召喚物 | 需召喚物系統 |
| T-49 | FAITHFUL：C12 RoleSkillEnd（3736–3749）不寫 in_skill；game_full.c FUN_0026b1dc（158490）在召喚時寫 in_skill=1。INFERRED：在 game_typed.c／game_full.c／game.c／game_named.c 只找到 C01/C02/C04/C05/C06/C07/C11 的 RoleSkillEnd 與 C03 DeadChild 會寫 in_skill=false，RGController 各方法皆不寫；故視首次召喚後 in_skill 常駐、之後每次施放為斬擊。未掃描原始 .so 的 `strb [rX,#0x55]`，不能證明原版完全沒有清除 | FAITHFUL＋INFERRED | CharSkillC12、CharSkillAdapters.hpp | 技能只召喚一次 | 實機驗證 |
| T-50 | C03 DeadChild（943–956）只在 brain 層實作與測試；sim 無英雄死亡／復活鉤子 | NOT WIRED | CharSkillC03 | 召喚物死亡處理 | P4.3 |
| T-51 | 表現層未移植：UpdateShadowLock 色調（含 C05 色值）、粒子、Animator 觸發、PlayEffect 音效、光環預製件 | NOT PORTED | 各 brain | 視覺／音效 | P6 |
| T-52 | 碰撞簡化：子彈 BoxCollider2D 取內切圓、忽略 offset（`EquivalentRadiusPx`）；敵人／boss 子彈沒有可解析的子彈預製件，沿用 4 px 半徑；缺身體碰撞子物件時（目前只有 e_snowman_temp），`MakeBodyGeom` 改用受擊框的根空間 offset 與內切圓（較短半邊）當身體；缺受擊框時子彈打身體圓；兩者皆缺才用佔位半徑 | PORT CHOICE | BodyGeom.hpp、SimConfig.hpp、Simulation::ResolveHits、validate_data.py 白名單 | 子彈命中邊界、該雪人推擠 | P1.13（敵彈預製件）／需完整物理 |
| T-53 | 擊退：repel 數值直接當 GetForce 力量（單位／秒，恆等），單位→px 只在 `EnemyController::ComputeVelocity` 做一次；舊版 ×30 在 px 制下每擊都頂到上限 28。原版 repel→力量的映射未恢復 | PORT CHOICE | EnemyController | 擊退距離（豌豆槍 repel 2：約 13 px／擊） | 需原碼 |
| T-54 | 敵人子彈速度固定 10 單位／秒＝320 px/s；舊公式「敵人走速×5」在真實走速下會變 800 px/s。`EnemyGunDef.bulletSpeed`（實值 6–24 u/s，146 種中只有 10 種有槍表）尚未接 | PORT CHOICE | 敵人開火路徑 | 敵彈手感 | P1.13 |
| T-55 | boss 移動 45 px/s 與 boss 子彈 300 px/s 為固定值，未改讀資料（資料 boss 速度 3–6 u/s＝96–192 px/s，接上會快 2–4 倍）；舊 Enemy／Boss 實體的 m_Speed 字面值未使用 | PORT CHOICE | BossController、entities/Enemy／Boss | boss 手感 | P3.5 |
| T-56 | 非資料的 px 預設值：缺物種走速時 60 px/s、WeaponController 裸參數 120 px/s、C02 衝刺 80 px、SK_AUTOWALK 300 px/s | PORT CHOICE | EnemyController、WeaponController、SkillDash、GameScene | 只影響缺資料或測試鉤子 | — |
| T-57 | 子彈掃掠碰撞的剩餘近似：目標在一步內視為直線移動（實際是先 x 後 y 的 L 形，差距小於一步位移）；玩家首步或單步位移超過 64 px 視為靜止；子彈對牆以取樣近似（不取樣起點、間距 max(r,1)、回傳的接觸比例是阻擋側，最多晚 1/256 個取樣間距），只會漏掉侵入掃掠膠囊不到約 0.134r 的擦邊，橫跨整條路徑寬度的牆不會穿；目標與牆同一 t 時目標優先（二分取阻擋側使約 0.03–0.1 px 內的近似平手也目標優先）；起點已重疊時 t＝0 命中，同時卡在牆與目標內時目標優先 | PORT CHOICE | SweptCollision.hpp、BodyGeom::HurtSweepBeforeWall、WorldCollision::SweepBlocked、Simulation::ResolveHits／IntegrateBullets、SimConfig `kMaxPlayerSweepPx` | 極端擦邊與平手 | — |
| T-58 | boss12_1／boss12_2 不進 boss 池：`IsBossId` 只收「boss＋數字」。依據：兩列資料相同（BossAI12、各 500 hp）、bosses.json 有 `BossAI12Parent`（boss12_parent，無 enemy_id）做血量協調、BossAI12 列只指向 boss12_1、BossAI12Adapter 註明複合血量未實作，故判讀為一隻 boss 的兩半（INFERRED）；port 因此不生成 boss12，火山層只會抽到 boss11 | PORT CHOICE | GameData.cpp `IsBossId`／`BiomeBossPool` | 火山 boss 少一隻 | P3.7（AI12 複合） |
| T-59 | 渲染排序（P2.17）：Unity sorting layer＋order 以單一 z 軸表示，z＝layer＋order×0.1−y×1e-6。FAITHFUL 的只有每個 SpriteRenderer 的 layer／order 來源（預製件）；同 layer 同 order 內以 y 排序（南側蓋北側）是 PORT CHOICE（原版同層同序的次序取決於 GraphicsSettings TransparencySortMode 2，未恢復），角色之間仍無逐幀 y 排序（Player／Enemy 同為 5.0）。非房間物件維持舊的相對順序而不照原版 layer：子彈（原版 `b` 多在 layer 6）、金幣（`coin_0` img layer 6）、地上武器（layer 5 order 1–2）留在角色之下（4.9／4.75／4.7）；boss 5.5 高於玩家 5.0（原版 boss body order 1 低於玩家 order 5）但仍低於 layer 6；原版 layer 5 的礦石／雕像光與 layer 7 的 texiao_01 光暈（原版金色 α0.22，port 無 per-drawable 著色）畫在 Wall 層頂端（4.4，角色之下），避免與角色同 z 隨世界 y 翻轉或蓋成白色大塊；shadow2 在 Floor 層道具（尖刺、速度板）下維持在底盤之下（2.05）。次序例外與證據：傳送門光（transfer_gate.prefab `light` layer 4 order 0）低於寶箱（chest00／01 `box05` 4/0、`box06` 4/2），與舊碼 1.5＜2.0 同向，故光 4.45、寶箱 4.5；傳送門 `img` 為 layer 5（4.6，仍在英雄之下）；舊碼寶箱與傳送門圖同為 2.0 平手，現為寶箱＜門圖 | PORT CHOICE | RenderLayout.hpp、Boss／Bullet／Chest／CoinPickup／WeaponPickup／VendingMachine／GameScene | 子彈與金幣仍畫在角色下方（原版在上）；同層次序可能與原版不同 | 子彈／拾取物切片 |
| T-60 | P2.17：(1) `MapManager.camera_bg`→`Camera.backgroundColor` 的賦值在 Ghidra 截斷處看不到，INFERRED；依據是 A1 (5,31,40)、B1 (7,26,49) 與實機參考影格虛空 (7,25,49)（差 1／255，B1 g 通道 0.1027×255＝26.19 對 25 的捨入差未查明）。(2) 點取樣（`Util::Image(path,false)`→PTSD `Texture::UseAntiAliasing(false)`：放大 GL_NEAREST、縮小 GL_NEAREST_MIPMAP_NEAREST；原版圖集 `filterMode: 0`）只套用到房間 sprite；角色／子彈／特效／HUD／傳送門保持 PTSD 預設雙線性，邊緣比原版柔。GL_CLAMP_TO_BORDER 不動（PTSD 為倉庫內 vendored 原始碼，不修改） | INFERRED／PORT CHOICE | GameScene::Render、`roomImage` | 角色邊緣較柔 | 表現層切片 |
| T-61 | 脫困與碰撞形狀（P2.18）。FAITHFUL（預製件資料）：13 個英雄與 147 個敵人／boss 預製件中 134 個 Rigidbody2D 為 Dynamic（c01 為 Continuous）、6 個 Kinematic（e_battery01、ex_battery01、e_chest1、e_chest2、boss07、boss06_c）、7 個無 Rigidbody2D（mineral_crystal／gold、e_succulent、ex_succulent、e_tentacle、e_tentacle_temp、boss12_parent）；碰撞器尺寸／偏移取自預製件。INFERRED（物理結果）：Dynamic 剛體會被 Box2D 沿接觸法線推出重疊的靜態碰撞器；port 對所有敵人／boss 一視同仁地推出，Kinematic 的 6 個預製件原版不會被推，是已知差異。PORT CHOICE：不移植 Box2D，以幾何近似「圓離開圓／AABB 阻擋的最小位移」：候選＝精確 MTV 法線出口＋四個面出口（中心在阻擋內時只有面出口），取不與任何阻擋重疊者中位移最小的一個，必要時迭代 ≤ 8 輪、仍出不來則以 2 px 環×32 方向取樣搜尋（192 px 內，只試 32 個方向，結果可能比真正最近點遠數 px）；出口多推 0.05 px（`Util::Overlap` 視相切為重疊）。INFERRED：阻擋層＝Default 0／Obstacle 12／Wall 13（Physics2DSettings 壓縮格式解碼）；每個物件只取預製件的第一個實心碰撞器（資料閘限制至多 1 個）；沒有關卡資料時 box／brazier 退回整格 32×32、obj_index 8／9／10 無碰撞；mineral 仍整格。沒有質量／摩擦／推擠，角色間不互相阻擋 | PORT CHOICE / INFERRED | Depenetration.hpp、WorldCollision.hpp、RoomBlockers.hpp、GameScene.cpp、build_gamedata.py `_prefab_blockers` | 擠出位置與 Box2D 不同：精確解＝最小位移＋0.05 px；取樣搜尋後備只試 32 個方向，可能比真正最近點遠數 px（測試實測 < 3 px） | 需原版物理才可逐點對照 |
| T-62 | 掃掠移動、衝刺與 dt 上限（P2.18）。FAITHFUL（來源鏈）：C02 大招呼叫 GetForce（game_typed.c:714），寫入方向與慣性速度（:43958）並交給 `Rigidbody2D.set_velocity`（:51043），遇牆不會被整段取消（R37 核實）。INFERRED：角色是 Continuous Rigidbody2D、Box2D 不穿牆；貼牆停下只是此結果的近似，不是逐點還原。PORT CHOICE：(1) `MoveBody` 把位移切成 ≤ 一個身體半徑（下限 1 px）的子步，每子步仍是分軸滑牆，且每個軸向子步以 `WorldCollision::MoveBlocked` 測**整段掃掠**而不只測終點。v1.48 的「兩端自由且步長 ≤ 半徑就不可能跨過任何阻擋」不成立（R37 反例：圓障礙 r16 於原點、身體 12.8，由 (-6,28.5) 到 (6,28.5)，步長 12 ≤ 12.8、兩端距離 29.125 > 28.8 皆自由，中點 28.5 < 28.8 重疊），已撤回並改為：`GameScene::MoveBlocked` 為精確解（線段對各阻擋的 Minkowski 和：圓＝線段到圓心距離 ≤ R＋r，AABB＝圓角方塊），故場景內以精確掃掠擋下擦邊（有回歸測試，只在此精確世界成立，不寫成無條件保證）；只回答圓查詢的世界（測試替身）用預設實作＝終點加每 max(r/4,1) px 取樣，**登記為近似**：可能漏掉深度達圓弧弓高 R−√(R²−s²/4) 的淺擦邊（s＝取樣間距、R＝身體＋阻擋半徑；小 s 時約 s²／(8R)，那只是近似值，不是對所有阻擋形狀的嚴格上界；12.8 px 英雄對 r16 圓為 0.04 px，最小 R＝12.8 時 0.10 px）；開闊世界（NullWorldCollision）逐位元等於 pos＋delta，golden 不變；位移超過 4096 px 一律截成 4096 px；(2) `ApplyDash` 先脫困再貼牆停下（舊版整段取消），距離仍是 T-56 的 80 px 手調值；(3) 場景 dt 夾在 160 ms（＝`FixedClock` 每次 `Advance` 本來就只跑的 8 步）：卡頓使遊戲變慢而不是讓玩家瞬移，13 個英雄走速皆為 6.5 單位／秒＝208 px/s，一個夾制幀 33.3 px；`RunController::RecordTime` 仍吃未夾制的牆鐘 dt。160 ms 是取捨：更小會讓 8 FPS 以下時慢動作更明顯，更大則破壞「場景步長 ≤ 模擬步長」 | PORT CHOICE | BodyMove.hpp、SkillDash.hpp、SimConfig.hpp、Simulation.cpp、GameScene.cpp | 極低幀率下遊戲速度變慢；走路停下時最多短一個身體半徑 | — |
| T-63 | 出生點過濾與就座（P2.18）。`ConnectedFloorCells` 不含箱／火盆疊加層，故原池含箱內格（掃描：波次取點 8.4% 落在阻擋內）。PORT CHOICE：出生池＝身體圓（英雄半徑 12.8 px）不與任何已存在的牆／走廊／箱／道具／已封門碰撞器重疊的格，順序不變、不抽亂數（`(serial×7+3) % size` 的取點公式不變，T-04 的步幅本就是取捨）、全被擋才退回原池；各物種再以自己的身體半徑就座（先試原點，再取同房最近的自由點，最後脫困）；玩家起點、boss 出生點、閘門位置、獎勵箱同樣取過濾後的格；掉落的武器經 8 px 探針脫困。原版出生格的取法（EnemyMaker.CreateEnemy）在 Ghidra 截斷處，即使原版不排除箱格，Box2D 也會把角色推出，故過濾是保險而非還原。行為後果（H3 A/B，`docs/evidence/p2.18/h3_ab.txt`，base 0912b66 對現況，同 seed 同 dt）：靜止不閃躲的 H3 英雄在現況更早陣亡——dt 30 base 清房、現況陣亡（第 208 幀）；dt 40／80 兩者皆陣亡、現況早 7 幀；牆鐘下 base 3／3 清房、現況 1／3。合理原因是敵人不再卡在箱內、出生在自由格並被重新就座、脫困後可走，因此較快抵達英雄；這是「不再卡住」的預期後果，三項修改（出生過濾／脫困／精確掃掠）各自的貢獻未隔離 | PORT CHOICE | RoomBlockers.hpp、GameScene.cpp（OnEnter、SpawnEnemy） | 出生分布略偏離原版（少了箱內格）；同 dt 下敵人較快抵達英雄 | 需原版 CreateEnemy 取點 |
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

### R18 — 2026-09-14 — Codex — 對象 P3.3.a＋P3.3.b @ fccff53 — **P3.3.a 驗收完成；P3.3.b 不通過**
來源抽查七組全部相符（C04 RoleAtk 1376–1387／KillSomeOne 1410–1428／RoleSkillEnd 1230–1237／EndSkillEffect 1287–1294；C08 GetHurt 2246–2261；C10/C13 HurtSomeOne 3368–3384／4112–4128）；資料相符；指定測試 35/35；gate GREEN；附件 2088。
P3.3.b 阻擋：Simulation.cpp:665 同一 `skill=true` 用於每個固定子步，一次按鍵先啟動下一步即切換關閉；C04 擊殺刷新後再按 EndSkill 又清冷卻、無擊殺手動結束留下 in_skill_effect；C10/C13 第 4／5 擊測試的敵人 3 HP 第三擊已死；缺 T-37 程式標記與 arm／強化分支登記；`lastHitByPlayer` 實為「曾被打過」，強制清場也算玩家擊殺；FORCE_SKILL 持續為 true 不能證明再按。
**回覆（v1.23）**：技能鍵改邊緣觸發（`m_SkillHeldPrev`）＋三例跨子步／跨幀／放開再按測試；C04 `m_RefreshedThisWindow`：本窗擊殺刷新後 EndSkill 不清 this_skill_time，結束一律清 in_skill_effect／has_cut，六殺序列冷卻進度與再啟動測試（skill_cd 11）；C08 盾值無下限為 2231–2261 可見（T-37b）；C10/C13 測試敵人 50 HP 觀測六次命中（第 3、6 次回甲，第 4、5 不回）；T-38 標 arm 與 skillStrengthen=false；擊殺歸因改為最後傷害來源（含玩家 buff DoT；KillAllHostiles 清旗標），測試為兩反例（強殺、非玩家 DoT）＋一正例（玩家 DoT）；`SK_FORCE_SKILL_PULSE=K1,K2` 取代持續按鍵，H12 改用；過期註解修正。ctest 2097。

### R19 — 2026-09-14 — Codex — 對象 P3.3.b v1.23 @ c3ce349 — **不通過**
C04 冷卻修正、C08 T-37b、C10/C13 六次命中、T-38 皆通過；44/44 指定測試、gate GREEN、T-01～T-38 齊。阻擋：`IsKeyDown` 為單幀脈衝，Advance 零子步時輸入被下一幀覆寫而丟失；Buff DoT 以「任一玩家來源」合併，玩家火傷＋非玩家毒傷致死仍算玩家擊殺；KillAllHostiles 只清旗標，Cull 前的玩家 DoT／命中可再設回；C08 切換處缺 T-37；GameScene.cpp:1127 仍寫共用模板／四角 stub；§9 R18「三例」實為兩反例＋一正例。
**回覆（v1.24）**：Advance 內偵測 false→true 鎖存 `m_SkillPressPending`，TickSkill 消費一次（零子步不丟；三例測試）；`TickResult` 拆 `damageFromPlayer／damageFromOther`，DoT 依來源分兩次結算（非玩家先），致死的那次決定歸因；`EntityState.deathSource` 列舉，`Forced` 不可改寫，Cull 只在 PlayerBullet／PlayerCut／PlayerDot 時呼叫 OnEnemyKilled；四例交錯反例；C08 加 T-37；GameScene 註解改正。ctest 2104。

### R20 — 2026-09-14 — Codex — 對象 P3.3.b v1.24 @ 4e4c823 — **不通過**
按鍵鎖存、混合 DoT 分結算、Forced 保護、C08 T-37、GameScene 註解皆通過；來源抽查（game_full.c 156478–156488／156975–156982／158184–158199）相符；24/24 指定測試；gate GREEN。阻擋：`SetDeathSource` 只鎖 Forced，Step 順序斬擊→DoT→子彈→Cull 之下，非玩家 DoT 致死後同步玩家子彈可改成 PlayerBullet、玩家斬擊致死後非玩家 DoT 可改成 Other；強殺測試未開技能窗、子彈在強殺前落地，`Combo==0` 非有效斷言；§9 R18 仍寫「三例反例」。
**回覆（v1.25）**：`SetDeathSource` 在第一次致死傷害時鎖定（`deathLocked`，Forced 仍優先）；新增同步交錯兩例（非玩家 DoT 致死→玩家子彈不改寫；玩家斬擊致死→非玩家 DoT 不改寫）；強殺例改為先開技能窗、子彈在飛、強殺後同步落地，以 Combo 為可翻轉觀測（已用舊邏輯回歸驗證會翻）；§9 R18 措辭更正。ctest 2106。

### R21 — 2026-09-14 — Codex — 對象 P3.3.b v1.25 @ 641bfd2 — **不通過（僅測試覆蓋描述）**
歸因鎖定、各傷害點順序、兩例交錯與重寫強殺例皆通過；26/26；gate GREEN；T-01～T-38 齊；§7 只有 8 不通過：§3 仍宣稱「強殺後待結算 DoT」而測試已無 DoT 設定；測試名 `…AndDot` 與註解「DoT test above」過期。
**回覆（v1.26）**：測試改名 `KillAttributionForcedDeathLocksOutAnInFlightPlayerBullet`、註解更正；§3 P3.3.b 覆蓋描述改為實際案例。純文件／命名變更。

### R22 — 2026-09-14 — Codex — 對象 P3.3.b v1.26 @ 7d86655 — **驗收完成（有取捨 T-36／T-37／T-38；T-37b 為可見原碼）**
diff 只有測試名／註解／文件；§7 全過（5 不適用）。已驗收切片：P1.10、P3.2、P3.3.a、P3.3.b、P6.7；架構書 R7 通過。

### R23 — 2026-10-04 — Codex — 對象 P3.3.c @ 9b10e32 — **不通過**
（第一次執行因暫存 CODEX_HOME 的 auth.json 遺失回 401，重建後重跑。）FAITHFUL 抽查六組相符；DISASM 實跑 `tools/arm_disasm.py` 確認 C03 雷擊 8、C05 尾端呼叫、C07 命中／未命中分支與 6／10、C11 治療 1／2、C12 in_skill 分支，DISASM 作為證據類別可接受；`build_character_roster()` 重算與 characters.json 完全相符；T-01～T-51 齊；指定 exe 100/100。阻擋：T-49「永不清」超出可見證據；C05 包裝函式寫死陣列長度 4；C03／C12 擊殺歸因測試只驗死亡、改錯來源仍會過；**H7 附件只剩三行開門（447 幀只走到第 3 層），腳本卻回 STATUS: OK**；brain 測試實為 61 例、§10 Python 5／3 例並存、§2 未同步。
**回覆（v1.29）**：T-49 拆為 FAITHFUL（End 不清、FUN_0026b1dc 召喚時設 1）與 INFERRED（四份匯出檔全掃無其他清除點；未掃原始 .so）；C05 `TryActivateSkill(variantCount)` 由 CharacterDef 傳入；C05 引用改 0x2661e8–0x2661ec；Simulation 加 `PlayerKillCount`／`LastDeathSourceForTest`，C03／C12 歸因測試改斷言來源與擊殺數並各配強殺反例（以錯誤來源回歸驗證會失敗）；C12 測試措辭與命名改正。證據腳本：新增 `require` 逐項斷言 13 案的必要事件（以 v1.28 舊 H7 驗證會報 4 個缺失），H7 改 700 幀、摘錄 20 行。§2／§3／§10 數字同步。ctest 2114。

### R24 — 2026-10-04 — Codex — 對象 P3.3.c v1.29 @ aaab948 — **不通過（證據腳本與文件）**
T-49 拆分、C05 資料化與 0x2661e8–0x2661ec、C03／C12 歸因正反例皆通過（指定 exe 282/282、gate GREEN、unittest 5/5、T-01～T-51 齊；來源核對 game_full.c 158451–158472／158489–158490／158514–158524）。阻擋：`require` 只驗存在、不驗順序與數量——刪掉 H1 的 B2、H3 前兩波、H7 boss 後開門、H11 死亡音或把 H4 縮成一筆仍會通過；§10 H7 行數（實 12、上限 20）與 H11 計數（槍 24、死亡 13、門 1、命中 1、BGM 1）不符；§2 驗收欄用了未定義的「審查中」。
**回覆（v1.30）**：腳本新增 `require_order`（依序；awk 以 ENVIRON 傳 pattern 避免反斜線被吃）與 `require_count`；H1 加 B2、H3 改為 9(15)→計畫→Active→9(6)→6(0)→cleared、H4 ≥3 筆、H6 先售價 10 再成交 10、H7 加 boss 後開門、H9／H9armor 加持續秒數、H10 改 applied→ended、H11 加死亡音。以 v1.29 附件驗證：原檔 0 失敗；照 R24 列的六種刪改各自報錯。§10 行數與計數、§2 狀態值改正。

### R25 — 2026-10-04 — Codex — 對象 P3.3.c v1.30 @ a3773b9 — **不通過（一個斷言漏洞＋一處文字）**
Codex 在暫存複本重做 R24 刪行：原附件 0 失敗；刪 H1 B2、H3 任一波、H7 全部 boss 後開門、H11 死亡音、H4 縮一筆皆失敗。漏洞：**只刪 boss 房那行開門仍通過**（後續樓層的開門行滿足泛用 pattern）。§10 後段仍寫「H7 摘錄上限改為 12 行」。另註：H9 的持續秒數斷言檢查的是 log 宣告值，非實測時間。
**回覆（v1.31）**：H7 先從 `BossRoom: room N` 取房號，再要求 `TransferGate: open in room N at` 依序出現在寶箱之後；§10 舊文字改為「上限 20 行（v1.16–v1.28 為 12 行）」。

### R26 — 2026-10-04 — Codex — 對象 P3.3.c v1.31 @ c908354 — **驗收完成（有取捨 T-39–T-51；C12 近戰沿用 T-36）**
H7 取 boss 房號 6 後斷言同房開門：原附件 0 失敗、只刪 boss 房開門 1 失敗；§10 行數一致；附件 STATUS OK、ctest 2114。§7 全過（5 不適用）。已驗收切片：P1.10、P3.2、P3.3.a、P3.3.b、P3.3.c、P6.7；架構書 R7 通過。

### 使用者回報 — 2026-10-04 — 人物比原版小
查證：地圖、寶箱、金幣、販賣機皆畫 2 倍（16 px→32 px），玩家／一般敵人／子彈／地上武器 1 倍、boss 1.5 倍；原版人物圖集同為 16 px/單位且身體縮放 1，故移植版人物只有原版一半（騎士約 0.8×0.9 格，應約 1.6×1.8）。使用者決定先修比例再決定 P3.0 速度。修正為 P2.14（v1.33）。實作過程：agent 一度因用量上限中斷後續跑；其回報的 boss_after.png 實際沒拍到 boss，已改用 440 幀那張（雪人 boss 在畫面中）。

### R27 — 2026-10-04 — Codex — 對象 P2.14 @ e7d6453 — **不通過**
核對通過：圖集 PPU 16；四種實體身體縮放 1（boss01 的 1.5 只在影子與定位點）；e_shark 1.5、ex_slime01 父 img 2 合成 body_scale 2；§1.2 碰撞數值全符；Physics2DSettings 層矩陣支持「Body(8) 擋牆不擋子彈、Bullet(11) 打 Body_P(9)／Body_E(10)」；1532 筆 pivot 與來源重算相符；八類實體皆用 kSpriteScale；2129/2129、gate GREEN、unittest 8/8；四張圖量得騎士約 0.8×0.9→1.55×1.7 格，boss_after 有雪人 boss。阻擋：抽取器未合成子物件 Transform 位移（礦物身體圓應 y 1.3 卻寫 0.5；boss05 (0.2,0.3)、boss11 (−0.074,−1.1) 的 sprite 錨點漏算，boss11 偏 35.2 px）；程式缺 T-52 標記且雪人退回規則描述不精確；「舊 20 px 邊界兩者皆命中」註解寫反；BuffTrigger 未真正驗「早一 tick」；§10 warnings／Python 例數與 H11 計數不符。
**回覆（v1.34）**：build_gamedata 逐層合成根空間位移，新增 `sprite_offset` 並套用到 SpriteAnchor，碰撞 offset 改根空間（非零清單見 §3 P2.14）；新增礦物、boss11、boss05、非零清單鎖定與 SpriteAnchor 偏移共 5 例；修正 20 px 註解並加兩個不中斷言；BuffTrigger 改斷言首擊恰早一 tick；T-52 標記補進 BodyGeom／SimConfig／ResolveHits，雪人規則改為「取受擊框內切圓與根空間 offset」。另發現 boss 池依章節固定生態、boss11 等永遠抽不到，登記為 P2.15。ctest 2134。

### R28（中斷）— 2026-10-04 — Codex — 對象 P2.14 @ ec016e0 — **未完成：OpenAI 用量上限**
中斷前已回報：指定 C++ 測試 37/37、gate GREEN（10 warnings）、Python 8/8；指出雪人載入警告仍寫「placeholder radius」，與 T-52 新規則不符。
**回覆（v1.35）**：GameData 載入警告依實際退回規則分三種文字（缺身體→`body -> hurtbox inscribed circle (T-52)`；缺受擊框或兩者皆缺→placeholder，此句 R28 指出有誤，v1.36 更正），實跑僅 e_snowman_temp 一行；ctest 仍 2134 全過。附件沿用 v1.34（只改 log 文字）。

### R28 — 2026-10-05 — Codex — 對象 P2.14 v1.35 @ 7918b0a — **不通過（兩處退回規則文字）**
功能全過：預製件直接核對 mineral_gold 圓心 y 0.8＋0.5＝1.3、boss05 (0.2,0.3)、boss11 (−0.074,−1.1)；146 筆敵人幾何重算與 JSON 全符；SpriteAnchor／BodyGeom 接線、新增 5 例、20 px 斷言、首擊早一 tick、T-52 標記與雪人規則皆成立；P2.15 屬實；37/37、gate GREEN、Python 8/8。阻擋：§3 P2.14 仍寫雪人「沿用 16 px」；GameData 把「有身體、缺受擊框」寫成 placeholder，實作（BodyGeom.hpp:105）是子彈打身體圓。另註：功能程式在 ec016e0，7918b0a 只改 log 與文件。
**回覆（v1.36）**：GameData 警告與註解改為「缺受擊框→子彈打身體圓（T-52）」；§3 改為「取受擊框內切圓，目前算得 16 px」；§6 T-52 補三種情況；§9 R28（中斷）回覆標註更正。

### R29 — 2026-10-05 — Codex — 對象 P2.14 v1.36 @ 455789a — **驗收完成（有取捨 T-52）**
fallbackText 三種情況與 MakeBodyGeom 一致（缺身體→受擊框內切圓；缺受擊框→身體圓；兩者皆缺→佔位）；雪人受擊框 1×1、offset (0,0.5)，算得 16 px；diff 2 檔 +13/−8 無行為變更。§7 全過。已驗收切片：P1.10、P2.14、P3.2、P3.3.a、P3.3.b、P3.3.c、P6.7；架構書 R7 通過。

### P3.0 實作紀錄 — 2026-10-09（送 Codex R30 前）
使用者先要求修人物比例（P2.14），驗收後裁決速度換算改 32。改動期望值的既有測試：BrainFactoryTest 與 WeaponBrainIntegration.Gun005 子彈速度 150→320 px/s（10 u/s × 32）；EnemyControllerTest 擊退速度 10→320 px/s（InertialVel 仍為單位制，衰減後 5）；CoinMagnetTest 範圍 90→192 px、速度 300→640 px/s，散射斷言由「<10 px」改為等比級數總和約 38.4 px ±1；SimulationTest.EmitsPlayerFireEnemyHurtAndDeathEvents 目標改 kinematic（真實 alien01 走速 160 px/s 會離開火線，該測試只驗事件）；CharSkillSimTest 兩個在飛子彈鎖定例重調幾何（子彈 100→50 u/s、敵人 x 115→120／205→225）以維持原本的命中順序；BuffTriggerTest.NoBuffWeaponUnaffectedByP32Wiring 由「恰早一 tick」放寬為「不晚、最多早一 tick」（每步 23 px 下多出的 2.4 px 只約 10% 機率跨過步界）；SimColliderTest 兩例改用 5 u/s 的慢速探針（真實速度下 0.5 px 擦邊會被步長混疊，原本是碰巧通過）。（v1.39 掃掠碰撞後：SimColliderTest 改回 36 u/s 並加 5／64 u/s 邊界；BuffTrigger 保留上下界並改為逐次對照獨立算出的接觸 tick。）golden 與 replay 全部未變、未重生。
證據鏈：只有 H3 的 `SK_FORCE_KILL_EVERY` 90→45（敵人會走近射擊，原節奏下英雄在第 3 波前陣亡，4 次中 4 次），事件順序與所有斷言不變；其他案例的幀數與斷言未動。v1.38 第二次整鏈 STATUS OK。
量測：weapon_000 子彈（36 u/s）以 `SK_DT_MS=20 SK_TRACE_BULLETS=1` 三幀間隔截圖，模擬座標 69.0 px／3 幀、截圖去除鏡頭位移後 103.5 螢幕 px／3 幀，皆為 35.9 格／秒；改前為 16.9 格／秒。

### R30 — 2026-10-09 — Codex — 對象 P3.0 @ d21ea10 — **不通過**
通過：kDataSpeedToPxPerSec 只由 kCellPx 推得；武器、敵人、玩家、擊退、磁吸／散射、BossAI01 距離皆入共同尺度，無殘留 ×15 或私有 ×30；T-54～T-56 有標記；測試改動合理（kinematic、幾何重調保留命中順序、慢速探針仍有中／不中反例）；三張速度圖與 48 px／格換算相符；28/28、Golden／Replay 140/140、gate GREEN、Python 8/8。阻擋：**T-57「正面命中不受影響」不成立**——weapon_033 子彈（半徑 8、64 u/s）對迎面 e_alien01（半寬 12.8、5 u/s）相對步長 44.16 px 大於命中區間 41.6 px，從 −22.08 跳到 +22.08 兩端皆不中；§3 P2.14 仍寫 BuffTrigger「恰早一 tick」；SpeedScaleTest 註解把 36 誤指為 bullets.json 速度（實為 weapons.json，bullet_0.speed 是 10）。
**回覆（v1.39）**：不只更正文字，改做掃掠碰撞（§3 P3.0b）：Codex 反例命中於 t＝0.029；2816 次對齊中 226 次是舊規則會穿過的；擦邊外仍不中；牆不會被穿；golden 未變。T-57 改寫為掃掠的剩餘近似；P2.14 與 SpeedScaleTest 措辭更正；SimColliderTest 改回真實速度。ctest 2149。

### R31 — 2026-10-09 — Codex — 對象 P3.0＋P3.0b @ 03b1b66 — **兩者皆不通過**
通過：相對運動、圓求根、圓角矩形聯集正確；R30 反例 t＝0.0289855 命中；步序、DeathSource、RNG、穿透與 canThrough 未變；36／10 來源與 SimCollider 真實速度更正；32/32、2149/2149、gate GREEN、Python 8/8。阻擋：(1) 子彈被牆截短後仍拿目標整步終點判斷，兩者不在同一時間軸——反例 tw＝0.48828125 時目標中心 y＝23.4375 應不中，卻在 0.8125 判中；(2) 所有子彈先查牆、之後才 `DamageObstacle`，同一步第一彈破箱後第二彈仍被舊的 wallStop 擋下；(3) 2816 次與 BuffTrigger 的「獨立預言」其實呼叫正式 `HurtSweep`，226 也只斷言 >0；§2 未同步 P3.0b、測試數應寫明各套件。
**回覆（v1.40）**：子彈改走完整一步，牆接觸比例 tw 與目標命中 t 同軸比較、t_hit ≤ tw 才算（平手目標優先）；牆查詢與破箱改為逐顆子彈依序處理；新增 `test/SweptOracle.hpp` 獨立預言（2000 子步、自寫雙精度判定、不呼叫正式程式），226 次釘成等式並逐次比對命中步；新增 24000 次與 20667 次隨機抽樣、R31 反例三種變體、平手規則、移動目標加牆 4224 次、同步破箱回歸（以還原舊碼驗證會失敗）；T-57 文字依實際行為重寫；§2 補 P3.0b。ctest 2156，golden 未變。

### R32 — 2026-10-10 — Codex — 對象 P3.0＋P3.0b @ f55f2dd — **兩者皆驗收完成（有取捨）**
同軸判定（原反例 25−3.2×0.48828125＝23.4375 > 22.4 不中；舊截短路徑假命中 0.8125 已被測試重現；平手 0.5 中、0.4999 不中）、逐顆查牆與破箱、獨立預言（無正式碰撞呼叫、2816／2774／226 為等式並逐次比對命中步）皆成立；41/41、gate GREEN、Python 8/8、附件 2156。取捨：P3.0 為 T-53～T-57；P3.0b 為 T-57，沿用 T-52。兩項措辭建議（反例變體、22 次近似平手、第二彈後續步命中）已併入 v1.41。已驗收切片：P1.10、P2.14、P3.0、P3.0b、P3.2、P3.3.a、P3.3.b、P3.3.c、P6.7；架構書 R7 通過。

### P2.15 實作紀錄 — 2026-10-10（送 Codex R33 前）
boss 改依當層生態選取（見 §3 P2.15）；sonnet 實作、另一個 sonnet verifier 獨立核對 8 項全過（池篩選同舊碼、無新亂數、`SK_ROOM_TELEPORT` 兩欄形式不變、測試會因忽略參數而失敗、T-58 標記一致、ASCII、`ChapterProgressionTest.*` 7/7、gate GREEN）。verifier 註記：沒有單元測試直接走 GameScene，若呼叫點退回章節生態只會被 p215 證據案的 `BossPool` log 斷言抓到。截圖過程發現 boss04／06／07 圖缺，登記 P2.16。

### R33 — 2026-10-10 — Codex — 對象 P2.15 @ 91b3cad — **不通過**
通過：六生態 boss 池重算相符、A1–F5 30/30 層非空、`IsBossId` 與排序未變、場景用 `LevelBiome()` 且無新亂數；T-58 成立（parent 預製件 `boss1_obj`／`boss2_obj` 指向兩半體，`game_typed.c:81197–81199` 加總兩者 HP）；P2.16 屬實；gate GREEN、Python 8/8、ChapterProgressionTest 7/7。阻擋：偏移以 `atof` 解析，`nan`／`inf`／`1e999` 會把玩家與鏡頭座標設成 NaN／Inf，§9「壞輸入安全」不實；offset 行未存證未斷言；P2.16 寶石幀尺寸不精確。
**回覆（v1.43）**：抽出 `Game::ParseRoomTeleport` 嚴格解析（規則見 §1.1），拒絕時整個鉤子作廢；新增 TestHooksTest 7 例；p215 四案過濾並斷言 offset 行；實跑 `12,4,nan,0` 存 `teleport_nan.txt`；§9 刪除「壞輸入安全」、P2.16 改實際尺寸。獨立 verifier 靜態核對通過（拒絕時無部分狀態、兩欄不印 offset、負 K／R 由場景的 ≥0 守門而不觸發）。ctest 2165。

### R34 — 2026-10-10 — Codex — 對象 P2.15 @ c1f8642 — **驗收完成（有取捨 T-58）**
解析器恰 2／4 欄、整欄 strtol／strtof、溢位、isfinite、±4096 皆成立；拒絕時 `spec.ok` 守門，場景無部分狀態；兩欄行為不變。RoomTeleportHookTest 7/7、ChapterProgressionTest 7/7、gate GREEN、Python 8/8、附件 2165。非阻擋建議：補 K／R 溢位與「dx 合法、dy 非法」測試；註明 teleport_nan 原為另行實跑；P2.16 另做。
**回覆（v1.44）**：補兩例（K 用 20 位數，使 64 位元 long 平台也溢位）；`teleport_nan` 併入 `tools/evidence_chain.sh`（require 拒絕行、傳送行計數 0）。v1.44 第一次證據鏈 ctest 有 34 例 0 秒且無輸出（測試程式未啟動，單獨重跑 50 例全過），整鏈重跑後 2167/2167、STATUS OK。已驗收切片：P1.10、P2.14、P2.15、P3.0、P3.0b、P3.2、P3.3.a、P3.3.b、P3.3.c、P6.7。

### P2.17 實作紀錄 — 2026-10-13（尚未送 Codex）
渲染版面第一刀（見 §3 P2.17）：sonnet 實作；本列所有量測為實跑（同 seed、同幀、同視窗的修前／修後截圖成對，修前用 `git stash` 還原建置）。獨立 verifier 未進行；Codex R35 見下。實作中的發現：(1) 灌木牆的 w2 臉（objects2_55）同為 (0.5,0.5)，與底圖一起下移 16 px，所以牆整體貼回格上而不是只動一層；(2) 箱子被打破時原本只隱藏底圖、頂蓋留在原地，頂蓋升到角色之上後會變成懸空幽靈，故連頂蓋一併隱藏；(3) 雕像神像在 layer 6，名牌原 z 在其上（1.34 對 1.31），改成 6.5 以免被神像蓋住；(4) `SK_ROOM_TELEPORT` 的偏移只在無出生點的房（出口／boss 房）生效，有出生點的房忽略（P2.15 既有行為），所以牆前／牆後案取 seed 1 的出口房 4；(5) 樹／箱等原版沒有影子的物件仍畫 shadow2、尖刺與速度板的 shadow2 被壓到底盤之下以維持舊畫面，影子本身留給陰影切片；(6) GitNexus 在本機損壞，影響面改以 codegraph（`GameScene::Render` 只有 main.cpp 一個呼叫者、無涵蓋測試）與 grep（所有 SetZIndex 呼叫點）查證。


### R35 — 2026-10-13 — Codex — 對象 P2.17（未提交工作樹，基底 d83f80a）— **不通過（僅文件事實；未發現功能阻擋）**
功能核對通過：162 筆 pivot 與原版資產相符、wall601＝4/0＋6/2、box02＝4/0＋6/0、c01＝5/5、TagManager 相符；levels／boss_rooms／statues／tile_pivots 記憶體重算與 JSON 一致；資料閘 GREEN、Python 11/11、RenderLayoutTest 11/11、golden／replay 57/57、像素斷言 6/6；碰撞、移動、模擬未改。阻擋（皆為文件）：(1) FAITHFUL 的 `m_Offset` 公式符號寫反（應為 (pivot−0.5)×尺寸，w601 y＝−12）；(2) 分類測試只走 levels 的 24 個非預設 stem、列名 17 個、只要求數量 ≥17，並非 36 筆，障礙頂蓋高於 boss 也只驗 layer 6；(3)「所有房間 sprite 走 PlaceRoomSprite」不實，門影與門閂直接呼叫 PivotCentre／SortZ；(4)「相對順序不變」不實，傳送門光原 1.5＜寶箱 2，改後 4.55＞4.5；(5) 成對圖與單張圖未區分（雕像、zoom 0.6 只有修後），地板縫「10＋2」是取樣座標，合併後 5 垂直＋1 水平。非阻擋：pivot 清單漏列 objects1_2／objects1_22／shadow0／shadow1／texiao_01；T-60 可寫明放大 GL_NEAREST、縮小 GL_NEAREST_MIPMAP_NEAREST。
**回覆（v1.46）**：(1) 改為 (pivot−0.5)×尺寸並註明與 `PivotCentre` 同值；(2) 測試改為走 `tile_pivots.json` 全 162 筆（新增 `GameData::TilePivots()`），逐筆斷言位移恰為 (0.5−pivot)×尺寸、36 筆非預設全部移動、列名 26 移動例與 8 不動例；頂蓋高於 boss 的描述改為「layer 6」，layer 7 光暈由另一例斷言；(3) 門影與門閂改走 `PlaceRoomSprite`（座標與 z 數學不變），文件列出不經它的 VendingMachine／Chest／傳送門／雕像名牌；(4) 查 `transfer_gate.prefab`：`light` layer 4 order 0、`img` layer 5；`chest00／01`：`box05` 4/0、`box06` 4/2，故傳送門光改 4.45＜寶箱 4.5（與舊碼同向，測試鎖定），並在 T-59 與 P2.17 列登記寶箱／門圖由平手改為寶箱＜門圖；(5) 文件與 §10 分列成對與單張，seam 改為 5 垂直＋1 水平（取樣座標 10＋2）；pivot 清單補齊 36 筆並說明構成；T-60 寫明放大／縮小濾鏡。證據重跑為 `docs/evidence/v1.46/`。

### R36 — 2026-10-13 — Codex — 對象 P2.17（未提交工作樹，基底 d83f80a，v1.46）— **驗收完成（有取捨 T-59／T-60）**
R35 五項逐項通過：162 筆 pivot／offset 相符、w601 y＝−12；新測試遍歷 `GameData::TilePivots()` 全 162 筆、恰 36 筆移動；門影／門閂已走 `PlaceRoomSprite`，例外清單（VendingMachine、Chest、傳送門、雕像名牌）與呼叫路徑一致；傳送門光 4.45＜寶箱 4.5＜門圖 4.6 有測試鎖定，原版門光 4/0、門圖 5/0、box05 4/0、box06 4/2 核實，寶箱／門圖由平手改先後已登記 T-59（同為 4/0 本身不保證嚴格先後，屬取捨）；成對／單張圖與 5＋1 條暗線與檔案一致；T-60 濾鏡正確（來源在倉庫內 `PTSD/src/Core/Texture.cpp`，`add_subdirectory(PTSD)`）。§7：1–8 全部通過（4 為附件核對、7 為附件 ctest 2178／2178 加局部實跑：RenderLayout 11/11、golden／replay 57/57、資料閘 GREEN、Python 11/11、像素斷言 6/6）。非阻擋建議：§3 P2.17／§9 R35 分寫「相對錨點偏移」與「相對修前位移」；「唯一刻意改變的次序」限定範圍以免與寶箱／門圖例外矛盾；`tools/p217_pixel_check.py` 過期註解「12＋2」。
**回覆（v1.47）**：三項皆照改（§3 P2.17 測試描述分寫兩式並註明等價；次序描述改為「依原版 sorting layer 刻意上移的只有 w2 頂蓋／神像，另有寶箱＜門圖一處平手拆解，見 T-59」；腳本註解改為取樣座標 10＋2、合併後 5＋1 條）。僅文件與註解變更，無程式行為改動，證據沿用 `docs/evidence/v1.46/`。


### P2.18 實作紀錄 — 2026-10-13（尚未送 Codex）
碰撞與移動第二刀（見 §3 P2.18）：sonnet 實作者自寫、未自驗；本列量測為實跑。實作中的發現與決定：(1) 出生池過濾要放在該房所有障礙碰撞器建好之後，因此閘門位置的計算順序後移（原本在障礙之前，用的是未過濾的格）；(2) 外牆外的虛空沒有碰撞，脫困若從牆內推出會落到地圖外，故敵人出生改為「同房最近自由點」優先、脫困只當最後手段，玩家不會處於牆內（移動被掃掠閘住）；(3) 衝刺原本整段取消（舊測試要求 `body.x == 0`），改為貼牆停下是行為變更，已改該測試並在 T-62 登記；(4) `obj11_01`／`signs01` 的圓碰撞器中心在格上方 0.9／0.75 單位，等於擋住該格北邊那一格，是預製件原值，已照放；(5) 設計房 obj_index 8 在資料中只有 3 處（9／10 為 0），所以 RC4 的設計房部分在實際房間影響很小，影響大的是雕像（2×2）與商店機器；(6) 每 tick 對每個存活敵人多一次 `BlocksAny`（沒重疊時只這一次，約為原本每 tick 兩次 `Blocks` 的 +50%），未量測幀時間；(7) GitNexus 在本機損壞，影響面以 codegraph 與 grep 查證：`WorldCollision` 的實作者只有 `GameScene`、`NullWorldCollision` 與測試替身，`ApplyDash` 的呼叫者只有 `GameScene::ConsumeSimEvents` 與 `RealBodyDriver`。
**UNVERIFIED**：脫困位置與 Box2D 的逐點一致性（無原版物理可比）；`SK_WALK_DIR`／`SK_FORCE_DASH` 只涵蓋出口房的外牆，未涵蓋箱列與角落；互動式遊玩（滑鼠／鍵盤）下的手感未人工操作。

### R37 — 2026-10-13 — Codex — 對象 P2.18（未提交工作樹，基底 0912b66，v1.48）— **不通過**
核實通過：衝刺來源鏈（`game_typed.c:714` GetForce → `:43958` 寫入方向與慣性速度 → `:51043` `Rigidbody2D.set_velocity`；舊版「整段取消」沒有此鏈支持，貼牆停下是合理的 T-62 近似）；所有指定預製件碰撞器數值；記憶體重建的 levels／boss_rooms／machines／statues 與 JSON 相符；出生過濾保持順序且不新增亂數；資料閘 GREEN、Python 13/13、指定 C++ 45/45、golden／replay 57/57、`p218_check.py` OK、附件 STATUS OK／ctest 2224。不通過：(1) T-62 與 `BodyMove.hpp` 的「步長 ≤ 半徑就不可能跨過任何阻擋」為偽（圓障礙 r16、身體 12.8、(-6,28.5)→(6,28.5) 兩端自由、中點重疊）；(2) FAITHFUL 超出來源可證範圍：「所有角色 Dynamic」忽略 Kinematic 例外，「階梯狀貼牆差 ≤ 0.05 px」不能代表整套脫困搜尋；(3) §10 的敵人位移寫 80.4／46.3／68.6，與 log 的 169.7／134.4／107.5 不符，「10 筆 boss blocked=0」沒有附件；(4) 4000 點測試上界是 64＋r＋4 而非「一格＋身體」，`TestHooksTest.*` 過濾沒有匹配（實際套件名不同）；(5) 房間連通只驗格網，建議納入 verifier 的格點洪填並做差分回歸。
**Claude 回覆（v1.49）**：(1) 撤回該保證；新增 `WorldCollision::MoveBlocked`（掃掠整段），`MoveBody` 的每個軸向子步與衝刺的貼牆二分都改用它；`GameScene` 覆寫為精確解（`Sim::SegmentBlockedBy`：線段對圓的膨脹圓、對 AABB 的圓角方塊，沿用 `SweptCollision.hpp` 的 `SegmentCircleEntry`／`SegmentRoundedBoxEntry`）；預設實作（測試替身）改為每 max(r/4,1) px 取樣並在 T-62 登記為淺擦邊近似（深度 ≤ s²／(8R)）；新增回歸測試：R37 反例（精確與取樣兩種世界都拒絕）、箱角擦邊、精確掃掠對 4000 條隨機線段與 0.05 px 密取樣一致、開闊世界 3000 個隨機位移逐位元等於 pos＋delta。**行為變更**：有牆世界裡的軸向移動現在擋下兩端自由的擦邊（只會更保守）；**未改動任何 golden／replay／戰鬥雜湊檔案**，全套 ctest 通過為證（這些測試使用的世界若不是 `NullWorldCollision`，其牆也不會擦邊；結果仍以實跑為準）。(2) FAITHFUL 限縮為「預製件設定與來源鏈」，物理結果標 INFERRED／近似：核對 `res/player`、`res/enemy` 全部預製件的 `m_BodyType`——Dynamic 134 個敵人／boss＋13 個英雄、Kinematic 6 個（e_battery01、ex_battery01、e_chest1、e_chest2、boss07、boss06_c；v1.48 文件寫 5 個有誤）、無 Rigidbody2D 7 個——已寫入 T-61、§3、`Depenetration.hpp`／`WorldCollision.hpp`／`SkillDash.hpp`／`BodyMove.hpp` 註解；「階梯狀 ≤ 0.05 px」改為「精確解＝最小位移＋0.05 px，取樣後備只試 32 個方向、實測 < 3 px」。(3) §10 位移改為 169.7／134.4／107.5 px；補 boss 掃描附件 `docs/evidence/p2.18/boss_sweep.txt`（與 `seed_sweep.txt`），腳本 `tools/p218_seed_sweep.sh`。(4) 屬性測試上界改寫為 64＋r＋4；列出實際鉤子套件名與過濾式 `*HookTest.*`。(5) 新增 `test/RoomConnectivityDiffTest.cpp`（移植 verifier 的格點洪填，差分「新規則不得新增不連通」，預設每種障礙碰撞器簽名取一層：7 簽名 × 108 房 × 3 seed＋boss 房，約 9 秒；`SK_CONN_FULL=1` 全 30 層 9720 組＋boss 42 組，約 35 秒；加上 G1 〔無障礙表、場景退回舊規則〕即 verifier 的 10044 組）：新規則獨有的不連通＝0（設計房與 boss 房皆然）；6 間房 r1_50／52／55／74／75／78 在新舊規則下都不連通（540 組），原因是可破壞箱被當成永久牆，列為已知限制。證據為 `docs/evidence/v1.49/`（ctest 2229／2229）與 `docs/evidence/p2.18/conn_full.txt`。

### R38 — 2026-10-13 — Codex — 對象 P2.18（未提交工作樹，基底 0912b66，v1.49）— **不通過（範圍縮小）**
核實通過：精確掃掠（反例與箱角擦邊回歸通過，未發現新穿牆）；Kinematic 6／Dynamic 134／無剛體 7 與六個名稱；位移 169.7／134.4／107.5 px；seed 456 隻、boss 10 筆（涵蓋 1-5 與 2-5）皆 blocked＝0；`64+r+4`；鉤子實名；資料閘 GREEN、Python 13/13、指定 C++ 63/63、全量連通 1/1。不通過：(1) `RoomConnectivityDiffTest` 略過被擋的門口，一個新規則完全封死單一門而其他門仍連通時可能通過，且整房布林值無法偵測既有不連通房再失去其他門對；檔頭仍寫全量 31 層／10044；(2) `SkillDash.hpp` 仍無條件寫 `NEVER tunnel`，T-62 的 s²／(8R) 是近似、精確弓高為 R−√(R²−s²/4)；(3) `GameScene.cpp` 出生過濾仍標 `FAITHFUL behaviour`、`WorldCollision.hpp` 的「accurate to ~0.01 px」與「只測終點」過期描述；(4) 掃描腳本不檢查退出碼、最低筆數與 blocked 計數，空輸出會被當成功。
**Claude 回覆（v1.50）**：(1) 連通差分改為逐門對：每扇門取「有效入口」＝本身未被擋的門口格點，洪填後逐門對比較，舊規則連通的門對在新規則下必須仍連通，入口完全被擋死的門算失去其所有門對（`LostPairs`／`LostDoors`）；新增三個比較本身的測試（一個正例、兩個反例）：裸房四門互通（正例）；合成一個蓋住整個西側入口的阻擋（其餘三門仍互通，整房布林值會放行）→ 比較得 `LostDoors`＝1、`LostPairs`＝3；合成既有已封閉房（北門被橫牆隔開）再被東側直牆隔開 → 比較得 ≥ 2 對遺失；檔頭改為全量 30 層／9720 組＋boss 42 組（G1 無障礙表，場景退回舊規則、不另跑；verifier 的 10044 含 G1）。真實資料結果：預設 2268＋42 組，12978 個舊規則連通門對、失去 0 對／0 扇門；全量 9720＋42 組，55620 個、失去 0 對／0 扇門；6 間房仍在新舊規則下都封閉（540 組，可破壞箱視為永久牆的已知限制）。(2) `SkillDash.hpp`「NEVER tunnel」改為「精確世界（GameScene `MoveBlocked`）下不穿，預設取樣世界可能漏淺擦邊」；T-62、`WorldCollision.hpp`、`BodyMove.hpp` 的近似描述改為圓弧弓高 R−√(R²−s²/4)（小 s 約 s²／(8R)，非對所有形狀的嚴格上界；12.8 px 英雄對 r16 圓 0.04 px、最小 R＝12.8 時 0.10 px）。(3) 出生過濾註解改為 PORT CHOICE (T-63)；`Depenetrate` 預設實作註解把 ~0.01 px 限定為徑向二分精度並註明只試 32 個方向；刪除過期的「移動閘只測終點」句，改為「連掃掠閘也會拒絕起點已重疊的每一步」；`BodyMove.hpp` 的 ~36 px 改 33.3 px；整個 diff 用 grep 掃過 never／guarantee／FAITHFUL／cannot cross，其餘皆為資料來源或布林事實（預製件資料、觸發器不擋、測試斷言）；測試註解 `NEVER`、`never carry` 一併改為「不／實測」。(4) `tools/p218_seed_sweep.sh` 檢查每次遊戲退出碼與逾時、每個 seed 至少一列、敵人列 ≥ 100／boss 列 ≥ 8、blocked 計數，任一不符非零退出，結尾 `P218 SWEEP: OK` 存 `sweep_status.txt`。**行為變更**：無（只有註解、測試與腳本；連通測試是新增斷言，遊戲程式碼未動），golden 未改。另：v1.50 第一次證據鏈 H3 失敗（`room 1 cleared` 未出現；牆鐘 dt 下英雄於 0:00:07 陣亡），當時把它歸為「與碰撞程式碼無關」，**該歸因 R39 判定不成立，已由 v1.51 的 A/B 取代（見 R39 回覆）**；H3 仍釘為 `SK_DT_MS=20`（寫入 §1.1 與 `evidence_chain.sh`）。證據為 `docs/evidence/v1.50/`（ctest 2232／2232）與 `docs/evidence/p2.18/`。

### R39 — 2026-10-13 — Codex — 對象 P2.18（未提交工作樹，基底 0912b66，v1.50）— **不通過（縮小到證據）**
核實通過：連通差分（逐門對）、衝刺措辭、T-63 與精度措辭；指定 C++ 66/66、全量連通 4/4。不通過：(1) H3 歸因：Codex 實跑牆鐘 dt 英雄於 0:00:07 陣亡、固定 20 ms 清房、固定 80 ms（低於 160 ms 夾制）於 0:00:08 在第一次強殺前陣亡，故夾制不是原因；較合理的是強殺以幀數計、戰鬥以時間計；但「與碰撞程式碼無關」沒有證據支持——P2.18 改了出生就座並讓敵人離開箱子、較快抵達英雄；要求以 base 對現況做真正的 A/B。(2) `tools/p218_seed_sweep.sh` 的「每個 seed 至少一列」對 boss 不成立（只檢查總數）；要求逐 boss seed 檢查並證明「某 seed 零列而總數仍達 8」會失敗。(3) `Simulation.cpp:610` 的「destination-only gate below」註解過期；§9 R38 的「三個反例」應為一個正例、兩個反例。
**Claude 回覆（v1.51）**：(1) 在倉庫外開 `git worktree`（`D:/wtb`，基底 0912b66，獨立 build 目錄，PTSD/lib 以 junction 指向主樹的相依來源；不動目前工作樹、不 stash），只加與現況相同的 `DeathTrace` 診斷（兩個累計計數與一行 log），以完全相同的 H3 命令、預設 seed、固定 dt 20／30／40／80 ms（各 2 次，確定性，兩次完全相同）與牆鐘 3 次各跑一遍，記錄每次的結果、死亡幀、模擬時間、牆鐘時間、存活敵人數、平均幀時間；A/B 完畢後移除 worktree（先移除 junction，主樹 PTSD/lib 完好）。結果（`docs/evidence/p2.18/h3_ab.txt`）：dt 20 兩者皆清房；**dt 30 base 清房、現況陣亡（第 208 幀，6.2 s，6 隻存活）**；dt 40 兩者皆陣亡（base 第 204 幀 8160 ms、現況第 197 幀 7880 ms）；dt 80 兩者皆陣亡（base 第 114 幀 9120 ms、現況第 107 幀 8560 ms，皆 21 隻存活）；牆鐘（約 32 ms／幀）base 3／3 清房、現況 2 次陣亡（第 209 幀，約 6.2 s）、1 次清房。結論：**現況在同一 dt 下讓靜止的 H3 英雄更早陣亡**，與夾制無關（所有 dt 都低於 160 ms）；合理原因是敵人不再卡在箱內、出生在自由格並被重新就座、脫困後可走，因此較快抵達英雄——這是「不再卡住」的預期後果，三項修改各自的貢獻未隔離，登記於 T-63；v1.50 的「與碰撞程式碼無關」撤回。H3 仍釘為 `SK_DT_MS=20`（base 與現況都清房的值），§1.1 改寫為 A/B 的實際內容。`DeathTrace`（`GameScene` 於「Player died」前多印一行 `frame／rawMs／simMs／meanFrameMs／enemiesAlive／bossAlive`，只診斷、無行為）與 `tools/p218_h3_ab.sh` 進倉庫。(2) `tools/p218_seed_sweep.sh` 逐 boss seed 檢查至少一列（`P218_GAME` 可換成假遊戲），新增 `tools/tests/test_p218_sweep.py` 5 例：健康輸出通過；boss seed 3 零列、其餘每 seed 2 列（總數 8，剛好達標）→ 必須失敗且訊息為 `boss seed 3 produced no boss row`（不是總數檢查）；敵人 seed 零列、blocked＝1、遊戲非零退出皆必須失敗。(3) `Simulation.cpp` 註解改為「掃掠或不掃掠的移動閘都會拒絕起點已重疊的每一步」；§9 R38「三個反例」更正為一個正例、兩個反例（§3 同步）。**行為變更**：無（只有診斷 log、腳本、測試與文件）；golden 未改。證據為 `docs/evidence/v1.51/`（ctest 2232／2232）與 `docs/evidence/p2.18/`。

### R40 — 2026-10-13 — Codex — 對象 P2.18（未提交工作樹，基底 0912b66，v1.51）— **不通過（只剩證據腳本）**
核實通過：T-63 足以說明 H3 差異；`DeathTrace` 僅診斷；遊戲邏輯 66/66；H3 重現（第 208 幀、6240 ms）。不通過：(1) `tools/evidence_chain.sh:108` 註解仍寫「unrelated to the collision code」；(2) `tools/p218_h3_ab.sh`：`unset` 案沒有真正移除 `SK_DT_MS`、seed 與其他 `SK_*` 鉤子未固定／清除、非零退出／逾時／NEITHER 不會使整體失敗、最後的 `cat` 會遮蔽失敗；(3) Python 測試的 `setUpClass` 在唯讀沙盒建不出暫存目錄。
**Claude 回覆（v1.52）**：(1) 證據鏈註解改為 A/B 結論（兩者在 20 ms 都清房；30 ms base 清房、現況陣亡；40／80 ms 兩者陣亡、現況早 7 幀；成因未隔離，T-63；故釘在兩者都清房的 dt）；「unrelated to the collision」已從現行註解移除（歷史引文與 `h3_ab.txt` 內「該說法是錯的」的撤回註記保留）。(2) `p218_h3_ab.sh` 重寫：以 `env -u` 移除呼叫端環境裡所有 `SK_*`，再只設 H3 鉤子與明定的 `SK_SEED=20240607`（實測與預設 seed 結果相同：dt 30 → 第 208 幀陣亡）；`unset` 案用 `-u SK_DT_MS`，不是空字串；任何一次遊戲非零退出、逾時（124）、NEITHER 都記入失敗並使腳本以非零結束，輸出檔結尾與最後一行是 `P218 H3 A/B: OK|FAIL (n)`，最後的 `cat` 失敗也計入，腳本退出碼就是整體狀態；缺參數退出 2。`tools/tests/test_p218_sweep.py` 新增 4 例（假遊戲 `tools/tests/fixtures/fake_p218_h3_game.sh`）：健康通過；非零退出必須失敗（輸出含 `FAIL(rc=3)`）；NEITHER 必須失敗；`unset` 案在呼叫端匯出 `SK_DT_MS=999`、`SK_FOO=1` 時，22 次執行中 6 次完全沒有 `SK_DT_MS`、16 次只有 20／30／40／80、沒有任何一次看到 999 或 `SK_FOO`、seed 全為 20240607。(3) 假遊戲改為已入庫 fixture（`tools/tests/fixtures/fake_p218_sweep_game.sh`），`setUpClass` 建不出暫存目錄時 `SkipTest`（輸出仍需可寫，無法完全免除，已在檔頭註明）。結果表欄位未變（另加了 seed／環境說明與結尾狀態行），base worktree 已移除，故不重跑 `h3_ab.txt`（腳本行為在假遊戲上驗證；A/B 數據仍是 v1.51 的實測）。**行為變更**：無（只有腳本、測試與註解），golden 未改。證據為 `docs/evidence/v1.52/`（ctest 2232／2232）。

### R41 — 2026-10-15 — Codex — 對象 P2.18（未提交工作樹，基底 0912b66，v1.52）— **驗收完成（有取捨 T-61／T-62／T-63）**
證據鏈註解、A/B 腳本（環境清除、固定 seed、失敗退出）與 Python 假遊戲測試均核實；沿用 R37–R40 已核實的來源鏈、預製件數值、精確掃掠、逐門對連通差分與 H3 A/B。非阻擋建議四項（見下）。
**Claude 回覆（v1.53）**：只套用非阻擋建議、不動其他：(1) §9 R40 的「全倉庫 grep 不再有」改為「已從現行註解移除，歷史引文與撤回註記保留」；(2) §9 R40／§10 的「輸出格式未變」改為「結果表欄位未變（另加 seed／環境說明與結尾狀態行）」；(3) `tools/p218_h3_ab.sh` 結尾改為：先寫狀態行、再顯示表格，任何一步失敗都累計並重算狀態，最後一行不可能在退出碼非零時印 OK（退出碼行為不變，既有假遊戲測試 9 例全過）；(4) 記錄 R41、P2.18 狀態改為驗收完成（有取捨）、§2 同步、版本 v1.53。僅文件與腳本，沿用 `docs/evidence/v1.52/`（ctest 2232／2232、gate GREEN、STATUS OK）；Python 22／22。已驗收切片：P1.10、P2.14、P2.15、P2.17、P2.18、P3.0、P3.0b、P3.2、P3.3.a、P3.3.b、P3.3.c、P6.7。
---

## 10. 證據附錄（每版最新一次實跑；檔案在 `docs/evidence/<版本>/`，由 `tools/evidence_chain.sh <版本>` 產生；腳本把結尾 `STATUS: OK|FAILED` 與 UTC 時戳寫入 `status.txt`，只有 OK 的附件有效）

v1.53（無新目錄）：僅文件措辭與 `tools/p218_h3_ab.sh` 結尾狀態行的改動，沿用 `docs/evidence/v1.52/` 的實跑（ctest 2232／2232、gate GREEN、`STATUS: OK`）；Python 22／22 於本版重跑。

v1.52（`docs/evidence/v1.52/`；`head.txt` = 建置時的基底 commit 0912b66，變更尚在工作樹；`status.txt` = `STATUS: OK`；取代 v1.51）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2232`（C++ 無變動）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 22 例（+4 H3 A/B 腳本）。
- V-S P2.18 與 H1–H12 同規格重跑；`docs/evidence/p2.18/h3_ab.txt` 沿用 v1.51（結果表欄位未變）。

v1.51（`docs/evidence/v1.51/`；已由 v1.52 取代，目錄保留；`head.txt` = 建置時的基底 commit 0912b66，變更尚在工作樹；`status.txt` = `STATUS: OK`；取代 v1.50）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2232`（相對 v1.50 無新增 C++ 測試；遊戲程式碼只多一行診斷 log）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 18 例（+5 掃描腳本）。
- V-S P2.18：`p218_s14_repro`、`p218_dash_wall`、`p218_dt_spike`、`p218_hooks_bad`、`p218_check.txt` = `P218 CHECK: OK`，與 v1.48–v1.50 同規格重跑；H3 釘 `SK_DT_MS=20`。
- `docs/evidence/p2.18/`：新增 `h3_ab.txt`（base 0912b66 對現況的 H3 A/B 表與讀法）；`seed_sweep.txt`／`boss_sweep.txt`／`sweep_status.txt` 由逐 seed 檢查版腳本重跑；其餘附件同 v1.50。

v1.50（`docs/evidence/v1.50/`；已由 v1.51 取代，目錄保留；`head.txt` = 建置時的基底 commit 0912b66，變更尚在工作樹；`status.txt` = `STATUS: OK`；取代 v1.49）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2232`（相對 v1.49 +RoomConnectivityDiffTest 3；遊戲程式碼與 golden 未動）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 13 例。
- V-S P2.18：`p218_s14_repro`、`p218_dash_wall`、`p218_dt_spike`、`p218_hooks_bad`、`p218_check.txt` = `P218 CHECK: OK`，與 v1.48／v1.49 同規格重跑。
- `docs/evidence/p2.18/` 更新：`conn_full.txt`（`SK_CONN_FULL=1`：9720＋42 組、舊規則連通門對 55620、失去 0 對／0 扇門、6 間房新舊規則皆封閉）；`seed_sweep.txt`／`boss_sweep.txt`／`sweep_status.txt`（`P218 SWEEP: OK`，由收緊後的 `tools/p218_seed_sweep.sh` 產生：456 隻敵人、10 筆 boss，blocked＝0）。其餘附件同 v1.49。

v1.49（`docs/evidence/v1.49/`；已由 v1.50 取代，目錄保留；`head.txt` = 建置時的基底 commit 0912b66，變更尚在工作樹；`status.txt` = `STATUS: OK`；取代 v1.48）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2229`（相對 v1.48 +BodyMoveTest 4、+RoomConnectivityDiffTest 1；golden／replay／戰鬥雜湊未改）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 13 例。
- V-S P2.18（`p218_*.txt`、`p218_*.png`、`p218_check.txt` = `P218 CHECK: OK`）：`p218_s14_repro`、`p218_dash_wall`、`p218_dt_spike`、`p218_hooks_bad`，內容與 v1.48 相同規格重跑。
- `docs/evidence/p2.18/`：`s14r1_f3_before.png`／`s14r1_f250_before.png` 與 `s14r1_f3_after.png`／`s14r1_f250_after.png`（修前取自診斷基底 d83f80a，無 P2.17 繪製）；`spawn_sweep.txt`（`RoomBlockersTest` 標準輸出）；`seed_sweep.txt`（seed 1–30 第 0 層 456 隻敵人 blocked＝0）；`boss_sweep.txt`（seed 1／2／3／5／7 第五關共 10 筆 boss blocked＝0）；`conn_full.txt`（`SK_CONN_FULL=1` 的 `RoomConnectivityDiffTest` 輸出：9720 組設計房＋42 組 boss 房，新規則獨有的不連通＝0，新舊規則都不連通的房＝r1_50／52／55／74／75／78）。
- 量測：AC1 第 3 幀房 1 三隻敵人 `blocked=0`，第 250 幀已移動 **169.7／134.4／107.5 px**；`SpawnFilter: 91 of 822 connected floor cells removed`；AC3（單元）1728 次建房，波次取點 13824 次中 1168 次落在阻擋內→0，connected 地板格 337328 個中 27570 個中心被擋→0，14 間 boss 房 344→0；AC4 衝刺 2744.0→2755.2；AC5 夾制 5000→160 ms，該幀位移被牆截在 22.1 px，停在 2754.5。

v1.48（`docs/evidence/v1.48/`；已由 v1.49 取代，目錄保留；`head.txt` = 建置時的基底 commit 0912b66；`status.txt` = `STATUS: OK`；本段的敵人位移寫 80.4／46.3／68.6 為誤，正確為 169.7／134.4／107.5 px）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2224`（+DepenetrationTest 16、BodyMoveTest 11、RoomBlockersTest 10、BodyCollisionSimTest 5、TestHooksTest 4；golden／replay／戰鬥雜湊未改）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 13 例。
- V-S P2.18（`p218_*.txt` 為過濾後 log，`p218_*.png` 為截圖，`p218_check.txt` = `P218 CHECK: OK`）：`p218_s14_repro`（AC1：seed 14 房 1）、`p218_dash_wall`（AC4：衝刺貼牆）、`p218_dt_spike`（AC5：5000 ms 尖峰）、`p218_hooks_bad`（壞鉤子值被拒）。
- `docs/evidence/p2.18/`：`s14r1_f3_before.png`／`s14r1_f250_before.png`（診斷階段修前實跑，第 3、250 幀：三隻敵人畫在箱塊內、不動）與 `s14r1_f3_after.png`／`s14r1_f250_after.png`（同 seed 同房同幀，修後；修前圖取自診斷基底 d83f80a，尚無 P2.17 的牆／箱繪製，故畫面風格不同，比較對象只有敵人與箱的相對位置）；`spawn_sweep.txt`（`SoulKnightTests --gtest_filter=RoomBlockersTest.*` 的標準輸出：出生池掃描修前／修後數字）；`seed_sweep.txt`（seed 1–30 第 0 層 456 隻敵人 `blocked=1` 計數＝0 的逐 seed 輸出）。
- 量測：AC1 第 3 幀房 1 三隻敵人 `blocked=0`，第 250 幀已移動 80.4／46.3／68.6 px；`SpawnFilter: 91 of 822 connected floor cells removed`；AC3（單元）1728 次建房，波次取點 13824 次中 1168 次落在阻擋內→0，connected 地板格 337328 個中 27570 個中心被擋→0，14 間 boss 房 344→0；AC4 衝刺 2744.0→2755.2；AC5 夾制 5000→160 ms，玩家 22.1 px 後停在 2754.5。

v1.46（`docs/evidence/v1.46/`；`head.txt` = 建置時的基底 commit d83f80a，變更尚在工作樹；`status.txt` = `STATUS: OK`；R35 修正後整鏈一次通過；前一版 v1.45 附件〔第一次整鏈曾因 H3 牆鐘 dt 的偶發失敗未過，重跑後 OK〕已由本版取代，目錄保留）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2178`（+RenderLayoutTest 11；golden／replay 未變；與 v1.45 同數，R35 只改了其中一例的內容）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 11 例。
- V-S P2.17（`p217_*.txt` 為過濾後 log，`p217_*.png` 為修後截圖，`p217_pixels.txt` = `P217 PIXELS: OK`）：`p217_a1_room1`（seed 1、A1 房 1）、`p217_a1_room1_z06`（zoom 0.6）、`p217_wall_behind`／`p217_wall_front`（出口房 4、offset (0,-128)／(0,128)、zoom 3）、`p217_b1_start`（seed 5 → B1）、`p217_boss_forest`。
- `docs/evidence/p2.17/`：成對 `*_before.png`（同 seed 同幀，`git stash` 還原後建置）／`*_after.png` 共五組（`ac1_a1_room1`、`ac2_wall_behind`、`ac2_wall_front`、`b1_start`、`boss_forest`）；單張（無修前對照）：`statue_after.png`、`a1_room1_zoom06_after.png`、`ac3_b1_reference_left_port_right.png`；`pixel_check_before.txt`（對修前截圖 5 條 FAIL）／`pixel_check_after.txt`（6／6 OK）；`ac3_b1_reference_left_port_right.png`（左：實機參考影格 `f0000`，右：port B1，虛空 (7,25,49) 對 (7,26,49)）；`ac4_seam_crop_before_x6.png`／`ac4_seam_crop_after_x6.png`（地板縫 6 倍放大前後）；`boss_forest_before.png` 取自 `docs/evidence/p2.15/p215_forest.png`；`statue_after.png` 顯示雕像 pivot（0.5,0.1）／（0.6,0）與名牌。
- 量測：AC1 灌木列頂 y 531→555（24 螢幕 px＝16 world px＝0.5 格）；AC2 牆後案 hero 腳部矩形內灌木綠 0.14→0.88，牆前案 hero 仍蓋住牆底圖；AC3 虛空 (0,0,0)→A1 (5,31,40)／B1 (7,26,49)，來源欄位 `levels.json camera_bg`＝`MapManager.camera_bg`；AC4 地板縫（>10 亮度的 1–3 px 暗線）：偵測器取樣座標 10 垂直＋2 水平，合併相鄰後為 5 條垂直＋1 條水平（各寬 2 px，深 13–30）→0。

v1.45（`docs/evidence/v1.45/`）：已由 v1.46 取代（內容為 R35 前的實作；STATUS OK 但文件宣稱有誤）。

v1.44（`docs/evidence/v1.44/`；`head.txt` = 建置時的基底 commit c1f8642；提交後 HEAD 見 git log；各案經 `require_order`／`require_count`／`require` 斷言）：
- 測試報告：`ctest.txt` = `100% tests passed, 0 tests failed out of 2167`（+ChapterProgressionTest 2、+TestHooksTest 9；golden 未變）；`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` 8 例。H3 改每 45 幀強殺。`H7.txt` 實際 12 行（摘錄上限 20 行；前四行為 B1–B4 的開門），依序含 `Level B5 (1-5)` → `BossRoom: room 6 uses r1_boss4` → `Boss defeated!` → `Chest: boss chest chest_boss13` → `TransferGate: open in room 6`（700 幀；腳本從 BossRoom 行取房號，要求同一房號開門）。
- 資料閘：`gate.txt` = `GATE: GREEN -- 0 errors, 10 warning(s)`；`gate_tests.txt` = 邊界 unittest 8 例（注入假引用必須報錯、P3.3.c 角色 skill_refs 必填、P2.14 碰撞與 pivot 必填）。
- H1–H9：`H1.txt` … `H9.txt`、`H2neg.txt`（=1）；截圖 `H5.png`；`status.txt`。H9 = `Buff: Fire applied to 61453 for 2.000000s` → `Buff: Fire ended on 61453`；`H9armor.txt` = `Buff: Armor applied to 61453 for 6.000000s` → `Buff: Armor ended on 61453`；`H10.txt`（40 行）= `Buff: Fire applied to <id>` … `Buff: Fire ended on <id>`（腳本斷言 ended）；`H12_<c>.txt` 九角：c03／c04／c07／c08／c10／c13 為 `activated` → `ended`（c07 另有 `effect bat_swarm n=6`）；c05 `effect bottle_throw n=1 variant=0/4 NOT_PORTED(T-44)`；c11 `effect holy_light`（14 s 冷卻內第二次按鍵無效）；c12 `effect wolf_summon n=2 NOT_PORTED(T-48)` 後第二次為 `effect atk_cut dmg=5`；`H11.txt`（40 行）本次 = `sfx fx_gun_1.wav` x21、`sfx enermy_die3.wav` x17、`bgm bgm_5Low.ogg` x1、`sfx fx_door.wav` x1。各音效次數隨實跑時序浮動（v1.29 死亡 13／命中 1、v1.30 死亡 12／命中 2），腳本只斷言 BGM 先於開火、死亡音至少 1 次。關鍵事件與 v1.4 相同（H7 摘錄上限 20 行（v1.29 起；v1.16–v1.28 為 12 行），前面多出前幾層的 `TransferGate: open` 四行）：H1 `hero c12 on floor 0` → `Level B1 (1-1)` → `Floor 0 CLEARED` → `floor 1` → `Level B2`；H2 `TransferGate: open in room 4` → `entered -> next floor` → `hero c01 on floor 1`；H3 `part spawned 9 (15 pending)` → `level 3 points 25 -> 24 planned, wave size 9` → `StartRoom -> Active` → `9 (6 pending)` → `6 (0 pending)` → `room 1 cleared`；H4 `Reward: e_miner02 dropped 2 pickups (coins 1 energy 8)` …；H5 `Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1`；H6 `bought health_pot_big for 10`；H7 `BossRoom: room 6 uses r1_boss4 (21x21, 27 objects)` → `Boss defeated!` → `Chest: boss chest chest_boss13` → `TransferGate: open in room 6`；H8 `Statue: worshipped statue_05 (Elf) -> statue_index 5`。
- V-S r2_boss3：`r2boss3_seed.txt`（含 `SK_ROOM_TELEPORT -> room 3 centre (no spawn points) at (-2624,1312)`）、`r2_boss3.png`（zoom 1.5 局部）、`r2boss3_full.txt`（含 `SK_ZOOM override 0.6`）、`r2_boss3_full.png`（整房）。對照 boss_rooms.json r2_boss3 21×21：box03 ×16 = 四角 2×2 叢（(3–4,2–3)、(3–4,18–19)、(16–17,2–3)、(16–17,18–19)）→ 截圖四角各見 2×2 箱體；obj22_stone ×4 與 rail ×4（y=0）未繪製＝P1.8；dropped_outside 4（rail y=21）＝T-28；boss 在房中央、門在下與右。**更正（v1.4 起）**：v1.3 的 `r2_boss3.png` 拍到的是起始房（當時 `SK_ROOM_TELEPORT` 對 level-0 房靜默跳過）。
- V-S P2.15：`p215_boss11.txt`（`Level F5 … biome 3-volcano` → `BossPool: biome 3-volcano -> boss11 (pool 1)` → 傳送）＋`p215_boss11.png`、`p215_boss11_full.png`（zoom 0.5）；`p215_forest.txt`（A5 → boss08）＋`p215_forest.png`（boss08 清楚可見）；`p215_graveyard.txt`（D5 → boss04）＋`p215_graveyard.png`（只見 boss04 寶石幀，P2.16）。過濾式未收 offset 行，v1.43 起過濾式含 offset 行並 `require` 之；`teleport_nan.txt`（v1.44 起由證據鏈產生，斷言拒絕行且無傳送行）= `SK_ROOM_TELEPORT rejected: bad offset (need finite number, |v| <= 4096)`。

v1.6（e1507b0，R7 通過的附件，已由 v1.8 取代）／v1.5（0f8ac9f）／v1.4（9536194）：ctest 2016/2016、gate GREEN、H1–H8、整房截圖（附件已由 v1.6 取代）。v1.3（458c595）：ctest 2015/2015。v1.2（1097c86）：ctest 2009/2009。
