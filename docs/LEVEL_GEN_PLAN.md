# 關卡生成計畫 — 決策記錄 (LEVEL_GEN_PLAN)

**狀態:Phase 1 完成並驗收 (2026-06)。** 本文記錄不可從 code 推回的「決定 + 切線 +
caveat」,供跨 session 接續。實作細節看 code/測試;這裡只留「為什麼這樣」。

關聯:[[RUN_LOOP_PLAN.md]](場景轉場 run loop,另一條線)、[[ROADMAP.md]](全局里程碑)。

---

## 1. 目標與切線

- **北極星:路線 (c) 混合** = 重現原版 **floor-1 的本貌**:以 **108 個設計房 (LoadRoom
  JSON) 為主**,程序生成房 (RGRoomX random_room) 為輔。不是「純程序地牢」,也不是「只擺設計房」。
- **B1 切線採 A**:先把關卡生成這條線 **Phase 1→2→3→4 全做完**,再回去做 B1(戰鬥內容
  SOP)。理由:地圖幾何是 boss/敵人/寶箱擺放的地基,先把地基釘死,B1 才不會反覆動地圖。
- **範圍先 floor-1**:只做第一層的完整重現;多樓層差異 (章節環境、boss 樓層 %5) 留後面。
  單一垂直切片先打通,方法論驗證過再規模化。

## 2. 技術岔路定案

| 岔路 | 定案 | 一句理由 |
|---|---|---|
| 房間間距 | **41-pitch = 41×32 = 1312px** | 每房佔一個 `MAP_SIZE=41` 邏輯塊,塊邊對塊邊鋪,走廊在塊間 margin 接縫 |
| 設計房選用 + 尺寸 | **`room_layouts.json`**(108 房 `{id,w,h,type}`) | **政策2「真選房」**:MapManager 按 slot type 選 floor-1 設計房【身份 `r1_N`】+ 用其 W×H 驅動建房 (P2),非寫死 15 |
| prefab sprite | **版面/collider 優先;PPtr→sprite「v2」當補皮** | 先把「房長怎樣、能不能走」做對 (layout JSON + collider),貼圖 (PPtr 解到實際 sprite) 後補,不卡進度 |
| tileset | **切已萃取圖集 (正交)** | 地板/牆用從圖集切出的正交瓦片 (P4),不自繪 |
| 範圍 | **floor-1** | 同上,先一層 |

## 3. Phase 1 幾何（不可恢復的重建決定）

「A 幾何」——floor 組裝是 **owner-side**,decomp 沒有 byte 級 body,故以下是 **依據
RGAisle/RoomGen 可恢復數學 + 最有據推斷** 定的設計決定,不是 recovered fact:

- **pitch = 41×32 = 1312**;房 (15×15) **居中**於 41 塊,`offset = (41−W)/2`(15→13)。
- **走廊** = 房緣外的 **junction/門格 (塊 col 28)** + **RGAisle floor (`offset−1` = 12 格,
  col 29..40)**,5 格寬 (`RGAisle::kAisleShortExtent`),對齊 RoomGen::CreateAisle 的門 band
  (`cy..cy+4`)。抵塊邊 (col 40)。
- **seam 40↔41**:相鄰塊的 col 40 與鄰塊 col 0 正交相接(pitch 1312 = 41 格,塊邊貼塊邊)。
- 「門格(1)+floor(12)=13」= 整個 offset;正確性押在「中間無 1 格斷」。

> **★ CAVEAT(務必延續這個誠實標記):** 41-pitch 這個數值的 **owner-side 組裝來源是
> thunk-阻斷**(SetRGRandomSeed 截斷、DAT 缺),所以 41/1312 是 **最有據重建**,**不是
> recovered fact**。若日後抓到真機 floor dump 與此不符,以真機為準、重定 pitch/offset。

實作:`world/FloorBlock`(純邏輯單一來源:`CorridorStrip`/`Build`/`ConnectedFloorCells`/
`DoorSealCells`)+ `GameScene` 消費同一 `CorridorStrip`,故「測到的可走空間 == 遊戲內碰撞」。

## 4. 三條接縫 + seal/unseal 對稱（各自釘成測試）

| 不變式 | 釘成的測試 | 斷言什麼 |
|---|---|---|
| 可達性(非端點開口) | `FloorConnectivityTest` | **真 BFS/flood-fill** 房A內部→房B內部,5 seed × **雙軸(竖直+橫向)** 每對相鄰房,+ **mutation guard**(junction-28 / seam-40↔41 斷 1 格 → BFS 紅,弱「兩端開口」仍綠) |
| seam① 門對齊 | `FloorBlockTest.Seam1` | code-11 門/aisle ↔ 走廊 junction,中線無斷 |
| seam② 走廊中性 | `CorridorNeutralityTest` + `Room::ContainsPoint` | 走廊座標在任何房 AABB 外 → `playerRoomId=-1` → 鎖門不誤觸(顯式斷言,非 latent) |
| seam③ 抵塊邊 | `FloorBlockTest.Seam3` | 走廊走到 col 40,鄰塊才接得上 |
| 出生不卡死角 | `FloorBlockTest.ConnectedFloorCells*` | 出生點取「能從門口 flood 到的 floor cell」,非 `FloorList[mid]`(可能困在障礙死角) |
| seal/unseal 對稱 | `DoorSealTest`(鎖定阻擋 / 清房重開,**整條 5 格 band 成對**) | 鎖房封全 band(非只 2 個 code-11),清房整組 `DoorSealCells` 一起解除(`m_LockedRoom` 整組 toggle,無 per-cell 重開路徑) |

(物理層 = radius-16 圓 vs AABB,跑真碰撞;其餘為 cell-grid。)

## 5. Phase 狀態

- **P1 走廊幾何 — DONE**(本文 §3/§4;1961 測試綠、golden 不變)。
- **P2 真選房(政策2)— 實作完成、待驗收**(前置 P1):**不是只挑尺寸**——MapManager 按
  slot type 選一個 floor-1 設計房【身份 `r1_N`】(`SelectDesignRooms`,跑在獨立 RNG 流
  `seed + 104729`,`usedRoom` 去重避免近期重複),把選到房的 **W×H 灌進** FloorBlock offset
  (`(41−W)/2` 通式,走廊 band `cy/cx` 通式 → 幾何 size-invariant)。**內裝仍 RoomGen 程序生**
  (prefab 內裝是 P3 的事);`roomId` 即 P3 的 load handle。special/badass slot 走 type-1
  fallback(**見 §7 顯式債**)。獨立 seed 流 ⇒ 主 random-walk 與下游 golden/combat 決定性
  byte-identical(`DesignSelectionDoesNotPerturbTheWalk`)。
- **P3 設計房內裝載入(b′)+ RGRoomX 生命週期(a)— 實作完成、待驗收**(前置 P1+P2):
  - **3.1 障礙層(決策 #2=b′)**:RoomGen shell(perimeter/floor/門格)**留著**(對設計房就是
    忠實——原版也 runtime 生 shell);設計房 slot 把程序障礙(`CreateObstacle` Phase A/B)
    **關掉**(`Options::proceduralObstacles=false`,**預設 true → golden byte-identical**),改
    stamp prefab 的 obj_index 障礙:**牆(0)→ grid**(連通屏障+collider,`designSolidCells`);
    **box(1–4)+ brazier(11)→ 碰撞 overlay**(擋身體、但**不在連通 grid** ⇒ box 不破壞門可達);
    **trap(5)/pad(6,7)→ 不擋**(deferred)。資料管線:`tools/extract_design_rooms.py` →
    `Resources/data/design_rooms.json`(108 房 / 4475 障礙)。座標 = 房中心固定 (20,20)、
    `grid = pos − 20 + (dim−1)/2`(**★CAVEAT,見 §7**)。
  - **3.2 RGRoomX 生命週期(決策 #3=a)**:`RGRoomX` 接成 per-room **單一真相源**(Uncleared/
    Active/Cleared + `door_open` + `ClearRoom` reward gate `room_type==1`)。GameScene 的鎖門/
    DoorSeal **改成 `RGRoomX.door_open` 的 grid 呈現**(`m_LockedRoom` 已移除,無第二可寫真相
    源)。初始態 = 門開、進房(有敵)`StartRoom` 才鎖、清空 `ClearRoom` 再開。box 破壞接線:
    sim 子彈擊牆 → `WorldCollision::DamageObstacle` → `RGBox.Hit` → 破壞後移除 collider(破壞
    前擋、破壞後通);此路徑在 `NullWorldCollision`(combat golden)下永不觸發 ⇒ combat hash 不變。
- **P4 tileset(正交)**(前置 P3):用切出的正交圖集瓦片替換 floor_tile/wall_tile 佔位圖。

## 6. 跨階段回歸守則

1. **golden hash 不變**:`RoomGenGoldenTest` 鎖 RoomGen 生成;動到生成管線(stage 順序、RNG
   draw、band/threshold)必先看它紅不紅。
2. **動共用路徑必回歸驗舊功能**:`pitch / origin / CellToWorld(Step) / FloorBlock` 是 P2-P4
   會反覆碰的共用幾何;每次動完跑全套 + in-game,確認舊房/走廊/鎖門沒退化。
3. **可達性以真 BFS,非端點開口**:任何「房間連通」驗收一律 flood-fill 走遍 walkable 集,
   禁用「兩端各有開口」(會漏中間斷格)。雙軸 + 多 seed,不外推。

## 7. 已知債（明標,不假裝沒有）

- **★ P4 tileset — 純視覺債(`tools/extract_tiles.py` + `GameScene` 渲染)**:
  - **floor/wall biome = biome 6 冰雪(已釘死,HIGH — P4-RE)**:floor-1 = world 1
    (`AB:level-1` bundle)= **biome 6**(`floor601`/`wall601`,淺藍雪地)。證據:biome 集**按 world
    bundle 分包**(`AB:level-1` = biome 5+6、`AB:level-3` = biome 4+7);已萃的 `MapManager.json`
    **`level='3-1'`(chapter 3)**,其 `floor_list` 整組 pid 解到 `AB:level-3` 的 floor701-706 =
    **biome 7 棕色 —— 那是 chapter-3、不是 floor-1**。**floor-1-ice 的 MapManager 配置(level `1-x`)
    未在已萃 dump**,按 world-1 bundle 歸屬 + 視覺雪地推定 biome 6(故先前「肉眼/med」措辭升級為釘死)。
    *殘留債(LOW)*:world-1 內 **1-1 vs 1-2 對 biome 5/6** 未釘死(`1-x` MapManager 未萃);biome 6 是
    world-1 唯一雪地、為專案要的冰雪,故選 6 正確,待 `1-x` dump / 真機 capture 終確認。
  - **floor 6 變體只用 1 個**:port grid floor 全 code 0、無變體碼,故只用 f601 一張(原版 f601–606)。
  - **wall 渲染 cell-stretch**:grid 牆(perimeter/corridor/設計牆)用既有 cell-fill(16×24→32×32 微拉),
    只有**障礙** sprite 走 bottom-anchored 原生比例。In-game 牆觀感 OK,列為微債。
  - **obj_index 0 設計牆 = 渲染成冰牆**(grid wall601),**非 `wall703` skin**(視覺一致 + 免雙描);
    碰撞不變(仍 grid solid)。
  - **obj_index 5/7 sprite = HIGH 推斷**:RGObjectSkin 末步 sprite-copy owner-truncated 未 byte 恢復
    (索引 `obstacle_list[obj_index]` 已二進位 pinned);**node 名 `skin_trap`@5 / `skin_speed_down`@7
    是 cosmetic**,渲染體 = obstacle_list(5=speed_up、7=sting01)。日後實機 capture 若 5/7 不符 →
    re-baseline(歸入下面「實機驗證債」)。**碰撞不受影響**(5/7 P3 皆非 blocker)。
  - **obj_index 8(obj11_07)**:sprite 確定,**floor-1 視覺意義 MED**。
  - **裝飾(5/6/7/8)實機截圖待補**:start room 無裝飾、naive autowalk 進不了裝飾房;已用**離線版面預覽**
    (r1_100 的 32 速度墊成環)+ 同一 `addObjSprite` 管線(in-game box 已證)確認 sprite+擺位正確。
    歸入「實機驗證債」一起補。
- **★ special/badass 房形狀缺失(P2 政策2 fallback — 顯式債,非 latent)**:floor-1 設計池
  只有 type-1 房(`room_layouts.json` 全 `type:1`),所以 type-2(special)/type-3(badass)
  slot 在 `MapManager::SelectDesignRooms` 走 **type-1 fallback** → 拿到的是 **type-1 房形狀**。
  special 房的獨特形狀**尚未重建**,需專門的 **special-room RE** pass,deferred。
  *(floor-0 實證:slot5 `type2 → r1_81`、slot4 `type3 → r1_17`,兩者都是 type-1 房。)*
  code 接點:`MapManager.cpp` 的 `TODO[debt: special-room content]`(與其上方的演算法保真
  `TODO[verify]` 是**兩個不同的債**)。
- **★ loot overlay 忠實度 UNVERIFIED**:special/badass slot 的 loot overlay(`wallLevel` +
  chest tier)維持**港版現行處理**,**忠實度未驗證**——**不標成已驗證忠實**;待 special-room
  RE 確認 floor-1 special 房是否架構獨立後才能判定。
- **★「忠實 floor-1」帶星號**:§1 北極星的「重現 floor-1 本貌」目前 = **normal 房忠實** /
  **special·badass 房 = type-1 形狀 + 未驗證 loot**。對 special 房尚未達成,須隨上面兩條一起收。
- **★ P3 實機 clear-flow 驗證債(顯式,deferred — 機制已證、缺實機操作)**:per-room「進房鎖
  → 殺敵 → `ClearRoom` → 開門 → reward gate」的**實機操作流**尚未在遊戲內驅成 —— naive
  `SK_AUTOWALK`(直線導航)在密集設計 box 場耗能/卡住,到不了敵房,故 `StartRoom`/`ClearRoom`
  未在實機觸發。**機制已 `RGRoomXTest` 單測**(StartRoom→Active→門關、ClearRoom→Cleared→開門
  + room_type==1 reward gate)+ **單一真相源 code 證**(`m_LockedRoom` 已移除、門唯一寫者 RGRoomX);
  in-game 已驗:box 擋/破壞(`SK_AUTOFIRE` 7-box 射穿)、floor-clear 轉場(`SK_FORCE_CLEAR`
  Floor 0→1→2→3)。**併入同一條:親手實玩走一次(房→走廊→鄰房 + 進戰鬥房感受鎖門)** —— 與上
  是同一件事(機制已證、缺真人/實機操作確認),攢著之後一次補。**不擋 P4,但進「完整可玩」收口前必補。**
- **★ P3 座標映射 = ★CAVEAT(不可 byte 恢復)**:prefab 房中心固定 (20,20)、`grid = pos − 20
  + (dim−1)/2`,四尺寸 + 門幾何自洽,但 (20,20) 原點與 offset 約定是**推斷非 recovered fact**
  (同 41-pitch CAVEAT)。實作但**靠實玩驗**;真機 dump 不符以真機為準。code:`extract_design_rooms.py`
  + `GameScene.cpp` 設計障礙註解。
- **★ P3 obj_index 8 + brazier 11 碰撞 UNVERIFIED**:`skin_obj`(8,3 例,金色置中,疑 loot)
  = **保守無 collider 佔位**;`skin_brazier`(11)= **保守 blocker(不可破)**。皆需 special-room
  RE 確認;code:`GameScene.cpp` 障礙 overlay 註解。
- **★ P3 box HP = 港版預設(UNVERIFIED)**:`kBoxHp=1`(一發即破);原版 HP 走 owner spawn,
  不可恢復。展示「破壞前擋破壞後通」契約用;真值恢復後再調。code:`GameScene.cpp:kBoxHp`。
- **★ P3 trap/pad 效果 deferred**:trap(5)/speed pad(6,7)目前**不擋、無效果**(只佔位),
  地板陷阱/加減速效果待後續 + 補皮 pass。
- **★ P3 RGDoor 門實體未搬(#2c deferred)**:prefab 的 4 個 RGDoor 5-寬 collider/視覺未接;
  門的**格子碰撞**港版已 grid 對齊(code-11 + DoorSeal,現經 RGRoomX 表達),門實體留**補皮 pass**。
- **★ P3 b_point 出生錨未用**:prefab 每房的 `b_point`(boss/spawn 錨)未接;出生續用
  `ConnectedFloorCells` 中位格。忠實 follow-up。
- **多角色身份 / buff**(B5):目前固定 c01;角色選擇 + buff 系統未接。
- **跨層技能冷卻延續**:每層從 template 重建(starts ready),冷卻不跨層帶。
- **c01 完整二手**(B1b):技能 brain 已接進 Player,但完整二手回收(full second-hand)未做。
- **多樓層萃取未做**:只重現 floor-1;章節環境、boss 樓層 %5 週期、16 層 cap 都未做。
