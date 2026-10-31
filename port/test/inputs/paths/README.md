# Round 10 "paths": short key files for code paths no bot route reaches

Hand-made key files (never TAS-derived) for the code paths round 9 left unchecked
(port-plan/DIVERGENCES.md D-48, D-49, D-52, P-27), each run on the GBA and on the plain PC
through the lag replay, every traced field on every tick and every frame of its window:

    make -f port/test/Makefile path-route PATH_NAME=pe14|pc7b|ch2g|ch8r|i14b|ps14|pd14
    make -f port/check-all.mk CHECK_LONG=1     # rows path-pe14, path-pc7b, path-ch2g, path-ch8r, path-i14b, path-ps14, path-pd14

Integration 11 ("nullunit2") added `ps14` and `pd14`: the NULL-unit stragglers of round 10's
audits (port-plan/pending/folded/audit10-nullunit.md N2 and the paths review's BATTLE_HandleItemDrop).

`path-route` makes the start save with `chain-save` when `port/test/saves/` lacks it, checks
it against the key file's `# Start:` line (FNV-1a of the first 32 KiB), and runs `route` with
`GATE0_SHOT_TICKS=PATH_SHOTS_<name>` (port/test/Makefile).

| Key file | Start (how chain-save makes it) | Ticks | Frames compared | What it reaches |
|---|---|---|---|---|
| `pe14.keys` | `pe14_start`: edited start (below) from `e14_start` | 6,254 | 2,000-6,253 | Eirika's Ch14, turn 1: Moulder as a Bishop (class 0x2B) on the stat screen, his item menu and staff list, the Restore, Barrier, Silence, Warp and Rescue target selections; Natasha as a Bishop (0x2C): Sleep, Hammerne and (after a move next to the door at (22,17)) Unlock selections, the item menu over her Lightning (the item panel's weapon stats), her Vulnerary; Colm as a Dancer: Dance, the ring, the special dance's selection; the unit list; Dozla's Devil Axe backfire on the cracked wall at (20,13), which kills him (P-27, the death record's killer 0x71) |
| `pc7b.keys` | `pc7b_start`: edited start from `ch7_start` | 3,113 | 2,000-3,112 | Chapter 7, turn 1: Neimi rides the ballista at (2,10), Attack, the ballista, a target, B: the back-out through `UnitActionMenu_Attack(NULL, NULL)` (D-49) into `StartUnitBallistaSelect`, then the attack |
| `ch2g.keys` | `ch2g_start`: `monkey/ch2_start_s1.keys` at tick 7,340, from `ch2_start` | 3,500 | 1,700-3,499 | the suspend written at Chapter 2's turn-1 green phase start, resumed: Ross (green) heals on his fort while `gBattleTarget` was not filled since boot (green actor on the right, NULL target on the left) |
| `ch8r.keys` | `ch8r_start`: `route_ch8.keys` at tick 72,800, from `ch8_start` | 3,500 | 1,700-3,499 | the suspend written at Chapter 8's turn-14 enemy phase start, resumed: the boss heals on his throne with `gBattleTarget` not filled since boot: a red actor with a non-red target is placed left, so the NULL target is on the right (D-48's right-side reads) |
| `i14b.keys` | `i14_start` (chapter save) | 2,249 | 1,700-2,199 | Ephraim's Ch14, turn 1: R on the two enemy Bishops at (21,11) and (7,11) (their stat screens: a Bishop actor, `gBattleTarget`'s `pClassData` NULL) |
| `ps14.keys` | `ps14_start`: edited start from `e14_start` | 3,569 | 2,000-3,568 | Eirika's Ch14, turn 1: Ewan as a Summoner summons his phantom (1 HP, an Iron Axe) onto (17,21); the phantom attacks the Soldier on the stairs at (17,20), whose counter kills it, so `TryMakeCantoUnit` reads the removed phantom's attributes through NULL (tick 2,860) |
| `pd14.keys` | `pd14_start`: edited start from `e14_start` | 3,508 | 2,000-3,507 | Eirika's Ch14, turn 1: Dozla (1 HP, a Devil Axe, `US_DROP_ITEM`) attacks the cracked wall at (20,13); the backfire kills him, so `BATTLE_HandleItemDrop` reads `UNIT_FACTION(unitB)` through `GetUnit(0)` = NULL (tick 2,847), then P-27's record; his death quote |

**Edited starts** (test coverage only, not story play): `chain-save`'s `edit:B-T` kind plays
`NAME_prep.keys` (a route's way to its first free cursor, then one unit waits, so the game
writes a suspend save) and `port/test/oracle/sram_boost.lua` applies `NAME_prep.edit` at the
end of tick B (`BOOST_STATS=0 BOOST_EDIT_FILE=...`); the SRAM is taken at tick T. Both sides
then boot from the same save with Resume Chapter, so the run is a fair GBA-against-PC check.
The edits (see each `.edit`): `pe14_prep.edit` makes Moulder and Natasha Bishops with staves
and staff rank S, poisons Gerik (the Restore target), moves Natasha and Saleh, makes Colm a
Dancer with a ring, puts Dozla (1 HP, luck 0, a Devil Axe) next to the cracked wall, gives the
green Queen 10 HP, and advances the RN state 6 steps so Dozla's first strike is a backfire;
`pc7b_prep.edit` puts Neimi (60 HP, defence 25) on the free ballista. `ps14_prep.edit` makes
Ewan a Summoner (class 0x31; no chapter save has one: Knoll is dead from e16/i16 on) at
(18,21) and advances the RN state 1 step so the Soldier's counter hits the phantom;
`pd14_prep.edit` is pe14's Dozla (and its 6 RN steps) with `US_DROP_ITEM` added to his state
word (`addstate=1000`; no blue unit has it in story play). `ps14_prep.keys` and
`pd14_prep.keys` press what `pe14_prep.keys` presses.

**Making the key files**: each `NAME.keys` is `python3 port/test/tools/keyscript.py
port/test/inputs/paths/NAME.ks port/test/inputs/paths/NAME.keys` from the repository root
(the `.ks` script: `prefix FILE N` takes a route's first N entries; `tap KEY GAP`, `rep N KEY`,
`wait N`; `header` lines go into the key file's header).

Results and the GBA bytes measured: port-plan/pending/folded/r10-paths.md (pe14 to i14b) and
port-plan/pending/folded/r10-nullunit2.md (ps14, pd14).
