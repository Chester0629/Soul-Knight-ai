# Soul-Knight-ai 專案架構書（元氣騎士 1.7.10 忠實復刻）

> 本文件是專案的**單一事實來源**：範圍、階段、每階段的資料來源與驗證方式、Codex 審查紀錄。
> 由 Claude（實作）與 Codex（審查）共同維護；規則見 §8。
> 狀態欄位只允許：`未開始 / 進行中 / 功能完成 / 驗收完成 / 驗收完成（有取捨）`。
> 「功能完成」＝接線可玩；「驗收完成」＝§1 的證據齊全且 Codex 通過。兩者分開記錄。
> 版本：v1.1（2026-09-12；依 Codex 首次審查修訂，見 §9 R1）

---

## 0. 目的、範圍、非目標、資料版本

**目的**：在自製的 C++ 遊戲系統（PTSD：SDL2 + OpenGL + ImGui，CMake，GoogleTest）上，以**資料驅動**的方式忠實復刻《元氣騎士》1.7.10（Unity 2017.4.1f1 IL2CPP，armeabi-v7a）的單人地牢循環，作為學術演示。之後要能**用高版本素材替換**並**新增內容**，所以數值、貼圖、布局都必須走「解包資料 → JSON/PNG → 程式讀表」。

**忠實度標籤**（程式碼註解與本文件共用）：
- `FAITHFUL @ <來源>`：直接來自反編譯（`_reverse/ghidra_export/game_full.c`）、AssetRipper 預製件（`_reverse/asset soul/ExportedProject`）或資料表（`_reverse/analysis/data_tables/*.csv`），可逐項對照。**只有原碼可見的部分**才能標 FAITHFUL；Ghidra 截斷處之後的邏輯一律標 INFERRED。
- `INFERRED`：來源存在但被截斷或欄位語意需推論，推論依據寫在註解裡。
- `PORT CHOICE`：原版邏輯不可恢復，移植版自行決定；必須登記在 §6 並集中在單一處。
- `NOT PORTED`：明知原版有、目前沒做，程式碼只記 log。

**資料版本矩陣**（§9 R1 要求）：

| 表 | 來源 | 版本 | 缺來源時的退化 | 狀態 |
|---|---|---|---|---|
| enemies.json（145 species） | `data_tables/enemies.csv`（由 1.7.10 AssetRipper 產生） | 1.7.10 | 退回 `1.07/MonoBehaviour` 的 16 個 AI 類別表 | 1.7.10 |
| characters.json | `res/player/c##.prefab` RoleAttributePlayer | 1.7.10 | 退回 1.07 單一模板 | 1.7.10 |
| weapons.json（194） | `data_tables/weapons.csv` + 預製件圖示 | 1.7.10 | 退回 1.07 的 46 類別表 | 1.7.10 |
| sprites.json / tiles | AssetRipper Animator→clip→sprite | 1.7.10 | 無（缺則用佔位圖） | 1.7.10 |
| levels.json / boss_rooms.json / boss_chests.json / shop.json | `res/map_manager`、`rgprefab/rooms`、`res/item`、`rgprefab/room/event` | 1.7.10 | 無（缺則舊路徑：固定冰原＋類別 roster） | 1.7.10 |
| bullets.json / bosses.json / buffs.json / enemy_guns.json | **`1.07/MonoBehaviour` 舊 dump** | **1.07** | — | **待升版（P1.10）** |
| droptables.json / weapon_catalog.json | `1.07/TextAsset` | 1.07 | — | 待升版（P1.10） |

**禁止降級條件**：任何標「驗收完成」的階段，其表必須是 1.7.10 來源；退化路徑只允許在 `_reverse/` 不存在的乾淨 checkout 觸發，且要在 log 印出 `fallback`。

**非目標**：多人連線、IAP／廣告、雲端存檔、Google Play 成就、季節 boss（Nian）。

**法律框架**：解包素材屬 ChillyRoom，僅供課堂演示；`Resources/sprites` 已列入 `.gitignore`。

---

## 1. 驗證體系

| 代號 | 方式 | 指令 / 位置 | 用途與限制 |
|---|---|---|---|
| V-T | GoogleTest | `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug` → `cmake --build build --config Debug --target SoulKnightTests` → `ctest --test-dir build -C Debug -E Tests_NOT_BUILT` | 每次驗收附**測試報告**：commit、`ctest` 的「N tests passed / failed」行。統計基準（2026-09-12，a3bb557）：`grep -c "^TEST(" test/*.cpp` = **2009 個 TEST 巨集 / 141 檔**；ctest 執行數以輸出為準。零測試視為失敗。 |
| V-G | 資料閘 | `python tools/validate_data.py`（pre-commit 亦執行；非 ASCII 原始碼檢查在 pre-commit hook，不在此指令） | 檢查 weapons/bullets/enemies/bosses/buffs/catalog/droptables/characters，以及（P1.11，v1.1 起）levels（31 筆、6 地板、12 障礙、敵池引用）、boss_rooms（14 房、物件在界內、皮膚存在、boss 層 type2 房存在）、boss_chests（招牌武器存在）、shop（機器賣的藥水存在、價格、回復量）。首次擴充即抓到 T-27／T-28。 |
| V-D | 資料對照 | 固定樣本表（§1.2） | 每個 re-key 階段附至少 5 筆「來源檔:欄位 → 預期值 → JSON 值」，另跑全表檢查（ID 唯一、引用存在、權重非零）。 |
| V-H | 無頭鉤子 | §1.1 的**可執行案例**（命令、seed、幀數、預期 log 順序、逾時） | 只能證明「該路徑可達且 log 順序正確」，**不能證明正常遊玩流程**；正常流程用 V-P。 |
| V-P | 實玩紀錄 | 人工遊玩 + `SK_SHOT` 或錄影，附 seed | 驗收「可玩」時必附。 |
| V-S | 截圖對照 | `SK_SHOT=<png>`；固定 1280×720、zoom 1.5、seed 20240607、指定樓層與房間 | 每張截圖旁列出對照的預製件數值（尺寸／pivot／偏移）；有原版畫面時併列。 |
| V-R | 反編譯對照 | `// FAITHFUL: <Class>__<Method> @ game_full.c:<line>` | Codex 抽查註解 ↔ 來源；截斷處之後不得標 FAITHFUL。 |
| V-C | Codex 審查 | §7 清單 | 獨立第二意見；每條寫「通過／不通過／不適用＋證據」。 |

### 1.1 無頭鉤子與可執行案例
鉤子（env）：`SK_MAX_FRAMES`、`SK_SHOT`、`SK_SKIP_MENU`、`SK_START_CHAR`、`SK_CHAR`、`SK_MENU=start`、`SK_FORCE_CLEAR=K`、`SK_FORCE_DIE=K`、`SK_FORCE_KILL=K`、`SK_FORCE_SKILL=K`、`SK_ENDSCENE=restart|quit|menu`、`SK_GATE_TELEPORT=K`、`SK_GATE_BESIDE=K`、`SK_ROOM_TELEPORT=K,R`、`SK_SHOP_BUY=K`、`SK_SHOP_MACHINE=N`、`SK_STATUE_WORSHIP=K`、`SK_COINS=N`、`SK_EVENT_ROOM=statue|mineral`、`SK_AUTOWALK`（不穩，僅 log 走位）、`SK_AUTOFIRE`（需搭配 AUTOWALK）。
`GameScene::m_Frame` 每層重新計數；預設 seed 20240607。

| 案例 | 命令（在 Soul-Knight-ai 根目錄，Debug 建置） | 預期 log 順序 | 逾時 |
|---|---|---|---|
| H1 入口流程 | `SK_START_CHAR=c12 SK_FORCE_CLEAR=60 SK_MAX_FRAMES=200 ./build/Debug/SoulKnight.exe` | `CharSelectScene: start run as c12` → `GameScene: hero c12 on floor 0` → `Level B1` → `Floor 0 CLEARED` → `hero c12 on floor 1` | 60 s |
| H2 傳送門 | `SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400 …` | `TransferGate: open in room N` → `TransferGate: entered -> next floor` → `hero c01 on floor 1`；同命令去掉 TELEPORT 時**不得**出現 floor 1 | 60 s |
| H3 分波與封鎖 | `SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_MAX_FRAMES=300 …` | `RGRoomX: room 1 StartRoom` → `EnemyMaker: room 1 part spawned` 於 kill 後約 0.8 s 再出現 → 無 pending 時 `room 1 cleared` | 60 s |
| H4 掉落經濟 | `SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700 …` | `Reward: … dropped` → `Pickup: coin +1 (coins now 1)` → `Pickup: energy +8` | 90 s |
| H5 結算 | `SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT=out.png …` | `Player died on floor 0 -> EndScene` → `EndScene: game over at 1-1 …` | 60 s |
| H6 商店 | `SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130 …`（seed 20240607 第 0 層為商店房） | `Shop: vending_machine_i … sells` → `Shop: bought <pot> for 10` | 60 s |
| H7 boss 層 | `SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=447 …` | `Level B5` → `BossRoom: room N uses r1_bossK` → `Boss defeated!` → `Chest: boss chest chest_bossNN` → `TransferGate: open` | 120 s |

### 1.2 固定對照樣本（V-D）
每個資料階段在此登記 5 筆；Codex 重跑即可核對。
- enemies：`enemies.csv` boss01 ra_max_hp=600 → `enemies.json` boss01.max_hp=600；boss05=800；e_succulent=8；e_slime01 reward_rate=15；boss03 reward_value=[5,3,0,10]。
- characters：c01 max_armor=5, energy=180, atk=4, skill_cd=10；c12 max_hp=6（`RoleAttributePlayer.json` 單檔是 Druid 的，不可當模板）。
- weapons：weapon_000 atk=3 consume=0 bullet_speed=36 item_value=12；weapon_043 atk=15 consume=5。
- levels：B1 (1-1, chest 1, map_long 3, floors f601..f606, walls wall602/wall601)；B5 type2=[r1_boss3,r1_boss4,r1_boss6]；C1 (2-1, chest 3, floors f201..)；G1 (3-6, map_long 1, type2=[r_end])。
- boss chests：chest_boss01 level 2 weapon_077；chest_boss13 weapon_175；無 chest_boss05/06/11/12。

---

## 2. 現況總表（截至 2026-09-12，分支 `feat/phase3-presentation-content`，HEAD a3bb557）

| 區塊 | 功能 | 驗收 | 主要 commit | 缺口 |
|---|---|---|---|---|
| P1 資料層 | 進行中 | 未驗收 | 12c3750 … 9d468ee | P1.8 內嵌物件、P1.9 未解析掉落物、P1.10 四張表仍為 1.07、P1.11 資料閘擴充 |
| P2 地圖 | 功能完成（P2.1–P2.9） | 未驗收 | 3eded10 … e2c5030 | P2.3 加級條件待對原碼、P2.10–P2.14 |
| P3 戰鬥忠實度 | 進行中（核心每擊數學、dash、AI 派發、18 boss brain 完成） | 未驗收 | phase 1–5 | buff、c03–c13、boss 攻擊模式、接觸傷害 |
| P4 進度循環 | 進行中（P4.1 掉落經濟功能完成；P4.6 結算畫面功能完成） | 未驗收 | 5c8c4b0、1cfb297 | 復活、重試、buff 三選一、寶石、16 層勝利、統計存檔 |
| P5 大廳 | 未開始 | — | — | — |
| P6 UI 表現 | 進行中（P6.1 入口流程功能完成） | 未驗收 | 6e46eed | 其餘全部 |
| P7 內容補完 | 未開始 | — | — | — |
| P8 擴充管線 | 未開始 | — | — | — |

「驗收完成」欄目前全部為空：v1.0 之前沒有做過 §1 全套證據，R1 審查後改為逐階段補證據再標記。

---

## 3. 階段規劃（細分）

固定格式：**目標 / 資料來源 / 交付物 / 驗證 / 取捨（§6 ID）/ 狀態（功能・驗收）**。

### P1 資料層
| 子階段 | 目標 | 資料來源 | 交付物 | 驗證 | 取捨 | 狀態 |
|---|---|---|---|---|---|---|
| P1.1 敵人 | 145 species 數值 | enemies.csv | enemies.json | V-D 樣本、V-T EnemyStatsTest | — | 功能完成・未驗收 |
| P1.2 角色 | 13 角 per-character | c##.prefab | characters.json | V-D、CharacterStatsTest | — | 功能完成・未驗收 |
| P1.3 動畫 | 156 實體幀清單 | Animator 鏈 | sprites.json | SpriteManifestTest | — | 功能完成・未驗收 |
| P1.4 武器 | 194 per-prefab | weapons.csv | weapons.json | WeaponStatsTest | — | 功能完成・未驗收 |
| P1.5 章節 | biome/chapter 標記、boss 池 | ExportedProject/res/enemy/<biome> | enemies.json 欄 | ChapterProgressionTest | — | 功能完成・未驗收 |
| P1.6 關卡表 | 31 關 | res/map_manager | levels.json | LevelTableTest（B1/B5/C1/G1）、§1.2 樣本；**缺**：全表引用檢查（P1.11） | T-01 | 功能完成・未驗收 |
| P1.7 boss 房／寶箱／商店表 | 14 房、11 箱、8 藥水 3 機器 | rgprefab/rooms、res/item、room/event | boss_rooms/boss_chests/shop.json | BossRoomTest、ShopTest | T-08 | 功能完成・未驗收 |
| P1.8 內嵌物件 | rail/stone02/carpet/obj22_house/table/stone（子物件自帶 SpriteRenderer） | rgprefab/rooms/*_boss* | boss_rooms.json prefabs 補齊 | BossRoomTest 加斷言 | — | 未開始 |
| P1.9 未解析掉落物 | mineral_obj、GetRoomReward、ChestXmlReader、GetPot/GetCoin | 反編譯（需 game_typed.c 交叉） | 對應 JSON 欄 | V-R | T-09 T-10 | 未開始 |
| P1.10 升版 1.07→1.7.10 | bullets/bosses/buffs/enemy_guns/droptables/weapon_catalog 改由 AssetRipper 產生 | res/*、rgprefab/bullet | 同名 JSON | V-D 全表 diff 報告 | — | 未開始 |
| P1.11 資料閘擴充 | validate_data.py 覆蓋 levels/boss_rooms/boss_chests/shop；引用完整性（敵池 id 存在、房間 id 存在、武器 id 存在） | — | validate_data.py | V-G（GREEN，8 warnings：T-27 ×4、P1.8 ×1） | T-27 T-28 | 功能完成・未驗收 |
| P1.12 數值出 C++ | 掉落物數值（金幣 5/3/1、能量 8、磁吸 speed/range、幀清單）、機器擺位、雕像表移到 JSON | res/register/coin_*、energy、statue_0N | items.json | ShopTest/EnemyRewardTest 改讀表 | — | 未開始 |

### P2 地圖與關卡
| 子階段 | 目標 | 取捨 | 狀態 |
|---|---|---|---|
| P2.1 關卡表驅動每層 | LevelIdFor；章節變體 | T-01 | 功能完成・未驗收 |
| P2.2 生態貼圖 | 六地板變體、牆頂＋牆面、每關障礙皮膚、寶箱等級 | T-02 | 功能完成・未驗收 |
| P2.3 EnemyMaker | 預算表、大房加級、加權池、分波 | T-03 T-04 T-05；**R1 指出**：加級只按寬度，原碼 `game_full.c:682128` 另有旗標與正等級條件且檢查另一尺寸 → 回到 V-R 重讀；分波時序測試缺 | **進行中** |
| P2.4 傳送門 | 走進去換層；boss 死後出現 | — | 功能完成・未驗收 |
| P2.5 比例／牆／箱／走廊 | 48 px/單位、底部錨定 | — | 功能完成・未驗收 |
| P2.6 boss 房布局 | 14 預製件 | P1.8 未畫 | 功能完成・未驗收 |
| P2.7 房間角色、寶箱 | 按距離指派；獎勵房箱；boss 箱 | T-06 T-07 T-10 | 功能完成・未驗收 |
| P2.8 商店房 | 三機器、E 購買 | T-08 T-11 T-12 | 功能完成・未驗收 |
| P2.9 門片／影子／事件房 | d1–d5、shadow2、雕像、礦物 | T-13 T-14 T-15 T-16 | 功能完成・未驗收 |
| P2.10 傭兵房 | TalkMercenary 雇傭 + 跟隨戰鬥（＝P7.2，只在此登記） | — | 未開始 |
| P2.11 視覺補完 | 外牆裝飾變體擲值門檻、角色影子、P1.8 內嵌物件繪製 | — | 未開始 |
| P2.12 一般房清場獎勵 | GetRoomReward 內容（P1.9） | — | 未開始 |
| P2.13 MapManager.CreateMap 對照 | 房間數 = map_long + 特殊房？目前 map_long+4 | T-17 | 未開始 |
（第 16 層與勝利 → P4.5，不在此重複。）

### P3 戰鬥忠實度（下一主線）
| 子階段 | 目標 | 資料來源 | 驗證 | 狀態 |
|---|---|---|---|---|
| P3.0 單位換算稽核 | 子彈用 `kDataSpeedToPxPerSec=15`、地圖 `kCellPx=32`（螢幕 48）；查原版 PPU 與速度單位，統一或證明兩者都對 | game_full.c RGBullet 速度、預製件 | V-R + 一個「子彈飛過 N 格的秒數」測試 | 未開始 |
| P3.1 Buff 本體 | BuffFire/Poison（GetHurt 每跳）、BuffIce（BuffEnd 減速）、BuffElectric、BuffArmor、ChangeSpeed | BuffXXX__*、buffs.json（P1.10 後） | 每種 buff 一個時序測試（固定 dt 重放：tick 間隔、持續、疊加） | 未開始 |
| P3.2 Buff 觸發連結 | 子彈預製件 `RGBulletBuffTrigger`(14)/`ReboundBuffEffector`(70)/`RGSwordBuffTrigger`/`RGArrowBuffTrigger`/`RGLaserBuff` → bullets.json `buff` 欄 | 預製件元件 | V-D 5 顆子彈；V-H：火武器命中後 log | 未開始 |
| P3.3.x 角色技能 | **每角一切片** c03…c13（11 切片），每切片：反編譯行號↔測試名 | CharSkillC0N__* | 每技能：觸發、冷卻、效果量、結束 4 個斷言 | 未開始 |
| P3.4 接觸傷害 | AI04/08/13 近戰、boss 撞擊、無敵幀 | RGEController__GetHurt、RGController__GetHurt | V-T | 未開始 |
| P3.5.x boss 攻擊模式 | **每 boss 一切片**（目前全為 Fan 近似） | BossAI0N__ChooseAttack | golden 重放（改動前後 hash） | 未開始 |
| P3.6.x 射擊模式 | 未移植的 25/46 武器射擊模式，**每模式一切片** | Gun*__* | WeaponBrainTest | 未開始 |
| P3.7 特殊 AI | AI05、AI06 召喚、AI12 複合血量 | 對應類別 | V-T | 未開始 |
| P3.8 精英 | ex_* 數值、badass_rate 真值 | EnemyMaker 私有欄位 | V-D | 未開始 |
| P3.9 雕像祝福 | 5 種未移植祝福 | BuffStatue01..04 | V-T | 未開始 |

### P4 進度與經濟循環
| 子階段 | 目標 | 依賴 | 狀態 |
|---|---|---|---|
| P4.1 擊殺經濟 | 掉落、磁吸、HUD | — | 功能完成・未驗收（T-18 T-19；GetReward 門檻比較在原碼截斷處之後 → INFERRED） |
| P4.2 寶石 | 來源（boss/結算/花園）、`PlayerSaveData.gems` | P5.3 存檔 | 未開始 |
| P4.3 復活／重試 | UIWindowReborn（200 寶石／免費一次）、window_try_again（1000 金幣） | P4.2 | 未開始 |
| P4.4 buff 三選一 | 24 個 Buff_info、RGGameProcess.buff_* 旗標、SaveBuffChoice | P3.1 | 未開始 |
| P4.5 16 層與勝利 | G1 終點房 r_end、Outcome::Victory、勝利結算 | P2 | 未開始 |
| P4.6a 結算畫面 | Statements 場景 | — | 功能完成・未驗收（T-20） |
| P4.6b 統計存檔 | StatisticData（通關次數、解鎖） | P5.3 | 未開始 |

### P5 大廳（HeroRoom）
P5.1 可行走大廳與進地牢；P5.2 角色解鎖／皮膚；P5.3 本地存檔 PlayerSaveData/RGSaveManager；P5.4 花園、寵物（=P7.1）、許願池；P5.5 練習房、教學。全部未開始。

### P6 UI 與表現
P6.1 入口流程（功能完成・未驗收）；P6.2 武器資訊；P6.3 小地圖 UIMap；P6.4 buff 列、boss 血條；P6.5 暫停／設定；P6.6 傷害數字、hit-stop；P6.7 每關 BGM、camera_bg；P6.8 開場／劇情／載入。P6.2–P6.8 未開始。

### P7 內容補完
P7.1 寵物（=P5.4）；P7.2 傭兵（=P2.10）；P7.3 無盡模式；P7.4 季節 boss（非目標，只登記）。

### P8 擴充管線
| 子階段 | 目標 | 驗證 |
|---|---|---|
| P8.0 能力邊界 | 明文：只改 JSON 能做的是「既有行為的變體」（新武器＝既有射擊模式＋數值＋貼圖；新敵人＝既有 brain＋數值＋幀）；新行為仍需 C++ brain | 文件 |
| P8.1 素材替換 | 換高版本 ExportedProject 重跑 `tools/extract_*.py` | V-S 全案例重跑 |
| P8.2 新增範例 | **提早做**：各新增一把武器、一種敵人、一間房，只動 JSON/PNG | V-T |
| P8.3 文件 | CONVENTIONS.md「資料驅動新增流程」 | Codex |

---

## 4. 執行順序

1. **驗收補證據**：先對 P1.6、P2.x、P4.1 依 §1 補齊證據（測試報告、H 案例、樣本表），把「未驗收」變「驗收完成」；同時做 P1.11（資料閘）與 P1.12（數值出 C++），因為 R1 兩條不通過都源於此。
2. **P3.0 單位稽核**（影響所有速度）。
3. **P3.1 → P3.2** buff；**P1.10** 升版 buffs/bullets 表在 P3.1 之前。
4. **P3.3.x** 技能逐角。
5. **P4.2 → P4.3 → P4.4**（寶石→復活→三選一；依賴 P5.3 存檔，先做最小存檔）。
6. **P3.4、P3.5.x、P3.6.x**。
7. **P5、P6、P2.10–P2.13** 穿插。
8. **P8.2 新增範例**提前到 P3 完成後做一次，驗證管線；其餘 P8 最後。

---

## 5. 每個切片的固定交付格式

1. commit：`<階段代號>: <一句話>`；內文列 FAITHFUL / INFERRED / PORT CHOICE / NOT PORTED。
2. 程式碼：每個移植方法帶 `// FAITHFUL: … @ game_full.c:<line>`（截斷處後改 `// INFERRED:`）或 `// PORT CHOICE: <T-ID>`。
3. 測試：至少一個 GoogleTest 斷言被移植的行為；資料變更過 `validate_data.py`。
4. 證據：測試報告行、H 案例編號與實際 log、截圖與對照數值。
5. 本文件：更新 §2、§3 狀態；新增取捨登記 §6（含 ID）；審查結果進 §9。

---

## 6. 取捨與不可恢復登記（單一清單；程式碼註解引用 T-ID）

| ID | 項目 | 標籤 | 位置 | 影響 | 補完階段 |
|---|---|---|---|---|---|
| T-01 | 章節變體 A/B 由 runSeed 決定 | PORT CHOICE | RunState.hpp `BiomeVariantFor` | 每章走哪個生態 | 需原碼 |
| T-02 | 地板變體取分類擲值否則取常駐擲值 | INFERRED | RoomGen.cpp CreateFloor | 地板花紋 | 需原碼 |
| T-03 | 精英率 stage×3% | PORT CHOICE | GameScene.cpp:897 | 精英數量 | P3.8 |
| T-04 | 生成上限 24、consume 最小 1、生成點分布 | PORT CHOICE | EnemyMaker.hpp、GameScene SpawnNextWave | 房間敵人數與位置 | P2.3 |
| T-05 | 大房加級只按寬度 >15/>21 | INFERRED（R1：與原碼旗標條件不符） | EnemyMaker.hpp `EnemyMakerLevel` | 敵人預算 | P2.3 |
| T-06 | 房間角色按距離指派（出口／獎勵／事件） | PORT CHOICE | GameScene.cpp 房間角色區 | 特殊房位置 | P2.13 |
| T-07 | boss 寶箱與傳送門重疊時下移兩格 | PORT CHOICE | GameScene.cpp:1623 | 視覺 | — |
| T-08 | r_chest / r_sell 由 floorSeed 奇偶決定 | PORT CHOICE | GameScene.cpp:359 | 獎勵房類型 | 需原碼 |
| T-09 | 獎勵箱 GetPot/GetCoin 分支、ChestXmlReader 機率 | NOT PORTED | GameScene 寶箱開啟 | 獎勵箱只出武器 | P1.9 |
| T-10 | 一般房清場獎勵 GetRoomReward | NOT PORTED | RGRoomX ClearRoom log | 清場無獎勵 | P2.12 |
| T-11 | 商店機器擺位一排 | PORT CHOICE | GameScene 商店區 | 視覺 | 需原碼 |
| T-12 | 飲水機＝免費補滿能量一次 | PORT CHOICE | GameScene.cpp:1955 | 能量經濟 | 需原碼 |
| T-13 | 事件房只在雕像／礦物間選（傭兵未做） | PORT CHOICE + NOT PORTED | GameScene.cpp:987 | 傭兵缺席 | P2.10 |
| T-14 | 雕像 5 種祝福未移植；index→名字為 i2 順序推論 | NOT PORTED / INFERRED | GameScene.cpp:2056、kStatueName | 祝福 | P3.9 |
| T-15 | 礦物掉 10 銅幣（mineral_obj 未解析） | PORT CHOICE | GameScene 礦物房 | 經濟 | P1.9 |
| T-16 | shadow0 不畫（需乘法材質） | PORT CHOICE | GameScene 影子區 | 視覺 | P6 |
| T-17 | 走法房間數 map_long+4 | PORT CHOICE | GameScene.cpp:165 | 樓層大小 | P2.13 |
| T-18 | reward_value[4]=[gold,silver,copper,energy] 計數；門檻比較 `Range(0,100)<reward_rate` | INFERRED（原碼在 Range 後截斷） | EnemyReward.hpp、GameScene SpawnReward | 掉落 | 需 game_typed.c 交叉 |
| T-19 | 掉落散射力度 6、磁吸數值寫在 C++ | PORT CHOICE | CoinPickup.cpp:17 | 手感 | P1.12 |
| T-20 | 結算畫面版面用移植座標；藥水／寶石欄為 0 | PORT CHOICE | EndScene.cpp | 視覺 | P4.2 |
| T-21 | 設計房座標以 (20,20) 為中心換算 | INFERRED（實玩驗證） | extract_design_rooms.py:27 | 布局 | — |
| T-22 | Gun008 的 3/0.1 解讀為次數／間隔 | INFERRED | Gun008.hpp:25 | 該武器 | P3.6 |
| T-23 | Gun017Child 追蹤無人機未移植 | NOT PORTED | Gun017.hpp:35 | 該武器 | P3.6 |
| T-24 | RGAisle、EnemyAI15 只移植純邏輯，效果交場景層 | 部分 | RGAisle.hpp:17、EnemyAI15.hpp:43 | 需登記接線位置 | P3.7 |
| T-25 | 子彈 15 px/單位 vs 地圖 32 px/單位 | 未定 | SimConfig.hpp:27、GameScene kCellPx | 子彈速度可能偏慢 | P3.0 |
| T-26 | Ghidra 單例呼叫後截斷（系統性） | — | game_full.c | 多處 INFERRED | 改讀 game_typed.c / frida |
| T-27 | ex_orc05 預製件在 Assets/GameObject 而非 res/enemy，enemies.csv 缺此列；場景以 AI01 代表種替代 | NOT PORTED（資料缺列） | validate_data.py 警告、GameScene SpawnEnemy 退化 | A2–A5 精英池 | P1.10 |
| T-28 | boss 房門帶外的物件（r2_boss3/4 的 rail，y=21）被抽取器丟棄 | PORT CHOICE | build_gamedata.py build_boss_rooms `dropped_outside` | 兩間 boss 房少 1 條軌道 | P2.11 |

---

## 7. Codex 審查清單（每條：通過 / 不通過 / 不適用 ＋ 證據）

| # | 項目 | 證據要求 |
|---|---|---|
| 1 | 來源可追溯：FAITHFUL 只覆蓋原碼可見部分；截斷後為 INFERRED | 抽 3 處到 game_full.c 行號 |
| 2 | 資料驅動：本應在 JSON 的數值不在 C++ | grep 常數；例外只允許幾何常數與鉤子 |
| 3 | 測試：斷言被移植的行為；golden 未變或有理由 | ctest 報告 + golden hash |
| 4 | 無頭證據：§1.1 案例可重現 | 貼實際 log |
| 5 | 視覺：尺寸／位置對得上預製件 | 截圖＋數值表 |
| 6 | 取捨登記：所有 PORT CHOICE / INFERRED / NOT PORTED 有 T-ID | grep 對 §6 |
| 7 | 回歸：ctest 全綠、validate_data 綠、無 `--no-verify` | 報告行 |
| 8 | 文件：§2/§3/§6/§9 同步；狀態值合法 | 目視 |

---

## 8. 維護規則（Claude 與 Codex）

- 本文件放在 `docs/MASTER_PLAN.md`；狀態變更與程式碼在**同一個 commit**。
- Claude：實作、更新 §2／§3、登記 §6（配 T-ID）、附證據。
- Codex：依 §7 審查，把結論寫進 §9（日期、commit、逐條結果、要求修正）；文件與程式不符時可直接修文件並註明。
- 雙方不得刪除對方紀錄；分歧在 §9 該筆下以「回覆」段落討論，使用者裁決。
- 版本：Codex 通過 → 小版號 +1；新增階段或改順序 → 中版號 +1。
- Codex 執行環境：`CODEX_HOME` 指向不含 MCP 的臨時設定（本機 unityMCP 未啟動會讓 codex 中止）；提示以 stdin 餵入（Windows 命令列上限）。

---

## 9. 審查紀錄

### R1 — 2026-09-12 — Codex（gpt-6-astra，codex-cli 0.154.0）— 對象 v1.0 @ a3bb557 — **不通過**
逐條：1 來源 ✗（FAITHFUL 超出原碼可證範圍：GetReward）；2 資料驅動 ✗（掉落數值、磁吸、動畫幀在 C++）；3 測試 ✗（分波時序、延後清場未覆蓋；golden 未證明未變）；4 無頭 ✗（無可重現案例）；5 視覺 ✗（無對照）；6 取捨 ✗（漏 10 項）；7 回歸 ✗（僅 validate_data 實跑 GREEN/3 warnings）；8 文件 ✗（狀態值不合法、P6.10 vs P6.8、TEST 數 2009 非 2013）。
抽查：P1.6 資料抽取有支持（B1/B5/C1 與預製件相符）但驗收不足；P2.3 不支持完成（加級條件與 `game_full.c:682128` 不符）；P4.1 功能有支持、取捨寫少。
要求：資料版本矩陣、資料閘擴充、可執行案例、六欄格式、依賴重排、P8 能力邊界、§6 補 T-ID。
**回覆（Claude，v1.1）**：全部採納；§0 矩陣、§1.1 H1–H7、§1.2 樣本、§2 功能／驗收分欄、§3 六欄與逐切片拆分、§6 T-01…T-26、§7 加不適用與證據欄。程式碼：GameScene SpawnReward 註解 FAITHFUL→INFERRED（T-18）。P1.11/P1.12/P3.0 列為最先做的切片。待 Codex R2。
