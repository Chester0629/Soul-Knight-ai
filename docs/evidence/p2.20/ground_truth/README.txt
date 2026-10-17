P2.20 ground truth (room details): crops of frames of the real Soul Knight 1.7.10 running on an LDPlayer emulator,
recorded and extracted as described in docs/evidence/p2.19/ground_truth/README.txt (ice biome 1-1, 1280x720, the
camera scale is 48 px per world unit = 3 px per source pixel = the port's SK_ZOOM=1.5).

  gt_walls_floor_open_door.png  frame f0000 of the run1 recording, region (232,90)-(1048,330): the north wall row of the
                                start room (light ice blocks with dark-crystal blocks in roughly 3 of 10 positions =
                                wall_list[0] / wall_list[1]), the six floor tiles, the OPEN door (door03 planks, no bar)
                                and the translucent shadow band under the wall.
  gt_closed_door.png            frame f2400, region (440,420)-(760,610): a CLOSED door (door02 base, door01 spike bar,
                                shadow) during combat.
  gt_open_door_south.png        frame f2250, region (440,250)-(800,380): the same kind of door OPEN right after the hero
                                walked in (door03).

No spike-trap or chest footage has been recorded yet: the sting01 timing (docs/MASTER_PLAN.md P2.20) and the
four-layer chest are FAITHFUL to the prefab YAML + decompile (DISASM of ObjectSting) but UNVERIFIED against the real
game's picture.
