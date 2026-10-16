Ground-truth crops from the real Soul Knight 1.7.10 recorded on an LDPlayer emulator (1280x720 frames; the
capture workflow is described in the P3.0c notes of docs/MASTER_PLAN.md). Frame labels are the extracted
frame numbers of the capture (fire1 = the knight firing weapon_000, fight1 = ice 1-1 fight, knight walking):
  z_knight_aim.png  30 crops (6 x 5, 240 px) of the Knight in combat: the gun is held at belly height and
                    ROTATES CONTINUOUSLY toward the aim (straight down 2196, down-left 0360, vertical up
                    1332 / 1980, horizontal left 1764 ...); the body is MIRRORED by the aim side (faces left
                    0360 / 1332 / 1764, right 0576); a GREEN ellipse ring sits under the feet and a dark
                    shadow ellipse inside it; an enemy has a RED ring (0468); the muzzle flash is at the gun
                    tip (0792, 1548, 2520). A straight-down aim keeps the last side (2088 / 2196 face left).
  z_run.png         fight1 f0036..f0066 (native scale, 240 px windows around the ring): the Knight walking left -- 'run' clip bob, body mirrored to the left,
                    gun horizontal in the facing direction, green ring + shadow under the feet.
  z_lock_ring.png   fight1 f0468 (x3): the locked enemy's RED ring next to the Knight's GREEN ring (the red
                    "3" glyphs are damage numbers, out of scope).
