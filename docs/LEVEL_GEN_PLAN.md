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
| 可變房尺寸來源 | **`room_layouts.csv`** | 原版每個設計房有自己的 W×H;由此表驅動 (P2),非寫死 15 |
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
- **P2 可變房尺寸**(前置 P1):RoomGen 從 `room_layouts.csv` 取每房 W×H,FloorBlock offset
  改用真尺寸(已是 `(41−W)/2` 通式,走廊 band 已用 `cy/cx` 通式 → 幾何已 size-invariant)。
- **P3 設計房 prefab 載入 + RGRoomX**(前置 P1+P2):載 108 房 LoadRoom JSON(版面/collider
  優先),與程序房 (RGRoomX random_room) 混排,接 MapManager。
- **P4 tileset(正交)**(前置 P3):用切出的正交圖集瓦片替換 floor_tile/wall_tile 佔位圖。

## 6. 跨階段回歸守則

1. **golden hash 不變**:`RoomGenGoldenTest` 鎖 RoomGen 生成;動到生成管線(stage 順序、RNG
   draw、band/threshold)必先看它紅不紅。
2. **動共用路徑必回歸驗舊功能**:`pitch / origin / CellToWorld(Step) / FloorBlock` 是 P2-P4
   會反覆碰的共用幾何;每次動完跑全套 + in-game,確認舊房/走廊/鎖門沒退化。
3. **可達性以真 BFS,非端點開口**:任何「房間連通」驗收一律 flood-fill 走遍 walkable 集,
   禁用「兩端各有開口」(會漏中間斷格)。雙軸 + 多 seed,不外推。

## 7. 已知債（明標,不假裝沒有）

- **多角色身份 / buff**(B5):目前固定 c01;角色選擇 + buff 系統未接。
- **跨層技能冷卻延續**:每層從 template 重建(starts ready),冷卻不跨層帶。
- **c01 完整二手**(B1b):技能 brain 已接進 Player,但完整二手回收(full second-hand)未做。
- **多樓層萃取未做**:只重現 floor-1;章節環境、boss 樓層 %5 週期、16 層 cap 都未做。
