#!/usr/bin/env python3
"""Extract the floor-1 room SIZE table from the reverse-engineering dump.

Phase 2 (variable room sizes) needs each dungeon slot to take a real floor-1
room's width/height instead of a forced 15x15. The authoritative per-design-room
sizes live in the RE dump (room_layouts.csv, kept local under ../_reverse/, not
committed). This emits just the floor-1 (r1_*) rooms' sizes as a small committed
data table the game loads at runtime.

NOTE: this is the SIZE source only. The room INTERIOR stays RoomGen-procedural in
Phase 2; loading the actual design-room prefab/layout is Phase 3. All 108 floor-1
rooms are room_type 1; sizes are 15x15 / 15x21 / 21x15 / 21x21 (no 25 on floor 1).

Source : <repo>/../_reverse/analysis/data_tables/room_layouts.csv  (RE dump, local)
Output : Resources/data/room_layouts.json  ->  [{id, w, h}, ...]  (floor-1 only)
Run    : python tools/extract_room_layouts.py
"""
import csv
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "..", "_reverse", "analysis", "data_tables", "room_layouts.csv")
OUT = os.path.join(REPO, "Resources", "data", "room_layouts.json")


def main():
    if not os.path.isfile(SRC):
        sys.exit(f"RE dump not found: {SRC}\n(kept local; clone the _reverse tree)")
    rooms = []
    with open(SRC, encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            rid = row["room"]
            if not rid.startswith("r1_"):  # floor-1 only
                continue
            rooms.append(
                {
                    "id": rid,
                    "w": int(row["room_width"]),
                    "h": int(row["room_height"]),
                    "type": int(row["room_type"]),
                }
            )
    rooms.sort(key=lambda r: r["id"])  # deterministic pool order
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(rooms, f, indent=1)
        f.write("\n")
    dist = {}
    for r in rooms:
        k = f'{r["w"]}x{r["h"]}'
        dist[k] = dist.get(k, 0) + 1
    print(f"wrote {len(rooms)} floor-1 rooms -> {OUT}")
    print("size distribution:", dist)


if __name__ == "__main__":
    main()
