# Soul-Knight-ai 專案架構書（元氣騎士 1.7.10 忠實復刻）

> 本文件是專案的**單一事實來源**：範圍、階段、每階段的資料來源與驗證方式、Codex 審查紀錄。
> 由 Claude（實作）與 Codex（審查）共同維護；規則見 §8。狀態欄位只允許四種值：`未開始 / 進行中 / 完成 / 完成（有取捨）`。
> 版本：v1.0（2026-09-06）

---

## 0. 目的、範圍、非目標

**目的**：在自製的 C++ 遊戲系統（PTSD：SDL2 + OpenGL + ImGui，CMake，GoogleTest）上，以**資料驅動**的方式忠實復刻《元氣騎士》1.7.10（Unity 2017.4.1f1 IL2CPP，armeabi-v7a）的單人地牢循環，作為學術演示。之後要能**用高版本素材替換**並**新增內容**，所以所有數值、貼圖、布局都必須走「解包資料 → JSON/PNG → 程式讀表」，不得寫死。

**忠實度定義**（由高到低，程式碼註解與本文件都用這套標籤）：
- `FAITHFUL @ <來源>`：數值或邏輯直接來自反編譯（`_reverse/ghidra_export/game_full.c`）、AssetRipper 預製件（`_reverse/asset soul/ExportedProject`）或資料表（`_reverse/analysis/data_tables/*.csv`），可逐項對照。
- `INFERRED`：來源存在但被 Ghidra 截斷或欄位語意需推論，推論依據必須寫在註解裡。
- `PORT CHOICE`：原版邏輯不可恢復，移植版自行決定，必須標明並集中在一處以便日後替換。
- `NOT PORTED`：明知原版有、目前沒做，程式碼只記 log。

**非目標**（不在本計畫內）：多人連線（NetControllerManager / UNet）、IAP 與廣告（SDKManager、I_reborn_AD 的廣告分支）、雲端存檔、Google Play 成就、季節活動 boss（Nian）。這些在程式碼中一律當 observed data，不實作。

**法律框架**：解包素材屬 ChillyRoom，僅供課堂演示，不散布、不商業化；`Resources/sprites` 已列入 `.gitignore`，倉庫只含程式與由資料表轉出的 JSON。

---

## 1. 驗證體系（每個階段都從這裡挑驗證方式）

| 代號 | 方式 | 指令 / 位置 | 用途 |
|---|---|---|---|
| V-T | GoogleTest 單元測試 | `cmake --build build --config Debug --target SoulKnightTests && ctest --test-dir build -C Debug -E Tests_NOT_BUILT` | 純邏輯、資料表載入、決定性（seed）。目前 **2013 個 TEST 巨集 / 141 個測試檔**，全綠。 |
| V-G | 資料閘 | `python tools/validate_data.py`（pre-commit 亦執行） | JSON 表的必要欄位、數值型別、reward_value 長度等；非 ASCII 原始碼會被擋。 |
| V-D | 資料對照 | 比對 `Resources/data/*.json` 與 `_reverse/analysis/data_tables/*.csv` / 預製件欄位 | 每個 re-key 階段都附「抽樣對照」：至少 5 筆手算相符。 |
| V-H | 無頭鉤子執行 | `SK_*` 環境變數（清單見 §1.1） | 不用輸入就能把流程跑到指定狀態，靠 log 斷言。 |
| V-S | 截圖 | `SK_SHOT=<png>`（離屏 FBO 讀取，任何幀數皆可） | 視覺項目（比例、貼圖、布局）必附截圖，由人與 Codex 目視對照原版預製件數值。 |
| V-R | 反編譯對照 | `// FAITHFUL: <Class>__<Method> @ game_full.c:<line>` 註解 | 每個移植方法都要能指到來源；Codex 抽查註解與來源是否一致。 |
| V-C | Codex 審查 | 每階段完成後由 Codex 依 §7 清單審查，結果記入 §9 | 獨立第二意見；不通過者退回。 |

### 1.1 無頭鉤子（環境變數）
`SK_MAX_FRAMES`（跑 N 幀後乾淨退出）、`SK_SHOT`（存最後一幀）、`SK_SKIP_MENU`（直接進地牢）、`SK_START_CHAR=cNN` / `SK_CHAR=cNN` / `SK_MENU=start`（入口流程）、`SK_FORCE_CLEAR=K`（第 K 幀強制換層）、`SK_FORCE_DIE=K`、`SK_FORCE_KILL=K`（殺光敵人）、`SK_FORCE_SKILL=K`、`SK_ENDSCENE=restart|quit|menu`、`SK_GATE_TELEPORT=K` / `SK_GATE_BESIDE=K`（傳送門）、`SK_ROOM_TELEPORT=K,R`、`SK_SHOP_BUY=K` / `SK_SHOP_MACHINE=N`、`SK_STATUE_WORSHIP=K`、`SK_COINS=N`、`SK_EVENT_ROOM=statue|mineral`。
注意：`GameScene::m_Frame` 是**每層**重新計數，鉤子在每層都會再觸發一次。

---

## 2. 現況總表（截至 2026-09-06，分支 `feat/phase3-presentation-content`）

| 區塊 | 狀態 | 主要 commit | 驗證 |
|---|---|---|---|
| 資料層 P1（敵人／角色／貼圖／武器／章節／關卡表／boss 房／寶箱／商店表） | 完成 | 12c3750 … 9d468ee | V-T V-G V-D |
| 入口流程 P6.1（主選單、13 角選角、i2 名字） | 完成 | 6e46eed | V-T V-H V-S |
| 結算畫面 P4.6（Statements 場景） | 完成（有取捨：金幣／寶石以外的分支未恢復） | 1cfb297 | V-T V-H V-S |
| 經濟 P4.1（擊殺掉落、金幣磁吸、HUD 計數） | 完成（有取捨：reward_value 四格語意為 INFERRED） | 5c8c4b0 | V-T V-H V-S |
| 地圖 P2.1–P2.5（關卡表驅動、六生態貼圖、EnemyMaker、傳送門、比例／牆／箱、走廊、特殊房殼） | 完成 | 3eded10 … d0a045c | V-T V-H V-S |
| 地圖 P2.6–P2.9（boss 房布局、寶箱與房間角色、商店、門片影子、事件房） | 完成（有取捨：見各節） | b46a65b … e2c5030 | V-T V-H V-S |
| 戰鬥核心（每擊數學、dash、掉落表、AI 派發、18 boss brain） | 完成（前期工作） | phase 1–5 | V-T |
| 戰鬥忠實度 P3（buff、c03–c13 技能、boss 攻擊模式、接觸傷害） | 未開始 / 部分 | — | — |
| 進度循環 P4.2–P4.5（復活、重試、每層 buff 三選一、寶石、16 層勝利） | 未開始 | — | — |
| 大廳 P5（HeroRoom、存檔、花園、寵物） | 未開始 | — | — |
| UI 表現 P6.2–P6.10（小地圖、武器資訊、buff 列、boss 血條、暫停、傷害數字、音樂） | 未開始 | — | — |
| 擴充 P8（高版本素材替換、新增內容管線） | 未開始 | — | — |

---

## 3. 階段規劃（細分）

每個子階段固定格式：**目標 / 資料來源 / 交付物 / 驗證 / 已知不可恢復（取捨）/ Codex 檢查點**。
估算單位是「切片」：一個切片 = 一次可獨立提交、可獨立驗證的工作。

### P1 資料層（完成）
| 子階段 | 內容 | 狀態 |
|---|---|---|
| P1.1 | 145 種敵人 per-species 數值（enemies.csv → enemies.json：hp/atk/speed/consume/reward） | 完成 |
| P1.2 | 13 角色 per-character 數值（c##.prefab RoleAttributePlayer） | 完成 |
| P1.3 | 156 個實體的動畫幀清單（prefab → Animator → clip → sprite guid） | 完成 |
| P1.4 | 194 把武器 per-prefab（weapons.csv：atk/consume/bullet/icon/item_value） | 完成 |
| P1.5 | 章節／生態標記與 boss 池 | 完成 |
| P1.6 | 31 個關卡表（map_manager/map_A1…G1：地板／牆／障礙表／房間池／加權敵池／精英池／寶箱等級／map_long） | 完成 |
| P1.7 | 14 個 boss 房布局 + 預製件皮膚表；11 個 boss 寶箱（招牌武器）；商店表（8 藥水、3 機器） | 完成 |
| P1.8 | **待補**：boss 房內嵌物件（rail/stone02/carpet/obj22_house/table/stone 是子物件自帶 SpriteRenderer，抽取器要讀子物件的 m_Sprite） | 未開始 |
| P1.9 | **待補**：`mineral_obj` 掉落物（guid 9b1b5bc8 / 3713bab6 未解析）、`RGRoomX.GetRoomReward` 一般房清場獎勵、`ObjectVendingMachine` 的 ChestXmlReader 武器機率表 | 未開始 |

Codex 檢查點（P1 通用）：抽 5 筆 JSON 對 CSV／預製件；`validate_data.py` 綠；沒有寫死的數值進到程式碼。

### P2 地圖與關卡（P2.1–P2.9 完成，P2.10–P2.14 待做）
| 子階段 | 內容 | 狀態 / 取捨 |
|---|---|---|
| P2.1 | 關卡表驅動每層（LevelIdFor：章節變體 A/B 由 runSeed 決定） | 完成（**PORT CHOICE**：A/B 選法未恢復） |
| P2.2 | 六生態地板六變體（RoomGen pass-3 擲值）、牆頂＋牆面、每關障礙皮膚、寶箱等級 | 完成（**INFERRED**：兩次擲值取哪一次） |
| P2.3 | EnemyMaker：點數預算表、大房加級、加權池扣 consume、分波（level×3、0.8 s） | 完成（**PORT CHOICE**：精英率 stage×3%；生成點分布） |
| P2.4 | 傳送門（走進去換層；boss 層 boss 死後出現；特殊房 EnemyMaker level 0 不生怪） | 完成 |
| P2.5 | 比例 48 px/單位、16×24 底部錨定牆、箱蓋、走廊同畫法、裝飾限室內、特殊房空殼 | 完成 |
| P2.6 | boss 房布局（14 預製件） | 完成（P1.8 內嵌物件未畫） |
| P2.7 | 房間角色（出口／獎勵／事件按距離指派）、獎勵房寶箱、boss 寶箱＋招牌武器 | 完成（**PORT CHOICE**：角色指派；獎勵箱藥水／金幣分支未恢復） |
| P2.8 | 商店房：三台機器、E 購買、藥水立即回復、飲水機（naughty_boy NPC 動畫） | 完成（**PORT CHOICE**：機器擺位、飲水機效果、r_chest/r_sell 抽法） |
| P2.9 | 門片 d1–d5、道具影子（shadow2）、雕像房（供奉、祭司／盜賊祝福）、礦物房 | 完成（**NOT PORTED**：另 5 種祝福；**PORT CHOICE**：礦物掉銅幣） |
| P2.10 | 傭兵房：TalkMercenary 雇傭、NpcMercenaryController 跟隨與戰鬥（brain 已在 port） | 未開始 |
| P2.11 | 外牆裝飾變體（CreateWall Range(0,100) 門檻）、角色影子、boss 房內嵌物件繪製 | 未開始 |
| P2.12 | 一般房清場獎勵（GetRoomReward 內容） | 未開始（需重讀反編譯） |
| P2.13 | 第 16 層 G1（r_end 終點房、勝利結局） | 未開始（併入 P4.5） |
| P2.14 | MapManager 隨機走法對照：原版 CreateMap 的房間數＝map_long＋特殊房；目前 port 用 map_long+4 | 未開始（V-R 重讀 CreateMap） |

Codex 檢查點（P2）：截圖與預製件數值對照（牆 16×24 pivot(0.5,0)、門 5 片、房間尺寸 15/11/19×13/21）；`levels.json` 抽查 3 關；無頭鉤子能走完「進門→封鎖→清場→開門→傳送門」。

### P3 戰鬥忠實度（下一個主線）
| 子階段 | 內容 | 資料來源 | 驗證 |
|---|---|---|---|
| P3.1 | Buff 本體：BuffFire / BuffPoison（GetHurt 每跳）、BuffIce（BuffEnd 減速）、BuffElectric、BuffArmor、ChangeSpeed | `game_full.c` BuffXXX__*、`buffs.json` 12 筆（cd/持續） | V-T：每種 buff 的時序（tick 間隔、持續、疊加）用固定 dt 重放；V-R |
| P3.2 | Buff 觸發連結：子彈預製件上的 `RGBulletBuffTrigger`(14) / `ReboundBuffEffector`(70) / `RGSwordBuffTrigger` / `RGArrowBuffTrigger` / `RGLaserBuff` → bullets.json 加 `buff` 欄 | 預製件元件 | V-T：抽 5 顆子彈對照；V-H：SK_FORCE_KILL 前用火武器打敵人看 log |
| P3.3 | 角色技能 c03–c13 效果本體（目前只有冷卻閘） | `CharSkillC03..13__*` | V-T：每個技能一個測試（Gun007/C01/C02 為範本） |
| P3.4 | 接觸傷害子系統（AI04/08/13 近戰、boss 撞擊） | `RGEController__GetHurt`、`RGController__GetHurt` | V-T：碰撞→傷害→無敵幀 |
| P3.5 | Boss 真實攻擊模式（目前全為 Fan 近似）：每隻 boss 的 roll→attack 表 | `BossAI01..14__ChooseAttack`（owner-truncated，需逐隻重讀） | V-T：golden 重放；V-H：SK 進 boss 房觀察 |
| P3.6 | 尚未移植的 25/46 武器射擊模式、AI05 brain、AI06 召喚、AI12 複合血量 | 對應 Gun*/EnemyAI05/BossAI06Child/BossAI12Parent | V-T |
| P3.7 | 精英（ex_*）數值與 badass_rate 真值 | `EnemyMaker` 私有欄位、`ex_*.prefab` | V-D |
| P3.8 | 雕像另 5 種祝福（召喚騎士、飛刀、震波、魔法飛彈、兔子） | `BuffStatue01..04` + `buffs.json` BuffStatue 欄位 | V-T |

Codex 檢查點（P3）：每個 buff/技能有「反編譯行號 ↔ 測試名」對照表；沒有為了讓測試過而改動 golden。

### P4 進度與經濟循環
| 子階段 | 內容 | 資料來源 |
|---|---|---|
| P4.1 | 擊殺掉落、金幣、HUD（完成） | — |
| P4.2 | 死亡→復活視窗 `UIWindowReborn`（200 寶石復活 / 免費一次）、`window_try_again`（從本層再試：1000 金幣） | window_reborn.prefab、i2 I_revive/I_try_again |
| P4.3 | 寶石（gems）來源：boss 掉落 / 結算獎勵 / 花園；`PlayerSaveData.gems` 跨局存檔 | PlayerSaveData、UIStatement.AddGem（×50？需重讀） |
| P4.4 | 每層開始的 buff 三選一（24 個 Buff_info，RGGameProcess.buff_* 旗標、SaveBuffChoice） | i2 Buff_info_00..23、RGGameProcess 欄位 |
| P4.5 | 16 層上限、G1 終點房、勝利結算（Outcome::Victory） | map_G1、r_end、I_victory |
| P4.6 | 結算畫面（完成）＋統計存檔 `StatisticData`（通關次數、解鎖物件） | UIStatement.level_list、StatisticData |

### P5 大廳（HeroRoom）
| 子階段 | 內容 |
|---|---|
| P5.1 | HeroRoom 場景可行走（r_start 類地圖、NPC 站位）、從大廳進地牢取代現在的選角畫面 |
| P5.2 | 角色解鎖／購買（寶石）、皮膚 |
| P5.3 | 本地存檔 `PlayerSaveData`（金幣寶石、解鎖、統計）與 `RGSaveManager` |
| P5.4 | 花園（ItemPlant/ItemPlantPot/ObjectPlant）、寵物（Pet*）、許願池（ItemWishingWell 已 port 純邏輯） |
| P5.5 | 練習房、教學（UITeaching） |

### P6 UI 與表現
| 子階段 | 內容 |
|---|---|
| P6.1 | 主選單／選角（完成；之後被 P5.1 大廳取代或並存） |
| P6.2 | 武器資訊（weapon_icon、weapon_consume）、彈藥／能量消耗提示 |
| P6.3 | 小地圖 `UIMap`（RGRoomX.GetMinimapSprite） |
| P6.4 | Buff 列 `UIBuffBar`、boss 血條（bossNN_info 預製件） |
| P6.5 | 暫停視窗 `UIWindowPause`、設定列 |
| P6.6 | 傷害數字／訊息（ShowTextHurt/ShowTempMessage）、hit-stop、震動 |
| P6.7 | 每關 BGM（MapManager.bgm_clip）、camera_bg 底色、音效表 |
| P6.8 | 開場／劇情／載入（Splash/Plot/Loading）|

### P7 內容補完
P7.1 寵物、P7.2 傭兵（=P2.10）、P7.3 無盡模式（EndlessMapManager）、P7.4 季節 boss（非目標，只記錄）。

### P8 擴充管線（使用者的最終目標）
| 子階段 | 內容 | 驗證 |
|---|---|---|
| P8.1 | 素材替換：所有貼圖經 `tools/extract_*.py` 由名稱解析，換高版本 ExportedProject 目錄即可重跑 | 換一組貼圖後 V-S 全部重跑 |
| P8.2 | 內容新增：新增一把武器／一種敵人／一間房只改 JSON 與 PNG，不改 C++ | 新增範例各一，V-T 通過 |
| P8.3 | 文件：CONVENTIONS.md 更新為「資料驅動新增流程」 | Codex 審 |

---

## 4. 執行順序（建議）

1. **P3.1 → P3.2**（buff 本體與觸發連結）：戰鬥手感差異最大、可完全用測試驗證。
2. **P3.3**（c03–c13 技能）：13 角選角已有，但 11 個技能沒效果。
3. **P4.2 → P4.4**（復活、重試、buff 三選一）：讓一局有完整的原版節奏。
4. **P3.4 → P3.5**（接觸傷害、boss 真攻擊模式）。
5. **P5**（大廳與存檔）。
6. **P6**（UI 表現）與 **P2.10–P2.14** 穿插。
7. **P8**（擴充管線）最後做，因為要等資料驅動的面完整。

每完成一個子階段：更新 §2 與對應表格的狀態、提交、交 Codex 審查、把結果寫進 §9。

---

## 5. 每個切片的固定交付格式

1. commit 訊息：`<階段代號>: <一句話>` ＋ 內文列「FAITHFUL 來源 / INFERRED / PORT CHOICE / NOT PORTED」。
2. 程式碼：每個移植方法帶 `// FAITHFUL: … @ game_full.c:<line>` 或 `// PORT CHOICE: …`。
3. 測試：新邏輯至少一個 GoogleTest；資料變更過 `validate_data.py`。
4. 證據：V-H log 片段或 V-S 截圖（放在對話與 §9 審查紀錄）。
5. 本文件：更新狀態表；新增的取捨寫進對應子階段。

---

## 6. 已知風險與不可恢復清單（集中登記）

| 項目 | 現況 | 處理 |
|---|---|---|
| 章節變體 A/B 選法 | 未恢復 | PORT CHOICE：runSeed 決定（`RunState::BiomeVariantFor`） |
| reward_value[4] 語意 | INFERRED（GetCoin 0..2 clamp + PrefabName.energy=8） | 若找到 GetReward 迴圈本體即修正 |
| EnemyMaker.badass_rate | 私有欄位未恢復 | PORT CHOICE stage×3%，單一處 |
| MapManager.CreateMap 房間數語意 | 反編譯截斷 | P2.14 重讀 |
| RGRoomX.GetRoomReward | 截斷 | P2.12 |
| RGChestRoomReward 藥水／金幣分支、ChestXmlReader | 截斷 | P1.9 |
| Ghidra 對單例呼叫後的「does not return」截斷 | 系統性 | 改讀 `game_typed.c` / `game_named.c` 交叉比對，必要時用 frida 動態驗證（工具已裝） |
| shadow0 黑色方塊 | 需乘法材質 | 不畫；P6 若加材質再補 |

---

## 7. Codex 審查清單（每次審查照此逐條回答：通過 / 不通過＋理由）

1. **來源可追溯**：新增或修改的每個數值與邏輯，是否有 FAITHFUL/INFERRED/PORT CHOICE 標籤且指向具體來源？隨機抽 3 處到 `game_full.c` 或預製件核對。
2. **資料驅動**：是否有本應在 JSON 的數值被寫進 C++？（允許：純幾何常數、鉤子）
3. **測試**：新增測試是否真的斷言了被移植的行為（不是只測「不崩潰」）？改動是否讓既有 golden／決定性測試變動？若變動，理由是否成立？
4. **無頭證據**：V-H log 是否能重現？命令是否寫在 commit 或本文件？
5. **視覺**：截圖中的尺寸／位置是否對得上預製件數值（pivot、單位、偏移）？
6. **取捨登記**：所有 PORT CHOICE 是否已寫進 §6，且集中在單一處便於替換？
7. **回歸**：`ctest` 全綠；`validate_data.py` 綠；沒有 `--no-verify`。
8. **文件**：§2 狀態表與對應階段表是否更新？

---

## 8. 維護規則（Claude 與 Codex）

- 本文件放在 `docs/MASTER_PLAN.md`，任何階段狀態變更都在**同一個 commit** 內更新。
- Claude 負責：實作、更新 §2／§3 狀態、登記 §6 取捨、附證據。
- Codex 負責：依 §7 審查，把結論寫進 §9（日期、審查對象 commit、逐條結果、要求修正的項目）；發現文件與程式不符時直接修文件並註明。
- 雙方都不得刪除對方的紀錄；有分歧時在 §9 該筆下方以「回覆」段落討論，最終由使用者裁決。
- 版本號：每次 Codex 審查通過後小版號 +1；新增階段或改變順序時中版號 +1。

---

## 9. 審查紀錄

| 日期 | 審查者 | 對象 | 結果 | 摘要 |
|---|---|---|---|---|
| 2026-09-06 | （待 Codex） | 本文件 v1.0 + commits 12c3750…e2c5030 | 待審 | 首次審查：確認範圍、階段切分與驗證方式是否足夠 |
