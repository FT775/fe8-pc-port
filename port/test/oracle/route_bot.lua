-- Route generator: plays fireemblem8.gba from power-on in mGBA, reading game
-- RAM to decide each tick's keys, and writes the keys it pressed as a Gate 0
-- key file. The key file is the product; this script is only how it was made.
-- Replaying the key file open-loop (route_check.lua, or gate0_capture.lua)
-- gives the same run, because mGBA is deterministic from reset.
--
--   ROUTE_OUT=port/test/inputs/x.keys ROUTE_STOP=pp:2 \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_bot.lua 3000000
--
-- Environment:
--   ROUTE_OUT    key file to write (required)
--   ROUTE_STOP   when to stop: "pp:N" (first free-cursor player phase of chapter N),
--                "save:N" (300 ticks after the game save that ends chapter N),
--                "saveto:K" (300 ticks after the first game save written with
--                chapterIndex K), "tick:T"
--   ROUTE_DIFF   difficulty menu choice: 0 Easy (tutorials), 1 Normal (default), 2 Difficult
--   ROUTE_LIB    path of route_lib.lua (default port/test/oracle/route_lib.lua)
--   ROUTE_VERBOSE=1  log decisions to stdout (2: also each change of input handler)
--   ROUTE_LORD_TH, ROUTE_LORD_DD  tactics v2: the lord's weights for threat and
--                distance (default 5 and 3; README-routes.md, "Tactics v2")
--   ROUTE_MAX_TURN  stop (writing the keys so far) when a chapter's turn
--                passes this (default 60), so a stalled try leaves a file
--   GATE0_SRAM_IN  start from this chapter save (the runner loads it; the bot
--                takes the title menu to Continue like any other menu, and
--                names the save in the key file's "# Start:" header line; a
--                save made with the test cheat, or after one, whose .sav.json
--                says "cheat", gets a "# Start save made with the test cheat"
--                line too)
--   ROUTE_TICK   "frame" (default) or "capture" (with MGBA_RUN_BP=$(GBA_BP)
--                of port/test/Makefile): the tick rule, route_lib.lua "Tick
--                rules". The routes from route_e11.keys on use "capture".
--   ROUTE_SPLIT  "ephraim": at the route split after Chapter 8 take the
--                menu's second entry (Ephraim's route). Unset: the bot's A
--                presses take the first entry (Eirika), as every older route.
--   ROUTE_CHEAT  "durability", "funds", "convoy" or several joined with "+":
--                the test-only cheats (port/test/README.md, "Test cheats";
--                route_lib.lua, test_cheat.lua): every item's uses back to
--                full, the party's gold at 999,999, a rolling convoy, at
--                each tick sample. Needs ROUTE_TICK=capture. Refused in
--                Eirika's Chapter 20 and final chapter, which are played
--                fair: the run stops at its first tick sample with
--                chapterIndex 0x14 to 0x16 before the game is complete
--                (test_cheat.lua), whatever ROUTE_E7 is, and at once with
--                ROUTE_E7=1. The key file is labelled cheat-assisted (its
--                "# Generated" line names ROUTE_CHEAT, and a
--                "# Cheat-assisted" line follows; from a boosted start, whose
--                .sav.json says "boosted", a "# Boosted, cheat-assisted"
--                line before it) and is replayed with GATE0_CHEAT=<the same>
--                on both sides (README-routes.md, "Cheat-assisted routes").
--   ROUTE_GALLERY=1  the item gallery (coverage 12, L.GAL below; port/test/inputs/gallery/README.md):
--                from gal_start every item of the rolling convoy's list taken from the supply and
--                used once (needs ROUTE_CHEAT=durability+funds+convoy); ROUTE_STOP=gallery:N
--   ROUTE_PG_REAL=1  the post-game with a real clear save's army: shopping before
--                the dungeon, a bigger refit (L.PR; README-routes.md, "The
--                post-game with the real army")
--
-- Tick semantics: a tick is sampled when a frame ends with
-- gBmSt.main_loop_ended == 1 (or, with ROUTE_TICK=capture, exactly as
-- gate0_capture.lua samples it), and the mask for tick t+1 is set right after
-- tick t is sampled. So the decision for tick t+1 sees the state left by
-- OnMain t.

local L = dofile(os.getenv("ROUTE_LIB") or "port/test/oracle/route_lib.lua")
local K = L.KEY
local outPath = assert(os.getenv("ROUTE_OUT"), "ROUTE_OUT is required")
local stopSpec = os.getenv("ROUTE_STOP") or "pp:1"
local wantDiff = tonumber(os.getenv("ROUTE_DIFF") or "1")
local verbose = os.getenv("ROUTE_VERBOSE") == "1" or os.getenv("ROUTE_VERBOSE") == "2"
ROUTE_DEBUG = os.getenv("ROUTE_DEBUG") == "1"   -- (log only: why a unit moves where it does)
local traceStates = os.getenv("ROUTE_VERBOSE") == "2"   -- also log each change of the input handler
local lastTraced = nil
local stopKind, stopArg = stopSpec:match("^(%a+):(%d+)$")
stopArg = tonumber(stopArg)
local tickRule = L.tickRule()   -- ROUTE_TICK: "frame" (default) or "capture" (route_lib.lua, "Tick rules")
local splitSpec = os.getenv("ROUTE_SPLIT") or ""
assert(splitSpec == "" or splitSpec == "ephraim", "ROUTE_SPLIT must be unset or ephraim, not " .. splitSpec)

local masks = { [1] = 0 }   -- masks[t + 1] is the mask for tick t
local queue = {}
local tick, frame = 0, 0
local milestones = L.newMilestones()
local mlog = {}

local function log(fmt, ...)
  if verbose then print(string.format("[%d] " .. fmt, tick, ...)); io.stdout:flush() end
end

local function push(mask, hold, gap)
  for _ = 1, hold do queue[#queue + 1] = mask end
  for _ = 1, gap do queue[#queue + 1] = 0 end
end
local function wait(n) push(0, 0, n or 1) end

---------------------------------------------------------------------------
-- Combat estimates (simplified: every hit lands, no crits; v1 and v2 also
-- leave out terrain, effectiveness and weapon rank checks, v3 has them).

V3 = false     -- tactics v3 (Chapter 8 on), set per decision; see "Tactics versions"
-- Two chains of tactics share this file (README-routes.md, "One bot, two chains"):
-- Ephraim's route after the split (port-bots10i: V5, V7 to V10, the tacticsV5 to
-- tacticsV10 below, chapterIndex 0x17 to 0x23 and 0x3E) and the chapters from
-- Eirika's Chapter 14 on, the boosted starts and the post-game (port-bots10e: the
-- rules marked "v5" and "v6" there are E5 and E6 here, tacticsE5 and tacticsE6,
-- never on Ephraim's chapters). Neither chain's rules run in the other's chapters,
-- so every route file of both comes out byte for byte.
V5 = false     -- tactics v5 (Ephraim's route, Chapter 9 on), set per decision
V7 = false     -- tactics v7 (Ephraim's route, Chapter 14 on), set per decision
V8 = false     -- tactics v8 (Ephraim's route, Chapter 15 on), set per decision
V9 = false     -- tactics v9 (Ephraim's route, Chapter 17 on), set per decision
V10 = false    -- tactics v10 (Ephraim's route, Chapter 20 on), set per decision
EXACT = false  -- v9 siege: no pessimism margins in sim and threatAt (see "siege" in think)
SIEGE = false  -- v9: every enemy left is a boss that never leaves its tile (set per decision in think)
E6 = false     -- Eirika's tactics v6 (ROUTE_SEARCH=1, index 15 on), set per decision
-- E6, chapter index 20 on, set per decision: the sacred weapons are effective x2, the
-- others x3 (ComputeBattleUnitAttack, src/bmbattle.c); before, every one counted x3
SACRED2 = false
-- Audhulma, Vidofnir, Garm, Nidhogg, Excalibur, Ivaldi, Sieglinde, Siegmund (both chains)
SACRED = { [0x91] = true, [0x8E] = true, [0x93] = true, [0x94] = true, [0x3E] = true, [0x87] = true, [0x85] = true, [0x92] = true }
-- E7 (coverage 11, port-bots11e; README-routes.md "Tactics E7"): Eirika's Chapter 20 and the
-- final chapter with the real army (not boosted). Only with ROUTE_E7=1 and only in her chapter
-- indices 0x14 to 0x16 (the world map before Chapter 20 has index 0x14 too), so every route
-- made without it (all the older ones) stays as it was. L.E7.on is set per decision (think,
-- trialValue, searchBranch). (In L: the main chunk is at Lua's 200-local limit.)
L.E7 = { env = os.getenv("ROUTE_E7") == "1", on = false }
function L.E7.chapter(ch) return L.E7.env and ch >= 0x14 and ch <= 0x16 end
-- (the test cheat, ROUTE_CHEAT, is not for those chapters: the user's decision, 2026-10-22. Its
-- refill stops the run at the first tick sample with chapterIndex 0x14 to 0x16, whatever ROUTE_E7
-- is (test_cheat.lua); ROUTE_E7=1, which only plays those chapters, stops it here at once.)
assert(not (L.E7.env and L.cheat), "ROUTE_CHEAT is not for Eirika's Chapter 20 and final chapter (ROUTE_E7=1 plays "
  .. "them): they are played fair (README-routes.md, \"Cheat-assisted routes\")")
if L.cheat then L.cheat.tick = function() return tick end end   -- (named by the cheat's refusal)

local function triangle(a, b)
  if (a == 0 and b == 2) or (a == 1 and b == 0) or (a == 2 and b == 1) then return 1 end
  if (b == 0 and a == 2) or (b == 1 and a == 0) or (b == 2 and a == 1) then return -1 end
  return 0
end
-- v5: the triangle of two weapons, with the reaver weapons (IA_REVERTTRIANGLE,
-- 1 << 8): each one that has it reverses the bonus and doubles it
-- (BattleApplyWeaponTriangleEffect, src/bmbattle.c).
IA_REVERTTRIANGLE = 1 << 8
local function triangleOf(wa, wb)
  local t = triangle(wa.weaponType, wb.weaponType)
  if V5 then
    if (wa.attributes & IA_REVERTTRIANGLE) ~= 0 then t = t * -2 end
    if (wb.attributes & IA_REVERTTRIANGLE) ~= 0 then t = t * -2 end
  end
  return t
end

-- v3: weapon d is effective against unit b (IsItemEffectiveAgainst, src/bmitem.c;
-- the flier-negation check is left out): might and triangle bonus count 3 times.
local function effective(d, b)
  if d.eff == 0 then return false end
  for i = 0, 63 do
    local c = emu:read8(d.eff + i)
    if c == 0 then return false end
    if c == b.class then return true end
  end
  return false
end

-- v3: unit b's terrain bonus to defence (or resistance) on its tile
-- (ClassData pTerrainDefenseLookup +0x48, pTerrainResistanceLookup +0x4C).
local function terrainBonus(b, magic)
  local tbl = emu:read32(b.classPtr + (magic and 0x4C or 0x48))
  return L.s8(emu:read8(tbl + L.mapGet(L.ADDR.gBmMapTerrain, b.x, b.y)))
end

IA_NEGATE_DEFENSE = 1 << 17
WPN_EFFECT_HPHALVE = 3
-- v10: the weapon's stat bonuses (ItemData pStatBonuses +0x0C, struct
-- ItemStatBonuses: hp, pow, skl, spd, def, res, lck, mov, con; GetUnitPower and
-- the rest add the equipped weapon's), and the sacred weapons' effectiveness,
-- x2 where every other weapon's is x3 (ComputeBattleUnitAttack, src/bmbattle.c).
-- Darkling Woods: Morva's Wretched Air gives pow +10, skl +10, def +20, res +10;
-- Seth's Audhulma, counted 72 against def 4 (one hit), did 54 against 24 (30),
-- and Morva's counter took 36 of his 43 HP where the bot counted 26.
BONUS = { pow = 1, skl = 2, spd = 3, def = 4, res = 5 }
local function wbonus(w, k)
  if not w then return 0 end
  local p = emu:read32(L.ADDR.gItemData + w.id * 0x24 + 0x0C)
  if p == 0 then return 0 end
  return L.s8(emu:read8(p + BONUS[k]))
end
-- (SACRED: above)
-- E6, chapter index 20 on: a weapon's stat bonuses while equipped (ItemData +0x0C
-- pStatBonuses, struct ItemStatBonuses: hp, pow, skl, spd, def, res; src/data_itembonus.c),
-- or nil. Morva's Wretched Air gives pow +10, def +20, res +10 and negates defence
-- (IA_NEGATE_DEFENSE): his counter took 36 off Myrrh, the model had said 11.
local function statBonuses(w)
  local p = emu:read32(L.ADDR.gItemData + w.id * 0x24 + 0x0C)
  if p == 0 then return nil end
  return { pow = L.s8(emu:read8(p + 1)), spd = L.s8(emu:read8(p + 3)), def = L.s8(emu:read8(p + 4)), res = L.s8(emu:read8(p + 5)) }
end

local function strike(a, b)
  local w = a.weapon
  if not w then return nil end
  if V10 then
    if emu:read8(L.ADDR.gItemData + w.id * 0x24 + 0x1F) == WPN_EFFECT_HPHALVE then return (b.hp + 1) // 2 end
    local tri = b.weapon and triangleOf(w, b.weapon) or 0
    local magic = (w.attributes & 0x42) ~= 0
    local atk = (w.might + tri) * (effective(w, b) and (SACRED[w.id] and 2 or 3) or 1)
    local pow = a.pow + wbonus(w, "pow")
    if (w.attributes & IA_NEGATE_DEFENSE) ~= 0 then return math.max(0, pow + atk) end
    local dv = magic and (b.res + wbonus(b.weapon, "res")) or (b.def + wbonus(b.weapon, "def"))
    return math.max(0, pow + atk - (dv + terrainBonus(b, magic)))
  end
  -- v8: a weapon that halves HP (WPN_EFFECT_HPHALVE: Eclipse, might 255 in
  -- the table) does (HP + 1) / 2 whatever the defence
  -- (ComputeBattleUnitSpecialWeaponStats, src/bmbattle.c). Scorched Sand: a
  -- Shaman's Eclipse counted 255, reached the whole map, and the army stood
  -- still for 40 turns.
  if V8 and emu:read8(L.ADDR.gItemData + w.id * 0x24 + 0x1F) == WPN_EFFECT_HPHALVE then return (b.hp + 1) // 2 end
  -- (E6: an HP-halving weapon, Eclipse: might 255 in the item data, the damage is
  -- half the defender's HP rounded up, src/bmbattle.c; E5 and before took the 255)
  if E6 and w.effect == 3 then return math.max(1, (b.hp + 1) // 2) end
  local tri = b.weapon and triangleOf(w, b.weapon) or 0   -- (without V5: triangle(), as before)
  if SACRED2 then
    -- (v6, chapter index 20 on: magic by the weapon's attributes only, IA_MAGIC or
    -- IA_MAGICDAMAGE, as ComputeBattleUnitDefense: a Divinestone hits defence; both
    -- sides' weapon stat bonuses; IA_NEGATE_DEFENSE leaves the defence out)
    local magic = (w.attributes & 0x42) ~= 0
    local ab, bb = statBonuses(w), b.weapon and statBonuses(b.weapon)
    local atk = (w.might + tri) * (effective(w, b) and (SACRED[w.id] and 2 or 3) or 1)
    local dfn = (magic and b.res or b.def) + (bb and (magic and bb.res or bb.def) or 0) + terrainBonus(b, magic)
    if (w.attributes & (1 << 17)) ~= 0 then dfn = 0 end
    return math.max(0, a.pow + (ab and ab.pow or 0) + atk - dfn)
  end
  if V3 then
    local magic = w.weaponType >= 5 or (w.attributes & 0x42) ~= 0
    -- v9: magic by the weapon's attributes only (IA_MAGIC | IA_MAGICDAMAGE), as
    -- ComputeBattleUnitDefense (src/bmbattle.c): a Dragonstone (type 11, no
    -- such bit) hits defence, not resistance.
    if V9 then magic = (w.attributes & 0x42) ~= 0 end
    local atk = (w.might + tri) * (effective(w, b) and 3 or 1)
    -- v8: IA_NEGATE_DEFENSE (Luna) leaves out the defence and its terrain bonus.
    if V8 and (w.attributes & IA_NEGATE_DEFENSE) ~= 0 then return math.max(0, a.pow + atk) end
    return math.max(0, a.pow + atk - ((magic and b.res or b.def) + terrainBonus(b, magic)))
  end
  local magic = w.weaponType >= 5 or (w.attributes & 0x40) ~= 0
  return math.max(0, a.pow + w.might + tri - (magic and b.res or b.def))
end

local function aspd(u)
  -- (E6: a unit carrying another fights at half speed, src/bmbattle.c)
  local spd = (E6 and (u.state & 0x10) ~= 0) and (u.spd // 2) or u.spd
  if not u.weapon then return spd end
  if V10 then return u.spd + wbonus(u.weapon, "spd") - math.max(0, u.weapon.weight - u.con) end
  if SACRED2 then   -- (E6, chapter index 20 on: the weapon's speed bonus)
    local bn = statBonuses(u.weapon)
    if bn then spd = spd + bn.spd end
  end
  return spd - math.max(0, u.weapon.weight - u.con)
end

local function nhits(a, b)
  if E6 and a.weapon and a.weapon.effect == 3 then return 1 end   -- (E6: an HP-halving weapon strikes once)
  if aspd(a) - aspd(b) >= 4 then return 2 end
  return 1
end
-- v10: a brave weapon (IA_BRAVE) strikes twice each time.
local function perStrike(u) if V10 and u.weapon and (u.weapon.attributes & 0x20) ~= 0 then return 2 end return 1 end

local function inRange(u, d)
  return u.weapon ~= nil and d >= u.weapon.minRange and d <= u.weapon.maxRange
end

local function sim(a, b, d)
  local ah, bh = a.hp, b.hp
  local da = inRange(a, d) and strike(a, b) or nil
  local db = inRange(b, d) and strike(b, a) or nil
  if db and not EXACT then db = db + 1 end   -- pessimism margin for the counter (none in a v9 siege)
  local seq = { "a", "b" }
  if nhits(a, b) == 2 then seq[#seq + 1] = "a" end
  if nhits(b, a) == 2 then seq[#seq + 1] = "b" end
  local pa, pb = perStrike(a), perStrike(b)   -- (v10: brave weapons; 1 otherwise)
  for _, s in ipairs(seq) do
    for _ = 1, (s == "a") and pa or pb do
      if ah <= 0 or bh <= 0 then break end
      if s == "a" and da then bh = bh - da elseif s == "b" and db then ah = ah - db end
    end
  end
  return ah, bh
end

local function dist(ax, ay, bx, by) return math.abs(ax - bx) + math.abs(ay - by) end

-- v6 (ROUTE_SEARCH=1; README-routes.md "Tactics v6"): the search below plays a
-- turn with one of several policies and keeps the best. Policy 0 is the plain
-- v5 bot; the others are v5 with these knobs. Without ROUTE_SEARCH the policy
-- stays 0, so every older route is made as before.
--   safe:     these units (by unit id) play safety first, as a hurt unit does:
--             they attack only where they are safe and move to the tile with the
--             least threat on the way (a healer heals only where the threat is
--             under half its HP); the search puts here the units that died in
--             the turn's earlier tries;
--   cautious: a unit other than a lord attacks only where it is safe, and keeps
--             off tiles whose threat reaches 70% of its HP;
--   hold:     a unit other than a lord that does not attack stands by the
--             nearest lord (or lord's carrier) instead of walking to the enemy;
--   amp/seed: a small deterministic noise (a hash of seed, turn, unit, tile)
--             on the attack and move scores, so equal and near-equal choices
--             come out differently;
--   order:    units of the same rank move in an order hashed from the seed.
P = { v = 0, seed = 0, amp = 0, cautious = false, hold = false, order = 0, safe = {} }
TR = nil   -- v6: the attack trial in progress (below the search, after finish); never in a save state
TRIAL_N = tonumber(os.getenv("ROUTE_TRIAL_N") or "4")
TRIAL_ALL = os.getenv("ROUTE_TRIAL_ALL") == "1"   -- (trials for every attack, not only bosses and stalls)
local SS_stalled = function() return false end   -- (the search's: the turn being played has stalled)
local function hash32(...)
  local h = 0x811C9DC5
  for _, v in ipairs({ ... }) do
    h = ((h ~ (v & 0xFFFF)) * 0x01000193) & 0xFFFFFFFF
    h = ((h ~ ((v >> 16) & 0xFFFF)) * 0x01000193) & 0xFFFFFFFF
  end
  h = h ~ (h >> 15)
  return h
end
local function noise(kind, a, b, c)
  if P.amp == 0 then return 0 end
  return ((hash32(P.seed, emu:read16(L.ADDR.gPlaySt + 0x10), kind, a, b, c) % 2001) - 1000) / 1000 * P.amp
end

---------------------------------------------------------------------------
-- Tactics versions. The six route files of README-routes.md (chapterIndex 0
-- to 4 and 6) were made with v1 and must regenerate byte for byte, so v1
-- stays as it was for those chapters. Every other chapter (5x = index 5,
-- then 7 on) uses v2:
--   - the enemy-phase threat comes from each enemy's real reach: its class
--     move costs over the map's terrain, blue and green units as walls, its
--     AI (AI1 3-5 "action standing" attack only from their own tile, AI1 6
--     does nothing), and the ranges of every weapon it carries;
--   - bosses are the red units whose character or class has CA_BOSS;
--   - a goal tile held by an enemy (the boss on the seize tile) still leads
--     the distance search (distanceField);
--   - the lord's moves weigh distance to its goal more (a bolder lord);
--   - a full inventory after a drop sends the new item to the supply (decide).
--
-- The three routes made with v2 (Chapters 5x, 6 and 7: chapterIndex 5, 7, 8)
-- must regenerate byte for byte too, so Chapter 8 (index 9) and every later
-- chapter use v3, which is v2 plus:
--   - weapons by the game's rules (CanUnitUseWeapon: weapon rank and locks);
--     a unit's weapon is the first usable one, as the game equips it;
--   - effectiveness (might x3) and the defender's terrain bonus in the
--     damage estimate;
--   - support: a unit with a healing staff heals the most hurt ally it can
--     reach; a hurt unit (under half HP) with a Vulnerary or Elixir uses it;
--     other hurt units and units without a weapon stay near the healers and
--     the fighters, safety first;
--   - a unit left without a usable weapon takes a spare one from an ally by
--     Trade;
--   - only the unit the game lets seize (CanUnitSeize: Eirika until the route
--     split) goes to the seize tile; the other lord stays safe.
local function tacticsV2(ch) return not ((ch >= 0 and ch <= 4) or ch == 6) end
local function tacticsV3(ch) return tacticsV2(ch) and ch ~= 5 and ch ~= 7 and ch ~= 8 end
-- v4 (Eirika's Chapter 11, index 61, and every chapter from index 12):
-- v3 plus world-map shopping and the hold-out mode (README-routes.md, "Tactics v4").
local function tacticsV4(ch) return tacticsV3(ch) and not (ch >= 0 and ch <= 11) end
-- v5 (Ephraim's route after the split: chapterIndex 0x17 to 0x23 and his
-- Chapter 11, 0x3E; README-routes.md "Tactics v5"): v4 plus the weapon chosen
-- per attack (with the reaver weapons' triangle), enemies as costly tiles
-- rather than walls in the distance search, and a 1-2 range weapon from the
-- world-map armory for every fighter that has none. No older route visits
-- these chapters, so the older files stay as they were.
local function tacticsV5(ch) return (ch >= 0x17 and ch <= 0x23) or ch == 0x3E end
-- v6 (Ephraim's Chapter 12 on: chapterIndex 0x19 to 0x23; README-routes.md
-- "Tactics v6"): v5 plus a red unit spared for a Talk counts double in the
-- threat. route_i9 to route_i11 (0x17, 0x18, 0x3E) were made before it.
local function tacticsV6(ch) return ch >= 0x19 and ch <= 0x23 end
-- v7 (Ephraim's Chapter 14 on: chapterIndex 0x1B to 0x23; README-routes.md
-- "Tactics v7"): v6 plus berserked allies as threats. route_i9 to
-- route_i13 (0x17 to 0x1A, 0x3E) were made before it.
local function tacticsV7(ch) return ch >= 0x1B and ch <= 0x23 end
-- v8 (from Scorched Sand on Ephraim's route: chapterIndex 0x1C to 0x23;
-- README-routes.md "Tactics v8"): v7 plus promotion items taken by Trade and
-- the damage of HP-halving and defence-negating weapons. route_i14 (0x1B)
-- was made before it.
local function tacticsV8(ch) return ch >= 0x1C and ch <= 0x23 end
-- v9 (from River of Regrets on Ephraim's route: chapterIndex 0x1E to 0x23;
-- README-routes.md "Tactics v9"): v8 plus magic by the weapon's attributes,
-- the siege of a lone standing boss (think, "siege"), a green unit to protect
-- carried by a mounted unit (PROTECT) and a stop at the game over. route_i14 to
-- route_i16 (0x1B to 0x1D) were made before it.
local function tacticsV9(ch) return ch >= 0x1E and ch <= 0x23 end
-- v10 (from Darkling Woods on Ephraim's route: chapterIndex 0x21 to 0x23;
-- README-routes.md "Tactics v10"): v9 plus the enemy's worst weapon in the
-- threat (bestStrike). route_i17 to route_i19 (0x1E to 0x20) were made before it.
local function tacticsV10(ch) return ch >= 0x21 and ch <= 0x23 end
-- v9: terrain that heals the unit standing on it (TerrainTable_HealAmount,
-- src/data_terrains.c: fort 20%, castle gate, throne, gate and church 10%).
HEAL_TERRAIN = { [0x0A] = true, [0x0B] = true, [0x1F] = true, [0x23] = true, [0x24] = true }
-- E5 (port-bots10e's "v5": every chapter from Eirika's Chapter 14, index 14, except
-- Chapter 11's index 61 and Ephraim's chapters): v4 plus the rules marked "v5" in
-- README-routes.md, "Tactics v5" of Eirika's chain.
-- The fifteen older routes never reach these chapters, so they stay as they were.
local function tacticsE5(ch) return tacticsV4(ch) and ch >= 14 and ch ~= 0x3D
  and not ((ch >= 0x17 and ch <= 0x23) or ch == 0x3E) end   -- (not Ephraim's chapters: tacticsV5 below)
-- E6 (port-bots10e's "v6": ROUTE_SEARCH=1, chapter index 15 on; README-routes.md
-- "Tactics v6"): E5 plus the turn search (below, after finish) and the rules marked
-- "v6". Off without ROUTE_SEARCH, so every route made without it stays as it was.
SEARCH = os.getenv("ROUTE_SEARCH") == "1"
SEARCH_FROM = 0x0F
local function tacticsE6(ch) return SEARCH and tacticsE5(ch) and ch >= SEARCH_FROM end
-- v4: chapters won by holding out until a turn (a TurnEventPlayer with
-- EVFLAG_WIN): the army keeps around the lord, which moves first, to the
-- safest tile away from the enemy; the others attack only where they are safe.
TURN_GOAL = { [0x0D] = 12,    -- Eirika's Chapter 13 (src/events/ch13a-eventinfo.h; or defeat the boss)
                    [0x18] = 11 }   -- Ephraim's Chapter 10 (src/events/ch10b-eventinfo.h; or defeat the boss)
-- (Ch19 on Ephraim's route, index 32, is also won at turn 14 or by the boss,
-- src/events/ch19b-eventinfo.h, but it is not played in hold-out mode: every
-- hold-out try lost Mansel or a lord by turn 12 to the 40-odd enemies, and the
-- usual tactics, with Mansel carried (PROTECT), won. README-routes.md "Tactics v9".)
local function holdOut(ch) return tacticsV4(ch) and TURN_GOAL[ch] ~= nil end
-- v9: a green unit whose death ends the game (its defeat quote sets
-- EVFLAG_GAMEOVER, src/data_battlequotes.c): Ch19 on Ephraim's route, Mansel
-- on the throne (16,10). A mounted unit rescues it (think, "rescue") and the
-- carrier keeps out of the fighting; a carried unit cannot be attacked.
PROTECT = { [0x20] = 0xC8 }
local function protectedUnit(ch)
  local c = PROTECT[ch]
  if not c then return nil end
  for _, g in ipairs(L.green()) do if g.char == c then return g end end   -- (a carried unit is not listed)
  return nil
end
MENUITEM_RESCUE = 0x63   -- the unit menu's Rescue (src/menu_def.c)
CA_MOUNTEDAID = 1
-- v9: the game over (ProcScr_BmGameOver, ProcScr_GameOverScreen) stops a try.
-- The title's "Resume Chapter" replays the same enemy phase with the same RNG;
-- a first Ch19 try did that for 20 minutes after Mansel's death, which the
-- lord check below does not see. (Also E6's PROTECT_E, below.)
PROC_BMGAMEOVER, PROC_GAMEOVERSCREEN = 0x089EDF78, 0x0859B358   -- ProcScr_BmGameOver, ProcScr_GameOverScreen
-- E5, experimental: Eirika's Chapter 15 (index 15, Rout; README-routes.md
-- "Tactics v5") is not won yet. Its lords are carried by a rescuer
-- (RESCUE_CHAPTERS, rescueLords below). ROUTE_ROUT_DEFEND=N also plays it as a
-- hold-out while more than N mobile enemies are left (AI1 0 and an AI2 other
-- than 3: with AI2 3 a unit waits until one of ours comes into its reach); off by
-- default (-1): in trials the hold-out kept the army waiting, and Ephraim, alone
-- in the south-west with his reinforcement-side group, died on turn 18.
-- (v6: also Chapter 20 on Eirika's route, index 20: her carrier takes her to the seize tile
-- once its boss is gone and drops her on it, think "carry"; walking, Eirika (level 1, 20 HP,
-- mounted, so the thickets are walls) never got through the monsters in a try)
RESCUE_CHAPTERS = { [0x0F] = true, [0x14] = true }
ROUT_DEFEND_N = tonumber(os.getenv("ROUTE_ROUT_DEFEND") or "-1")
local function routDefend(ch)
  if not tacticsE5(ch) or not RESCUE_CHAPTERS[ch] or ROUT_DEFEND_N < 0 then return false end
  local n = 0
  for _, e in ipairs(L.red()) do
    if (e.state & L.US.HIDDEN) == 0 and emu:read8(e.addr + 0x42) == 0 and emu:read8(e.addr + 0x44) ~= 3 then n = n + 1 end
  end
  return n > ROUT_DEFEND_N
end
local function defendMode(ch) return holdOut(ch) or routDefend(ch) end
-- v6, chapter index 19 on: a green unit whose defeat quote sets EVFLAG_GAMEOVER
-- (src/data_battlequotes.c): Chapter 19 (Last Hope, index 19), Mansel on the
-- throne. A mounted unit that is not a lord rescues it (think, "protect") and,
-- carrying it, plays as a lord that never attacks and keeps away from the enemy
-- (a carried unit cannot be attacked); the search counts the game over as a lost
-- lord (lordDeadNow). The same rule as port-bots10i's Tactics v9 for Ephraim's
-- Last Hope (index 32, PROTECT above), where every hold-out try lost Mansel or a lord.
PROTECT_E = { [0x13] = 0xC8 }
-- the ending (ROUTE_STOP=title:N): gProcScr_FinScreen (src/ending_details.c), gProcScr_TitleScreen
-- (src/titlescreen.c); seen = the ticks they were first seen. (In L: the main chunk has no
-- room for more locals, Lua's limit is 200.)
L.END = { fin = 0x08A3D498, title = 0x08AA6A50, seen = {} }
-- v6, the final chapter's part 2 (Eirika's index 0x16): its boss, the Demon King (character 0xBE,
-- 120 HP; Demonlight +10 defence, +15 resistance) is attacked by any unit, a lord too, that
-- survives the fight and then one more attack of his (exact damage; the other enemies' threat
-- left to the turn's search): with the lords' usual rule (threat under half the HP left) only
-- Siegmund and Audhulma could hurt him and nobody ever attacked (route_b21 try 1, turns 14 to 43)
L.FINALBOSS = { [0x16] = 0xBE }
-- the post-game (ROUTE_POSTGAME=1, ROUTE_WM_NODE=N[,M...]): the world-map nodes to enter in turn
-- (dungeons: Tower of Valni 26, Lagdou Ruins 27); after each dungeon's first floor but the
-- last node's, the "Proceed to the next floor?" choice (EventScr_FloorClearInTower) gets B
-- (result 0: retreat to the world map, StartRetreatProcessing); idx = the node now wanted
L.PG = { list = {}, idx = 1, done = {} }
for n in (os.getenv("ROUTE_WM_NODE") or ""):gmatch("%d+") do L.PG.list[#L.PG.list + 1] = tonumber(n) end
-- PR (coverage 12, port-c12-postgame-real; README-routes.md "The post-game with the real army"):
-- the post-game's floors with a real clear save's army (e_clear.sav: Eirika level 1, Ephraim,
-- Franz, Myrrh; no boost). Only with ROUTE_PG_REAL=1, which no older route file has, so every
-- older file comes out byte for byte:
--   - the world map's shopping (v4's shop plan, made at the run's first free cursor) comes
--     before L.PG's dungeon node: the lord walks to the armory first (with ROUTE_POSTGAME alone the
--     dungeon's node wins and the plan is never used; scratch et1a: Ephraim's last weapon, a
--     3-use Siegmund, and Myrrh's Divinestone broke on Tower floor 1);
--   - a bigger refit: a fighter whose usable weapons have fewer than LOW uses in all buys up to
--     COUNT weapons (about WANT uses in all, 20 a weapon), none dearer than MAXPRICE, keeping KEEP
--     gold; Door and Chest Keys count as junk to sell when a slot is needed; the vendor's heal
--     items (v10's rule: an Elixir for each unit that has none, lords first) in the post-game too.
L.PR = { on = os.getenv("ROUTE_PG_REAL") == "1", LOW = 60, WANT = 80, COUNT = 3, MAXPRICE = 2000, KEEP = 500 }
function L.PR.count(U) return math.min(L.PR.COUNT, math.max(1, (L.PR.WANT - U + 19) // 20)) end
function L.PR.junk(w) return L.PR.on and (w.id == 0x69 or w.id == 0x6A) end   -- ITEM_CHESTKEY, ITEM_DOORKEY
--   - the siege (v9's, think "siege") on the post-game floors (chapter indices 0x24 to 0x37) once every
--     enemy left stands on its tile (AI1 3 to 5), boss or not: exact damage, and a unit at 3/4 of its HP
--     or more (not a lord) attacks it wherever it outlives the counter; L.PR.siege is set per decision.
function L.PR.floor(ch) return L.PR.on and ch >= 0x24 and ch <= 0x37 end
--   - a weak lord keeps back (L.PR.BACK, Lagdou Ruins floor 4 on, chapter indices 0x31 to 0x37; a lord
--     with fewer than BACK_HP max HP): it takes no part in the walls rule (L.F.WALLS), and its threat
--     counts every cracked wall as open floor (cost 1: an ally may break it later in the turn) and the
--     blue units that have yet to move as out of the way (v10's `loose`). Scratch er4b, Ruins floor 4
--     (36 enemies, AI2 3, behind the cracked wall at (4,5)): every policy lost Eirika (level 1, 20 HP)
--     on turn 1 or 2: the walls rule walked her next to the wall, her threat was counted with the wall
--     standing and Ephraim in the way, and Ephraim broke the wall and stepped aside after her move.
--     (Both lords kept back, scratch er4c: Ephraim, 50 HP, waited in the north with Eirika from turn
--     23 to 50, and the army never met the 31 enemies left.)
L.PR.BACK, L.PR.BACK_HP = {}, 30
for ch = 0x31, 0x37 do L.PR.BACK[ch] = true end
function L.PR.back(ch, u) return L.PR.on and L.PR.BACK[ch] == true and u.maxHp < L.PR.BACK_HP end
--   - and a strong lord leads (L.PR.lead, the same floors, a lord with BACK_HP max HP or more, not hurt): he
--     walks to the enemies as any fighter does, and only a tile whose threat could kill him is ruled out (E7's
--     "lead" score). Scratch er4d: with no goal for a lord on these floors (no seize tile, no Seth) Ephraim
--     stood still once Franz had fallen (Myrrh and Riev have no weapon they can use), and the army waited
--     from turn 17 to 50 with 26 enemies left.
function L.PR.lead(ch, u) return L.PR.on and L.PR.BACK[ch] == true and u.maxHp >= L.PR.BACK_HP end
-- (the "# Generated" line: with ROUTE_PG_REAL=1 it also names the post-game settings, which older
-- files left to their README rows)
function L.PR.header()
  if not L.PR.on then return "" end
  local t = {}
  for _, k in ipairs({ "ROUTE_POSTGAME", "ROUTE_WM_NODE", "ROUTE_PG_REAL", "ROUTE_MAX_TURN" }) do
    local v = os.getenv(k)
    if v and v ~= "" then t[#t + 1] = ", " .. k .. "=" .. v end
  end
  return table.concat(t)
end
-- Feature routes (coverage 11, port-bots11p; README-routes.md "Feature routes"). Each one is on
-- only with its own setting, which no older route file has, so every older file comes out byte
-- for byte (the walls rule, L.F.WALLS, only in the post-game floors it lists):
--   ROUTE_ARENA=x,y,c     the unit with character c walks to the arena tile (x,y) and fights there
--                         (r9-uiarena's scratch copy of the bot, the same rules); ROUTE_ARENA_YIELD=N:
--                         B held 90 ticks, N ticks into the fight; ROUTE_ARENA_CHOICE=no: decline
--                         the wager; ROUTE_STOP=arena:N (N ticks after the arena screens close)
--   ROUTE_SUPPORT=a,b     character a walks next to character b, and once their support can grow
--                         a level (CanUnitSupportNow, src/bmreliance.c) takes the Support command
--                         with b; the conversation gets A only (no Start); ROUTE_STOP=support:N
--   ROUTE_CHEST=c         character c (a thief with a Lockpick, or a Chest Key holder) walks to the
--                         nearest closed chest (TERRAIN_CHEST_FULL 0x21) and opens it; ROUTE_STOP=chest:N
--   ROUTE_SUSPEND=T       at turn T's first free cursor: the map menu's Suspend, Yes; then the bot's
--                         A presses take the title's Resume Chapter; ROUTE_STOP=resume:N (N ticks
--                         after the resumed chapter's free cursor)
--   ROUTE_PROMO=1         the promotion screens (ProcScr_PromoMain: a trainee's level-10 promotion,
--                         classchg-event.c) get A in any chapter; ROUTE_STOP=promo:N
--   ROUTE_SKIRMISH=n[,r]  on the world map the lord walks to node n while monsters hold it, and the
--                         skirmish is played with the rules of chapter index r (default 0x39, Melkaen
--                         Coast: no story chapter's seize, boss, talk or hold-out rules; the skirmish
--                         map is a story chapter's); ROUTE_STOP=skirmish:N (N ticks after the world
--                         map's free cursor is back)
-- Coverage 12 (port-c12-features; README-routes.md "Feature routes (coverage 12)"), each on only
-- with its own setting, so every older file comes out byte for byte:
--   ROUTE_VISIT=c[,c...]  the units with these characters visit, nearest first, every open village
--                         with a Village event (VILL in the chapter's location events) and every house
--                         with a visit event (LOCA, TILE_COMMAND_VISIT); ROUTE_STOP=visit:N (N ticks
--                         after the last one's event ends; ROUTE_VISIT_N=K: after the K-th)
--   ROUTE_DOOR=c          character c (a Door Key holder) walks next to the nearest closed door
--                         (TERRAIN_DOOR) and takes Door; ROUTE_STOP=door:N
--   ROUTE_SHOP=c,x,y,buy,sell  character c walks onto the map shop at (x,y) (an armory, vendor or
--                         secret shop: the tile command of its SHOP location event) and buys the items
--                         `buy` and sells the items `sell` (hex ids joined by "+", or "-" for none);
--                         ROUTE_STOP=shop:N (N ticks after the shop closes)
--   ROUTE_SUPPORTS=a,b[;c,d...]  several support pairs: each `a` moves after the rest of the army (the
--                         pairs' `a`s in the list's order) and ends its turn next to its `b` (a tile
--                         whose threat it outlives), so the
--                         pair's points grow each turn (ProcessTurnSupportExp); every level that is
--                         ready (the points at the top of their level, no level this chapter yet) is
--                         taken with Support, the conversation with A only; ROUTE_SUPPORTS_N=K:
--                         ROUTE_STOP=support:N counts from the end of the K-th conversation (default 1)
--   ROUTE_WATCH=1         from the route's first free cursor on, every event gets A, never Start (the
--                         scenes are watched, not skipped). (ROUTE_WATCH=fast|slow is the scenes' rule,
--                         L.W below, which watches every scene at a human's pace; "1" is this one only)
--   ROUTE_WMSHOP=n,c,buy,sell  on the world map the lord walks to node n, whose menu's "? Shop" (the
--                         secret shop, WMMenu_IsSecretShopAvailable: the post-game only) opens, and the
--                         unit with character c buys and sells the lists (as ROUTE_SHOP); ROUTE_STOP=shop:N
--   ROUTE_PREFIX=FILE:N   the route's first N entries are key file FILE's, played as they are (the bot
--                         thinks from tick N on). (ROUTE_PREFIX=FILE with no ":N", and ROUTE_PREFIX_TICKS,
--                         is the scenes' rule, L.W below; a value ending in ":N" is this one only)
--   ROUTE_LORDPROMO=no    the first two talk choices get [No] (Chapter 16's ending: "promote now?" for
--                         each lord, EventScr_089F73F0/EventScr_089F7470: No gives the Lunar or Solar Brace)
--   ROUTE_USEITEM=c:it[,c:it...]  the unit with character c uses its item it (hex) once, Item, Use, as v7
--                         uses a promotion item (plan.promote)
--   ROUTE_PROMO_CHOICE=n  the promotion screen's class list (gMenuDef_PromoSel) takes its n-th class
--   ROUTE_TRAINEE=c       the unit with character c (a trainee) moves first each turn (its own attack
--                         rules: it fights where it is safe), so it gets the most fights
--   ROUTE_TALKRUSH=c,t    the unit with character c (a recruiter) moves first each turn and walks at the
--                         red or green unit with character t by the shortest way (no threat rule; the
--                         least threat among equal ways), and takes Talk once it can stand next to it
--                         (a looting thief who runs: Rennac, the Member Card's holder)
-- Their changing state is in L.F.st (the search's save states keep it). Numbers: decimal or 0x..
L.F = { on = false, st = {}, ACT = { arena = 0x62, support = 0x5B, chest = 0x5D, supports = 0x5B }, WALLS = {} }
function L.F.parse()
  local F = L.F
  local function env(k) local v = os.getenv(k); if v == "" then return nil end return v end
  local a = env("ROUTE_ARENA")
  if a then
    local x, y, c = a:match("^(%w+),(%w+),(%w+)$")
    F.arena = { x = tonumber(x), y = tonumber(y), char = tonumber(c), yield = tonumber(env("ROUTE_ARENA_YIELD") or ""),
                no = env("ROUTE_ARENA_CHOICE") == "no" }
    assert(F.arena.char, "ROUTE_ARENA=x,y,char")
  end
  local s = env("ROUTE_SUPPORT")
  if s then
    local x, y = s:match("^(%w+),(%w+)$")
    F.supp = { a = tonumber(x), b = tonumber(y) }
    assert(F.supp.a and F.supp.b, "ROUTE_SUPPORT=charA,charB")
  end
  if env("ROUTE_CHEST") then F.chest = { char = assert(tonumber(env("ROUTE_CHEST")), "ROUTE_CHEST=char") } end
  if env("ROUTE_SUSPEND") then F.susp = { turn = assert(tonumber(env("ROUTE_SUSPEND")), "ROUTE_SUSPEND=turn") } end
  if env("ROUTE_PROMO") == "1" then F.promo = true end
  local k = env("ROUTE_SKIRMISH")
  if k then
    local n, r = k:match("^(%w+),?(%w*)$")
    F.sk = { node = assert(tonumber(n), "ROUTE_SKIRMISH=node[,rule chapter]"), rule = (r ~= "" and tonumber(r)) or 0x39 }
    -- (the skirmish's rules: the bot's own reads of gPlaySt see chapter index sk.rule on the skirmish's
    -- map; the milestones read the game's own, route_lib.lua)
    local raw = L.playSt
    F.rawPlaySt = raw
    L.playSt = function()
      local ps = raw()
      if F.st.skStory and not F.st.skDone and ps.chapter ~= F.st.skStory then ps.chapter = F.sk.rule end
      return ps
    end
  end
  -- (coverage 12)
  local function chars(v) local t = {}; for c in v:gmatch("[^,]+") do t[assert(tonumber(c), "a character: " .. c)] = true end return t end
  local function ids(v) local t = {}; if v ~= "-" then for c in v:gmatch("[^+]+") do t[#t + 1] = assert(tonumber(c, 16), "an item id: " .. c) end end return t end
  if env("ROUTE_VISIT") then F.visit = chars(env("ROUTE_VISIT")); F.visitN = tonumber(env("ROUTE_VISIT_N") or "") end
  if env("ROUTE_DOOR") then F.door = { char = assert(tonumber(env("ROUTE_DOOR")), "ROUTE_DOOR=char") } end
  local sh = env("ROUTE_SHOP")
  if sh then
    local c, x, y, b, s2 = sh:match("^(%w+),(%w+),(%w+),([%w+-]+),([%w+-]+)$")
    F.shop = { char = tonumber(c), x = tonumber(x), y = tonumber(y), buy = ids(b or "-"), sell = ids(s2 or "-") }
    assert(F.shop.char and F.shop.x and F.shop.y, "ROUTE_SHOP=char,x,y,buy,sell")
  end
  local sps = env("ROUTE_SUPPORTS")
  if sps then
    F.supps, F.suppLast = {}, {}
    for a2, b2 in sps:gmatch("(%w+),(%w+)") do
      F.supps[#F.supps + 1] = { a = tonumber(a2), b = tonumber(b2) }
      F.suppLast[tonumber(a2)] = F.suppLast[tonumber(a2)] or #F.supps   -- (the pairs' `a`s move in the list's order)
    end
    assert(#F.supps > 0, "ROUTE_SUPPORTS=a,b[;c,d...]")
    F.suppN = tonumber(env("ROUTE_SUPPORTS_N") or "1")
  end
  if env("ROUTE_WATCH") == "1" then F.watch = true end
  local pre = env("ROUTE_PREFIX")
  if pre and pre:match(":%d+$") then   -- (FILE:N only: ROUTE_PREFIX=FILE is L.W's rule, coverage 12's merge)
    local path, n = pre:match("^(.+):(%d+)$")
    local all = L.readKeys(assert(path, "ROUTE_PREFIX=FILE:N"))
    F.prefix = {}
    for i = 1, math.min(tonumber(n), #all) do F.prefix[i] = all[i] end
  end
  if env("ROUTE_LORDPROMO") == "no" then F.lordNo = true end
  local ui = env("ROUTE_USEITEM")
  if ui then
    F.useItem = {}
    for c, it in ui:gmatch("(%w+):(%w+)") do F.useItem[#F.useItem + 1] = { char = tonumber(c), item = tonumber(it, 16) } end
  end
  local wms = env("ROUTE_WMSHOP")
  if wms then
    local n, c, b, s2 = wms:match("^(%w+),(%w+),([%w+-]+),([%w+-]+)$")
    F.wmshop = { node = tonumber(n), char = tonumber(c), buy = ids(b or "-"), sell = ids(s2 or "-") }
    assert(F.wmshop.node and F.wmshop.char, "ROUTE_WMSHOP=node,char,buy,sell")
  end
  if env("ROUTE_TRAINEE") then F.trainee = assert(tonumber(env("ROUTE_TRAINEE")), "ROUTE_TRAINEE=char") end
  local pc = env("ROUTE_PROMO_CHOICE")
  if pc then F.promoChoice = assert(tonumber(pc), "ROUTE_PROMO_CHOICE=n") end
  local tr = env("ROUTE_TALKRUSH")
  if tr then
    local c, t = tr:match("^(%w+),(%w+)$")
    F.talkRush = { by = tonumber(c), target = tonumber(t) }
    assert(F.talkRush.by and F.talkRush.target, "ROUTE_TALKRUSH=char,target")
  end
  F.on = (F.arena or F.supp or F.chest or F.susp or F.promo or F.sk or F.visit or F.door or F.shop or F.supps or F.watch
          or F.wmshop or F.prefix or F.lordNo or F.useItem or F.promoChoice or F.trainee or F.talkRush) ~= nil
end
L.F.parse()
-- (the key file's "# Generated" line names the settings above that are set, and only those)
function L.F.header()
  local t = {}
  for _, k in ipairs({ "ROUTE_ARENA", "ROUTE_ARENA_YIELD", "ROUTE_ARENA_CHOICE", "ROUTE_SUPPORT", "ROUTE_CHEST",
                       "ROUTE_SUSPEND", "ROUTE_PROMO", "ROUTE_SKIRMISH", "ROUTE_GALLERY",
                       "ROUTE_VISIT", "ROUTE_VISIT_N", "ROUTE_DOOR", "ROUTE_SHOP", "ROUTE_SUPPORTS", "ROUTE_SUPPORTS_N", "ROUTE_WATCH",
                       "ROUTE_WMSHOP", "ROUTE_PREFIX", "ROUTE_LORDPROMO", "ROUTE_USEITEM", "ROUTE_PROMO_CHOICE",
                       "ROUTE_TRAINEE", "ROUTE_TALKRUSH" }) do
    local v = os.getenv(k)
    -- (coverage 12's merge: ROUTE_WATCH other than 1 and ROUTE_PREFIX with no ":N" are L.W's, its header)
    if (k == "ROUTE_WATCH" and v ~= "1") or (k == "ROUTE_PREFIX" and not (v or ""):match(":%d+$")) then v = nil end
    if v and v ~= "" then t[#t + 1] = ", " .. k .. "=" .. v end
  end
  return table.concat(t)
end
-- I11 (coverage 11, port-bots11i; README-routes.md "Tactics I11"): Ephraim's final chapter,
-- chapterIndex 0x22 (part 1, Defeat Boss: Lyon, char 0x6C, on (11,2)) and 0x23 (part 2, the
-- Demon King, char 0xBE). No older route plays a turn there (route_i20 ends 245 ticks after
-- part 1's map loads), so every route file stays as it was. A global table: the main chunk
-- is at Lua's 200-local limit.
--   - the boss is the "holder" of v10 (no seize tile): the bait (a healthy fighter waits in
--     his reach with the weapon whose counter hurts him most; v10's rule) and the strike;
--     the weapon that hurts him most is kept for him whatever its uses (non-lords);
--   - the bait heals in place (Item, an Elixir, on its bait tile) when the heal lets it
--     outlive his next attack, so it counters every enemy phase (I11.baitHeal);
--   - part 1: never stop on the corridor (10,5)-(12,6) below the throne room (AREA
--     EventScr_089FDAD4: three Wights by Lyon every turn for ten turns) nor in the east
--     middle (12,7)-(22,12) (AREA EventScr_089FDA2C: five Arch Mogalls, Crimson Eye); the
--     distance fields count both as walls, so the army goes round by the west room;
--   - with ROUTE_SEARCH=1 the turn search (E6's, below) plays these two chapters too.
I11 = { BOSS = { [0x22] = 0x6C, [0x23] = 0xBE }, EAST = os.getenv("ROUTE_I11_EAST") == "1" }
function I11.on(ch) return ch == 0x22 or ch == 0x23 end
function I11.search(ch) return SEARCH and I11.on(ch) end
-- part 1's tiles a unit never ends its move on (ROUTE_I11_EAST=1: the east middle allowed)
function I11.noStop(ch, x, y)
  if ch ~= 0x22 then return false end
  if x >= 10 and x <= 12 and y >= 5 and y <= 6 then return true end
  if not I11.EAST and x >= 12 and x <= 22 and y >= 7 and y <= 12 then return true end
  return false
end
local function protectOn(ch) return tacticsE6(ch) and PROTECT_E[ch] ~= nil end
local function protectedUnitE(ch)
  if not protectOn(ch) then return nil end
  for _, g in ipairs(L.green()) do if g.char == PROTECT_E[ch] then return g end end   -- (a carried unit is not listed)
  return nil
end
LORD_TH = tonumber(os.getenv("ROUTE_LORD_TH") or "5")
LORD_DD = tonumber(os.getenv("ROUTE_LORD_DD") or "3")
MAX_TURN = tonumber(os.getenv("ROUTE_MAX_TURN") or "60")
V2 = false     -- set per decision from gPlaySt.chapterIndex
local reachSets = nil -- v2: enemy -> set of tiles it can attack this enemy phase
-- v6: enemy -> true while it is spared for a Talk; its threat counts double
-- (nobody attacks it, so it stays at full strength; Ephraim's Chapter 12:
-- Dozla stood next to Marisa and her Shamshir's critical killed him).
local spared = nil
-- v7: blue units under Berserk (statusIndex 4: the low nibble of +0x30). The
-- game moves them by its AI at the end of the player phase
-- (CpOrderBerserkInit, src/cp_order.c), and they attack anyone in reach
-- (Ephraim's Chapter 14: a Druid's Berserk staff turned Dozla, whose Hand
-- Axe killed Ephraim). They count in the threat like enemies.
local madAllies = nil
UNIT_STATUS_BERSERK = 4
local function isBerserk(u) return (emu:read8(u.addr + 0x30) & 0x0F) == UNIT_STATUS_BERSERK end

-- v3: CanUnitUseWeapon (src/bmitem.c): the item locks against the unit's
-- character and class attributes, then the weapon rank. The silence check and
-- IsItemUnsealedForUnit are left out (an IA_UNUSABLE weapon counts as unusable).
LOCKS = { { 1 << 11, 1 << 16 }, { 1 << 18, 1 << 28 }, { 1 << 19, 1 << 29 }, { 1 << 20, 1 << 30 },
                { 1 << 21, 1 << 31 }, { 1 << 12, 1 << 17 } }
IA_LOCK_ANY = (1 << 13) | (1 << 11) | (1 << 12) | (1 << 10) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21) | (1 << 16)
local function unitAttr(u) return emu:read32(emu:read32(u.addr) + 0x28) | emu:read32(u.classPtr + 0x28) end
local function canUseWeapon(u, d)
  if (d.attributes & 1) == 0 then return false end
  if (d.attributes & IA_LOCK_ANY) ~= 0 then
    local ca = unitAttr(u)
    for _, lk in ipairs(LOCKS) do
      if (d.attributes & lk[1]) ~= 0 and (ca & lk[2]) == 0 then return false end
    end
    if (d.attributes & (1 << 10)) ~= 0 then return (ca & (1 << 18)) ~= 0 end
    if (d.attributes & (1 << 16)) ~= 0 then return false end
  end
  return emu:read8(u.addr + 0x28 + d.weaponType) >= d.rank
end
local function canUseStaff(u, d)
  return (d.attributes & 4) ~= 0 and emu:read8(u.addr + 0x28 + d.weaponType) >= d.rank
end
-- v3: the game's equipped weapon is the first usable one (GetUnitEquippedWeapon).
local function equip(u)
  u.weapon = nil
  for _, it in ipairs(u.items) do
    local d = L.item(it & 0xFF)
    if canUseWeapon(u, d) then u.weapon = d; return u end
  end
  return u
end
local function equipAll(list) for _, u in ipairs(list) do equip(u) end return list end
HEAL_STAVES = { [0x4B] = true, [0x4C] = true, [0x4D] = true, [0x4E] = true } -- Heal, Mend, Recover, Physic (used at range 1)
HEAL_ITEMS = { [0x6C] = true, [0x6D] = true }                                   -- Vulnerary, Elixir
local function healStaffSlot(u)
  for k, it in ipairs(u.items) do
    local d = L.item(it & 0xFF)
    if HEAL_STAVES[d.id] and canUseStaff(u, d) then return k - 1 end
  end
end
local function healItemSlot(u)
  for k, it in ipairs(u.items) do if HEAL_ITEMS[it & 0xFF] then return k - 1 end end
end
-- v7: promotion items (CanUnitUsePromotionItem, src/bmitemuse.c): level 10 or
-- more and the unit's class in the item's 0-ended list (gItemUseJidList_*):
-- Hero Crest, Knight Crest, Orion's Bolt, Elysian Whip, Guiding Ring, Master
-- Seal, Ocean Seal. The class menu that follows (gMenuDef_PromoSel) takes its
-- first class, the confirm menu (Menu_PromoSubConfirm) "Change".
PROMO_LISTS = { [0x64] = 0x088ADF57, [0x65] = 0x088ADF5E, [0x66] = 0x088ADF64, [0x67] = 0x088ADF67,
                      [0x68] = 0x088ADF6B, [0x88] = 0x088ADF76, [0x97] = 0x088ADF9E }
MENUDEF_PROMOSEL, MENUDEF_PROMOCONFIRM = 0x08B129E4, 0x08B12930
local function promoFits(u, id)
  local list = PROMO_LISTS[id]
  if not list or u.level < 10 then return false end
  for i = 0, 63 do
    local c = emu:read8(list + i)
    if c == 0 then return false end
    if c == u.class then return true end
  end
  return false
end
local function promoSlot(u)
  for k, it in ipairs(u.items) do
    if promoFits(u, it & 0xFF) then return k - 1 end
  end
end
-- GetUnitKeyItemSlotForTerrain (src/bmunit.c) for a door: a Lockpick for a
-- CA_THIEF unit, else a Door Key.
local function canOpenDoor(u)
  for _, it in ipairs(u.items) do
    if (it & 0xFF) == 0x6A then return true end
    if (it & 0xFF) == 0x6B and (unitAttr(u) & (1 << 3)) ~= 0 then return true end
  end
  return false
end
local function isFighter(u)   -- has a rank in some non-staff weapon type
  for t = 0, 7 do if t ~= 4 and emu:read8(u.addr + 0x28 + t) > 0 then return true end end
  return false
end
-- CanUnitSeize (src/bmdifficulty.c): Eirika, or Ephraim on his route and in Chapter 5x.
local function canSeize(u)
  local ps = L.playSt()
  local leader = (ps.mode == 3 or ps.chapter == 5) and L.CHAR.EPHRAIM or L.CHAR.EIRIKA
  return u.char == leader
end

local function weaponRanges(u)
  local lo, hi
  for _, it in ipairs(u.items) do
    local d = L.item(it & 0xFF)
    if (d.attributes & 1) ~= 0 and (d.attributes & 4) == 0 and d.maxRange > 0 and (not V3 or canUseWeapon(u, d)) then
      if lo == nil or d.minRange < lo then lo = d.minRange end
      if hi == nil or d.maxRange > hi then hi = d.maxRange end
    end
  end
  return lo, hi
end

-- v2: the tiles enemy e can attack in the coming enemy phase. Blue and green
-- units block it (it passes red ones but cannot stop on them); `mover` (the
-- blue unit being planned) is left out, since it is about to move.
-- v10 `loose` (for a lord's threat): a blue unit that has yet to move does not
-- block either, since it may step aside (Darkling Woods: Ephraim, moving after
-- Eirika, opened the way for the second Deathgoyle that killed her).
local function enemyReach(e, mover, mad, loose, walls)
  local set = {}
  if not e.weapon then return set end
  local ai1 = emu:read8(e.addr + 0x42)
  if ai1 == 6 and not mad then return set end
  local w, h = L.mapSize()
  local lo, hi = weaponRanges(e)
  if not lo then return set end
  local function occupant(x, y)
    local o = L.mapGet(L.ADDR.gBmMapUnit, x, y)
    if loose and o > 0 and o < 0x40 and (emu:read32(L.ADDR.gUnitArrayBlue + (o - 1) * 0x48 + 0x0C) & L.US.UNSELECTABLE) == 0 then return 0 end
    return o
  end
  local function blocks(x, y)
    local o = occupant(x, y)
    if o == 0 or (mover and o == mover.id) then return false end
    if mad then return o >= 0x40 end   -- v7: a berserked blue unit passes blue units
    return o < 0x80   -- blue (0x01-0x3F) and green (0x40-0x7F)
  end
  local stops = {}
  if ai1 >= 3 and ai1 <= 5 and not mad then
    stops[1] = { e.x, e.y }
  else
    local costTable = emu:read32(e.classPtr + 0x38)
    local D = { [e.y * 256 + e.x] = 0 }
    local open, head = { { e.x, e.y } }, 1
    while head <= #open do
      local cx, cy = open[head][1], open[head][2]; head = head + 1
      local cd = D[cy * 256 + cx]
      for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local nx, ny = cx + dv[1], cy + dv[2]
        if nx >= 0 and ny >= 0 and nx < w and ny < h and not blocks(nx, ny) then
          local c = L.s8(emu:read8(costTable + L.mapGet(L.ADDR.gBmMapTerrain, nx, ny)))
          if walls and L.mapGet(L.ADDR.gBmMapTerrain, nx, ny) == 0x1B then c = 1 end   -- (PR: L.PR.BACK, a cracked wall open)
          if c > 0 then
            local nd, k = cd + c, ny * 256 + nx
            if nd <= e.mov and (D[k] == nil or nd < D[k]) then D[k] = nd; open[#open + 1] = { nx, ny } end
          end
        end
      end
    end
    for k in pairs(D) do
      local x, y = k % 256, k // 256
      local o = occupant(x, y)
      if o == 0 or o == e.id or (mover and o == mover.id) then stops[#stops + 1] = { x, y } end
    end
  end
  for _, s in ipairs(stops) do
    for dy = -hi, hi do
      for dx = -hi, hi do
        local d = math.abs(dx) + math.abs(dy)
        if d >= lo and d <= hi then set[(s[2] + dy) * 256 + (s[1] + dx)] = true end
      end
    end
  end
  return set
end

-- v10: the worst strike of enemy e on unit u over every weapon e can use (the
-- AI picks its weapon; e.weapon is only the first usable one). Darkling Woods:
-- a Gorgon's first weapon is Stone (might 0), and its Demon Surge took 16 of
-- Ephraim's 28 HP on a tile the bot counted as safe. Stone itself (weapon
-- effect 5, petrification, no damage) counts STONE_THREAT.
WPN_EFFECT_PETRIFY, STONE_THREAT = 5, 8
local function bestStrike(e, u)
  local dmg, hits = strike(e, u) or 0, nhits(e, u)
  local save = e.weapon
  for _, it in ipairs(e.items) do
    local d = L.item(it & 0xFF)
    if (d.attributes & 4) == 0 and canUseWeapon(e, d) then
      e.weapon = d
      local s, n = strike(e, u) or 0, nhits(e, u) * perStrike(e)
      if emu:read8(L.ADDR.gItemData + d.id * 0x24 + 0x1F) == WPN_EFFECT_PETRIFY then s, n = STONE_THREAT, 1 end
      if s * n > dmg * hits then dmg, hits = s, n end
    end
  end
  e.weapon = save
  return dmg, hits
end

-- Enemy-phase threat on unit u standing at (x, y): sum of the worst damage of
-- every enemy that could reach attack range (v1: Manhattan, terrain ignored;
-- v2: the enemy's real reach, enemyReach above).
local function threatAt(u, x, y, enemies, exclude)
  local t = 0
  if V3 and (u.x ~= x or u.y ~= y) then   -- v3: the terrain bonus of the tile (x, y)
    local c = {}
    for k, v in pairs(u) do c[k] = v end
    c.x, c.y = x, y
    u = c
  end
  for _, e in ipairs(enemies) do
    if e ~= exclude and e.weapon then
      local can
      if V2 and reachSets then can = reachSets[e] and reachSets[e][y * 256 + x]
      else can = dist(e.x, e.y, x, y) <= e.mov + e.weapon.maxRange end
      if can then
        local dmg, hits = strike(e, u) or 0, nhits(e, u)
        -- (E7 too: a Gorgon's first weapon is Stone, its Demon Surge takes 20 of Franz's 41)
        if V10 or L.E7.on then dmg, hits = bestStrike(e, u) end
        t = t + (dmg + (EXACT and 0 or 1)) * hits * ((spared and spared[e]) and 2 or 1)
      end
    end
  end
  for _, m in ipairs(madAllies or {}) do   -- v7: berserked allies
    if m.id ~= u.id and m.weapon and reachSets and reachSets[m] and reachSets[m][y * 256 + x] then
      t = t + ((strike(m, u) or 0) + 1) * nhits(m, u)
    end
  end
  return t
end

---------------------------------------------------------------------------
-- Map helpers.

-- Dijkstra over the map for unit u's class move costs, from the goal tiles.
-- Enemy tiles are walls. Returns dist[y][x] (nil = unreachable).
-- v3: with `doors`, a closed door counts as a tile of cost DOOR_COST (someone
-- in the army can open it).
-- v5: an enemy's tile costs this much more instead of being a wall, so a
-- corridor held by enemies still leads somewhere (Ephraim's Chapter 9: the
-- fort's columns are full of enemies, and with walls the whole army had no
-- distance to anything and waited in the corner for eight turns).
ENEMY_COST = 8
-- E5: with `roofs`, a roof tile (TERRAIN_ROOF 0x22: a closed room of Eirika's
-- Chapter 14, which the game turns into floor once the room is opened) counts
-- as a tile of cost 1. (Only E5 passes `roofs`; only V5 makes enemy tiles costly.)
TERRAIN_DOOR, DOOR_COST, TERRAIN_ROOF = 0x1E, 4, 0x22
local function distanceField(u, goals, enemies, doors, roofs, corridor)
  local w, h = L.mapSize()
  local costTable = emu:read32(u.classPtr + 0x38)
  local block = {}
  for _, e in ipairs(enemies) do block[e.y * 256 + e.x] = true end
  local i11ch = L.playSt().chapter   -- (I11: part 1's no-stop tiles are walls here)
  if not I11.on(i11ch) then i11ch = nil end
  -- v2: a goal tile held by an enemy (a boss on the seize tile) still spreads
  -- the distance; in v1 it was a wall, so nothing led to it.
  if V2 then for _, g in ipairs(goals) do block[g.y * 256 + g.x] = nil end end
  local function cost(x, y)
    local extra = 0
    if i11ch and I11.noStop(i11ch, x, y) then return nil end
    if block[y * 256 + x] then
      if not V5 then return nil end
      extra = ENEMY_COST
    end
    -- (E7: `corridor` (a function of x, y: an extra cost, or nil for a wall) keeps the way to
    -- its tiles: L.E7.wayCost)
    if corridor then
      local ex = corridor(x, y)
      if ex == nil then return nil end
      extra = extra + ex
    end
    if doors and L.mapGet(L.ADDR.gBmMapTerrain, x, y) == TERRAIN_DOOR then return DOOR_COST + extra end
    if roofs and L.mapGet(L.ADDR.gBmMapTerrain, x, y) == TERRAIN_ROOF then return 1 end
    local c = L.s8(emu:read8(costTable + L.mapGet(L.ADDR.gBmMapTerrain, x, y)))
    if c < 0 then return nil end
    return c + extra
  end
  local D, open = {}, {}
  for y = 0, h - 1 do D[y] = {} end
  for _, g in ipairs(goals) do
    if g.x >= 0 and g.y >= 0 and g.x < w and g.y < h then
      D[g.y][g.x] = 0; open[#open + 1] = { g.x, g.y }
    end
  end
  -- Simple label-correcting search (maps are at most ~30x30).
  local head = 1
  while head <= #open do
    local cx, cy = open[head][1], open[head][2]; head = head + 1
    local cd = D[cy][cx]
    for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = cx + dv[1], cy + dv[2]
      if nx >= 0 and ny >= 0 and nx < w and ny < h then
        local c = cost(cx, cy)   -- cost of the tile we leave (walking back toward the goal)
        if c then
          local nd = cd + c
          if D[ny][nx] == nil or nd < D[ny][nx] then
            D[ny][nx] = nd; open[#open + 1] = { nx, ny }
          end
        end
      end
    end
  end
  return D
end

-- E7, Chapter 20 (index 0x14): Eirika's carrier takes the southern way to the seize tile
-- (rows 9 down: the SE corner's exit at x 26-29, west along rows 10-14, down by x 17-18 to
-- rows 17-19, the mountain at (9,19), up the west side x 5-8 to the basin round (11,12)).
-- The northern way passes 17 monsters and the spawn points of two area waves (Cyclops at
-- (11,1), Deathgoyles at (1,1)); in step E3 every carrier that went north stalled there.
function L.E7.way(x, y)
  if L.E7.ch == 0x14 then return y >= 9 end
  return true
end
-- (as an extra cost in a distance field: a tile off the way costs 30 more, so a unit that is
-- off it, after a fight or a heal, still has a way back; with the way as a wall Franz, who had
-- fought his way to (19,6), had no distance and took E6's own way, north, alone)
function L.E7.wayCost(x, y) return L.E7.way(x, y) and 0 or 30 end
-- The carrier's distance field to the tiles next to the seize tile (its class's move costs,
-- the red units left out, the way above), or nil.
function L.E7.field(u, seize)
  if not seize then return nil end
  L.E7.ch = L.playSt().chapter
  local W, H = L.mapSize()
  local ct = emu:read32(u.classPtr + 0x38)
  local goals = {}
  for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
    local x, y = seize.x + dv[1], seize.y + dv[2]
    if x >= 0 and y >= 0 and x < W and y < H and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0 then
      goals[#goals + 1] = { x = x, y = y }
    end
  end
  return distanceField(u, goals, {}, false, false, L.E7.wayCost)
end

-- Tiles the active unit can end its move on (movement map < 120, no unit there).
local function reachable(u)
  local w, h = L.mapSize()
  local list = {}
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      if L.mapGet(L.ADDR.gBmMapMovement, x, y) < 120 then
        local occ = L.mapGet(L.ADDR.gBmMapUnit, x, y)
        if occ == 0 or (x == u.x and y == u.y) then list[#list + 1] = { x = x, y = y } end
      end
    end
  end
  return list
end

-- v10: the tiles blue unit u could end its move on this turn, by its own
-- flood (the game fills gBmMapMovement only for the selected unit): its class
-- move costs, red units block, blue and green ones can be passed but not
-- stopped on. `free` (a unit) counts as gone (the enemy a strike kills first).
local function moveTiles(u, free)
  local w, h = L.mapSize()
  local costTable = emu:read32(u.classPtr + 0x38)
  local D = { [u.y * 256 + u.x] = 0 }
  local open, head = { { u.x, u.y } }, 1
  local function occ(x, y)
    if free and x == free.x and y == free.y then return 0 end
    return L.mapGet(L.ADDR.gBmMapUnit, x, y)
  end
  while head <= #open do
    local cx, cy = open[head][1], open[head][2]; head = head + 1
    local cd = D[cy * 256 + cx]
    for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = cx + dv[1], cy + dv[2]
      if nx >= 0 and ny >= 0 and nx < w and ny < h and occ(nx, ny) < 0x80 then
        local c = L.s8(emu:read8(costTable + L.mapGet(L.ADDR.gBmMapTerrain, nx, ny)))
        if c > 0 then
          local nd, k = cd + c, ny * 256 + nx
          if nd <= u.mov and (D[k] == nil or nd < D[k]) then D[k] = nd; open[#open + 1] = { nx, ny } end
        end
      end
    end
  end
  local list = {}
  for k in pairs(D) do
    local x, y = k % 256, k // 256
    local o = occ(x, y)
    if o == 0 or o == u.id then list[#list + 1] = { x = x, y = y } end
  end
  table.sort(list, function(a, b) if a.y ~= b.y then return a.y < b.y end return a.x < b.x end)
  return list
end

---------------------------------------------------------------------------
-- Chapter knowledge (src/events/*-eventinfo.h).

local function seizeTile(ch)
  if ch == 1 then return { x = 2, y = 2 } end
  if ch == 3 then return { x = 14, y = 1 } end
  if ch == 5 then return { x = 13, y = 7 } end   -- Chapter 5x (chapterIndex 5), Ephraim
  if ch == 8 then return { x = 9, y = 4 } end    -- Chapter 7 (chapterIndex 8), src/events/ch7-eventinfo.h
  if ch == 9 then return { x = 10, y = 2 } end   -- Chapter 8 (chapterIndex 9), src/events/ch8-eventinfo.h
  if ch == 0x0B then return { x = 15, y = 1 } end -- Eirika's Chapter 10 (chapterIndex 11), src/events/ch10a-eventinfo.h
  if ch == 0x17 then return { x = 22, y = 7 } end -- Ephraim's Chapter 9 (chapterIndex 23), src/events/ch9b-eventinfo.h
  if ch == 0x1B then return { x = 14, y = 2 } end -- Ephraim's Chapter 14 (chapterIndex 27), src/events/ch14b-eventinfo.h
  if ch == 0x1D then return { x = 13, y = 3 } end -- Chapter 16 on Ephraim's route (chapterIndex 29), src/events/ch16b-eventinfo.h
  if ch == 0x21 then return { x = 11, y = 11 } end -- Chapter 20 on Ephraim's route (chapterIndex 33), src/events/ch20b-eventinfo.h
  if ch == 0x0E then return { x = 9, y = 5 } end  -- E5: Eirika's Chapter 14 (chapterIndex 14), src/events/ch14a-eventinfo.h
  if ch == 0x10 then return { x = 13, y = 3 } end -- E6: Chapter 16 on Eirika's route (chapterIndex 16), src/events/ch16a-eventinfo.h
  if ch == 0x14 then return { x = 11, y = 11 } end -- E6: Chapter 20 on Eirika's route (chapterIndex 20), src/events/ch20a-eventinfo.h
  return nil
end

local function isLordUnit(u) return u.char == L.CHAR.EIRIKA or u.char == L.CHAR.EPHRAIM end

-- v5: a lord carried by an ally (Rescue). In a RESCUE_CHAPTERS chapter the
-- army's toughest fighter in reach rescues each lord: a rescued unit is off the
-- map and cannot be attacked, so only the carrier is at risk (Eirika, level 1
-- with 16 HP, found no safe tile from turn 3 of Eirika's Chapter 15 in trials;
-- carried, both lords were alive on turn 60). The carrier then plays as a lord
-- (safety first, never attacks, last to move; in a hold-out the others stand by it). GetUnitAid and CanUnitRescue (src/bmunit.c): aid = con - 1 on foot,
-- 25 - con mounted (20 - con for women); the lord's con must not exceed it.
local function unitAid(u)
  local ca = emu:read32(emu:read32(u.addr) + 0x28) | emu:read32(u.classPtr + 0x28)
  if (ca & 1) == 0 then return u.con - 1 end
  if (ca & (1 << 14)) ~= 0 then return 20 - u.con end
  return 25 - u.con
end
local function carriesLord(u)
  if (u.state & L.US.RESCUING) == 0 then return false end
  local id = emu:read8(u.addr + 0x1B)
  if id == 0 or id >= 0x40 then return false end
  local r = L.readUnit(L.ADDR.gUnitArrayBlue + (id - 1) * 0x48)
  return r ~= nil and isLordUnit(r)
end
local function rescueLords(ch) return tacticsE5(ch) and RESCUE_CHAPTERS[ch] ~= nil end
-- v6 (PROTECT): u carries the unit to protect (a green unit, ids 0x41 to 0x7F).
local function carriesProtected(u)
  if (u.state & L.US.RESCUING) == 0 then return false end
  local id = emu:read8(u.addr + 0x1B)
  if id < 0x41 or id >= 0x80 then return false end
  local r = L.readUnit(L.ADDR.gUnitArrayGreen + (id - 0x41) * 0x48)
  return r ~= nil and r.char == PROTECT_E[L.playSt().chapter]
end
local function lordLike(u)
  local ch = L.playSt().chapter
  return isLordUnit(u) or (rescueLords(ch) and carriesLord(u)) or (protectOn(ch) and carriesProtected(u))
    or (L.E7.chapter(ch) and carriesLord(u))   -- (E7: Eirika's carrier, the final chapter too)
end

-- v3: recruits by Talk (the CHAR events of src/events/*-eventinfo.h that make
-- a red or green unit join): target character, and the characters who can
-- talk to it.
TALKS = {
  [0x0A] = { { target = 0x12, by = { 0x04, 0x01 } } },               -- E9: Amelia, by Franz or Eirika
  [0x0B] = { { target = 0x0B, by = { 0x01, 0x22 } },                 -- E10: Innes, by Eirika or Tana
             { target = 0x14, by = { 0x0B, 0x15 } },                 --      Gerik, by Innes or Tethys
             { target = 0x15, by = { 0x0B, 0x14 } },                 --      Tethys, by Innes or Gerik
             { target = 0x16, by = { 0x14 } } },                     --      Marisa, by Gerik
  -- E13 (index 13) has two Talk recruits, Amelia (by Eirika or Franz) and
  -- Cormag (by Eirika). They are left out: in a trial with them (cov9 bot
  -- step 3), sparing them cost six units and then Eirika (turn 11); she is
  -- still level 1 and never found a safe tile next to them.
  -- E5: E14 (index 14): Rennac, by L'Arachel (EventScr_089F619C: CUSA). Eirika's
  -- talk to him (EventScr_089F61C4) is a gold offer, so she is left out.
  [0x0E] = { { target = 0x1C, by = { 0x19 } } },
  [0x17] = { { target = 0x12, by = { 0x0F, 0x04 } } },               -- Ephraim's Ch9: Amelia, by Ephraim or Franz (src/events/ch9b-eventinfo.h)
  [0x18] = { { target = 0x1D, by = { 0x0F } },                       -- Ephraim's Ch10: Duessel (green), by Ephraim (src/events/ch10b-eventinfo.h)
             { target = 0x0E, by = { 0x1D, 0x22 } } },               --      Cormag, by Duessel or Tana
  [0x3E] = { { target = 0x19, by = { 0x0F } },                       -- Ephraim's Ch11: L'Arachel (green, turn 5), by Ephraim (src/events/ch11b-eventinfo.h)
             { target = 0x1A, by = { 0x19 } } },                     --      Dozla (green), by L'Arachel
  [0x19] = { { target = 0x16, by = { 0x18 } } },                     -- Ephraim's Ch12: Marisa, by Ewan (src/events/ch12b-eventinfo.h)
  -- Ephraim's Ch14: Rennac (red, turn 2), by L'Arachel (src/events/ch14b-eventinfo.h; Ephraim's
  -- own Talk asks for gold, EventScr_089FC52C, so it is left out).
  [0x1B] = { { target = 0x1C, by = { 0x19 } } },
  -- Ch17 on Ephraim's route: Syrene (green, with four civilians), by Innes, Tana or Vanessa
  -- (src/events/ch17b-eventinfo.h; CUSA, EventScr_089F7CAC).
  [0x1E] = { { target = 0x21, by = { 0x0B, 0x22, 0x06 } } },
}
-- v5: villages whose visit makes a unit join (a Village location event that
-- loads a blue unit), with the character who joins. The fighter nearest the
-- village walks to it; any unit that can stand on it safely visits.
RECRUIT_VILLAGES = {
  [0x19] = { { x = 5, y = 5, joins = 0x18 } },   -- Ephraim's Ch12: Ewan (EventScr_089FB934, src/events/ch12b-eventscript.h)
}
TERRAIN_VILLAGE = 0x03   -- TERRAIN_VILLAGE_REGULAR (a visited village becomes TERRAIN_VILLAGE_CLOSED)
local function recruitVillage(ch)
  for _, v in ipairs(RECRUIT_VILLAGES[ch] or {}) do
    if L.mapGet(L.ADDR.gBmMapTerrain, v.x, v.y) == TERRAIN_VILLAGE then
      local joined = false
      for i = 0, 61 do
        local b = L.readUnit(L.ADDR.gUnitArrayBlue + i * 0x48)
        if b and b.char == v.joins then joined = true end
      end
      if not joined then return v end
    end
  end
  return nil
end
-- E6: map vendors (the Vendor() entries of src/events/*-eventinfo.h; the list
-- is the ShopList's address in fireemblem8.elf).
VENDORS = {
  [0x0F] = { { x = 8, y = 4, list = 0x089ED860 } },   -- E15: ShopList_Event_089ED860 (Thunder ... Antitoxin)
}
-- E6: promotion items and their class lists (gItemUseJidList_*, src/bmitemuse.c CanUnitUsePromotionItem;
-- Ephraim's chain has its own, PROMO_LISTS above, with four more items)
PROMO_LISTS_E = { [0x64] = 0x088ADF57, [0x65] = 0x088ADF5E, [0x88] = 0x088ADF76 }   -- Hero Crest, Knight Crest, Master Seal
MENUDEF_PROMO_SEL, MENUDEF_PROMO_CONFIRM = 0x08B129E4, 0x08B12930   -- gMenuDef_PromoSel, Menu_PromoSubConfirm
PROC_PROMO_MAIN = 0x08B12614                                         -- ProcScr_PromoMain
MENUITEM_VENDOR = 0x60                                               -- src/menu_def.c (Vendor)
-- (MENUITEM_SUPPLY, 0x69: below, before support)
-- E6: the supply screen (src/prep_itemsupply.c): ProcScr_BmSupplyScreen; struct
-- PrepItemSupplyProc unk_33 (Give 0 / Take 1) +0x33, currentPage +0x35, idxPerPage[9]
-- +0x3A; the page's list gPrepScreenItemList (4 bytes each, item +2) and its length
-- gUnknown_02012F56; the convoy gConvoyItemArray (u16[100]).
SUPPLY = { Proc = 0x08A192EC, GiveTake = 0x0809DCD5, Take = 0x0809E421, List = 0x020122D4, ListN = 0x02012F56,
                 Convoy = 0x0203A81C }
VENDOR_KEEP_GOLD = 3000
TALK_GIVE_UP_TURN = 15   -- after this turn a red talk target may be attacked
local function talkEntries(ch) return TALKS[ch] or {} end
-- A red unit we do not attack yet: a talk target whose talker is in the army.
local function spareForTalk(ch, e, blue)
  if L.playSt().turn > TALK_GIVE_UP_TURN then return false end
  for _, tk in ipairs(talkEntries(ch)) do
    if tk.target == e.char then
      for _, b in ipairs(blue) do
        for _, c in ipairs(tk.by) do if b.char == c then return true end end
      end
    end
  end
  return false
end

CA_BOSS = 0x8000   -- include/bmunit.h
local function isBoss(e)
  if V2 then
    return ((emu:read32(emu:read32(e.addr) + 0x28) | emu:read32(e.classPtr + 0x28)) & CA_BOSS) ~= 0
  end
  return e.char == L.CHAR.ONEILL or e.char == L.CHAR.BREGUET or e.char == L.CHAR.BONE or e.char == L.CHAR.BAZBA
end
-- v6, chapter index 20 on (README-routes.md "Tactics v6", "The boss on the seize
-- tile"): the boss standing on the chapter's seize tile (Chapter 20: Morva on
-- (11,11), AI1 3, never leaves it), or nil once it is gone.
local function seizeBoss(ch, enemies)
  if not (tacticsE6(ch) and ch >= 0x14) then return nil end
  local s = seizeTile(ch)
  if not s then return nil end
  for _, e in ipairs(enemies or L.red()) do
    if e.x == s.x and e.y == s.y
        and ((emu:read32(emu:read32(e.addr) + 0x28) | emu:read32(e.classPtr + 0x28)) & CA_BOSS) ~= 0 then return e end
  end
  return nil
end
-- v6, chapter index 20 on: unit u's usable weapons ({ d = item data, slot }, inventory order);
-- the one that strikes the seize tile's boss sb hardest; the first one that is not effective
-- against sb, for any other target (nil: none). Morva on (11,11) has 62 HP and defence 4:
-- Ephraim's Siegmund (x2) or Myrrh's Divinestone (x3) kill him in one round, but in two
-- tries both units spent their effective weapons on the way (Divinestone 24 uses, Vidofnir
-- 19) and nobody could hurt him any more.
local function usableWeapons(u)
  local out = {}
  for k, it in ipairs(u.items) do
    local d = L.item(it & 0xFF)
    if canUseWeapon(u, d) then out[#out + 1] = { d = d, slot = k - 1 } end
  end
  return out
end
local function bossWeapon(u, sb)
  local best, bw
  for _, w in ipairs(usableWeapons(u)) do
    local c = {}
    for k, v in pairs(u) do c[k] = v end
    c.weapon = w.d
    local s = strike(c, sb) or 0
    if best == nil or s > best then best, bw = s, w end
  end
  return bw
end
-- A unit with only sb weapons uses them on others while more than this many uses are left
-- (60: never; the weapon's 50 at most): the enemy phase's counters spend them anyway, and
-- with 20, 8 or 6 Myrrh reached Morva with 5 uses (too late), or none.
BOSS_KEEP_USES = 60
local function trashWeapon(u, sb)
  for _, w in ipairs(usableWeapons(u)) do
    if not effective(w.d, sb) then return w end
  end
  return nil
end
-- (the weapon for a target e other than sb: a trash weapon, else the sb weapon while it has
-- more than BOSS_KEEP_USES uses left, or more than 2 against a unit within 2 tiles of a
-- tile u can stand on and strike sb from with it (sb's only open side, for Morva (11,12)),
-- which may bar the way: Myrrh, whose only weapon is the Divinestone; she spent its 24
-- uses on the way in tries, and in another a mogall on (13,12) barred her way)
local function otherWeapon(u, sb, e)
  local w = trashWeapon(u, sb)
  if w then return w end
  local bw = bossWeapon(u, sb)
  if not bw then return nil end
  local left = u.items[bw.slot + 1] >> 8
  if left > BOSS_KEEP_USES then return bw end
  if not e or left <= 2 then return nil end
  local W, H = L.mapSize()
  local ct = emu:read32(u.classPtr + 0x38)
  for dy = -bw.d.maxRange, bw.d.maxRange do
    for dx = -bw.d.maxRange, bw.d.maxRange do
      local d, x, y = math.abs(dx) + math.abs(dy), sb.x + dx, sb.y + dy
      if d >= bw.d.minRange and d <= bw.d.maxRange and x >= 0 and y >= 0 and x < W and y < H
          and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0 and dist(e.x, e.y, x, y) <= 2 then
        return bw
      end
    end
  end
  return nil
end
-- (the weapon menu's entries: L.MENUITEM.WeaponSelect0 + inventory slot, route_lib.lua)
MENUITEM_DROP = 0x64      -- the unit menu's Drop (src/menu_def.c)

---------------------------------------------------------------------------
-- Planner. plan = { uid, dest = {x,y}, action = "attack"|"wait"|"seize",
-- target = enemy unit id, tries }

local plan = nil
local endTurn = false
local skipUnits = {}      -- uid -> true when a unit could not be commanded this turn
local lastTurnKey = nil
local badDest = {}        -- "uid:x:y" -> true
local lastAlive = nil     -- verbose: blue unit id -> char at the last player phase

-- v5: a red unit under a roof (US_HIDDEN with US_UNDER_A_ROOF, include/bmunit.h:
-- the rooms of Eirika's Chapter 14 until someone walks in) is not on the map yet,
-- so it is neither a target nor a goal. v3 kept planning bow and magic attacks at
-- such units through the wall, every turn, and the Attack menu never offered them.
local function hiddenRed(e) return tacticsE5(L.playSt().chapter) and (e.state & L.US.HIDDEN) ~= 0 end

-- v3: the talk target (a red or green unit) this unit should go and talk to.
local function talkTargetFor(u)
  for _, tk in ipairs(talkEntries(L.playSt().chapter)) do
    for _, c in ipairs(tk.by) do
      if c == u.char then
        for _, e in ipairs(L.red()) do if e.char == tk.target and not hiddenRed(e) then return e end end
        for _, g in ipairs(L.green()) do if g.char == tk.target then return g end end
      end
    end
  end
end

-- v10: the supply (convoy). The lord's unit menu has Supply (overrideId 0x69),
-- and so has a unit next to the lord (SupplyUsability, src/bmmenu.c: Ephraim
-- on his route). Darkling Woods: the four units left carry plain weapons, and
-- the supply holds Siegmund (Ephraim's), Vidofnir and Audhulma (S rank, both
-- effective against monsters), a Brave Lance. A unit takes the best weapon it
-- can use when that beats its own best by SUPPLY_GAIN (a weapon effective
-- against monsters counts SUPPLY_EFF more), giving the supply the weapons it
-- cannot use when it needs the room; each unit once per chapter.
MENUITEM_SUPPLY = 0x69
MENUITEM_EQUIP = 0x35   -- gItemSubMenuItems' Equip (src/menu_def.c)
CONVOY, CONVOY_N = 0x0203A81C, 100   -- gConvoyItemArray (u16[100])
SUPPLY_GAIN, SUPPLY_EFF = 20, 100
MONSTER_EFF = 0x088ADF39             -- ItemEffectiveness_Monsters (src/data_itemuse.c)
local supplyTried = {}                     -- "chapter:uid" -> true once a supply plan was made
local function itemCountOf(u, id)
  local n = 0
  for _, it in ipairs(u.items) do if (it & 0xFF) == id then n = n + 1 end end
  return n
end
local function supplyScore(u, w)
  local s = w.might * 10 - math.max(0, w.weight - u.con) * 6
  if w.eff == MONSTER_EFF then s = s + SUPPLY_EFF end
  if (w.attributes & 0x20) ~= 0 then s = s + 60 end   -- IA_BRAVE
  return s
end
local function convoyItems()
  local t = {}
  for i = 0, CONVOY_N - 1 do
    local it = emu:read16(CONVOY + 2 * i)
    if it ~= 0 then t[#t + 1] = it & 0xFF end
  end
  return t
end
local function supplyLord()
  local ps = L.playSt()
  local pid = (ps.mode == 3) and L.CHAR.EPHRAIM or L.CHAR.EIRIKA
  for _, b in ipairs(L.blue()) do if b.char == pid then return b end end
end
-- The supply plan for unit u: { take = {id}, give = {ids}, base = {[id] = count} } or nil.
local function supplyPlanFor(u)
  local ps = L.playSt()
  local mine, uses = 0, 0
  for _, it in ipairs(u.items) do
    local w = L.item(it & 0xFF)
    if (w.attributes & 4) == 0 and canUseWeapon(u, w) then
      mine = math.max(mine, supplyScore(u, w))
      uses = uses + ((w.attributes & 8) ~= 0 and 99 or (it >> 8))   -- (IA_UNBREAKABLE)
    end
  end
  -- (again, a turn later, when its usable weapons are nearly worn out)
  local tried = supplyTried[ps.chapter .. ":" .. u.id]
  -- (I11: also when it holds no heal item and the supply has one: Seth's Elixir, the bait's)
  local noHeal = false
  if I11.on(ps.chapter) and itemCountOf(u, 0x6D) + itemCountOf(u, 0x6C) == 0 then
    for _, id in ipairs(convoyItems()) do if id == 0x6C or id == 0x6D then noHeal = true end end
  end
  if tried and not ((uses < 10 or noHeal) and ps.turn > tried) then return nil end
  if uses < 10 then mine = 0 end
  local best, bestId
  local stock = convoyItems()
  for _, id in ipairs(stock) do
    local w = L.item(id)
    if (w.attributes & 4) == 0 and canUseWeapon(u, w) and itemCountOf(u, id) == 0 then
      local s = supplyScore(u, w)
      if s >= mine + SUPPLY_GAIN and (best == nil or s > best) then best, bestId = s, id end
    end
  end
  local take = {}
  if bestId then take[1] = bestId end
  -- (and a heal item when it holds none: an Elixir first)
  if itemCountOf(u, 0x6D) + itemCountOf(u, 0x6C) == 0 then
    local heal
    for _, id in ipairs(stock) do
      if id == 0x6D then heal = id elseif id == 0x6C and not heal then heal = id end
    end
    if heal then take[#take + 1] = heal end
  end
  if #take == 0 then return nil end
  local give = {}
  local need = #take - (5 - #u.items)
  for _, it in ipairs(u.items) do
    if need <= 0 then break end
    local w = L.item(it & 0xFF)
    local junk = ((w.attributes & 1) ~= 0 and (w.attributes & 4) == 0 and not canUseWeapon(u, w))
              or ((w.attributes & 4) ~= 0 and not canUseStaff(u, w))
              -- (an item it has no use for: a gem, a promotion item that does not fit it)
              or (w.weaponType == 9 and not HEAL_ITEMS[w.id] and not (w.id >= 0x5B and w.id <= 0x63)
                  and not promoFits(u, w.id) and w.id ~= 0x69 and w.id ~= 0x6A and w.id ~= 0x6B)
    if junk then give[#give + 1] = w.id; need = need - 1 end
  end
  while need > 0 do take[#take] = nil; need = need - 1 end
  if #take == 0 then return nil end
  local base = {}
  for _, id in ipairs(take) do base[id] = itemCountOf(u, id) end
  for _, id in ipairs(give) do base[id] = itemCountOf(u, id) end
  return { take = take, give = give, base = base, score = best or 0, mine = mine }
end

-- v3: support moves (heal, use a Vulnerary, talk to a recruit, take a weapon
-- by Trade, open a door), tried before attacks. Returns a plan or nil.
local function support(u, tiles, enemies, ok)
  local blue = equipAll(L.blue())
  -- v10: the supply (see supplyPlanFor), from the lord's own tile or one next to him.
  -- (E7 too: Eirika takes Sieglinde, sitting in the supply, and gives what she cannot use)
  if V10 or L.E7.on then
    local sp = supplyPlanFor(u)
    local lord = sp and supplyLord()
    if lord then
      local best, bestT
      for _, t in ipairs(tiles) do
        local here = (lord.id == u.id) and (t.x == u.x and t.y == u.y) or (lord.id ~= u.id and dist(t.x, t.y, lord.x, lord.y) == 1)
        if here and ok(t) then
          local th = threatAt(u, t.x, t.y, enemies)
          if (isLordUnit(u) and th * 2 < u.hp or not isLordUnit(u) and th < u.hp) and (best == nil or th < best) then best, bestT = th, t end
        end
      end
      if bestT then
        supplyTried[L.playSt().chapter .. ":" .. u.id] = L.playSt().turn
        local ts, gs = {}, {}
        for _, id in ipairs(sp.take) do ts[#ts + 1] = string.format("%02x", id) end
        for _, id in ipairs(sp.give) do gs[#gs + 1] = string.format("%02x", id) end
        log("supply plan: char %02x takes %s (weapon score %d over %d), gives %s", u.char, table.concat(ts, " "), sp.score, sp.mine,
          #gs > 0 and table.concat(gs, " ") or "-")
        return { dest = bestT, action = "supply", supply = sp }
      end
    end
  end
  -- v10: a stat booster (ITEM_BOOSTER_HP to ITEM_BOOSTER_CON, 0x5B-0x63): the safest tile, then Item, Use.
  if V10 then
    for k, it in ipairs(u.items) do
      local id = it & 0xFF
      if id >= 0x5B and id <= 0x63 then
        local best, bestT
        for _, t in ipairs(tiles) do
          if ok(t) then
            local th = threatAt(u, t.x, t.y, enemies)
            if best == nil or th < best then best, bestT = th, t end
          end
        end
        return { dest = bestT or { x = u.x, y = u.y }, action = "item", slot = k - 1 }
      end
    end
  end
  -- I11, part 2: the lord bait (Ephraim) without an Elixir takes one by Trade from an ally that
  -- holds one, giving the first item he cannot use when he is full: the Demon King's 37 leave him
  -- 2 of 39 HP, and only an Elixir brings him back for the next round (a Vulnerary's 10 do not).
  if I11.lordBait(L.playSt().chapter, u) and itemCountOf(u, 0x6D) == 0 then
    local best, bestP
    for _, a in ipairs(blue) do
      for k, it in ipairs(a.id ~= u.id and a.items or {}) do
        if (it & 0xFF) == 0x6D then
          for _, t in ipairs(tiles) do
            if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
              local th = threatAt(u, t.x, t.y, enemies)
              if th * 2 < u.hp and (best == nil or th < best) then
                best, bestP = th, { dest = t, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = k - 1 }
              end
            end
          end
        end
      end
    end
    if bestP then
      if #u.items >= 5 then
        for k, it in ipairs(u.items) do
          if not canUseWeapon(u, L.item(it & 0xFF)) and not HEAL_ITEMS[it & 0xFF] then bestP.giveSlot = k - 1; break end
        end
        bestP.giveSlot = bestP.giveSlot or 4
      end
      log("elixir trade: char %02x takes slot %d of %02x", u.char, bestP.takeSlot, bestP.target)
      return bestP
    end
  end
  -- E5: rescue a lord (rescueLords above): the toughest fighter (max HP + 2 x
  -- defence) among those within move + 1 of it that can carry it.
  -- (v6, chapter index 20 on: not here; Ephraim carries her, think "carry")
  if rescueLords(L.playSt().chapter) and not lordLike(u) and isFighter(u) and u.weapon
      and u.hp * 10 >= u.maxHp * 4 and not (tacticsE6(L.playSt().chapter) and L.playSt().chapter >= 0x14) then
    for _, b in ipairs(blue) do
      -- (v6: not on turn 1 while the lord is out of danger there, so the army can
      -- use the supply next to Eirika first)
      local wait1 = tacticsE6(L.playSt().chapter) and L.playSt().turn == 1 and threatAt(b, b.x, b.y, enemies) * 2 < b.hp
      -- (v6: only a weak lord is carried, max HP + 2 x defence under 45: Eirika, still
      -- level 1; Ephraim fights, with his Reginleif, and the search keeps him alive)
      local strong = tacticsE6(L.playSt().chapter) and b.maxHp + 2 * b.def >= 45
      if isLordUnit(b) and unitAid(u) >= b.con and not wait1 and not strong then
        local mine = u.maxHp + 2 * u.def
        local bestOther = false
        for _, a in ipairs(blue) do
          if a.id ~= u.id and not lordLike(a) and isFighter(a) and a.weapon and (a.state & L.US.HAS_MOVED) == 0
              and a.hp * 10 >= a.maxHp * 4 and unitAid(a) >= b.con and dist(a.x, a.y, b.x, b.y) <= a.mov + 1
              and (a.maxHp + 2 * a.def > mine or (a.maxHp + 2 * a.def == mine and a.id < u.id)) then bestOther = true end
        end
        if not bestOther then
          local best, bestT
          for _, t in ipairs(tiles) do
            if ok(t) and dist(t.x, t.y, b.x, b.y) == 1 then
              local th = threatAt(u, t.x, t.y, enemies)
              if best == nil or th < best then best, bestT = th, t end
            end
          end
          if bestT then return { dest = bestT, action = "rescue", target = b.id, targetPos = { x = b.x, y = b.y } } end
        end
      end
    end
  end
  local E6 = tacticsE6(L.playSt().chapter)
  local function promoFitsE(id)
    local lst = PROMO_LISTS_E[id]
    if not lst or u.level < 10 then return false end
    for i = 0, 40 do
      local c = emu:read8(lst + i)
      if c == 0 then return false end
      if c == u.class then return true end
    end
    return false
  end
  -- v6: a promotion item this unit can use (level 10 or more, its class in the
  -- item's list): use it at once, on the safest tile in reach.
  if E6 and u.level >= 10 then
    for k, it in ipairs(u.items) do
      if promoFitsE(it & 0xFF) then
        local best, bestT
        for _, t in ipairs(tiles) do
          if ok(t) then
            local th = threatAt(u, t.x, t.y, enemies)
            if best == nil or th < best then best, bestT = th, t end
          end
        end
        return { dest = bestT or { x = u.x, y = u.y }, action = "item", slot = k - 1 }
      end
    end
  end
  -- v6: the supply holds a promotion item this unit can use (and it has none),
  -- or weapons it can use while its own have fewer than 40 uses in all (up to
  -- two): take them from a tile next to Eirika, while she is on the map
  -- (SupplyUsability, src/bmmenu.c). A promotion item is used on a later turn
  -- by the rule above; a weapon comes last in the inventory, so it is used when
  -- the ones before it break.
  if E6 and #u.items < 5 and not isLordUnit(u) and not lordLike(u) and not L.E7.on then   -- (E7: v10's supply above)
    local eir
    for _, b in ipairs(blue) do if b.char == L.CHAR.EIRIKA then eir = b end end
    if eir then
      local take, free = {}, 5 - #u.items
      local held, uses = false, 0
      for _, h in ipairs(u.items) do
        if PROMO_LISTS_E[h & 0xFF] then held = true end
        if canUseWeapon(u, L.item(h & 0xFF)) then uses = uses + (h >> 8) end
      end
      local weapons = {}
      for i = 0, 99 do
        local it = emu:read16(SUPPLY.Convoy + 2 * i)
        if it == 0 then break end
        local d = L.item(it & 0xFF)
        if not held and promoFitsE(it & 0xFF) and #take < free then take[#take + 1] = it & 0xFF; held = true
        elseif isFighter(u) and uses < 40 and (d.attributes & 4) == 0 and canUseWeapon(u, d) then
          weapons[#weapons + 1] = d
        end
      end
      table.sort(weapons, function(a, b) if a.might ~= b.might then return a.might > b.might end return a.id < b.id end)
      for k = 1, math.min(2, #weapons) do if #take < free then take[#take + 1] = weapons[k].id end end
      -- (in a chapter whose lords are carried, one tile next to Eirika is left for her rescuer)
      local freeNext = 0
      local w, h = L.mapSize()
      for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local nx, ny = eir.x + dv[1], eir.y + dv[2]
        if nx >= 0 and ny >= 0 and nx < w and ny < h and L.mapGet(L.ADDR.gBmMapUnit, nx, ny) == 0 then freeNext = freeNext + 1 end
      end
      if #take > 0 and (freeNext >= 2 or not rescueLords(L.playSt().chapter)) then
        local best, bestT
        for _, t in ipairs(tiles) do
          if ok(t) and dist(t.x, t.y, eir.x, eir.y) == 1 then
            local th = threatAt(u, t.x, t.y, enemies)
            if th < u.hp and (best == nil or th < best) then best, bestT = th, t end
          end
        end
        if bestT then return { dest = bestT, action = "supply", take = take } end
      end
    end
  end
  -- Heal the most hurt ally this unit can stand next to.
  local ss = healStaffSlot(u)
  if ss then
    local best, bestP
    for _, a in ipairs(blue) do
      local miss = a.maxHp - a.hp
      if a.id ~= u.id and miss >= 7 then
        for _, t in ipairs(tiles) do
          if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
            local th = threatAt(u, t.x, t.y, enemies)
            if th < u.hp and not (P.safe[u.id] and th * 2 >= u.hp) then   -- (v6 policy "safe": under half its HP)
              local score = miss * 10 + (isLordUnit(a) and 60 or 0) + (a.char == L.CHAR.SETH and 30 or 0) - th * 4
              if best == nil or score > best then
                best, bestP = score, { dest = t, action = "staff", target = a.id, targetPos = { x = a.x, y = a.y }, slot = ss }
              end
            end
          end
        end
      end
    end
    if bestP then return bestP end
  end
  -- (v6, the final chapter's part 2 while the Demon King stands: a unit 15 HP or more down heals
  -- with an item (below), and one with no heal item first takes one from an ally next to a tile
  -- in reach that has two or more (Trade): an attack on him needs the HP for his counter and his
  -- next attack, and Ephraim, with no heal item, stood at 35 of 60 for turns)
  local fbOn = false
  if E6 and L.FINALBOSS[L.playSt().chapter] then
    for _, e in ipairs(enemies) do if e.char == L.FINALBOSS[L.playSt().chapter] then fbOn = true end end
  end
  -- (I11: any fighter but a lord, the bait's Elixirs: Seth holds none at the start)
  -- (not in part 2, where the Elixirs are the lord bait's: Franz and Ephraim traded one back and forth)
  local i11Trade = L.playSt().chapter == 0x22 and not isLordUnit(u)
  -- (E7 too, the whole way: Ephraim, his Elixir spent, stood at 4 of 46 HP on turn 52 of a try)
  if (fbOn or i11Trade or L.E7.on) and not healItemSlot(u) and u.maxHp - u.hp >= 15 and #u.items < 5 then
    local best, bestP
    for _, a in ipairs(blue) do
      local n, slot = 0, nil
      for k, it in ipairs(a.items) do
        if HEAL_ITEMS[it & 0xFF] then n = n + 1; if slot == nil or (it & 0xFF) == 0x6D then slot = k - 1 end end
      end
      if a.id ~= u.id and n >= 2 then
        for _, t in ipairs(tiles) do
          if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
            local th = threatAt(u, t.x, t.y, enemies)
            if best == nil or th < best then
              best, bestP = th, { dest = t, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = slot }
            end
          end
        end
      end
    end
    if bestP then return bestP end
  end
  -- Under half HP with a Vulnerary or Elixir: the safest tile, then use it.
  local hs = healItemSlot(u)
  -- (I11, part 2: a Vulnerary first unless more than 20 HP are missing: the Elixirs are the bait's)
  if L.playSt().chapter == 0x23 and hs and u.maxHp - u.hp <= 20 then
    for k, it in ipairs(u.items) do if (it & 0xFF) == 0x6C then hs = k - 1; break end end
  end
  -- (v9 siege: also a unit under 3/4 of its HP, so it can attack again)
  -- (v10: a lord under 3/4 of its HP too; Darkling Woods: Eirika kept 15 of 26
  -- HP for eight turns until two Deathgoyles killed her)
  -- (v10: every unit under 3/4: the bait needs a fighter at 3/4 or more, and
  -- Seth kept 28 of 48 HP and two Elixirs for 20 turns)
  -- (E6, the Demon King standing: fbOn above)
  -- (E7: under 60%: an Elixir heals it all, and the carrier at under half could not get away)
  if hs and (((u.hp * 2 < u.maxHp or (SIEGE and u.hp * 4 < u.maxHp * 3) or (V10 and u.hp * 4 < u.maxHp * 3)
      or (L.E7.on and u.hp * 5 < u.maxHp * 3))
      and u.maxHp - u.hp >= 10) or (fbOn and u.maxHp - u.hp >= 15)) then
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) then
        local th = threatAt(u, t.x, t.y, enemies)
        -- (E7: on Eirika's way when it can, L.E7.way: Ephraim healed on a quiet tile in the
        -- north and fought on there, off her way)
        if L.E7.on and not L.E7.way(t.x, t.y) then th = th + 30 end
        if best == nil or th < best then best, bestT = th, t end
      end
    end
    return { dest = bestT or { x = u.x, y = u.y }, action = "item", slot = hs }
  end
  -- v7: a promotion item the unit can use: the safest tile, then Item, Use.
  local ps7 = V7 and promoSlot(u)
  if ps7 then
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) then
        local th = threatAt(u, t.x, t.y, enemies)
        if best == nil or th < best then best, bestT = th, t end
      end
    end
    return { dest = bestT or { x = u.x, y = u.y }, action = "item", slot = ps7, promote = true }
  end
  -- v8: an ally holds a promotion item this unit can use and the ally cannot
  -- (the Knight Crest Vigarde drops goes to Ephraim; Franz can use it): take
  -- it by Trade from a safe tile next to the ally, giving back the first item
  -- that is neither a usable weapon nor a heal item when the unit is full.
  if V8 and u.level >= 10 then
    local best, bestP
    for _, a in ipairs(blue) do
      if a.id ~= u.id then
        for k, it in ipairs(a.items) do
          local id = it & 0xFF
          if promoFits(u, id) and not promoFits(a, id) then
            for _, t in ipairs(tiles) do
              if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
                local th = threatAt(u, t.x, t.y, enemies)
                if th < u.hp and (best == nil or th < best) then
                  best, bestP = th, { dest = t, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = k - 1 }
                end
              end
            end
          end
        end
      end
    end
    if bestP then
      if #u.items >= 5 then
        for k, it in ipairs(u.items) do
          if not canUseWeapon(u, L.item(it & 0xFF)) and not HEAL_ITEMS[it & 0xFF] then bestP.giveSlot = k - 1; break end
        end
        bestP.giveSlot = bestP.giveSlot or 4
      end
      return bestP
    end
  end
  -- v5: visit a recruit village standing on it (a lord only where the threat
  -- stays under half its HP).
  local village = V5 and recruitVillage(L.playSt().chapter)
  if village then
    for _, t in ipairs(tiles) do
      if ok(t) and t.x == village.x and t.y == village.y then
        local th = threatAt(u, t.x, t.y, enemies)
        if (isLordUnit(u) and th * 2 < u.hp) or (not isLordUnit(u) and th < u.hp) then
          return { dest = t, action = "visit" }
        end
      end
    end
  end
  -- E6: a map vendor in reach: a healer with no healing staff it can use buys
  -- the best one on sale (Mend, else Heal); anyone with fewer than two healing
  -- items buys Vulneraries up to two; VENDOR_KEEP_GOLD gold is kept. Only where
  -- the threat on the vendor's tile stays under the unit's HP (half for "safe").
  if E6 and #u.items < 5 then
    for _, vd in ipairs(VENDORS[L.playSt().chapter] or {}) do
      local stock = {}
      for i = 0, 19 do
        local v = emu:read16(vd.list + 2 * i)
        if v == 0 then break end
        stock[v & 0xFF] = true
      end
      local gold, free, buy = L.playSt().gold - VENDOR_KEEP_GOLD, 5 - #u.items, {}
      if emu:read8(u.addr + 0x28 + 4) > 0 and not ss then
        for _, id in ipairs({ 0x4C, 0x4B }) do   -- Mend, Heal
          local d = L.item(id)
          if stock[id] and canUseStaff(u, d) and d.uses * d.cost <= gold then
            buy[#buy + 1] = id; gold = gold - d.uses * d.cost; free = free - 1
            break
          end
        end
      end
      local have = 0
      for _, it in ipairs(u.items) do if HEAL_ITEMS[it & 0xFF] then have = have + 1 end end
      local vu = L.item(0x6C)
      while have < 2 and free > 0 and stock[0x6C] and vu.uses * vu.cost <= gold do
        buy[#buy + 1] = 0x6C; gold = gold - vu.uses * vu.cost; free = free - 1; have = have + 1
      end
      if #buy > 0 then
        for _, t in ipairs(tiles) do
          if t.x == vd.x and t.y == vd.y and ok(t) then
            local th = threatAt(u, t.x, t.y, enemies)
            if th < u.hp and not (P.safe[u.id] and th * 2 >= u.hp) then
              return { dest = t, action = "vendor", buy = buy }
            end
          end
        end
      end
    end
  end
  -- Talk to a recruit next to a tile in reach (a lord only where the threat
  -- stays under half its HP).
  local tgt = talkTargetFor(u)
  if tgt then
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) and dist(t.x, t.y, tgt.x, tgt.y) == 1 then
        local th = threatAt(u, t.x, t.y, enemies, tgt)
        if (isLordUnit(u) and th * 2 < u.hp) or (not isLordUnit(u) and th < u.hp) then
          if best == nil or th < best then best, bestT = th, t end
        end
      end
    end
    if bestT then return { dest = bestT, action = "talk", target = tgt.id, targetPos = { x = tgt.x, y = tgt.y } } end
  end
  -- No usable weapon: take a spare one (the ally cannot use it, or has another).
  if not u.weapon and isFighter(u) then
    local best, bestP
    for _, a in ipairs(blue) do
      if a.id ~= u.id then
        local own = 0
        for _, it in ipairs(a.items) do if canUseWeapon(a, L.item(it & 0xFF)) then own = own + 1 end end
        for k, it in ipairs(a.items) do
          local d = L.item(it & 0xFF)
          if canUseWeapon(u, d) and (not canUseWeapon(a, d) or own >= 2) then
            for _, t in ipairs(tiles) do
              if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
                local score = d.might * 5 - threatAt(u, t.x, t.y, enemies) * 3
                if best == nil or score > best then
                  best, bestP = score, { dest = t, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = k - 1 }
                end
              end
            end
          end
        end
      end
    end
    if bestP then
      if #u.items >= 5 then   -- full: give back the first weapon it cannot use (else the last item)
        for k, it in ipairs(u.items) do
          local d = L.item(it & 0xFF)
          if (d.attributes & 1) ~= 0 and not canUseWeapon(u, d) then bestP.giveSlot = k - 1; break end
        end
        bestP.giveSlot = bestP.giveSlot or 4
      end
      return bestP
    end
  end
  -- v5 (RESCUE_CHAPTERS only, so route_e14.keys stays as it was made): a healer
  -- with no healing staff it can use takes one an ally cannot use, by Trade
  -- (Natasha's Mend broke in Eirika's Chapter 14, and Colm carried one he cannot
  -- use; empty-handed she died on turn 3 of Chapter 15 in a trial).
  if rescueLords(L.playSt().chapter) and not ss and emu:read8(u.addr + 0x28 + 4) > 0 and #u.items < 5 then
    local best, bestP
    for _, a in ipairs(blue) do
      if a.id ~= u.id then
        for k, it in ipairs(a.items) do
          local d = L.item(it & 0xFF)
          if HEAL_STAVES[d.id] and canUseStaff(u, d) and not canUseStaff(a, d) then
            for _, t in ipairs(tiles) do
              if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
                local th = threatAt(u, t.x, t.y, enemies)
                if th < u.hp and (best == nil or th < best) then
                  best, bestP = th, { dest = t, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = k - 1 }
                end
              end
            end
          end
        end
      end
    end
    if bestP then return bestP end
  end
  -- A closed door that leads toward the enemy (the distance to them, doors
  -- counted as open, drops through it): stand next to it and open it.
  if canOpenDoor(u) and #enemies > 0 then
    local goals = {}
    -- v5: in a seize chapter only a door on the way to the seize tile is opened.
    -- Eirika's Chapter 14 has nine doors and few keys (the drops of four enemies);
    -- v3 spent them on the side rooms (toward any enemy), and the only way to the
    -- throne, the doors at (17,2) and (18,2), stayed shut (turn 60 in a trial).
    local seizeGoal = tacticsE5(L.playSt().chapter) and seizeTile(L.playSt().chapter)
    if seizeGoal then
      goals[1] = seizeGoal
    else
      for _, e in ipairs(enemies) do
        for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do goals[#goals + 1] = { x = e.x + dv[1], y = e.y + dv[2] } end
      end
    end
    -- (v5: that way ignores the enemies on it, since the throne room's guards
    -- fill its only entrance, and goes through the closed rooms, roofs counted
    -- as floor: with either as walls the seize tile is unreachable)
    local D = distanceField(u, goals, seizeGoal and {} or enemies, true, seizeGoal and true)
    local w, h = L.mapSize()
    local best, bestP
    for _, t in ipairs(tiles) do
      if ok(t) and D[t.y][t.x] then
        for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          local nx, ny = t.x + dv[1], t.y + dv[2]
          if nx >= 0 and ny >= 0 and nx < w and ny < h and L.mapGet(L.ADDR.gBmMapTerrain, nx, ny) == TERRAIN_DOOR
              and D[ny][nx] and D[ny][nx] < D[t.y][t.x] then
            local th = threatAt(u, t.x, t.y, enemies)
            if th < u.hp then
              local score = D[ny][nx] * 10 + th
              if best == nil or score < best then best, bestP = score, { dest = t, action = "door", door = { x = nx, y = ny } } end
            end
          end
        end
      end
    end
    if bestP then return bestP end
    if seizeGoal then log("door: key holder %02x at (%d,%d), distance to the seize tile %s, no door on the way in reach",
      u.id, u.x, u.y, tostring(D[u.y] and D[u.y][u.x])) end
  end
  return nil
end

-- v3: someone in the army can open doors; a tile next to a closed door.
local function armyHasKey()
  for _, b in ipairs(L.blue()) do if canOpenDoor(b) then return true end end
  return false
end
local function nextToDoor(t)
  local w, h = L.mapSize()
  for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
    local nx, ny = t.x + dv[1], t.y + dv[2]
    if nx >= 0 and ny >= 0 and nx < w and ny < h and L.mapGet(L.ADDR.gBmMapTerrain, nx, ny) == TERRAIN_DOOR then return true end
  end
  return false
end

-- v10: the strike. In a seize chapter whose seize tile an enemy holds (the
-- holder: Darkling Woods, Morva on (11,11)), when the units yet to move can
-- kill the holder this turn (exact damage, every hit landing, each one
-- outliving the holder's counter) and the seizer can then reach the tile, they
-- attack it whatever the threat afterwards, and the seizer, moving last,
-- seizes: the chapter ends before any enemy phase. A strike that misses leaves
-- the holder alive, and the seizer then keeps to its usual safety.
local function holderOf(ch, enemies)
  -- (I11: the final chapter's boss, a Defeat Boss holder with no seize tile; his fall ends
  -- the part at once, so a strike needs no seizer)
  if I11.on(ch) then
    for _, e in ipairs(enemies) do if e.char == I11.BOSS[ch] then return e, nil end end
    return nil
  end
  local s = seizeTile(ch)
  if not s then return nil end
  for _, e in ipairs(enemies) do if e.x == s.x and e.y == s.y then return e, s end end
end
local function seizerUnit(blue)
  for _, b in ipairs(blue) do if canSeize(b) then return b end end
end
-- The best attack of unit u on h from one of `tiles` (not in `used`): { t, slot, dealt }.
local function bestHitOn(u, h, tiles, used)
  local best
  local old = EXACT
  EXACT = true
  for k, it in ipairs(u.items) do
    local w = L.item(it & 0xFF)
    if canUseWeapon(u, w) then
      for _, t in ipairs(tiles) do
        local d = dist(t.x, t.y, h.x, h.y)
        if d >= w.minRange and d <= w.maxRange and not (used and used[t.y * 256 + t.x]) then
          local mine = {}
          for kk, v in pairs(u) do mine[kk] = v end
          mine.weapon, mine.x, mine.y = w, t.x, t.y
          local uh, eh = sim(mine, h, d)
          local dealt = h.hp - math.max(eh, 0)
          if uh > 0 and dealt > 0 and (best == nil or dealt > best.dealt or (dealt == best.dealt and uh > best.uh)) then
            best = { t = t, slot = k - 1, dealt = dealt, uh = uh }
          end
        end
      end
    end
  end
  EXACT = old
  return best
end
-- Is a strike on for unit u (about to move, its own tiles `tiles`)? Returns u's hit or nil.
local function strikeFor(u, tiles, enemies, ch)
  local h, s = holderOf(ch, enemies)
  if not h then return nil end
  local blue = equipAll(L.blue())
  local sz
  if s then   -- (I11: no seize tile, no seizer to wait for)
    sz = seizerUnit(blue)
    if not sz or sz.id == u.id or (sz.state & (L.US.UNSELECTABLE | L.US.HAS_MOVED)) ~= 0 then return nil end
    local reach = false
    for _, t in ipairs(moveTiles(sz, h)) do if t.x == s.x and t.y == s.y then reach = true end end
    if not reach then return nil end
  end
  local mine = bestHitOn(u, h, tiles)
  if not mine then log("strike check: char %02x has no hit on %02x", u.char, h.char); return nil end
  local total, used = mine.dealt, { [mine.t.y * 256 + mine.t.x] = true }
  for _, b in ipairs(blue) do
    if b.id ~= u.id and (not sz or b.id ~= sz.id) and (b.state & (L.US.UNSELECTABLE | L.US.HAS_MOVED)) == 0 and total < h.hp then
      local bh = bestHitOn(b, h, moveTiles(b), used)
      if bh then total = total + bh.dealt; used[bh.t.y * 256 + bh.t.x] = true end
    end
  end
  if total < h.hp then log("strike check: char %02x, %d of %d (hp %d)", u.char, mine.dealt, total, h.hp); return nil end
  log("strike: char %02x on %02x (hp %d) from (%d,%d), %d of %d by the units yet to move", u.char, h.char, h.hp, mine.t.x, mine.t.y, mine.dealt, total)
  mine.target = h
  return mine
end

-- v10: the staging tiles: the tiles from which the seizer reaches the seize
-- tile in one move (its distance field to it, the holder's tile counted free),
-- as { [y * 256 + x] = cost }. The bait waits for the seizer to stand on one
-- (or reach one this turn) without a deadly threat, so the holder's fall is
-- followed by the seize before more enemies come (Mogalls load on the free
-- tile, src/events/ch20b-eventscript.h).
local function stagingTiles(sz, seize, enemies)
  local D = distanceField(sz, { seize }, enemies, false)
  local set = {}
  local w, h = L.mapSize()
  for y = 0, h - 1 do for x = 0, w - 1 do
    if D[y][x] and D[y][x] <= sz.mov and not (x == seize.x and y == seize.y) then set[y * 256 + x] = D[y][x] end
  end end
  return set
end

-- E5: with Seth dead (Eirika's Chapter 12), the lord keeps by the army's
-- toughest fighter instead: the most max HP + 2 x defence among the blue units
-- that can fight and are not hurt. Without it Eirika (level 1, 16 HP) stayed in
-- Chapter 14's starting sand alone with the healers, and the turn-6
-- reinforcements there (three cavaliers at (24,26)) killed her on turn 7.
local function anchorUnit(u)
  local best, bestS
  -- (v6, chapter index 20 on: not a unit whose every weapon is kept for the seize tile's
  -- boss, the "rusher" of think, which walks off to it)
  local sb = SACRED2 and seizeBoss(L.playSt().chapter) or nil
  for _, b in ipairs(equipAll(L.blue())) do
    if b.id ~= u.id and not isLordUnit(b) and b.weapon and b.hp * 10 >= b.maxHp * 4
        and not (sb and otherWeapon(b, sb) == nil) then
      local s = b.maxHp + 2 * b.def
      if bestS == nil or s > bestS then best, bestS = b, s end
    end
  end
  return best
end

-- Feature routes (L.F above): the moves. And the post-game's cracked walls (L.F.WALLS): Tower of
-- Valni floor 6 (index 0x29) splits its rooms with cracked walls (TERRAIN_WALL_DAMAGED 0x1B, a
-- TRAP_OBSTACLE with HP: Attack lists it, TryAddTrapsToTargetList, src/bmtarget.c); the enemies
-- left behind them (AI2 3: they wait until someone comes into reach) never come out, and the
-- army waited from turn 13 to 60 (a try in c10-e4, bpg1). A unit that has no way to any enemy
-- breaks the wall nearest to them on the way (a distance with the walls passable, WALL_COST).
-- On the floors no route before coverage 11 plays: Tower 6 to 8, Ruins 2 to 10.
for ch = 0x29, 0x2B do L.F.WALLS[ch] = true end
for ch = 0x2F, 0x37 do L.F.WALLS[ch] = true end
L.F.WALL_COST = 6
function L.F.supportExp(u, cb)   -- unit u's support points with character cb (struct SupportData, bmreliance.h)
  local sd = emu:read32(emu:read32(u.addr) + 0x2C)
  if sd == 0 then return nil end
  for k = 0, emu:read8(sd + 0x15) - 1 do
    if emu:read8(sd + k) == cb then return emu:read8(u.addr + 0x32 + k) end
  end
end
function L.F.supportReady(u, cb)   -- CanUnitSupportNow's points test: at the top of their level (80, 160, 240 is A)
  local e = L.F.supportExp(u, cb)
  if not e or e >= 240 then return false end
  local lvl = (e > 160 and 2) or (e > 80 and 1) or 0
  return e == ({ [0] = 80, [1] = 160, [2] = 240 })[lvl]
end
-- (coverage 12) a support level gained with character cb this chapter already (struct Unit
-- supportBits +0x39, HasUnitGainedSupportLevel, src/bmreliance.c)
function L.F.suppGained(u, cb)
  local sd = emu:read32(emu:read32(u.addr) + 0x2C)
  if sd == 0 then return true end
  for k = 0, emu:read8(sd + 0x15) - 1 do
    if emu:read8(sd + k) == cb then return (emu:read8(u.addr + 0x39) & (1 << k)) ~= 0 end
  end
  return true
end
-- (coverage 12) the chapter's location events (GetChapterEventDataPointer: gChapterDataTable 0x088B0890,
-- 0x94 a chapter, mapEventDataId +0x74; gChapterDataAssetTable 0x088B363C; struct ChapterEventGroup
-- locationBasedEvents +0x08; 12 bytes an entry: kind (EVT_LIST_CMD_*), flag, script, x, y, tile command)
function L.F.locEvents(ch)
  local grp = emu:read32(0x088B363C + 4 * emu:read8(0x088B0890 + 0x94 * ch + 0x74))
  local p, out = emu:read32(grp + 8), {}
  for _ = 1, 100 do
    local k = emu:read8(p)
    if k == 0 then break end
    out[#out + 1] = { kind = k, flag = emu:read16(p + 2), x = emu:read8(p + 8), y = emu:read8(p + 9), tc = emu:read8(p + 10) }
    p = p + 12
  end
  return out
end
-- (coverage 12, ROUTE_VISIT) the open villages with a Village event (kind 6 on an open village,
-- TERRAIN_VILLAGE_REGULAR 0x03) and the houses with a visit event (kind 5, TILE_COMMAND_VISIT 0x10, on
-- TERRAIN_HOUSE 0x05) not visited yet
function L.F.visitGoals(ch)
  local goals, vis = {}, L.F.st.visited or {}
  for _, e in ipairs(L.F.locEvents(ch)) do
    local t = L.mapGet(L.ADDR.gBmMapTerrain, e.x, e.y)
    if not vis[e.x .. "," .. e.y] and ((e.kind == 6 and t == 0x03) or (e.kind == 5 and e.tc == 0x10 and t == 0x05)) then
      goals[#goals + 1] = { x = e.x, y = e.y, kind = e.kind == 6 and "village" or "house" }
    end
  end
  return goals
end
-- distanceField's search with the cracked walls passable (cost L.F.WALL_COST): dist[y][x]
function L.F.wallField(u, goals, enemies)
  local w, h = L.mapSize()
  local costTable = emu:read32(u.classPtr + 0x38)
  local block = {}
  for _, e in ipairs(enemies) do block[e.y * 256 + e.x] = true end
  for _, g in ipairs(goals) do block[g.y * 256 + g.x] = nil end
  local function cost(x, y)
    if block[y * 256 + x] then return nil end
    local t = L.mapGet(L.ADDR.gBmMapTerrain, x, y)
    if t == 0x1B then return L.F.WALL_COST end
    local c = L.s8(emu:read8(costTable + t))
    if c < 0 then return nil end
    return c
  end
  local D, open = {}, {}
  for y = 0, h - 1 do D[y] = {} end
  for _, g in ipairs(goals) do D[g.y][g.x] = 0; open[#open + 1] = { g.x, g.y } end
  local head = 1
  while head <= #open do
    local cx, cy = open[head][1], open[head][2]; head = head + 1
    local c = cost(cx, cy)
    if c then
      for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local nx, ny = cx + dv[1], cy + dv[2]
        if nx >= 0 and ny >= 0 and nx < w and ny < h and (D[ny][nx] == nil or D[cy][cx] + c < D[ny][nx]) then
          D[ny][nx] = D[cy][cx] + c; open[#open + 1] = { nx, ny }
        end
      end
    end
  end
  return D
end
-- A plan for unit u from the feature rules (or the walls rule), or nil for the usual rules.
function L.F.think(u, tiles, enemies, ok, ps)
  local F, st = L.F, L.F.st
  local function safest(scoreOf)   -- the ok tile of `tiles` with the lowest score (nil: not a candidate)
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) then
        local s = scoreOf(t)
        if s and (best == nil or s < best) then best, bestT = s, t end
      end
    end
    return bestT
  end
  -- ROUTE_ARENA (r9-uiarena's rule): to the arena tile, and Arena there
  local a = F.arena
  if a and not st.arenaSeen and u.char == a.char then
    for _, t in ipairs(tiles) do
      if t.x == a.x and t.y == a.y then return { dest = t, action = "arena" } end
    end
    local D = distanceField(u, { { x = a.x, y = a.y } }, enemies, false)
    local bestT = safest(function(t)
      local th = threatAt(u, t.x, t.y, enemies)
      return ((D[t.y][t.x]) or 999) * 10 + ((th >= u.hp) and 5000 or th)
    end)
    return { dest = bestT or { x = u.x, y = u.y }, action = "wait" }
  end
  -- ROUTE_SUPPORT: next to the partner (the safest such tile it outlives), Support once it is ready
  local sp = F.supp
  if sp and not st.supportAt and u.char == sp.a then
    local b
    for _, x in ipairs(L.blue()) do if x.char == sp.b then b = x end end
    if b then
      local ready = F.supportReady(u, sp.b)
      local bestT = safest(function(t)
        if dist(t.x, t.y, b.x, b.y) ~= 1 then return nil end
        local th = threatAt(u, t.x, t.y, enemies)
        if th >= u.hp then return nil end
        return th
      end)
      if bestT then
        log("support: char %02x to (%d,%d) next to char %02x, points %s%s", u.char, bestT.x, bestT.y, b.char,
          tostring(F.supportExp(u, sp.b)), ready and " (a level is ready)" or "")
        if ready then return { dest = bestT, action = "support", target = b.id, targetPos = { x = b.x, y = b.y } } end
        return { dest = bestT, action = "wait" }
      end
    end
  end
  -- ROUTE_CHEST: to the nearest closed chest, Chest on it
  local c = F.chest
  if c and not st.chestAt and u.char == c.char then
    local w, h = L.mapSize()
    local goals = {}
    for y = 0, h - 1 do for x = 0, w - 1 do
      if L.mapGet(L.ADDR.gBmMapTerrain, x, y) == 0x21 then goals[#goals + 1] = { x = x, y = y } end
    end end
    if #goals > 0 then
      for _, t in ipairs(tiles) do
        if ok(t) and L.mapGet(L.ADDR.gBmMapTerrain, t.x, t.y) == 0x21 and threatAt(u, t.x, t.y, enemies) < u.hp then
          log("chest: char %02x opens the chest at (%d,%d)", u.char, t.x, t.y)
          return { dest = t, action = "chest" }
        end
      end
      local D = distanceField(u, goals, enemies, false)
      local bestT = safest(function(t)
        local th = threatAt(u, t.x, t.y, enemies)
        return ((D[t.y][t.x]) or 999) * 10 + ((th >= u.hp) and 5000 or th)
      end)
      return { dest = bestT or { x = u.x, y = u.y }, action = "wait" }
    end
  end
  -- (coverage 12) ROUTE_USEITEM: the item, once, where the unit stands (Item, Use; plan.promote)
  if F.useItem then
    for _, ui in ipairs(F.useItem) do
      if u.char == ui.char and not (st.usedItem and st.usedItem[ui.char]) then
        for k, it in ipairs(u.items) do
          if (it & 0xFF) == ui.item then
            st.usedItem = st.usedItem or {}
            st.usedItem[ui.char] = tick
            log("useitem: char %02x uses item %02x (slot %d)", u.char, ui.item, k - 1)
            return { dest = { x = u.x, y = u.y }, action = "item", slot = k - 1, promote = true }
          end
        end
      end
    end
  end
  -- (coverage 12) ROUTE_TALKRUSH: the recruiter at its target by the shortest way, Talk once next to it
  local tr = F.talkRush
  if tr and u.char == tr.by then
    local tgt
    for _, e in ipairs(L.red()) do if e.char == tr.target then tgt = e end end
    if not tgt then for _, g in ipairs(L.green()) do if g.char == tr.target then tgt = g end end end
    if tgt then
      local best, bestT
      for _, t in ipairs(tiles) do
        if ok(t) and dist(t.x, t.y, tgt.x, tgt.y) == 1 then
          local th = threatAt(u, t.x, t.y, enemies, tgt)
          if best == nil or th < best then best, bestT = th, t end
        end
      end
      if bestT then
        log("talkrush: char %02x talks to char %02x at (%d,%d) from (%d,%d), threat %d", u.char, tr.target, tgt.x, tgt.y, bestT.x, bestT.y, best)
        return { dest = bestT, action = "talk", target = tgt.id, targetPos = { x = tgt.x, y = tgt.y } }
      end
      local D = distanceField(u, { { x = tgt.x, y = tgt.y } }, enemies, false)
      local bestW = safest(function(t)
        local d = D[t.y][t.x]
        return d and (d * 100 + math.min(threatAt(u, t.x, t.y, enemies), 99))
      end)
      if bestW then log("talkrush: char %02x to (%d,%d), char %02x at (%d,%d)", u.char, bestW.x, bestW.y, tr.target, tgt.x, tgt.y) end
      return { dest = bestW or { x = u.x, y = u.y }, action = "wait" }
    end
  end
  -- (coverage 12) ROUTE_SUPPORTS: next to the partner each turn; Support once a level is ready
  if F.supps and F.suppLast[u.char] then
    for _, pr in ipairs(F.supps) do
      if pr.a == u.char then
        local b
        for _, x in ipairs(L.blue()) do if x.char == pr.b and (emu:read32(x.addr + 0x0C) & 0x25) == 0 then b = x end end
        if b then
          -- (CanUnitSupportNow's exp test: at the top of the level, 80, 160, or 240 for A; supportReady
          -- above stops before A)
          local e = F.supportExp(u, pr.b)
          local ready = (e == 80 or e == 160 or e == 240) and not F.suppGained(u, pr.b)
          local bestT = safest(function(t)
            if dist(t.x, t.y, b.x, b.y) ~= 1 then return nil end
            local th = threatAt(u, t.x, t.y, enemies)
            if th >= u.hp then return nil end
            return th
          end)
          if bestT then
            log("supports: char %02x to (%d,%d) next to char %02x, points %s%s", u.char, bestT.x, bestT.y, b.char,
              tostring(F.supportExp(u, pr.b)), ready and " (a level is ready)" or "")
            if ready then return { dest = bestT, action = "supports", target = b.id, targetPos = { x = b.x, y = b.y } } end
            return { dest = bestT, action = "wait" }
          end
        end
      end
    end
  end
  -- (coverage 12) ROUTE_VISIT: to the nearest open village or house with a visit event, Visit there
  if F.visit and F.visit[u.char] then
    local goals = F.visitGoals(ps.chapter)
    if #goals > 0 then
      for _, t in ipairs(tiles) do
        for _, g in ipairs(goals) do
          if ok(t) and t.x == g.x and t.y == g.y and threatAt(u, t.x, t.y, enemies) < u.hp then
            st.visited = st.visited or {}
            st.visited[g.x .. "," .. g.y] = tick
            st.visitLast = tick
            log("visit: char %02x visits (%d,%d) (%s)", u.char, g.x, g.y, g.kind)
            return { dest = t, action = "visit" }
          end
        end
      end
      local D = distanceField(u, goals, enemies, false)
      local bestT = safest(function(t)
        local th = threatAt(u, t.x, t.y, enemies)
        return ((D[t.y][t.x]) or 999) * 10 + ((th >= u.hp) and 5000 or th)
      end)
      return { dest = bestT or { x = u.x, y = u.y }, action = "wait" }
    end
  end
  -- (coverage 12) ROUTE_DOOR: next to the nearest closed door, Door
  local dr = F.door
  if dr and not st.doorAt and u.char == dr.char then
    local w, h = L.mapSize()
    local goals = {}
    for y = 0, h - 1 do for x = 0, w - 1 do
      if L.mapGet(L.ADDR.gBmMapTerrain, x, y) == TERRAIN_DOOR then
        for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          local nx, ny = x + dv[1], y + dv[2]
          if nx >= 0 and ny >= 0 and nx < w and ny < h and L.mapGet(L.ADDR.gBmMapTerrain, nx, ny) ~= TERRAIN_DOOR then
            goals[#goals + 1] = { x = nx, y = ny, door = { x = x, y = y } }
          end
        end
      end
    end end
    if #goals > 0 then
      for _, t in ipairs(tiles) do
        for _, g in ipairs(goals) do
          if ok(t) and t.x == g.x and t.y == g.y and threatAt(u, t.x, t.y, enemies) < u.hp then
            log("door: char %02x opens the door at (%d,%d) from (%d,%d)", u.char, g.door.x, g.door.y, t.x, t.y)
            return { dest = t, action = "door", door = g.door }
          end
        end
      end
      local D = distanceField(u, goals, enemies, false)
      local bestT = safest(function(t)
        local th = threatAt(u, t.x, t.y, enemies)
        return ((D[t.y][t.x]) or 999) * 10 + ((th >= u.hp) and 5000 or th)
      end)
      return { dest = bestT or { x = u.x, y = u.y }, action = "wait" }
    end
  end
  -- (coverage 12) ROUTE_SHOP: onto the map shop's tile, its command
  local sh = F.shop
  if sh and not st.shopAt and u.char == sh.char then
    local menuId
    for _, e in ipairs(F.locEvents(ps.chapter)) do
      if e.kind == 10 and e.x == sh.x and e.y == sh.y then menuId = ({ [0x16] = 0x5F, [0x17] = 0x60, [0x18] = 0x61 })[e.tc] end
    end
    if menuId then
      for _, t in ipairs(tiles) do
        if ok(t) and t.x == sh.x and t.y == sh.y and threatAt(u, t.x, t.y, enemies) < u.hp then
          log("shop: char %02x to the shop at (%d,%d) (menu item %02x)", u.char, sh.x, sh.y, menuId)
          return { dest = t, action = "mapshop", menuId = menuId }
        end
      end
      local D = distanceField(u, { { x = sh.x, y = sh.y } }, enemies, false)
      local bestT = safest(function(t)
        local th = threatAt(u, t.x, t.y, enemies)
        return ((D[t.y][t.x]) or 999) * 10 + ((th >= u.hp) and 5000 or th)
      end)
      return { dest = bestT or { x = u.x, y = u.y }, action = "wait" }
    end
  end
  -- the cracked walls (L.F.WALLS): only for a unit with no way to any enemy
  if F.WALLS[ps.chapter] and #enemies > 0 and u.weapon and not (L.PR.back(ps.chapter, u) and isLordUnit(u)) then   -- (PR: L.PR.BACK)
    local goals = {}
    for _, e in ipairs(enemies) do goals[#goals + 1] = { x = e.x, y = e.y } end
    local D = distanceField(u, goals, enemies, false)
    if D[u.y][u.x] == nil then
      local D2 = F.wallField(u, goals, enemies)
      local w, h = L.mapSize()
      local walls = {}
      for y = 0, h - 1 do for x = 0, w - 1 do
        if L.mapGet(L.ADDR.gBmMapTerrain, x, y) == 0x1B and D2[y][x] then walls[#walls + 1] = { x = x, y = y, d = D2[y][x] } end
      end end
      local lo, hi = weaponRanges(u)
      local best, bestT, bestW
      for _, t in ipairs(tiles) do
        if ok(t) and lo then
          local th = threatAt(u, t.x, t.y, enemies)
          for _, wt in ipairs(walls) do
            local d = dist(t.x, t.y, wt.x, wt.y)
            if d >= lo and d <= hi and th < u.hp then
              local s = wt.d * 100 + th
              if best == nil or s < best then best, bestT, bestW = s, t, wt end
            end
          end
        end
      end
      if bestT then
        log("walls: char %02x breaks the wall at (%d,%d) from (%d,%d) (its distance to the enemy %d)", u.char, bestW.x, bestW.y, bestT.x, bestT.y, bestW.d)
        return { dest = bestT, action = "attack", wallAt = { x = bestW.x, y = bestW.y }, targetPos = { x = bestW.x, y = bestW.y } }
      end
      -- none in reach this turn: toward the tiles next to the walls on the way
      local adj = {}
      for _, wt in ipairs(walls) do
        for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do adj[#adj + 1] = { x = wt.x + dv[1], y = wt.y + dv[2] } end
      end
      if #adj > 0 then
        local D3 = distanceField(u, adj, enemies, false)
        local bestM = safest(function(t)
          if not D3[t.y] or D3[t.y][t.x] == nil then return nil end
          local th = threatAt(u, t.x, t.y, enemies)
          return D3[t.y][t.x] * 10 + ((th >= u.hp) and 5000 or th)
        end)
        if bestM then log("walls: char %02x walks to (%d,%d), toward a wall", u.char, bestM.x, bestM.y); return { dest = bestM, action = "wait" } end
      end
    end
  end
  return nil
end
-- (the unit menu's command of a feature action was taken)
function L.F.chosen(action)
  L.F.st[action .. "At"] = tick
  log("%s: the unit menu's command taken", action)
end

-- I11: the bait's heal. Unit u (not a lord, below its max HP, its first usable weapon the one
-- that hurts the holder most) uses its heal item (an Elixir first) on a tile of the holder's
-- reach from which, healed, it outlives the holder's attack and every other threat there, and
-- counters (v10's bait reckoning, exact for the holder). No other fighter may be bait already.
-- Returns the plan (Item, the heal item, on that tile) or nil.
function I11.supplyHasHeal()
  for _, id in ipairs(convoyItems()) do if id == 0x6C or id == 0x6D then return true end end
  return false
end
-- I11, part 2: the lure. The monsters the Demon King summoned that stay by him (AttackInRange:
-- an Arch Mogall, a Draco Zombie) attack what comes into their reach; anything that strikes them
-- next to him stands in his reach too, so nobody did, and a first try stood still from turn 5 to
-- 17. A healthy fighter (not a lord) with no attack to make waits on a tile out of his reach and
-- in theirs, where it outlives every enemy that can reach it (the threat with its margins), and
-- counters there. Returns the plan or nil.
function I11.lure(u, okTiles, enemies, holder)
  if not reachSets or not reachSets[holder] then return nil end
  local best, bestT, bestTh, bestN
  for _, t in ipairs(okTiles) do
    local k = t.y * 256 + t.x
    if not reachSets[holder][k] then
      local n = 0
      for _, e in ipairs(enemies) do
        if e ~= holder and reachSets[e] and reachSets[e][k] then n = n + 1 end
      end
      if n > 0 then
        local th = threatAt(u, t.x, t.y, enemies)
        if th < u.hp and (best == nil or n * 100 - th > best) then best, bestT, bestTh, bestN = n * 100 - th, t, th, n end
      end
    end
  end
  if not bestT then return nil end
  log("lure: char %02x waits at (%d,%d) in the reach of %d (threat %d), out of %02x's", u.char, bestT.x, bestT.y, bestN, bestTh, holder.char)
  return { dest = bestT, action = "wait", th = bestTh }
end
-- I11, part 2: Ephraim may be the Demon King's bait (his Siegmund, 24 a hit to him, is the
-- army's one weapon left once Audhulma is spent on Lyon; the king's 37 leave him 2 of 39 HP)
function I11.lordBait(ch, u) return ch == 0x23 and u.char == L.CHAR.EPHRAIM end
function I11.baitHeal(u, okTiles, enemies, holder, lordBait)
  for _, b in ipairs(L.blue()) do
    if b.id ~= u.id and reachSets[holder][b.y * 256 + b.x] and (not isLordUnit(b) or lordBait) then return nil end
  end
  local hs, healed
  for k, it in ipairs(u.items) do
    if (it & 0xFF) == 0x6D then hs, healed = k - 1, u.maxHp; break end
    if (it & 0xFF) == 0x6C and not hs then hs, healed = k - 1, math.min(u.maxHp, u.hp + 10) end
  end
  if not hs then return nil end
  local eq, best
  for _, it in ipairs(u.items) do
    local w = L.item(it & 0xFF)
    if canUseWeapon(u, w) then
      if not eq then eq = w end
      local mine = {}
      for kk, v in pairs(u) do mine[kk] = v end
      mine.weapon = w
      local s = (strike(mine, holder) or 0) * nhits(mine, holder) * perStrike(mine)
      if best == nil or s > best then best = s end
    end
  end
  if not eq then return nil end
  local mine0 = {}
  for kk, v in pairs(u) do mine0[kk] = v end
  mine0.weapon = eq
  if (strike(mine0, holder) or 0) * nhits(mine0, holder) * perStrike(mine0) < best or best <= 0 then return nil end
  local bestS, bestT, bestL
  for _, t in ipairs(okTiles) do
    local d = dist(t.x, t.y, holder.x, holder.y)
    if reachSets[holder][t.y * 256 + t.x] and d >= eq.minRange and d <= eq.maxRange then
      local mine = {}
      for kk, v in pairs(mine0) do mine[kk] = v end
      mine.hp, mine.x, mine.y = healed, t.x, t.y
      local hh, mh = sim(holder, mine, d)
      local left = mh - threatAt(mine, t.x, t.y, enemies, holder)
      local dealt = holder.hp - math.max(hh, 0)
      if left > 0 and dealt > 0 and (bestS == nil or dealt * 10 + left > bestS) then bestS, bestT, bestL = dealt * 10 + left, t, left end
    end
  end
  if not bestT then return nil end
  log("bait heal: char %02x heals (item slot %d, to %d HP) at (%d,%d) for %02x, %d HP left at worst", u.char, hs, healed,
    bestT.x, bestT.y, holder.char, bestL)
  return { dest = bestT, action = "item", slot = hs }
end

-- E7: the uses left of unit u's weapons when every one it can use is a dragonstone (Myrrh's
-- Divinestone: 24 uses for Morva, Lyon and the Demon King, and nothing can replace it), else nil.
-- Past Morva she clears the way only while more than L.E7.KEEP are left (Lyon takes about 4
-- hits, the Demon King 3); she fights bosses at any count.
L.E7.KEEP = 12
-- The final chapter's bosses (Lyon on his throne in part 1, CHARACTER_LYON_FINAL; the Demon King,
-- CHARACTER_FOMORTIIS, in part 2): Myrrh attacks them from full HP where she outlives the fight
-- and one more attack of theirs; Ephraim leads the way to them.
L.E7.BOSS = { [0x15] = 0x6C, [0x16] = 0xBE }
function L.E7.stoneUses(u)
  local n, any = 0, false
  for _, it in ipairs(u.items) do
    local d = L.item(it & 0xFF)
    if canUseWeapon(u, d) then
      if d.weaponType ~= 11 then return nil end
      any = true; n = n + (it >> 8)
    end
  end
  return any and n or nil
end

-- E7: the way ahead of Eirika (her carrier, or herself on the ground): her distance field to
-- the seize tile (L.E7.field), her distance dc0 now, the "front" (the tiles 0 to 12 points
-- ahead of her on that field) and `near` (every tile within 2 of the front), and the escorts'
-- goals (the tiles 4 to 9 points ahead, or the tiles round the seize tile's open side once
-- she is within 8). Nil when she is not on the map.
function L.E7.front(seize)
  if not seize then return nil end
  local c
  for _, b in ipairs(L.blue()) do
    if b.char == L.CHAR.EIRIKA or carriesLord(b) then c = b end
  end
  if not c then return nil end
  local D = L.E7.field(c, seize)
  local dc0 = D[c.y] and D[c.y][c.x]
  if not dc0 then return nil end
  local W, H = L.mapSize()
  local near, goals, front = {}, {}, {}
  -- (the escorts' goals: 3 to 9 points past the most advanced of Eirika and the escorts that
  -- fight with plain weapons (Ephraim), at most 16 past Eirika; with goals 4 to 9 past Eirika
  -- alone, she waited for Ephraim to go ahead and he for her: turns 6 to 35 of a try)
  local anchor = dc0
  for _, b in ipairs(L.blue()) do
    local db = b.id ~= c.id and D[b.y] and D[b.y][b.x]
    if db and isFighter(b) and L.E7.stoneUses(b) == nil and db < anchor then anchor = db end
  end
  local lo = math.max(anchor - 9, dc0 - 16)
  local hi = math.max(anchor - 3, lo + 4)
  for y = 0, H - 1 do for x = 0, W - 1 do
    local d = D[y][x]
    if d and d <= dc0 and d >= dc0 - 12 and L.E7.way(x, y) then
      front[#front + 1] = { x = x, y = y }
      for dy = -2, 2 do for dx = -2, 2 do
        if math.abs(dx) + math.abs(dy) <= 2 then near[(y + dy) * 256 + (x + dx)] = true end
      end end
    end
    -- (near the end: 2 to 4 from the open side, and out of the seize tile's holder's reach,
    -- 3 tiles or more from it: Morva's Wretched Air reaches 2 and takes 36)
    if d and ((dc0 > 8 and d <= hi and d >= lo and d >= 2)
              or (dc0 <= 8 and d >= 2 and d <= 4 and dist(x, y, seize.x, seize.y) >= 3)) and L.E7.way(x, y) then
      goals[#goals + 1] = { x = x, y = y }
    end
  end end
  return { c = c, D = D, dc0 = dc0, near = near, goals = goals, front = front }
end

---------------------------------------------------------------------------
-- E7, the final chapter (Eirika's indices 0x15, Sacred Stone part 1, and 0x16, part 2; only with
-- ROUTE_E7=1, so no older route reaches it): the director (README-routes.md, "Tactics E7: the
-- final chapter"). Both parts are won by the boss's death (DefeatBoss: Lyon, character 0x6C, on
-- (11,2); the Demon King, 0xBE), and the army is four: Eirika (level 1, 20 HP), carried by Franz
-- the whole way (a carried unit cannot be attacked); Ephraim (Siegmund, and the supply's Spear);
-- Myrrh, whose Divinestone (8 uses, nothing replaces it) is the one weapon that hurts Lyon much.
-- Lyon (73 HP, def 26, attack speed 4) takes 22 a hit from her, twice, and 16 twice from
-- Siegmund: both strike him in one player phase (the strike, L.E7.strikePlan), Myrrh first;
-- their hit rates are 100, so he dies before any enemy phase, and the two Stone Gorgons beside
-- him never act. Until then the director plays by chances, not worst cases: each enemy's real
-- hit rate (FE8's 2RN, Roll2RN) and damage over the fight's strikes (L.E7.fight) give each
-- tile's chance of death in the enemy phase (L.E7.risk); the turn's search plays the turn again
-- when a unit dies anyway. In the bot's earlier tries the worst-case threat (every hit lands)
-- kept Ephraim off every tile near the throne room (two Shadowshots, 19 each at 8%), and the
-- army stood still from turn 21 to 60 with 17 enemies left.
-- (In L: the main chunk is at Lua's 200-local limit.)

-- P2[h]: the chance of a hit at the displayed rate h: (rn1 + rn2) / 2 < h (Roll2RN, src/random.c)
L.E7.P2 = (function()
  local t = {}
  for h = 0, 100 do
    local n = 0
    for a = 0, 99 do n = n + math.min(100, math.max(0, 2 * h - a)) end
    t[h] = n / 10000
  end
  return t
end)()
L.E7.STONE = 0xB5          -- ITEM_MONSTER_STONE (weapon effect 5, petrify)
L.E7.CA_CRITBONUS = 1 << 6 -- include/bmunit.h
-- Siegmund's uses Ephraim keeps for the bosses (his own attacks on the others stop there)
L.E7.KEEP_SIEG = 4

-- The stat bonuses of weapon w (ItemData +0x0C, struct ItemStatBonuses: hp, pow, skl, spd, def,
-- res, lck, mov, con; GetUnitPower and the rest add the equipped weapon's).
function L.E7.bonus(w)
  local z = { hp = 0, pow = 0, skl = 0, spd = 0, def = 0, res = 0, lck = 0, con = 0 }
  if not w then return z end
  local p = emu:read32(L.ADDR.gItemData + w.id * 0x24 + 0x0C)
  if p == 0 then return z end
  z.hp, z.pow, z.skl, z.spd = L.s8(emu:read8(p)), L.s8(emu:read8(p + 1)), L.s8(emu:read8(p + 2)), L.s8(emu:read8(p + 3))
  z.def, z.res, z.lck, z.con = L.s8(emu:read8(p + 4)), L.s8(emu:read8(p + 5)), L.s8(emu:read8(p + 6)), L.s8(emu:read8(p + 8))
  return z
end
-- The weapon triangle (BattleApplyWeaponTriangleEffect, src/bmbattle.c): +1 attacker's side,
-- a reaver weapon (IA_REVERTTRIANGLE) reverses it and doubles it.
function L.E7.tri(wa, wb)
  if not wa or not wb then return 0 end
  local t = triangle(wa.weaponType, wb.weaponType)
  if (wa.attributes & IA_REVERTTRIANGLE) ~= 0 then t = -2 * t end
  if (wb.attributes & IA_REVERTTRIANGLE) ~= 0 then t = -2 * t end
  return t
end
-- Unit u's battle numbers with weapon w on (x, y) (InitBattleUnit and ComputeBattleUnit*, src/
-- bmbattle.c; GetUnitSkill and GetUnitSpeed halve skill and speed while it carries a unit).
function L.E7.bu(u, w, x, y)
  local z = L.E7.bonus(w)
  local resc = (u.state & L.US.RESCUING) ~= 0
  local spd = (resc and u.spd // 2 or u.spd) + z.spd
  local ter = L.mapGet(L.ADDR.gBmMapTerrain, x, y)
  local cp = u.classPtr
  return {
    u = u, w = w, x = x, y = y, hp = u.hp,
    pow = u.pow + z.pow, skl = (resc and u.skl // 2 or u.skl) + z.skl, lck = u.lck + z.lck,
    as = math.max(0, spd - math.max(0, (w and w.weight or 0) - u.con)),
    def = u.def + z.def + L.s8(emu:read8(emu:read32(cp + 0x48) + ter)),
    res = u.res + z.res + L.s8(emu:read8(emu:read32(cp + 0x4C) + ter)),
    avo = L.s8(emu:read8(emu:read32(cp + 0x44) + ter)),
    cb = (unitAttr(u) & L.E7.CA_CRITBONUS) ~= 0,
  }
end
-- a's damage a hit on b (ComputeBattleUnitAttack and ComputeBattleUnitDefense: the sacred weapons
-- effective x2, the others x3; IA_NEGATE_DEFENSE leaves the defence out; Stone does none).
function L.E7.dmg(a, b)
  local w = a.w
  if not w then return 0 end
  if w.effect == WPN_EFFECT_PETRIFY then return 0 end
  if w.effect == WPN_EFFECT_HPHALVE then return (b.hp + 1) // 2 end
  local m = w.might + L.E7.tri(w, b.w)
  if effective(w, b.u) then m = m * (SACRED[w.id] and 2 or 3) end
  local atk = a.pow + m
  if (w.attributes & IA_NEGATE_DEFENSE) ~= 0 then return math.max(0, atk) end
  return math.max(0, atk - (((w.attributes & 0x42) ~= 0) and b.res or b.def))
end
function L.E7.hitRate(a, b)
  local w = a.w
  local h = a.skl * 2 + w.hit + a.lck // 2 + 15 * L.E7.tri(w, b.w) - math.max(0, b.as * 2 + b.avo + b.lck)
  return math.max(0, math.min(100, h))
end
function L.E7.critRate(a, b)
  local w = a.w
  if w.id == L.E7.STONE then return 0 end
  for _, it in ipairs(b.u.items) do if (L.item(it & 0xFF).attributes & 0x8000) ~= 0 then return 0 end end   -- IA_NEGATE_CRIT
  return math.max(0, math.min(100, w.crit + a.skl // 2 + (a.cb and 15 or 0) - b.lck))
end
-- The fight of a (attacking) and b at distance d, as a list of outcomes { p, ah, bh, pet (b
-- petrified), petA, sa, sb (each side's hits) } (BattleUnwind: a's round, b's round, then one more
-- round of the side 4 or more faster; a brave weapon strikes twice a round; a weapon with
-- IA_UNCOUNTERABLE allows no counter; a Stone hit ends the fight; every hit rolls 2RN, a crit 1RN).
function L.E7.fight(a, b, d)
  local function can(x) return x.w ~= nil and d >= x.w.minRange and d <= x.w.maxRange end
  local ca = can(a)
  local cb = can(b) and not (a.w and (a.w.attributes & 0x80) ~= 0)
  local seq = {}
  local function round(who)
    local x = (who == 1) and a or b
    for _ = 1, ((x.w.attributes & 0x20) ~= 0) and 2 or 1 do seq[#seq + 1] = who end
  end
  if ca then round(1) end
  if cb then round(2) end
  if ca and a.as - b.as >= 4 then round(1) end
  if cb and b.as - a.as >= 4 then round(2) end
  local ph = { ca and L.E7.P2[L.E7.hitRate(a, b)] or 0, cb and L.E7.P2[L.E7.hitRate(b, a)] or 0 }
  local pc = { ca and L.E7.critRate(a, b) / 100 or 0, cb and L.E7.critRate(b, a) / 100 or 0 }
  local dm = { ca and L.E7.dmg(a, b) or 0, cb and L.E7.dmg(b, a) or 0 }
  local pet = { ca and a.w.effect == WPN_EFFECT_PETRIFY, cb and b.w.effect == WPN_EFFECT_PETRIFY }
  local out = {}
  local function go(i, p, ah, bh, sa, sb)
    if p < 1e-7 then return end
    if i > #seq or ah <= 0 or bh <= 0 then out[#out + 1] = { p = p, ah = ah, bh = bh, sa = sa, sb = sb }; return end
    local who = seq[i]
    local h, c, k = ph[who], pc[who], dm[who]
    if h < 1 then go(i + 1, p * (1 - h), ah, bh, sa, sb) end
    if h > 0 then
      local na, nb = sa + ((who == 1) and 1 or 0), sb + ((who == 2) and 1 or 0)
      if pet[who] then
        out[#out + 1] = { p = p * h, ah = ah, bh = bh, sa = na, sb = nb, pet = (who == 1), petA = (who == 2) }
      else
        for _, cc in ipairs({ { 1 - c, k }, { c, 3 * k } }) do
          if cc[1] > 0 then
            if who == 1 then go(i + 1, p * h * cc[1], ah, bh - cc[2], na, nb)
            else go(i + 1, p * h * cc[1], ah - cc[2], bh, na, nb) end
          end
        end
      end
    end
  end
  go(1, 1, a.hp, b.hp, 0, 0)
  return out
end
-- e's usable weapons ({ w, slot }), uses left or unbreakable.
function L.E7.weapons(e)
  local t = {}
  for k, it in ipairs(e.items) do
    local w = L.item(it & 0xFF)
    if (w.attributes & 1) ~= 0 and (w.attributes & 4) == 0 and ((it >> 8) > 0 or (w.attributes & 8) ~= 0) and canUseWeapon(e, w) then
      t[#t + 1] = { w = w, slot = k - 1, uses = it >> 8 }
    end
  end
  return t
end
-- The enemy phase on unit u standing on (x, y) with hp HP (default its own) and its weapon w
-- (default u.weapon) equipped for the counters: every enemy whose reach (reachSets, think) covers
-- the tile (`skip`: a set of enemies left out) attacks it, with the weapon and from the distance
-- its AI would like best (the most damage to u, a kill or Stone above all, less half of the
-- counter's), each fight independent. Returns { pDeath, pStone, eDmg, uses (u's expected hits in
-- counters: Myrrh's Divinestone uses), n (the attackers), big (the attackers that hurt u) }.
function L.E7.risk(u, x, y, enemies, skip, hp, w)
  hp = hp or u.hp
  local me = L.E7.bu(u, w or u.weapon, x, y)
  me.hp = hp
  local dist0 = { [0] = 1 }
  local r = { pDeath = 0, pStone = 0, eDmg = 0, uses = 0, n = 0, big = 0 }
  local noStone = 1
  local key0 = string.format("%d:%d:%d:%d:%d", u.id, L.mapGet(L.ADDR.gBmMapTerrain, x, y), hp, me.w and me.w.id or 0, me.as)
  L.E7.cache = L.E7.cache or {}
  for _, e in ipairs(enemies) do
    local k = y * 256 + x
    if not (skip and skip[e]) and reachSets and reachSets[e] and reachSets[e][k] then
      local ck = key0 .. ":" .. e.id .. ":" .. e.hp .. ":" .. dist(e.x, e.y, x, y)
      local best = L.E7.cache[ck]
      if best == nil then
        local a1 = emu:read8(e.addr + 0x42)
        local standing = a1 >= 3 and a1 <= 5
        local bestV
        for _, wc in ipairs(L.E7.weapons(e)) do
          local lo, hi = wc.w.minRange, math.min(wc.w.maxRange, 10)
          local ds = standing and { dist(e.x, e.y, x, y) } or { lo, math.min(hi, math.max(lo, 2)) }
          for _, d in ipairs(ds) do
            if d >= lo and d <= hi then
              local eb = L.E7.bu(e, wc.w, e.x, e.y)
              local out = L.E7.fight(eb, me, d)
              local ed, pk, ps, cd, us = 0, 0, 0, 0, 0
              local dd = {}
              for _, o in ipairs(out) do
                local taken = hp - math.max(0, o.bh)
                ed = ed + o.p * taken; cd = cd + o.p * (e.hp - math.max(0, o.ah)); us = us + o.p * o.sb
                if o.bh <= 0 then pk = pk + o.p end
                if o.pet then ps = ps + o.p end
                dd[taken] = (dd[taken] or 0) + o.p
              end
              local v = ed + 40 * pk + 30 * ps - cd / 2
              if bestV == nil or v > bestV then bestV, best = v, { dd = dd, ed = ed, ps = ps, us = us } end
            end
          end
        end
        best = best or false
        L.E7.cache[ck] = best
      end
      if best then
        r.n = r.n + 1
        if best.ed >= 1 then r.big = r.big + 1 end
        r.eDmg = r.eDmg + best.ed
        r.uses = r.uses + best.us
        noStone = noStone * (1 - best.ps)
        local nd = {}
        for t0, p0 in pairs(dist0) do
          for t1, p1 in pairs(best.dd) do
            local t2 = math.min(hp, t0 + t1)
            nd[t2] = (nd[t2] or 0) + p0 * p1
          end
        end
        dist0 = nd
      end
    end
  end
  r.pDeath = dist0[hp] or 0
  r.pStone = 1 - noStone
  return r
end
-- The death chance a move or attack of unit u may take this turn: 3% (a death makes the turn's
-- search play the turn again); the search's policies: "safe" for u 0.5%, "cautious" 1.2%,
-- "press" (a stalled turn) 8%, "bold" 15%.
function L.E7.theta(u)
  if P.safe[u.id] then return 0.005 end
  if P.bold then return 0.15 end
  if P.press then return 0.08 end
  if P.cautious then return 0.012 end
  return 0.03
end
-- The ways in part 1 (index 0x15), as a distance field's extra cost (nil: a wall). The stairs into
-- the throne room (10,5)-(12,6) are walls: a unit ending its move there calls three Revenants by
-- Lyon every turn for ten turns (AREA (10,5)-(12,6), EventScr_089F9428 and _9448,
-- src/events/ch21a-eventscript.h). The hall's west half (x 0-11, rows 7-12) costs 20 more: a unit
-- in it calls five Arch Mogalls (Crimson Eye, 20 to Franz at about 45%; UnitDef_088C1C04), the
-- east half four Deathgoyles (lances, 3 to Franz; UnitDef_088C1C7C); a move ends there only at a
-- cost (L.E7.endCost) until the Mogalls are on the map. (An attack does not count that cost: in
-- route_e21.keys Franz's attack from (3,11) called them on turn 6, and the army then went up the
-- west side.) An enemy's tile costs 5 more (12 for one that never leaves it, AI1 3 to 5).
function L.E7.corr(x, y)
  if L.E7.ch == 0x15 then
    if x >= 10 and x <= 12 and y >= 5 and y <= 6 then return nil end
  end
  local c = 0
  if L.E7.ch == 0x15 and y >= 7 and y <= 12 and x <= 11 then c = 20 end
  return c + ((L.E7.occ and L.E7.occ[y * 256 + x]) or 0)
end
-- the cost of ending a move on (x, y) in part 1: the trigger tiles above
function L.E7.endCost(x, y)
  if L.E7.ch ~= 0x15 then return 0 end
  if x >= 10 and x <= 12 and y >= 5 and y <= 6 then return 1000000 end
  if y >= 7 and y <= 12 and x <= 11 and not L.E7.mogalls then return 5000 end
  return 0
end
-- The tiles next to boss B a unit of u's class can stand on (Divinestone and Siegmund: range 1).
function L.E7.bossTiles(u, B)
  local W, H = L.mapSize()
  local ct = emu:read32(u.classPtr + 0x38)
  local g = {}
  for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
    local x, y = B.x + dv[1], B.y + dv[2]
    if x >= 0 and y >= 0 and x < W and y < H and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0 then g[#g + 1] = { x = x, y = y } end
  end
  return g
end
-- u's distance field to the tiles next to B along the ways (L.E7.corr), the red units as costs
function L.E7.bfield(u, B)
  L.E7.occ = {}
  for _, e in ipairs(L.red()) do
    local a1 = emu:read8(e.addr + 0x42)
    L.E7.occ[e.y * 256 + e.x] = (a1 >= 3 and a1 <= 5) and 12 or 5
  end
  return distanceField(u, L.E7.bossTiles(u, B), {}, false, false, L.E7.corr)
end
-- The units of the army: M (the dragonstone unit, Myrrh), E (Ephraim), C (Eirika's carrier),
-- R (Eirika, on the map).
function L.E7.army()
  local A = {}
  for _, b in ipairs(equipAll(L.blue())) do
    if b.char == L.CHAR.EPHRAIM then A.E = b
    elseif b.char == L.CHAR.EIRIKA then A.R = b
    elseif L.E7.stoneUses(b) then A.M = b
    elseif carriesLord(b) then A.C = b
    elseif (unitAttr(b) & 1) ~= 0 and isFighter(b) and not A.C then A.F = b end
  end
  A.C = A.C or A.F
  return A
end
function L.E7.free(u) return (u.state & (L.US.UNSELECTABLE | L.US.HAS_MOVED)) == 0 end
-- The strike: Myrrh attacks B from a free tile next to B she can reach this turn, then Ephraim
-- with the weapon that hurts B most from another tile; exact chances over both fights in turn.
-- Returns { m = { t, slot }, e = { t, slot }, pK, pD (Ephraim's death in his fight) } when B
-- dies at 85% or more and Ephraim at 3% or less; only while both have yet to move.
function L.E7.strikePlan(B, enemies)
  local A = L.E7.army()
  local M, E = A.M, A.E
  if not B or not M or not E or not L.E7.free(M) or not L.E7.free(E) then return nil end
  local mt, et = moveTiles(M), moveTiles(E)
  local best
  local bb = L.E7.bu(B, B.weapon, B.x, B.y)
  for _, wm in ipairs(L.E7.weapons(M)) do
    for _, tm in ipairs(mt) do
      local dm = dist(tm.x, tm.y, B.x, B.y)
      if dm >= wm.w.minRange and dm <= wm.w.maxRange and not (L.E7.endCost(tm.x, tm.y) >= 1000000) then
        local o1 = L.E7.fight(L.E7.bu(M, wm.w, tm.x, tm.y), bb, dm)
        local pM = 0
        for _, o in ipairs(o1) do if o.ah <= 0 then pM = pM + o.p end end
        for _, we in ipairs(L.E7.weapons(E)) do
          for _, te in ipairs(et) do
            local de = dist(te.x, te.y, B.x, B.y)
            if not (te.x == tm.x and te.y == tm.y) and de >= we.w.minRange and de <= we.w.maxRange
                and not (L.E7.endCost(te.x, te.y) >= 1000000) then
              local pK, pD = 0, 0
              local eb = L.E7.bu(E, we.w, te.x, te.y)
              for _, o in ipairs(o1) do
                if o.bh <= 0 then pK = pK + o.p
                elseif o.ah > 0 then
                  local b2 = L.E7.bu(B, B.weapon, B.x, B.y)
                  b2.hp = o.bh
                  for _, x in ipairs(L.E7.fight(eb, b2, de)) do
                    if x.bh <= 0 then pK = pK + o.p * x.p end
                    if x.ah <= 0 then pD = pD + o.p * x.p end
                  end
                end
              end
              local v = pK - 3 * pD - pM
              if best == nil or v > best.v then
                best = { v = v, pK = pK, pD = pD, pM = pM, m = { t = tm, slot = wm.slot }, e = { t = te, slot = we.slot } }
              end
            end
          end
        end
      end
    end
  end
  -- (Myrrh's own death chance in her fight at most 1% against Lyon, 8% against the Demon King, whose
  -- Demonlight crits 6% of the time: the turn's search plays the turn again)
  local mMax = (L.playSt().chapter == 0x16) and 0.08 or 0.01
  if best and best.pK >= 0.85 and best.pD <= 0.03 and best.pM <= mMax then return best end
  return nil
end
-- The unit order of a player phase in the final chapter: on a strike turn Myrrh, then Ephraim;
-- else Ephraim (he clears the way and takes the supply's Spear on turn 1), Myrrh, Eirika,
-- her carrier last (on turn 1 it picks her up after Ephraim's supply).
function L.E7.finOrder(order)
  local A = L.E7.army()
  local B
  for _, e in ipairs(L.red()) do if e.char == L.E7.BOSS[L.playSt().chapter] then B = e end end
  L.E7.ch = L.playSt().chapter
  local sp = B and L.E7.strikePlan(equipAll({ B })[1], equipAll(L.red()))
  local o, seen = {}, {}
  for _, key in ipairs(sp and { "M", "E" } or { "E", "M", "R" }) do
    local u = A[key]
    if u then o[#o + 1] = u; seen[u.id] = true end
  end
  for _, u in ipairs(order) do if not seen[u.id] and not carriesLord(u) and not (A.C and u.id == A.C.id) then o[#o + 1] = u; seen[u.id] = true end end
  for _, u in ipairs(order) do if not seen[u.id] then o[#o + 1] = u; seen[u.id] = true end end
  if sp then log("E7: strike on %02x this turn (%.0f%%; Ephraim's death %.1f%%)", B.char, sp.pK * 100, sp.pD * 100) end
  return o
end
-- The tile of `tiles` with the lowest cost(t) (ok(t) only), or u's own.
function L.E7.pick(u, tiles, ok, cost)
  local best, bestT
  for _, t in ipairs(tiles) do
    if ok(t) then
      local c = cost(t) + noise(2, u.id, t.x, t.y) * 8
      if best == nil or c < best then best, bestT = c, t end
    end
  end
  return bestT or { x = u.x, y = u.y }, best
end
-- A heal from u's own items (an Elixir heals all), on the tile of the lowest cost.
function L.E7.heal(u, tiles, ok, cost)
  local hs = healItemSlot(u)
  if not hs then return nil end
  local t = L.E7.pick(u, tiles, ok, cost)
  return { dest = t, action = "item", slot = hs }
end
-- An Elixir (else a Vulnerary) by Trade from ally a, from the tile next to a of the lowest cost.
function L.E7.trade(u, a, tiles, ok, cost)
  local slot
  for k, it in ipairs(a.items) do
    if (it & 0xFF) == 0x6D then slot = k - 1; break end
    if (it & 0xFF) == 0x6C and not slot then slot = k - 1 end
  end
  if not slot then return nil end
  local best, bestT
  for _, t in ipairs(tiles) do
    if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
      local c = cost(t)
      if best == nil or c < best then best, bestT = c, t end
    end
  end
  if not bestT or best >= 100000 then return nil end
  local give
  if #u.items >= 5 then
    for k, it in ipairs(u.items) do
      if not give and not canUseWeapon(u, L.item(it & 0xFF)) and not HEAL_ITEMS[it & 0xFF] then give = k - 1 end
    end
    if not give then return nil end
  end
  return { dest = bestT, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = slot, giveSlot = give }
end
-- The best attack of fighter u on any enemy but B (and B only when bossOk), with its chance of
-- death in the fight and in the enemy phase after it at most th; nil if none. Score: a kill 1000,
-- 8 a point of damage, the targets that reach Myrrh or Eirika's carrier 250 more, a Stone
-- holder 300 more; the weapon's uses cost (Siegmund, which only Lyon and the Demon King need,
-- 60, kept at L.E7.KEEP_SIEG; others 12); 12 a point of the way still to go (D).
function L.E7.attack(u, tiles, enemies, ok, B, th, D, opts)
  opts = opts or {}
  local A = L.E7.army()
  local best, bestP
  local sieg = 0
  for _, it in ipairs(u.items) do if (it & 0xFF) == 0x92 then sieg = it >> 8 end end
  for _, e in ipairs(enemies) do
    if (e ~= B or opts.bossOk) and not hiddenRed(e) and not (opts.only and e ~= opts.only) then
      -- (the enemies that could reach Myrrh or the carrier where they stand)
      local prio = 0
      for _, key in ipairs({ "M", "C" }) do
        local b = A[key]
        if b and b.id ~= u.id and reachSets[e] and reachSets[e][b.y * 256 + b.x] then prio = prio + 250 end
      end
      for _, it in ipairs(e.items) do if (it & 0xFF) == L.E7.STONE and (it >> 8) > 0 then prio = prio + 300 end end
      local eb = L.E7.bu(e, e.weapon, e.x, e.y)
      for _, wc in ipairs(L.E7.weapons(u)) do
        local keep = wc.w.id == 0x92 and B ~= nil and e ~= B and wc.uses <= (opts.keepSieg or L.E7.KEEP_SIEG)   -- (E7 on map 0x23: its own)
        if not keep and not (opts.slot and wc.slot ~= opts.slot) then
          local wcost = (wc.w.id == 0x92 or wc.w.eff == MONSTER_EFF or wc.w.rank >= 251) and 60 or 12
          for _, t in ipairs(tiles) do
            local d = dist(t.x, t.y, e.x, e.y)
            if ok(t) and d >= wc.w.minRange and d <= wc.w.maxRange and L.E7.endCost(t.x, t.y) < 1000000 then
              local out = L.E7.fight(L.E7.bu(u, wc.w, t.x, t.y), eb, d)
              local pK, pD, pP, dealt, taken, hits, hpK, hpA, pA = 0, 0, 0, 0, 0, 0, 0, 0, 0
              for _, o in ipairs(out) do
                dealt = dealt + o.p * (e.hp - math.max(0, o.bh))
                taken = taken + o.p * (u.hp - math.max(0, o.ah))
                hits = hits + o.p * o.sa
                if o.ah <= 0 then pD = pD + o.p
                elseif o.petA then pP = pP + o.p
                elseif o.bh <= 0 then pK = pK + o.p; hpK = hpK + o.p * o.ah
                else pA = pA + o.p; hpA = hpA + o.p * o.ah end
              end
              if dealt > 0 then
                -- the enemy phase after it: the target gone (if killed) or not, with the HP left
                local uw = {}
                for kk, v in pairs(u) do uw[kk] = v end
                uw.weapon = wc.w
                local after = 0
                if pK > 0 and e ~= B then after = after + pK * L.E7.risk(uw, t.x, t.y, enemies, { [e] = true }, math.max(1, math.floor(hpK / pK))).pDeath end
                if pA > 0 then after = after + pA * L.E7.risk(uw, t.x, t.y, enemies, nil, math.max(1, math.floor(hpA / pA))).pDeath end
                local pDeath = pD + after
                if pDeath <= th and pP <= th then
                  local s = 1000 * pK + 8 * dealt + prio * pK - 3 * taken - 100000 * pDeath - 30000 * pP - wcost * hits
                    - 12 * ((D and D[t.y] and D[t.y][t.x]) or 60) + noise(1, u.id, t.y * 64 + t.x, e.id) * 40
                  if best == nil or s > best then
                    best, bestP = s, { dest = t, action = "attack", target = e.id, targetPos = { x = e.x, y = e.y }, wslot = wc.slot,
                                       pK = pK, pDeath = pDeath }
                  end
                end
              end
            end
          end
        end
      end
    end
  end
  return bestP, best
end
-- Eirika's carrier (or the unit that is to carry her): turn 1, Rescue once Ephraim has taken
-- from the supply (she must be on the map for it); carrying, it keeps to the tiles of the least
-- risk near Myrrh and Ephraim, heals under 70%, and attacks only a kill at a blow where nothing
-- can then hurt it.
function L.E7.carrier(u, tiles, enemies, ok, A, th, canto)
  local function cost(t, hp)
    local rk = L.E7.risk(u, t.x, t.y, enemies, nil, hp)
    local near = 0
    local m = A.M or A.E
    if m and m.id ~= u.id then near = math.max(0, dist(t.x, t.y, m.x, m.y) - 4) end
    return 100000 * rk.pDeath + 50000 * rk.pStone + 300 * rk.eDmg + 3 * near + L.E7.endCost(t.x, t.y)
  end
  if canto then return { dest = L.E7.pick(u, tiles, ok, cost), action = "wait" } end
  if A.R and not carriesLord(u) then
    local sup = A.E and supplyTried[L.playSt().chapter .. ":" .. A.E.id] == nil and L.E7.free(A.E)
    if unitAid(u) >= A.R.con and not sup then
      local best, bestT
      for _, t in ipairs(tiles) do
        if ok(t) and dist(t.x, t.y, A.R.x, A.R.y) == 1 then
          local c = cost(t)
          if best == nil or c < best then best, bestT = c, t end
        end
      end
      if bestT then return { dest = bestT, action = "rescue", target = A.R.id, targetPos = { x = A.R.x, y = A.R.y } } end
    end
  end
  if u.hp * 10 < u.maxHp * 7 then
    local p = L.E7.heal(u, tiles, ok, function(t) return cost(t, u.maxHp) end)
    if p then return p end
  end
  local p = L.E7.attack(u, tiles, enemies, ok, A.B, 0.002, nil)
  if p and p.pK >= 0.95 then return p end
  return { dest = L.E7.pick(u, tiles, ok, cost), action = "wait" }
end
-- Myrrh: her Divinestone is for the bosses. She strikes (L.E7.strikePlan); in part 2 she also
-- attacks the Demon King alone from full HP where her death chance in his counter and the enemy
-- phase after stays at most L.E7.SOLO (45 a hit, 120 HP: the strike with Ephraim, 45 and 48,
-- needs a first hit; his Demonlight does 12 to her, Ravager 18 and 54 with a crit, 15%: with
-- 2% she never attacked him, from turn 9 to 46 of a try); else she keeps out of every enemy's
-- reach that would cost her uses (her counters) or HP, out of every Stone's, as close to the boss
-- as that allows (her distance field), and heals below full HP.
L.E7.SOLO = 0.2
function L.E7.myrrh(u, tiles, enemies, ok, A, B, th)
  local sp = B and L.E7.strikePlan(B, enemies)
  if sp then
    log("E7: strike: Myrrh at (%d,%d), Ephraim at (%d,%d), %.0f%%", sp.m.t.x, sp.m.t.y, sp.e.t.x, sp.e.t.y, sp.pK * 100)
    return { dest = sp.m.t, action = "attack", target = B.id, targetPos = { x = B.x, y = B.y }, wslot = sp.m.slot }
  end
  local D = B and L.E7.bfield(u, B)
  local ds = L.E7.stoneUses(u) or 0
  if B and L.playSt().chapter == 0x16 and ds > 0 and u.hp == u.maxHp then
    local p = L.E7.attack(u, tiles, enemies, ok, B, P.safe[u.id] and L.E7.SOLO / 2 or L.E7.SOLO, D, { bossOk = true, only = B })
    if p then log("E7: Myrrh on %02x alone (death chance %.1f%%)", B.char, p.pDeath * 100); return p end
  end
  local function cost(t, hp)
    local rk = L.E7.risk(u, t.x, t.y, enemies, nil, hp)
    local dd = (D and D[t.y] and D[t.y][t.x]) or 80
    local far = (A.E and A.E.id ~= u.id) and math.max(0, dist(t.x, t.y, A.E.x, A.E.y) - 7) or 0
    return 100000 * rk.pDeath + 1000000 * rk.pStone + 4000 * rk.uses + 300 * rk.eDmg + 12 * dd + 20 * far + L.E7.endCost(t.x, t.y)
  end
  if u.hp < u.maxHp then
    local p = L.E7.heal(u, tiles, ok, function(t) return cost(t, u.maxHp) end)
    if p then return p end
  end
  return { dest = L.E7.pick(u, tiles, ok, cost), action = "wait" }
end
-- Ephraim: turn 1, the supply's Spear (he gives the two swords he cannot use); then he clears the
-- way to the boss: his best attack (L.E7.attack, death chance at most th) or the tile nearest the
-- boss along the ways whose death chance is at most th; under 60% of his HP, or near the boss
-- under what Lyon's counter takes (43), he heals (an Elixir, by Trade from Myrrh or the carrier
-- once his own are gone). On a strike turn, after Myrrh, his part of the strike.
function L.E7.ephraim(u, tiles, enemies, ok, A, B, th)
  local ch = L.playSt().chapter
  -- turn 1: the supply (from a tile next to Eirika)
  if A.R and supplyTried[ch .. ":" .. u.id] == nil then
    local stock = convoyItems()
    local take, give = {}, {}
    for _, id in ipairs(stock) do
      local w = L.item(id)
      if #take == 0 and (w.attributes & 1) ~= 0 and (w.attributes & 4) == 0 and canUseWeapon(u, w) and w.eff ~= MONSTER_EFF
          and w.maxRange >= 2 then take[1] = id end
    end
    for _, it in ipairs(u.items) do
      local w = L.item(it & 0xFF)
      if (w.attributes & 1) ~= 0 and not canUseWeapon(u, w) then give[#give + 1] = w.id end
    end
    supplyTried[ch .. ":" .. u.id] = L.playSt().turn
    if #take > 0 then
      local best, bestT
      for _, t in ipairs(tiles) do
        if ok(t) and dist(t.x, t.y, A.R.x, A.R.y) == 1 then
          local c = L.E7.risk(u, t.x, t.y, enemies).pDeath
          if best == nil or c < best then best, bestT = c, t end
        end
      end
      if bestT then
        local base = {}
        for _, id in ipairs(take) do base[id] = itemCountOf(u, id) end
        for _, id in ipairs(give) do base[id] = itemCountOf(u, id) end
        log("supply plan: char %02x takes %02x, gives %d", u.char, take[1], #give)
        return { dest = bestT, action = "supply", supply = { take = take, give = give, base = base, score = 0, mine = 0 } }
      end
    end
  end
  local D = B and L.E7.bfield(u, B)
  -- the strike, Ephraim's part: Myrrh has struck from next to B this turn and B stands
  if B and A.M and not L.E7.free(A.M) and dist(A.M.x, A.M.y, B.x, B.y) == 1 then
    local p = L.E7.attack(u, tiles, enemies, ok, B, (ch == 0x15) and 0.35 or 0.05, D, { bossOk = true, only = B })
    if p and (ch == 0x15 or p.pK >= 0.5) then log("E7: strike, Ephraim's part (death chance %.1f%%)", p.pDeath * 100); return p end
    local bt = L.E7.bossTiles(u, B)
    for _, t in ipairs(tiles) do
      for _, g in ipairs(bt) do
        if t.x == g.x and t.y == g.y and ok(t) then
          for _, wc in ipairs(L.E7.weapons(u)) do
            if wc.w.minRange <= 1 then
              local o = L.E7.fight(L.E7.bu(u, wc.w, t.x, t.y), L.E7.bu(B, B.weapon, B.x, B.y), 1)
              local pK, pD = 0, 0
              for _, x in ipairs(o) do if x.bh <= 0 then pK = pK + x.p end; if x.ah <= 0 then pD = pD + x.p end end
              if pK >= 0.5 and pD <= 0.35 then
                log("E7: strike, Ephraim's part: %.0f%% (death %.0f%%)", pK * 100, pD * 100)
                return { dest = t, action = "attack", target = B.id, targetPos = { x = B.x, y = B.y }, wslot = wc.slot }
              end
            end
          end
        end
      end
    end
  end
  local lyon = 0
  if B and ch == 0x15 then
    local bb = L.E7.bu(B, B.weapon, B.x, B.y)
    lyon = L.E7.dmg(bb, L.E7.bu(u, u.weapon, u.x, u.y))
  end
  local near = D and D[u.y] and D[u.y][u.x] and D[u.y][u.x] <= 2 * u.mov
  local function cost(t, hp)
    local rk = L.E7.risk(u, t.x, t.y, enemies, nil, hp)
    local dd = (D and D[t.y] and D[t.y][t.x]) or 80
    local over = (rk.pDeath > th or rk.pStone > th) and 100000 or 0
    return over + 100000 * rk.pDeath + 30000 * rk.pStone + 8 * rk.eDmg + 15 * dd + L.E7.endCost(t.x, t.y)
  end
  -- heal: under 60%, or near the boss under Lyon's counter
  if u.hp * 5 < u.maxHp * 3 or (near and lyon > 0 and u.hp <= lyon and u.maxHp > lyon) then
    local p = L.E7.heal(u, tiles, ok, function(t) return cost(t, u.maxHp) end)
    if p then return p end
    for _, key in ipairs({ "M", "C" }) do
      local a = A[key]
      if a and a.id ~= u.id then
        local q = L.E7.trade(u, a, tiles, ok, function(t) return cost(t) end)
        if q then return q end
      end
    end
  end
  local p, ps = L.E7.attack(u, tiles, enemies, ok, B, th, D)
  local t, ts = L.E7.pick(u, tiles, ok, function(t0) return cost(t0) end)
  if p and (ts == nil or ps > -ts) then return p end
  -- (the Spear equipped for the enemy phase's counters when Siegmund is first: its uses are the bosses')
  local eq
  for k, it in ipairs(u.items) do if not eq and canUseWeapon(u, L.item(it & 0xFF)) then eq = k - 1 end end
  if eq and (u.items[eq + 1] & 0xFF) == 0x92 then
    for _, wc in ipairs(L.E7.weapons(u)) do
      if wc.w.id ~= 0x92 then return { dest = t, action = "equip", slot = wc.slot } end
    end
  end
  return { dest = t, action = "wait" }
end
-- Eirika on the map (turn 1, before her carrier picks her up; or her carrier gone): the tile of
-- the least risk.
function L.E7.eirika(u, tiles, enemies, ok, A)
  local function cost(t)
    local rk = L.E7.risk(u, t.x, t.y, enemies)
    local stay = (t.x == u.x and t.y == u.y) and 0 or 1
    return 100000 * rk.pDeath + 50000 * rk.pStone + 300 * rk.eDmg + stay + L.E7.endCost(t.x, t.y)
  end
  return { dest = L.E7.pick(u, tiles, ok, cost), action = "wait" }
end
function L.E7.fin(u, tiles, enemies, ok, canto)
  L.E7.cache = {}
  L.E7.ch = L.playSt().chapter
  local A = L.E7.army()
  local B
  for _, e in ipairs(enemies) do if e.char == L.E7.BOSS[L.E7.ch] then B = e end end
  A.B = B
  L.E7.mogalls = false
  for _, e in ipairs(enemies) do if e.class == 0x60 then L.E7.mogalls = true end end
  local th = L.E7.theta(u)
  if canto then
    local function cost(t)
      local rk = L.E7.risk(u, t.x, t.y, enemies)
      return 100000 * rk.pDeath + 50000 * rk.pStone + 300 * rk.eDmg + L.E7.endCost(t.x, t.y)
    end
    return { dest = L.E7.pick(u, tiles, ok, cost), action = "wait" }
  end
  if u.char == L.CHAR.EIRIKA then return L.E7.eirika(u, tiles, enemies, ok, A) end
  if A.M and u.id == A.M.id then return L.E7.myrrh(u, tiles, enemies, ok, A, B, th) end
  if A.E and u.id == A.E.id then return L.E7.ephraim(u, tiles, enemies, ok, A, B, th) end
  return L.E7.carrier(u, tiles, enemies, ok, A, th, canto)
end
-- The turn search's view of a turn in the final chapter (searchBranch): Myrrh's Divinestone uses
-- spent cost 3,000 each, a petrified unit 80,000, each point of the boss's HP taken counts 300,
-- and Ephraim's and Myrrh's ways to the boss 40 and 15 a point.
function L.E7.finBase()
  local A = L.E7.army()
  local B
  for _, e in ipairs(L.red()) do if e.char == L.E7.BOSS[L.playSt().chapter] then B = e end end
  return { ds = A.M and L.E7.stoneUses(A.M) or 0, bossHp = B and B.hp or 0 }
end
function L.E7.finScore(base)
  L.E7.ch = L.playSt().chapter
  local A = L.E7.army()
  local B
  for _, e in ipairs(L.red()) do if e.char == L.E7.BOSS[L.E7.ch] then B = e end end
  local s = 0
  if A.M then s = s - 3000 * math.max(0, base.ds - (L.E7.stoneUses(A.M) or 0)) end
  for _, key in ipairs({ "M", "E", "C" }) do
    local b = A[key]
    local st = b and (emu:read8(b.addr + 0x30) & 0x0F)
    if st == 11 or st == 13 then s = s - 80000 end
  end
  if B then
    s = s + 300 * math.max(0, base.bossHp - B.hp)
    for _, w in ipairs({ { A.E, 40 }, { A.M, 15 } }) do
      if w[1] then
        local D = L.E7.bfield(w[1], B)
        s = s - w[2] * ((D[w[1].y] and D[w[1].y][w[1].x]) or 80)
      end
    end
  end
  return s
end

---------------------------------------------------------------------------
-- R10 (coverage 12, c12-br10; README-routes.md "Tactics R10"): the Lagdou Ruins' last floor,
-- chapter index 0x37, played cheat-assisted (ROUTE_CHEAT=durability) by the boosted army. Only
-- in index 0x37: no other route plays a turn there (route_br9.keys stops at its first free
-- cursor), so every older file stays as it was. (In L: the main chunk is at Lua's 200-local limit.)
-- The floor holds ten Draco Zombies (class 0x65; Wretched Air: might 10, range 1-2, negates
-- defence, +10 power, +20 defence: 30 to 32 a hit whatever our defence, so two hits kill any of
-- the army's 60-HP units). Nine wait (AI2 3) until one of ours stands in their reach; the boss
-- on (9,3) never leaves his tile (AI1 3). Myrrh's Divinestone (x3, 40 a hit, twice) and Ephraim's
-- Siegmund (x2, 38 twice) each kill one a turn; Eirika's and Franz's sacred weapons take 66 and
-- 54. With the plain rules (every enemy hit lands) the army either stayed out of every reach or
-- walked into three at once (Franz died on turn 3 of every try). The director plays by E7's exact
-- model (L.E7.fight, L.E7.risk: FE8's 2RN hit roll):
--   - an attack is taken when the unit's death chance, in the fight and in the enemy phase after
--     it over every outcome of the fight (its HP left, its target dead or not), is at most theta
--     (L.E7.theta: 3%, the search's policies 0.5% to 15%; half that for a lord); a target the
--     fight leaves alive counts as dead when a unit yet to move can finish it at 90% or more (the
--     unit order then has it do so); a kill scores 1000, a point of damage 8;
--   - the zombies awake (whose reach covers one of our units' tiles) stay at most L.R10.cap()
--     (2, about what the army kills in a turn; 1 with "cautious", 3 with "press" or "bold");
--   - with no attack, a unit that one more hit would kill heals (its Elixir, which the cheat
--     keeps full) on its safest tile; a unit that can take a hit walks toward the nearest
--     zombie on tiles whose death chance is at most theta (25 a point of the way, 5 a point of
--     the expected damage, 300 less for a tile that wakes one more zombie within the cap: the
--     bait), a hurt one keeps off (300 a point);
--   - a unit that cannot hurt a zombie (Valter: a Steel Lance, 1 a hit) keeps to its safest tile,
--     and under 70% of its HP heals (by Trade, an Elixir from an ally with two heal items);
--   - the order: Myrrh, Ephraim, Eirika, the other fighters (Franz), the rest (Valter).
L.R10 = { CH = 0x37, CAP = 2 }
function L.R10.on(ch) return ch == L.R10.CH end
function L.R10.cap()
  if P.bold or P.press then return 3 end
  if P.cautious then return 1 end
  return L.R10.CAP
end
-- the enemies awake: whose reach covers the tile of one of our units other than u
function L.R10.woken(u, enemies)
  local w = {}
  for _, b in ipairs(L.blue()) do
    if b.id ~= u.id then
      for _, e in ipairs(enemies) do
        if reachSets[e] and reachSets[e][b.y * 256 + b.x] then w[e] = true end
      end
    end
  end
  return w
end
-- how many more enemies than the cap would be awake with u on t (`gone`: an enemy killed first)
-- (a tile that wakes no enemy more is always within it: at the turn's start more may be awake)
function L.R10.over(t, enemies, woken, gone)
  local n, nw = 0, 0
  for _, e in ipairs(enemies) do
    if e ~= gone then
      if woken[e] then nw = nw + 1 end
      if woken[e] or (reachSets[e] and reachSets[e][t.y * 256 + t.x]) then n = n + 1 end
    end
  end
  return n - math.max(L.R10.cap(), nw)
end
-- u can hurt the enemies: its best hit on the first one is 10 or more
function L.R10.fighter(u, enemies)
  local e = enemies[1]
  if not e then return true end
  local eb = L.E7.bu(e, e.weapon, e.x, e.y)
  for _, wc in ipairs(L.E7.weapons(u)) do
    if L.E7.dmg(L.E7.bu(u, wc.w, u.x, u.y), eb) >= 10 then return true end
  end
  return false
end
-- the most one enemy hit can take from u (a crit counts when it can happen)
function L.R10.maxHit(u, enemies)
  local m = 0
  local me = L.E7.bu(u, u.weapon, u.x, u.y)
  for _, e in ipairs(enemies) do
    local eb = L.E7.bu(e, e.weapon, e.x, e.y)
    if eb.w then
      local d = L.E7.dmg(eb, me)
      if L.E7.critRate(eb, me) > 0 then d = 3 * d end
      if d > m then m = d end
    end
  end
  return m
end
-- the best chance that one of our fighters yet to move (not u) kills e, left with hp HP, from a
-- tile it can reach this turn other than `taken` (u's), where its own death chance (the fight, and
-- the enemy phase after it with e dead) is at most its theta, as its own attack rule asks
function L.R10.finish(u, e, hp, taken, enemies)
  local key = e.id .. ":" .. hp .. ":" .. taken.x .. ":" .. taken.y
  if L.R10.fc[key] ~= nil then return L.R10.fc[key] end
  local best = 0
  local eb = L.E7.bu(e, e.weapon, e.x, e.y)
  eb.hp = hp
  for _, b in ipairs(equipAll(L.blue())) do
    if b.id ~= u.id and L.E7.free(b) and L.R10.fighter(b, enemies) then
      L.R10.mt[b.id] = L.R10.mt[b.id] or moveTiles(b)
      local thb = L.E7.theta(b) * (isLordUnit(b) and 0.5 or 1)
      for _, wc in ipairs(L.E7.weapons(b)) do
        for _, tb in ipairs(L.R10.mt[b.id]) do
          local d = dist(tb.x, tb.y, e.x, e.y)
          if d >= wc.w.minRange and d <= wc.w.maxRange and not (tb.x == taken.x and tb.y == taken.y) then
            local pk, pd = 0, 0
            for _, o in ipairs(L.E7.fight(L.E7.bu(b, wc.w, tb.x, tb.y), eb, d)) do
              if o.ah <= 0 then pd = pd + o.p
              elseif o.bh <= 0 then
                pk = pk + o.p
                pd = pd + o.p * L.E7.risk(b, tb.x, tb.y, enemies, { [e] = true }, o.ah, wc.w).pDeath
              else pd = pd + o.p * L.E7.risk(b, tb.x, tb.y, enemies, nil, o.ah, wc.w).pDeath end
            end
            if pd <= thb and pk > best then best = pk end
          end
        end
      end
    end
  end
  L.R10.fc[key] = best
  return best
end
-- u's best attack (nil if none) with its death chance at most th (above)
function L.R10.attack(u, tiles, enemies, ok, th, woken)
  local best, bestP
  for _, e in ipairs(enemies) do
    if not hiddenRed(e) then
      local eb = L.E7.bu(e, e.weapon, e.x, e.y)
      for _, wc in ipairs(L.E7.weapons(u)) do
        for _, t in ipairs(tiles) do
          local d = dist(t.x, t.y, e.x, e.y)
          if d >= wc.w.minRange and d <= wc.w.maxRange and ok(t) then
            local pK, pF, pDeath, dealt, taken = 0, 0, 0, 0, 0
            for _, o in ipairs(L.E7.fight(L.E7.bu(u, wc.w, t.x, t.y), eb, d)) do
              dealt = dealt + o.p * (e.hp - math.max(0, o.bh))
              taken = taken + o.p * (u.hp - math.max(0, o.ah))
              if o.ah <= 0 then pDeath = pDeath + o.p
              else
                local q = 1
                if o.bh <= 0 then pK = pK + o.p
                else
                  q = L.R10.finish(u, e, o.bh, t, enemies)
                  if q < 0.9 then q = 0 end
                  pF = pF + o.p * q
                end
                local r1 = q > 0 and L.E7.risk(u, t.x, t.y, enemies, { [e] = true }, o.ah, wc.w).pDeath or 0
                local r0 = q < 1 and L.E7.risk(u, t.x, t.y, enemies, nil, o.ah, wc.w).pDeath or 0
                pDeath = pDeath + o.p * (q * r1 + (1 - q) * r0)
              end
            end
            if dealt > 0 and pDeath <= th and L.R10.over(t, enemies, woken, (pK + pF >= 0.5) and e or nil) <= 0 then
              local s = 1000 * pK + 8 * dealt - 3 * taken - 100000 * pDeath + noise(1, u.id, t.y * 64 + t.x, e.id) * 40
              if best == nil or s > best then
                best, bestP = s, { dest = t, action = "attack", target = e.id, targetPos = { x = e.x, y = e.y }, wslot = wc.slot,
                                   pK = pK, pF = pF, pDeath = pDeath }
              end
            end
          end
        end
      end
    end
  end
  return bestP, best
end
-- u's tile for a wait (or a heal: `heal`), standing there with hp HP: its death chance at most th
-- above all, the enemies awake within the cap, then the expected damage and the way to the
-- nearest enemy (the boss last: he never moves) for a unit that can take a hit
function L.R10.move(u, tiles, enemies, ok, th, woken, hp, heal)
  local fresh = not heal and hp > L.R10.maxHit(u, enemies)
  local D
  if fresh then
    local goals, any = {}, false
    for _, e in ipairs(enemies) do
      local a1 = emu:read8(e.addr + 0x42)
      if not (a1 >= 3 and a1 <= 5) then any = true end
    end
    for _, e in ipairs(enemies) do
      local a1 = emu:read8(e.addr + 0x42)
      if not any or not (a1 >= 3 and a1 <= 5) then
        for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do goals[#goals + 1] = { x = e.x + dv[1], y = e.y + dv[2] } end
      end
    end
    D = distanceField(u, goals, {}, false, false, nil)
  end
  -- (the bait: a unit that can take a hit wakes an enemy more, within the cap and theta, for 300 less:
  -- the zombies of the last group stand within 2 of each other, and with only the way to them
  -- counted the army stood at the edge of their reach from turn 15 to 30 of a try; one woken
  -- alone comes out to the army and dies next turn)
  local function cost(t)
    local rk = L.E7.risk(u, t.x, t.y, enemies, nil, hp)
    local over = L.R10.over(t, enemies, woken, nil)
    local c = 100000 * rk.pDeath + ((rk.pDeath > th) and 100000 or 0) + 50000 * math.max(0, over)
    if fresh then
      local new = false
      for _, e in ipairs(enemies) do
        if not woken[e] and reachSets[e] and reachSets[e][t.y * 256 + t.x] then new = true end
      end
      if new and over <= 0 and rk.pDeath <= th then c = c - 300 end
      return c + 5 * rk.eDmg + 25 * ((D[t.y] and D[t.y][t.x]) or 80)
    end
    return c + 300 * rk.eDmg
  end
  return (L.E7.pick(u, tiles, ok, cost))
end
-- a heal for unit u under 70% of its HP (its own heal item, else an Elixir or Vulnerary by Trade
-- from an ally that has two or more), on its safest tile
function L.R10.heal(u, tiles, enemies, ok, th, woken)
  if u.hp * 10 >= u.maxHp * 7 then return nil end
  local hs = healItemSlot(u)
  if hs then return { dest = L.R10.move(u, tiles, enemies, ok, th, woken, u.maxHp, true), action = "item", slot = hs } end
  for _, a in ipairs(L.blue()) do
    local n = 0
    for _, it in ipairs(a.items) do if HEAL_ITEMS[it & 0xFF] then n = n + 1 end end
    if a.id ~= u.id and n >= 2 then
      local q = L.E7.trade(u, a, tiles, ok, function(t)
        local rk = L.E7.risk(u, t.x, t.y, enemies, nil, u.hp)
        return 100000 * rk.pDeath + 300 * rk.eDmg
      end)
      if q then return q end
    end
  end
  return nil
end
function L.R10.think(u, tiles, enemies, ok, canto)
  L.E7.cache, L.R10.fc, L.R10.mt = {}, {}, {}
  equip(u)
  local th = L.E7.theta(u) * (isLordUnit(u) and 0.5 or 1)
  local woken = L.R10.woken(u, enemies)
  if not canto and not L.R10.fighter(u, enemies) then
    local q = L.R10.heal(u, tiles, enemies, ok, th, woken)
    if q then log("R10: unit %02x heals (%s) at (%d,%d)", u.id, q.action, q.dest.x, q.dest.y); return q end
  end
  if canto or not L.R10.fighter(u, enemies) then
    local t = L.R10.move(u, tiles, enemies, ok, th, woken, u.hp, true)
    log("R10: unit %02x %s to (%d,%d)", u.id, canto and "canto" or "keeps safe", t.x, t.y)
    return { dest = t, action = "wait" }
  end
  local p = L.R10.attack(u, tiles, enemies, ok, th, woken)
  local hurt = u.hp < u.maxHp and (u.hp <= L.R10.maxHit(u, enemies) or u.hp * 2 < u.maxHp)
  if hurt and not (p and p.pK >= 0.5) then
    local hs = healItemSlot(u)
    if hs then
      local t = L.R10.move(u, tiles, enemies, ok, th, woken, u.maxHp, true)
      log("R10: unit %02x heals at (%d,%d)", u.id, t.x, t.y)
      return { dest = t, action = "item", slot = hs }
    end
  end
  if p then
    log("R10: unit %02x attacks %02x from (%d,%d): kill %.0f%%, finished %.0f%%, death %.1f%%", u.id, p.target, p.dest.x, p.dest.y,
      p.pK * 100, p.pF * 100, p.pDeath * 100)
    return p
  end
  local t = L.R10.move(u, tiles, enemies, ok, th, woken, u.hp, hurt)
  log("R10: unit %02x waits at (%d,%d)", u.id, t.x, t.y)
  return { dest = t, action = "wait" }
end
-- the unit order (above): Myrrh, Ephraim, Eirika, the other fighters, the rest
function L.R10.order(order)
  local red = equipAll(L.red())
  local rank = {}
  for i, u in ipairs(order) do
    equip(u)
    local r = 5
    if L.E7.stoneUses(u) then r = 1
    elseif u.char == L.CHAR.EPHRAIM then r = 2
    elseif u.char == L.CHAR.EIRIKA then r = 3
    elseif L.R10.fighter(u, red) then r = 4 end
    rank[u.id] = r * 100 + i
  end
  local o = {}
  for _, u in ipairs(order) do o[#o + 1] = u end
  table.sort(o, function(a, b) return rank[a.id] < rank[b.id] end)
  return o
end

---------------------------------------------------------------------------
-- E7 on Ephraim's map (coverage 12, port-c12-iend-real; README-routes.md "Tactics E7 on
-- Ephraim's part 2"): the final chapter's part 2 on Ephraim's route (chapterIndex 0x23, the
-- Demon King) with his real army, only with ROUTE_E7=1 (L.E7.eph). Every route made before
-- stays as it was: nothing below runs in another chapter or without ROUTE_E7=1, and Eirika's
-- E7 chapters (0x14 to 0x16, L.E7.chapter) do not reach it. The map's numbers are the fields
-- of L.E7.MAP[0x23]; the chances are E7's (L.E7.fight, L.E7.risk, L.E7.attack: FE8's 2RN).
--
-- The army after part 1 (route_i21.keys): Ephraim (39 HP; Siegmund, effective: 24 a hit to
-- the Demon King at his power 20, 79% to hit), Seth (Audhulma, 2 uses: 19 twice a round, 90% each), Franz
-- and Eirika (28 HP); no healer, the supply empty; the heals are Franz's Elixir (2 uses) and
-- five Vulneraries (Ephraim 3, Eirika 2). Nothing else hurts him (def 25 + 10 with
-- Demonlight). He (120 HP) counters with his equipped weapon, Demonlight: 32 to Ephraim at
-- 96%, and a crit (4%) kills. DemonKingAI (src/cp_data.c gAiScript_DemonKing): turn 1 he
-- summons eight monsters (AiTryDKSummon; they first act on turn 2), later summons and
-- Nightmare need 5 of our units in range (never with four); then AI_ACTION, with AI2
-- NeverMove: he attacks whoever is in his reach (2 moves, range 3) and never moves without a
-- target. So the director:
--   - keeps every unit out of his reach at every enemy phase (L.E7.MAP[].dkCost): he then
--     never acts, and Demonlight stays equipped (an attack with Ravager, 37 at 100% with a
--     16% crit, would equip it: cp_perform.c's EquipUnitItemSlot, and his counters with it);
--   - the chip (L.E7.chipPlan): Ephraim, at more HP than the counter takes, attacks him from
--     a tile next to him, and a Paladin rescues Ephraim from a tile next to his and takes him
--     out of every reach in the same player phase (Canto after Rescue: TryMakeCantoUnit,
--     UNIT_MOV less the move's cost); next turn it drops him (a dropped unit is greyed:
--     UnitDrop) near the launch tiles (L.E7.launch), he heals, and chips again;
--   - the strike (E7's same-turn strike, L.E7.eStrike): Seth (Audhulma), then Ephraim (or
--     Ephraim first), when the two kill him at 85% or more;
--   - the monsters he summons come to the army (AI1 0, ActionInRange; L.E7.movers); it meets
--     them out of his reach by E7's chances (L.E7.attack, L.E7.risk), Franz carrying Eirika by
--     Ephraim (L.E7.cohesion), Seth's Audhulma kept (his Iron Sword equipped for the counters),
--     Siegmund kept at 8 uses for him; no chip while one is left (Ephraim is the best at
--     killing them). The ones that wait (AI2 NeverMove, nobody in their reach: L.E7.waiting)
--     are left alone, out of their reach as out of his.
-- The turn search plays each turn (ROUTE_SEARCH=1) and keeps the best end by L.E7.score23: the
-- policies' moves change the random numbers the fights get (a unit's path with ties takes
-- them: GenerateBestMovementScript, src/bmidoten.c), so a chip that missed is played again.
L.E7.MAP = {
  [0x23] = {
    boss = 0xBE,        -- CHARACTER_FOMORTIIS
    lance = 0x92,       -- ITEM_LANCE_SIEGMUND: Ephraim's chip and his part of the strike
    sword = 0x91,       -- ITEM_SWORD_AUDHULMA: Seth's, kept for the Demon King
    ravager = 0xA9,     -- ITEM_RAVAGER (his first item once he has attacked with it)
    keepSieg = 8,       -- Siegmund's uses Ephraim keeps for him
    chipTh = 0.05,      -- the death chance a chip may take (his crit on the counter, 3.8%)
    strikeK = 0.85,     -- the strike's kill chance
    dkCost = 20000,     -- a tile in his reach at the end of a move
    wakeCost = 15000,   -- a tile in the reach of a monster that waits (L.E7.waiting)
    elixirAt = 25,      -- Ephraim takes an Elixir rather than a Vulnerary from this much HP missing
  },
}
function L.E7.eph(ch) return L.E7.env and L.E7.MAP[ch] ~= nil end
function L.E7.cfg() return L.E7.MAP[L.playSt().chapter] end
-- The blue unit b carries (its rescue id, +0x1B), or nil.
function L.E7.held(b)
  if (b.state & L.US.RESCUING) == 0 then return nil end
  local id = emu:read8(b.addr + 0x1B)
  if id == 0 or id >= 0x40 then return nil end
  return L.readUnit(L.ADDR.gUnitArrayBlue + (id - 1) * 0x48)
end
-- The army: E (Ephraim), R (Eirika), S (Seth, the Audhulma's), F (the other mounted unit,
-- Franz), on the map or carried; EC and RC: Ephraim's and Eirika's carriers.
function L.E7.eArmy(m)
  local A = {}
  local function role(b)
    if b.char == L.CHAR.EPHRAIM then A.E = b
    elseif b.char == L.CHAR.EIRIKA then A.R = b
    elseif b.char == L.CHAR.SETH or itemCountOf(b, m.sword) > 0 then A.S = A.S or b
    elseif (unitAttr(b) & CA_MOUNTEDAID) ~= 0 then A.F = A.F or b end
  end
  for _, b in ipairs(equipAll(L.blue())) do
    role(b)
    local h = L.E7.held(b)
    if h then
      equip(h); role(h)
      if h.char == L.CHAR.EPHRAIM then A.EC = b elseif h.char == L.CHAR.EIRIKA then A.RC = b end
    end
  end
  return A
end
function L.E7.eBoss(m, enemies)
  for _, e in ipairs(enemies or L.red()) do if e.char == m.boss then return e end end
end
-- (x, y) is in boss B's reach this enemy phase (reachSets, think)
function L.E7.inDK(B, x, y)
  return B ~= nil and reachSets ~= nil and reachSets[B] ~= nil and reachSets[B][y * 256 + x] ~= nil and reachSets[B][y * 256 + x] ~= false
end
-- The monsters that still come (enemies other than B): every summoned one attacks what is in its
-- reach (AI1 0, ActionInRange); with AI2 0 (MoveToEnemy) it walks to the army, with AI2 3
-- (NeverMove: the Draco Zombies, the Arch Mogall) it stays put while nobody is in its reach.
-- Their count (those with AI2 not 3, and those with 3 whose reach holds a blue unit now), and the
-- nearest one's distance from (x, y) (0 when none).
function L.E7.movers(B, enemies, x, y)
  local n, md = 0, nil
  local blue = L.blue()
  local act = {}
  for _, e in ipairs(enemies) do
    if not (B and e.id == B.id) then
      local on = emu:read8(e.addr + 0x44) ~= 3
      if not on and reachSets and reachSets[e] then
        for _, b in ipairs(blue) do if reachSets[e][b.y * 256 + b.x] then on = true end end
      end
      if on then
        n = n + 1; act[#act + 1] = e
        if x then local d = dist(e.x, e.y, x, y); if md == nil or d < md then md = d end end
      end
    end
  end
  return n, md or 0, act
end
-- the distance from (x, y) to the nearest unit of list (0 when empty)
function L.E7.nearest(list, x, y)
  local md
  for _, e in ipairs(list) do local d = dist(e.x, e.y, x, y); if md == nil or d < md then md = d end end
  return md or 0
end
-- The army's anchor (Ephraim, or the unit carrying him) and the cost of unit u standing on
-- (x, y) away from it: w a tile past 2 (twice that with the policy "hold").
function L.E7.cohesion(A, u, x, y, w)
  local a = A.EC or A.E
  if not a or a.id == u.id then return 0 end
  return w * (P.hold and 2 or 1) * math.max(0, dist(x, y, a.x, a.y) - 2)
end
-- Unit u's move costs from (x0, y0) within m points (its class's costs; red units block, the
-- others are passed): D[y * 256 + x].
function L.E7.flood(u, x0, y0, m)
  local W, H = L.mapSize()
  local ct = emu:read32(u.classPtr + 0x38)
  local D = { [y0 * 256 + x0] = 0 }
  local open, head = { { x0, y0 } }, 1
  while head <= #open do
    local cx, cy = open[head][1], open[head][2]; head = head + 1
    local cd = D[cy * 256 + cx]
    for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = cx + dv[1], cy + dv[2]
      if nx >= 0 and ny >= 0 and nx < W and ny < H and L.mapGet(L.ADDR.gBmMapUnit, nx, ny) < 0x80 then
        local c = L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, nx, ny)))
        if c > 0 then
          local nd, k = cd + c, ny * 256 + nx
          if nd <= m and (D[k] == nil or nd < D[k]) then D[k] = nd; open[#open + 1] = { nx, ny } end
        end
      end
    end
  end
  return D
end
-- A copy of unit u standing on (x, y), carrying (RESCUING) when `carry`.
function L.E7.at(u, x, y, carry)
  local c = {}
  for k, v in pairs(u) do c[k] = v end
  c.x, c.y = x, y
  if carry then c.state = c.state | L.US.RESCUING end
  return c
end
-- The cost of unit u (a copy, L.E7.at) ending its move on (x, y) with hp HP: E7's risk, the
-- boss's reach, and the reach of a monster that waits (L.E7.wake).
function L.E7.eCost(m, u, x, y, enemies, B, hp)
  local rk = L.E7.risk(u, x, y, enemies, nil, hp)
  return 100000 * rk.pDeath + 50000 * rk.pStone + 300 * rk.eDmg + (L.E7.inDK(B, x, y) and m.dkCost or 0)
    + (L.E7.inWake(x, y) and m.wakeCost or 0), rk
end
-- The monsters that wait (AI2 3, NeverMove, and no blue unit in their reach now: a Draco Zombie
-- by him): left alone, they never act; a unit in their reach at an enemy phase wakes them (in a
-- try one walked out after Eirika and took her to 1 HP). L.E7.wake is set per decision (fin23).
function L.E7.waiting(B, enemies)
  local list, blue = {}, L.blue()
  for _, e in ipairs(enemies) do
    if not (B and e.id == B.id) and emu:read8(e.addr + 0x44) == 3 and reachSets and reachSets[e] then
      local on = false
      for _, b in ipairs(blue) do if reachSets[e][b.y * 256 + b.x] then on = true end end
      if not on then list[#list + 1] = e end
    end
  end
  return list
end
function L.E7.inWake(x, y)
  for _, e in ipairs(L.E7.wake or {}) do if reachSets[e] and reachSets[e][y * 256 + x] then return true end end
  return false
end
-- Rescuer Y's part of a chip: from a tile r next to (xt, yt) (where X stands, or is about to)
-- that Y reaches this turn (cost(r): its move's cost, or nil), Rescue, then Canto with the
-- move it has left (UNIT_MOV - the move's cost, TryMakeCantoUnit) to the tile c of the least
-- cost (L.E7.eCost, carrying). `gone`: tiles that are free by then (X's old tile, Y's own).
-- Returns { r, c, cost, p (Y's death chance at c) } or nil.
function L.E7.rescuePlan(m, Y, xt, yt, cost, gone, enemies, B)
  local W, H = L.mapSize()
  local best
  for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
    local rx, ry = xt + dv[1], yt + dv[2]
    local d = rx >= 0 and ry >= 0 and rx < W and ry < H and cost(rx, ry)
    if d and Y.mov - d > 0 then
      local D = L.E7.flood(Y, rx, ry, Y.mov - d)
      for k, _ in pairs(D) do
        local cx, cy = k % 256, k // 256
        local o = L.mapGet(L.ADDR.gBmMapUnit, cx, cy)
        if o == 0 or gone[k] or (cx == xt and cy == yt) then
          local c, rk = L.E7.eCost(m, L.E7.at(Y, cx, cy, true), cx, cy, enemies, B)
          c = c + noise(4, Y.id, cy * 64 + cx, 0) * 8
          if best == nil or c < best.cost then best = { r = { x = rx, y = ry }, c = { x = cx, y = cy }, cost = c, p = rk.pDeath } end
        end
      end
    end
  end
  return best
end
-- The chip: Ephraim (A.E, not yet moved) attacks B with the Siegmund from a tile next to B, and
-- a rescuer (A.F or A.S, free, carrying nobody, aid at least his con) gets him out
-- (L.E7.rescuePlan). Returns the best { t, slot, Y, r, c, pD (his death in the fight), pY,
-- dealt (expected damage), pK } or nil.
function L.E7.chipPlan(m, A, B, tiles, ok, enemies)
  local E = A.E
  if not E or not B then return nil end
  local best
  local bb = L.E7.bu(B, B.weapon, B.x, B.y)
  for _, wc in ipairs(L.E7.weapons(E)) do
    if wc.w.id == m.lance then
      for _, t in ipairs(tiles) do
        if ok(t) and dist(t.x, t.y, B.x, B.y) == 1 then
          local o = L.E7.fight(L.E7.bu(E, wc.w, t.x, t.y), bb, 1)
          local pK, pD, dealt = 0, 0, 0
          for _, x in ipairs(o) do
            if x.bh <= 0 then pK = pK + x.p end
            if x.ah <= 0 then pD = pD + x.p end
            dealt = dealt + x.p * (B.hp - math.max(0, x.bh))
          end
          for _, Y in ipairs({ A.F, A.S }) do
            if Y and Y.id ~= E.id and L.E7.free(Y) and (Y.state & L.US.RESCUING) == 0 and unitAid(Y) >= E.con then
              local D = L.E7.flood(Y, Y.x, Y.y, Y.mov)
              local gone = { [E.y * 256 + E.x] = true, [Y.y * 256 + Y.x] = true }
              local function cost(x, y)
                local k = y * 256 + x
                local o2 = L.mapGet(L.ADDR.gBmMapUnit, x, y)
                if (o2 == 0 or gone[k]) and not (x == t.x and y == t.y) then return D[k] end
              end
              local rp = L.E7.rescuePlan(m, Y, t.x, t.y, cost, gone, enemies, B)
              if rp and rp.cost < 3000 then
                local v = 10 * dealt - 1000 * pD - rp.cost + noise(3, E.id, t.y * 64 + t.x, Y.id) * 20
                if best == nil or v > best.v then
                  best = { v = v, t = t, slot = wc.slot, Y = Y, r = rp.r, c = rp.c, pD = pD, pY = rp.p, pK = pK, dealt = dealt }
                end
              end
            end
          end
        end
      end
    end
  end
  return best
end
-- The strike: Seth (A.S) with the Audhulma and Ephraim (A.E) with the Siegmund attack B from two
-- tiles next to it in one player phase, in the better order; exact chances over both fights.
-- Returns { first = "S" or "E", s = { t, slot }, e = { t, slot }, pK, pD } when B dies at
-- m.strikeK or more and Ephraim's death in his fight stays under m.chipTh (both free).
function L.E7.eStrike(m, A, B)
  local S, E = A.S, A.E
  if not B or not S or not E or not L.E7.free(S) or not L.E7.free(E) then return nil end
  local function opts(u, id)
    local o = {}
    for _, wc in ipairs(L.E7.weapons(u)) do
      if wc.w.id == id then
        for _, t in ipairs(moveTiles(u)) do
          if dist(t.x, t.y, B.x, B.y) == 1 then o[#o + 1] = { t = t, w = wc.w, slot = wc.slot } end
        end
      end
    end
    return o
  end
  local so, eo = opts(S, m.sword), opts(E, m.lance)
  local best
  for _, a in ipairs(so) do
    for _, b in ipairs(eo) do
      if not (a.t.x == b.t.x and a.t.y == b.t.y) then
        for _, first in ipairs({ "S", "E" }) do
          local u1, x1, u2, x2 = S, a, E, b
          if first == "E" then u1, x1, u2, x2 = E, b, S, a end
          local pK, pD = 0, 0
          for _, o in ipairs(L.E7.fight(L.E7.bu(u1, x1.w, x1.t.x, x1.t.y), L.E7.bu(B, B.weapon, B.x, B.y), 1)) do
            if o.bh <= 0 then pK = pK + o.p
            elseif o.ah <= 0 then if first == "E" then pD = pD + o.p end
            else
              local b2 = L.E7.bu(B, B.weapon, B.x, B.y)
              b2.hp = o.bh
              for _, x in ipairs(L.E7.fight(L.E7.bu(u2, x2.w, x2.t.x, x2.t.y), b2, 1)) do
                if x.bh <= 0 then pK = pK + o.p * x.p end
                if x.ah <= 0 and first == "S" then pD = pD + o.p * x.p end
              end
            end
          end
          local v = pK - 3 * pD + noise(5, a.t.y * 64 + a.t.x, b.t.y * 64 + b.t.x, first == "S" and 1 or 2) * 0.002
          if best == nil or v > best.v then
            best = { v = v, first = first, s = { t = a.t, slot = a.slot }, e = { t = b.t, slot = b.slot }, pK = pK, pD = pD }
          end
        end
      end
    end
  end
  if best and best.pK >= m.strikeK and best.pD <= m.chipTh then return best end
  return nil
end
-- B's damage a hit on unit u with weapon w (his counter), and the HP u needs to outlive it.
function L.E7.dkHit(B, u, w)
  local d = L.E7.dmg(L.E7.bu(B, B.weapon, B.x, B.y), L.E7.bu(u, w, u.x, u.y))
  return d, d + 1
end
-- The heal item id that unit u (hp of maxHp) uses: an Elixir from m.elixirAt HP missing,
-- else a Vulnerary; either when the other is gone. Slot or nil.
function L.E7.healSlot(m, u)
  local miss = u.maxHp - u.hp
  local order = (miss >= m.elixirAt) and { 0x6D, 0x6C } or { 0x6C, 0x6D }
  for _, id in ipairs(order) do
    for k, it in ipairs(u.items) do if (it & 0xFF) == id then return k - 1 end end
  end
end
-- A heal item from ally a by Trade (the item healSlot would pick), from the tile next to a of
-- the least cost(t); u gives an item it cannot use when its five slots are full.
function L.E7.eTrade(m, u, a, tiles, ok, cost)
  local miss = u.maxHp - u.hp
  local slot
  for _, id in ipairs((miss >= m.elixirAt) and { 0x6D, 0x6C } or { 0x6C, 0x6D }) do
    for k, it in ipairs(a.items) do if not slot and (it & 0xFF) == id then slot = k - 1 end end
  end
  if not slot then return nil end
  local give
  if #u.items >= 5 then
    for k, it in ipairs(u.items) do
      if not give and not canUseWeapon(u, L.item(it & 0xFF)) and not HEAL_ITEMS[it & 0xFF] then give = k - 1 end
    end
    if not give then return nil end
  end
  local best, bestT
  for _, t in ipairs(tiles) do
    if ok(t) and dist(t.x, t.y, a.x, a.y) == 1 then
      local c = cost(t)
      if best == nil or c < best then best, bestT = c, t end
    end
  end
  if not bestT or best >= 100000 then return nil end
  return { dest = bestT, action = "trade", target = a.id, targetPos = { x = a.x, y = a.y }, takeSlot = slot, giveSlot = give }
end
-- Carrier u drops the unit it holds (h) on a tile next to its move's end d: d and the drop tile
-- out of every reach (h with its own HP), plus extra(x, y) for h's tile (or nil).
function L.E7.eDrop(m, u, h, tiles, ok, enemies, B, extra)
  local W, H = L.mapSize()
  local ct = emu:read32(h.classPtr + 0x38)
  local best, bestP
  for _, t in ipairs(tiles) do
    if ok(t) then
      local cu = L.E7.eCost(m, L.E7.at(u, t.x, t.y, false), t.x, t.y, enemies, B)
      for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local qx, qy = t.x + dv[1], t.y + dv[2]
        if qx >= 0 and qy >= 0 and qx < W and qy < H and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, qx, qy))) > 0
            and (L.mapGet(L.ADDR.gBmMapUnit, qx, qy) == 0 or (qx == u.x and qy == u.y)) and not (qx == t.x and qy == t.y) then
          local ch = L.E7.eCost(m, L.E7.at(h, qx, qy, false), qx, qy, enemies, B, h.hp)
          local c = 3 * ch + cu + (extra and extra(qx, qy) or 0) + noise(6, u.id, qy * 64 + qx, t.y * 64 + t.x) * 8
          if best == nil or c < best then best, bestP = c, { dest = t, action = "drop", dropAt = { x = qx, y = qy } } end
        end
      end
    end
  end
  return bestP, best
end
-- The tile of the least cost for u on the map now: E7's risk, his reach, `extra(t)`, and 1 for a
-- move (staying wins a tie).
function L.E7.ePick(m, u, tiles, ok, enemies, B, extra)
  return L.E7.pick(u, tiles, ok, function(t)
    return (L.E7.eCost(m, L.E7.at(u, t.x, t.y, false), t.x, t.y, enemies, B)) + (extra and extra(t) or 0)
      + ((t.x == u.x and t.y == u.y) and 0 or 1)
  end)
end
-- The rescue now: unit X (Ephraim first, then Seth) has acted on a tile in B's reach (his chip,
-- or the strike that left B standing); u (mounted, free, carrying nobody) rescues it
-- (L.E7.rescuePlan with u's movement map) and its Canto takes them out (think's canto).
function L.E7.rescueNow(m, u, A, B, tiles, ok, enemies)
  if not B or (u.state & L.US.RESCUING) ~= 0 or (unitAttr(u) & CA_MOUNTEDAID) == 0 then return nil end
  for _, X in ipairs({ A.E, A.S }) do
    if X and X.id ~= u.id and (X.state & (L.US.UNSELECTABLE | L.US.RESCUED)) == L.US.UNSELECTABLE
        and L.E7.inDK(B, X.x, X.y) and unitAid(u) >= X.con then
      local okT = {}
      for _, t in ipairs(tiles) do if ok(t) then okT[t.y * 256 + t.x] = true end end
      local function cost(x, y)
        if okT[y * 256 + x] then return L.mapGet(L.ADDR.gBmMapMovement, x, y) end
      end
      local rp = L.E7.rescuePlan(m, u, X.x, X.y, cost, { [u.y * 256 + u.x] = true }, enemies, B)
      if rp then
        log("E7i: rescue %02x at (%d,%d) from (%d,%d), Canto to (%d,%d) (death %.1f%%)", X.char, X.x, X.y, rp.r.x, rp.r.y,
          rp.c.x, rp.c.y, rp.p * 100)
        return { dest = rp.r, action = "rescue", target = X.id, targetPos = { x = X.x, y = X.y } }
      end
    end
  end
end
-- Ephraim's way to the boss: his distance field to the tiles next to B (L.E7.bfield).
function L.E7.eWay(u, B)
  if not B then return nil end
  L.E7.ch = L.playSt().chapter
  return L.E7.bfield(u, B)
end
-- The launch tiles of unit u's class: tiles it can stand on, out of B's reach and of the waiting
-- monsters', at most maxD on its way to the tiles next to B (L.E7.eWay: Ephraim's move, 7, for
-- his chip; 5 for a rescuer, which then has the Canto to get out); and u's distance field to
-- them (a field to the tiles next to B alone left Ephraim in a corner of the throne room for 90
-- turns: every nearer tile was in a reach).
function L.E7.launch(u, B, maxD)
  if not B then return nil end
  local D = L.E7.eWay(u, B)
  local W, H = L.mapSize()
  local ct = emu:read32(u.classPtr + 0x38)
  local goals = {}
  for y = 0, H - 1 do
    for x = 0, W - 1 do
      local d = D[y] and D[y][x]
      if d and d <= maxD and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0
          and not L.E7.inDK(B, x, y) and not L.E7.inWake(x, y) then goals[#goals + 1] = { x = x, y = y } end
    end
  end
  if #goals == 0 then return nil end
  return distanceField(u, goals, {}, false, false, nil)
end
function L.E7.gAt(G, x, y) return (G and G[y] and G[y][x]) or 60 end
function L.E7.ephraim23(m, u, tiles, enemies, ok, okOut, A, B, th)
  local turn = L.playSt().turn
  local movers, _, act = L.E7.movers(B, enemies, u.x, u.y)
  local D = L.E7.eWay(u, B)
  local sieg
  for _, wc in ipairs(L.E7.weapons(u)) do if wc.w.id == m.lance then sieg = wc end end
  local need = B and sieg and select(2, L.E7.dkHit(B, u, sieg.w)) or 999
  local function cost(t, hp) return (L.E7.eCost(m, L.E7.at(u, t.x, t.y, false), t.x, t.y, enemies, B, hp)) end
  -- turn 1 (he summons in his phase and attacks nobody; the eight land round him and first act
  -- on turn 2): the army holds its start tiles; Ephraim takes Franz's Elixir by Trade
  if turn == 1 then
    if A.F and (A.F.state & L.US.RESCUED) == 0 and itemCountOf(u, 0x6D) == 0 then
      local q = L.E7.eTrade(m, L.E7.at(u, u.x, u.y, false), A.F, tiles, okOut, function(t0)
        return cost(t0) + 3 * dist(t0.x, t0.y, u.x, u.y) end)
      if q then
        for k, it in ipairs(A.F.items) do if (it & 0xFF) == 0x6D then q.takeSlot = k - 1 end end
        log("E7i: turn 1, Ephraim takes the Elixir from %02x by Trade", A.F.char); return q
      end
    end
    return { dest = { x = u.x, y = u.y }, action = "wait" }
  end
  -- the strike, Ephraim's part (after Seth's attack this turn) or first (L.E7.eStrike)
  local sp = B and L.E7.eStrike(m, A, B)
  if sp and sp.first == "E" then
    log("E7i: strike, Ephraim first (%.0f%%; his death %.1f%%)", sp.pK * 100, sp.pD * 100)
    return { dest = sp.e.t, action = "attack", target = B.id, targetPos = { x = B.x, y = B.y }, wslot = sp.e.slot }
  end
  if B and sieg and L.E7.strikeE and A.S and not L.E7.free(A.S) and dist(A.S.x, A.S.y, B.x, B.y) == 1 and u.hp >= need then
    local p = L.E7.attack(u, tiles, enemies, ok, B, m.chipTh, D, { bossOk = true, only = B, slot = sieg.slot })
    if p then log("E7i: strike, Ephraim's part (death %.1f%%)", p.pDeath * 100); return p end
  end
  -- the chip (only when no monster is left: he is the best at killing them)
  if B and sieg and movers == 0 and u.hp >= need and not P.safe[u.id] then
    local cp = L.E7.chipPlan(m, A, B, tiles, ok, enemies)
    if cp and cp.pD <= m.chipTh then
      log("E7i: chip from (%d,%d) (%.1f expected, kill %.0f%%, death %.1f%%); %02x rescues from (%d,%d), Canto to (%d,%d) (death %.1f%%)",
        cp.t.x, cp.t.y, cp.dealt, cp.pK * 100, cp.pD * 100, cp.Y.char, cp.r.x, cp.r.y, cp.c.x, cp.c.y, cp.pY * 100)
      return { dest = cp.t, action = "attack", target = B.id, targetPos = { x = B.x, y = B.y }, wslot = cp.slot }
    end
  end
  -- heal: below the HP a chip needs (no monster left), or (monsters left) under 60% or when the
  -- tile of the least risk still has a death chance over th
  local hurt = u.hp < u.maxHp and ((movers == 0 and u.hp < need) or u.hp * 5 < u.maxHp * 3)
  if not hurt and movers > 0 and u.hp < u.maxHp then
    local t0 = L.E7.ePick(m, u, tiles, okOut, enemies, B)
    local _, rk2 = L.E7.eCost(m, L.E7.at(u, t0.x, t0.y, false), t0.x, t0.y, enemies, B)
    hurt = rk2.pDeath > th
  end
  if hurt then
    local hs = L.E7.healSlot(m, u)
    if hs then
      local t = L.E7.pick(u, tiles, okOut, function(t0) return cost(t0, u.maxHp) end)
      log("E7i: Ephraim heals (item %02x) at (%d,%d), %d/%d HP", u.items[hs + 1] & 0xFF, t.x, t.y, u.hp, u.maxHp)
      return { dest = t, action = "item", slot = hs }
    end
    for _, a in ipairs({ A.F, A.R, A.S }) do
      if a and a.id ~= u.id and (a.state & L.US.RESCUED) == 0 then
        local q = L.E7.eTrade(m, u, a, tiles, okOut, function(t0) return cost(t0) end)
        if q then log("E7i: Ephraim takes a heal item from %02x by Trade", a.char); return q end
      end
    end
  end
  -- the monsters: E7's best attack (Siegmund kept at m.keepSieg uses for him), out of his reach;
  -- else the tile of the least risk, 3 a tile nearer the nearest monster (one that waits out
  -- of reach is gone after)
  if movers > 0 then
    local p, ps = L.E7.attack(u, tiles, enemies, okOut, B, th, nil, { keepSieg = m.keepSieg })
    local t, ts = L.E7.ePick(m, u, tiles, okOut, enemies, B, function(t0) return 3 * L.E7.nearest(act, t0.x, t0.y) end)
    if p and (ts == nil or ps > -ts) then return p end
    return { dest = t, action = "wait" }
  end
  -- staging: out of every reach, on (or on the way to) a launch tile (a chip next turn)
  local G = L.E7.launch(u, B, u.mov)
  local t = L.E7.ePick(m, u, tiles, okOut, enemies, B, function(t0) return 40 * L.E7.gAt(G, t0.x, t0.y) end)
  return { dest = t, action = "wait" }
end
-- Seth (S) and Franz (F): the strike (Seth), the rescue of a unit left in B's reach, the drop of
-- the unit it carries, Eirika's carry while monsters that move are left (Franz), the monsters
-- (Seth with his other sword, the Audhulma kept), and the staging near Ephraim.
function L.E7.mounted23(m, u, tiles, enemies, ok, okOut, A, B, th)
  local movers = L.E7.movers(B, enemies)
  local isS = A.S and u.id == A.S.id
  -- the strike (Seth first, or his part after Ephraim's attack this turn)
  if isS and B then
    local sp = L.E7.eStrike(m, A, B)
    if sp and sp.first == "S" then
      log("E7i: strike, Seth first (%.0f%%; Ephraim's death %.1f%%)", sp.pK * 100, sp.pD * 100)
      return { dest = sp.s.t, action = "attack", target = B.id, targetPos = { x = B.x, y = B.y }, wslot = sp.s.slot }
    end
    if A.E and not L.E7.free(A.E) and (A.E.state & L.US.RESCUED) == 0 and dist(A.E.x, A.E.y, B.x, B.y) == 1 and L.E7.strikeE then
      for _, wc in ipairs(L.E7.weapons(u)) do
        if wc.w.id == m.sword then
          local p = L.E7.attack(u, tiles, enemies, ok, B, 0.5, nil, { bossOk = true, only = B, slot = wc.slot })
          if p then log("E7i: strike, Seth's part"); return p end
        end
      end
    end
  end
  -- the rescue now
  local rn = L.E7.rescueNow(m, u, A, B, tiles, ok, enemies)
  if rn then return rn end
  -- the unit it carries: Ephraim or Seth dropped next turn; Eirika once no monster that moves is left
  local h = L.E7.held(u)
  if h then
    equip(h)
    local isE = h.char == L.CHAR.EPHRAIM
    if h.char ~= L.CHAR.EIRIKA or movers == 0 then
      -- (Ephraim: as near his way as is safe, for a chip the turn after; Eirika: 8 or more from him)
      local goal = isE and L.E7.launch(h, B, h.mov) or nil
      local extra = function(qx, qy)
        if goal then return 12 * L.E7.gAt(goal, qx, qy) end
        if h.char == L.CHAR.EIRIKA and B then return 500 * math.max(0, 8 - dist(qx, qy, B.x, B.y)) end
        return 0
      end
      local p, c = L.E7.eDrop(m, u, h, tiles, okOut, enemies, B, extra)
      if p and c < 30000 then
        log("E7i: %02x drops %02x at (%d,%d) from (%d,%d)", u.char, h.char, p.dropAt.x, p.dropAt.y, p.dest.x, p.dest.y)
        return p
      end
    end
    -- (carrying: E7's carrier, out of his reach)
    local function cost(t, hp) return (L.E7.eCost(m, L.E7.at(u, t.x, t.y, false), t.x, t.y, enemies, B, hp)) end
    if u.hp * 10 < u.maxHp * 7 then
      local hs = healItemSlot(u)
      if hs then return { dest = L.E7.pick(u, tiles, okOut, function(t) return cost(t, u.maxHp) end), action = "item", slot = hs } end
    end
    local p = L.E7.attack(u, tiles, enemies, okOut, B, 0.002, nil)
    if p and p.pK >= 0.95 then return p end
    return { dest = L.E7.ePick(m, u, tiles, okOut, enemies, B, function(t) return L.E7.cohesion(A, u, t.x, t.y, 25) end), action = "wait" }
  end
  -- Franz, turn 1 (or while monsters that move are left): Eirika picked up (E7's carrier)
  if not isS and A.R and (A.R.state & L.US.RESCUED) == 0 and movers + ((L.playSt().turn == 1) and 1 or 0) > 0 and unitAid(u) >= A.R.con then
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) and dist(t.x, t.y, A.R.x, A.R.y) == 1 then
        local c = L.E7.eCost(m, L.E7.at(u, t.x, t.y, true), t.x, t.y, enemies, B)
        if best == nil or c < best then best, bestT = c, t end
      end
    end
    if bestT then return { dest = bestT, action = "rescue", target = A.R.id, targetPos = { x = A.R.x, y = A.R.y } } end
  end
  -- Seth: his first weapon other than the Audhulma equipped (the counters), the Audhulma kept
  local other
  for _, wc in ipairs(L.E7.weapons(u)) do if wc.w.id ~= m.sword and not other then other = wc end end
  if isS and other and u.weapon and u.weapon.id == m.sword then
    log("E7i: Seth equips %02x (the Audhulma kept)", other.w.id)
    return { dest = L.E7.ePick(m, u, tiles, okOut, enemies, B), action = "equip", slot = other.slot }
  end
  -- turn 1: he summons, nobody attacks (the army holds)
  if L.playSt().turn == 1 then return { dest = { x = u.x, y = u.y }, action = "wait" } end
  local function cost(t, hp) return (L.E7.eCost(m, L.E7.at(u, t.x, t.y, false), t.x, t.y, enemies, B, hp)) end
  if u.hp * 5 < u.maxHp * 3 then
    local hs = healItemSlot(u)
    if hs then return { dest = L.E7.pick(u, tiles, okOut, function(t) return cost(t, u.maxHp) end), action = "item", slot = hs } end
  end
  -- the monsters (Seth: never with the Audhulma)
  if #enemies > ((B and 1) or 0) then
    local p = L.E7.attack(u, tiles, enemies, okOut, B, th, nil, { slot = isS and other and other.slot or nil, keepSieg = m.keepSieg })
    if p and not (isS and not other) then return p end
  end
  -- staging, out of every reach: while monsters are left, by Ephraim (L.E7.cohesion); then about 4
  -- from the tiles next to him (a rescue: the chip's tile and the Canto out within 8)
  local G = movers == 0 and L.E7.launch(u, B, 5) or nil
  local t = L.E7.ePick(m, u, tiles, okOut, enemies, B, function(t0)
    if G then return 30 * L.E7.gAt(G, t0.x, t0.y) end
    return L.E7.cohesion(A, u, t0.x, t0.y, 10)
  end)
  return { dest = t, action = "wait" }
end
-- The director (think, chapterIndex 0x23 with ROUTE_E7=1).
function L.E7.fin23(u, tiles, enemies, ok, canto)
  L.E7.cache = {}
  L.E7.ch = L.playSt().chapter
  local m = L.E7.cfg()
  local A = L.E7.eArmy(m)
  local B = L.E7.eBoss(m, enemies)
  if B then equip(B) end
  local th = L.E7.theta(u)
  L.E7.wake = L.E7.waiting(B, enemies)
  -- (out of his reach and of the waiting monsters': a move's end there only for a chip, the
  -- strike or a rescue; ok otherwise)
  local okOut = function(t) return ok(t) and not L.E7.inDK(B, t.x, t.y) and not L.E7.inWake(t.x, t.y) end
  if canto then
    local h = L.E7.held(u)
    local goal = h and h.char == L.CHAR.EPHRAIM and L.E7.launch(u, B, 5) or nil
    return { dest = L.E7.ePick(m, u, tiles, ok, enemies, B, function(t)
      if not goal then return L.E7.cohesion(A, u, t.x, t.y, (h and h.char == L.CHAR.EIRIKA) and 25 or 10) end
      return 10 * L.E7.gAt(goal, t.x, t.y)
    end), action = "wait" }
  end
  if u.char == L.CHAR.EPHRAIM then return L.E7.ephraim23(m, u, tiles, enemies, ok, okOut, A, B, th) end
  if u.char == L.CHAR.EIRIKA then
    return { dest = L.E7.ePick(m, u, tiles, okOut, enemies, B, function(t)
      return ((t.x == u.x and t.y == u.y) and 0 or 1) + (B and 50 * math.max(0, 8 - dist(t.x, t.y, B.x, B.y)) or 0)
    end), action = "wait" }
  end
  return L.E7.mounted23(m, u, tiles, enemies, ok, okOut, A, B, th)
end
-- The unit order of a player phase on map 0x23: the strike (Seth or Ephraim first); else the
-- carriers of Ephraim or Seth (their drop), Ephraim (his chip, then his rescuer), the others,
-- Eirika's carrier, Eirika.
function L.E7.order23(order)
  local m = L.E7.cfg()
  local A = L.E7.eArmy(m)
  local B = L.E7.eBoss(m)
  L.E7.strikeE = nil
  if B then equip(B) end
  local o, seen = {}, {}
  local function add(u) if u and not seen[u.id] then o[#o + 1] = u; seen[u.id] = true end end
  local sp = B and L.E7.eStrike(m, A, B)
  if sp then
    L.E7.strikeE = true
    log("E7i: strike this turn (%.0f%%, %s first; Ephraim's death %.1f%%)", sp.pK * 100, sp.first, sp.pD * 100)
    if sp.first == "S" then add(A.S); add(A.E) else add(A.E); add(A.S) end
  end
  for _, u in ipairs(order) do if L.E7.held(u) and not (A.RC and u.id == A.RC.id) then add(u) end end
  add(A.E)
  for _, u in ipairs(order) do if not (A.RC and u.id == A.RC.id) and u.char ~= L.CHAR.EIRIKA then add(u) end end
  for _, u in ipairs(order) do add(u) end
  return o
end
-- The turn search's view of a turn on map 0x23 (searchBranch): each point of his HP taken
-- 300, an Elixir use spent 1,200 and a Vulnerary's 350 (the heals pay for the chips),
-- Ephraim's HP 25 a point, a petrified unit 80,000, Ravager equipped (he attacked) 20,000.
function L.E7.heals23()
  local n = { [0x6C] = 0, [0x6D] = 0 }
  local hp
  for i = 0, 61 do
    local b = L.readUnit(L.ADDR.gUnitArrayBlue + i * 0x48)
    if b and (b.state & (L.US.DEAD | L.US.NOT_DEPLOYED)) == 0 then
      for _, it in ipairs(b.items) do if n[it & 0xFF] then n[it & 0xFF] = n[it & 0xFF] + (it >> 8) end end
      if b.char == L.CHAR.EPHRAIM then hp = b.hp end
    end
  end
  return n, hp or 0
end
function L.E7.base23()
  local m = L.E7.cfg()
  local B = L.E7.eBoss(m)
  local n, hp = L.E7.heals23()
  return { bossHp = B and B.hp or 0, elx = n[0x6D], vul = n[0x6C], ehp = hp }
end
function L.E7.score23(base)
  local m = L.E7.cfg()
  local B = L.E7.eBoss(m)
  local n, hp = L.E7.heals23()
  local s = -1200 * math.max(0, base.elx - n[0x6D]) - 350 * math.max(0, base.vul - n[0x6C]) + 25 * hp
  for i = 0, 61 do
    local b = L.readUnit(L.ADDR.gUnitArrayBlue + i * 0x48)
    if b and (b.state & (L.US.DEAD | L.US.NOT_DEPLOYED)) == 0 then
      local st = emu:read8(b.addr + 0x30) & 0x0F
      if st == 11 or st == 13 then s = s - 80000 end
    end
  end
  if B then
    s = s + 300 * math.max(0, base.bossHp - B.hp)
    if B.items[1] and (B.items[1] & 0xFF) == m.ravager then s = s - 20000 end
  end
  return s
end

local function think(u)
  local ps = L.playSt()
  if L.GAL.on then return L.GAL.think(u) end   -- (the item gallery, ROUTE_GALLERY=1: L.GAL below)
  V2 = tacticsV2(ps.chapter)
  V3 = tacticsV3(ps.chapter)
  V5 = tacticsV5(ps.chapter)
  V7 = tacticsV7(ps.chapter)
  V8 = tacticsV8(ps.chapter)
  V9 = tacticsV9(ps.chapter)
  V10 = tacticsV10(ps.chapter)
  E6 = tacticsE6(ps.chapter)
  SACRED2 = E6 and ps.chapter >= 0x14
  L.E7.on = L.E7.chapter(ps.chapter)
  L.E7.ch = ps.chapter
  L.E7.keepWay = false   -- (set below, once this unit's role is known)
  local E5 = tacticsE5(ps.chapter)
  local enemies = L.red()
  if V3 then equip(u); equipAll(enemies) end
  -- v9 siege: every enemy left is a boss whose AI never leaves its tile (AI1
  -- 3 to 5, "action standing": River of Regrets, Lyon on the gate (20,6),
  -- which only (20,7) touches). The pessimistic model never let anyone attack
  -- him (his counter and his enemy-phase spell, each with the +1 margin,
  -- topped every unit's HP), and the army waited from turn 14 to 60. In a
  -- siege the damage is exact, a healthy unit (at least 3/4 of its HP, not a
  -- lord) attacks the boss wherever it outlives the counter, and the hurt ones
  -- heal (items, or the healing tiles) out of his reach.
  -- (PR: the post-game floors' siege, L.PR.siege: every enemy left stands, boss or not; Tower floor 8,
  -- scratch et8a: a Cyclops with a Killer Axe, AI1 3, alone on (11,2) from turn 10, and the army waited
  -- out of its reach to turn 50)
  SIEGE = false
  L.PR.siege = false
  if (V9 or L.PR.floor(ps.chapter)) and #enemies > 0 then
    SIEGE = true
    for _, e in ipairs(enemies) do
      local ai1 = emu:read8(e.addr + 0x42)
      if not ((isBoss(e) or L.PR.floor(ps.chapter)) and ai1 >= 3 and ai1 <= 5) then SIEGE = false end
    end
    L.PR.siege = SIEGE and L.PR.floor(ps.chapter)
  end
  EXACT = SIEGE
  reachSets = nil
  if V2 then
    reachSets = {}
    local loose = (V10 or L.PR.back(ps.chapter, u)) and isLordUnit(u)   -- v10: see enemyReach (PR: L.PR.BACK too)
    local walls = L.PR.back(ps.chapter, u) and isLordUnit(u)   -- (PR: L.PR.BACK, the cracked walls open)
    for _, e in ipairs(enemies) do reachSets[e] = enemyReach(e, u, nil, loose, walls) end
  end
  spared = nil
  if tacticsV6(ps.chapter) then
    spared = {}
    local army0 = L.blue()
    for _, e in ipairs(enemies) do if spareForTalk(ps.chapter, e, army0) then spared[e] = true end end
  end
  madAllies = nil
  if tacticsV7(ps.chapter) and reachSets then
    madAllies = {}
    for _, b in ipairs(L.blue()) do
      if b.id ~= u.id and isBerserk(b) then
        equip(b); madAllies[#madAllies + 1] = b; reachSets[b] = enemyReach(b, u, true)
      end
    end
  end
  local tiles = reachable(u)
  local isLord = isLordUnit(u) or (E5 and lordLike(u))   -- v5: a lord's carrier plays as a lord
  local seizer = isLord           -- v1, v2: either lord; v3: the game's rule
  if V3 then seizer = canSeize(u) end
  local seize = seizeTile(ps.chapter)
  local bossAlive = false
  for _, e in ipairs(enemies) do if isBoss(e) then bossAlive = true end end
  -- v10: what keeps the lord off the seize tile is the unit on it; any other
  -- boss may live (Darkling Woods: Riev, CA_BOSS, beside Morva on the tile).
  if V10 and seize then bossAlive = L.mapGet(L.ADDR.gBmMapUnit, seize.x, seize.y) >= 0x80 end
  -- v7: a boss that cannot fight back (no usable weapon left; Ephraim's
  -- Chapter 14: Vigarde's Spear wore out on the throne, which has one tile
  -- next to it) is the lord's: the others keep off the tiles next to it, and
  -- the lord moves first (handleMainIdle) and walks to it.
  local unarmedBoss
  if V7 then
    for _, e in ipairs(enemies) do if isBoss(e) and not e.weapon then unarmedBoss = e end end
  end
  -- E6, chapter index 20 on: only the boss on the seize tile holds the seize up
  local sb = seizeBoss(ps.chapter, enemies)
  if E6 and seize and ps.chapter >= 0x14 then bossAlive = sb ~= nil end
  -- (v6, chapter index 20 on, while sb stands: a unit other than a lord whose every usable
  -- weapon is effective against sb, a "rusher" (Myrrh, the Divinestone), keeps them for
  -- sb (it attacks nothing else, otherWeapon) and walks to sb outside any zone policy; with
  -- the zone policies, which the search preferred, she stayed by the lords. The others keep
  -- by the lords: in a try where Ephraim and Franz walked to sb too, Eirika was left alone
  -- in the south-east and died)
  local rusher = SACRED2 and sb and not isLord and u.weapon ~= nil and otherWeapon(u, sb) == nil
    and not L.E7.on   -- (E7: no rusher; Myrrh strikes Morva at the end, v10's strike, and keeps her uses till then)
  -- (and the walks to sb, and the seizer's to the seize tile, ignore the red units on the
  -- way: Chapter 20's ring of monsters closes every way for the mounted units, who then
  -- never moved; the attacks clear the way)
  local bossWalk = false
  local canto = (u.state & L.US.HAS_MOVED) ~= 0
  -- v6 (PROTECT): the unit to protect while it is on the map, and whether this unit carries it
  local guardE = protectedUnitE(ps.chapter)
  local carryingProt = protectOn(ps.chapter) and carriesProtected(u)
  -- v4 hold-out mode: the lord (moved first this turn) is the others' anchor.
  local defend = defendMode(ps.chapter)   -- v5: or a rout played as a hold-out (routDefend)
  -- v6 policy "hold": this unit stands by the nearest lord (or carrier) as in a hold-out,
  -- when it does not attack (its attacks are as without the policy)
  local holdU = defend or (P.hold and not isLord)
  local lordPos
  if (holdU or P.zone) and not isLord then
    for _, b in ipairs(L.blue()) do
      if isLordUnit(b) or (E5 and lordLike(b)) then
        -- v5: stand by the nearest lord (or lord's carrier). Ephraim joins Eirika's Chapter 15 far away
        -- in the south-west with Duessel and Knoll; with everyone standing by Eirika
        -- he was left alone, ran out of Vulneraries and died on turn 15 in a trial.
        if not E5 then lordPos = b; break end
        if lordPos == nil or dist(u.x, u.y, b.x, b.y) < dist(u.x, u.y, lordPos.x, lordPos.y) then lordPos = b end
      end
    end
  end
  -- v9: the unit to protect (PROTECT), and whether this unit carries one.
  local guard = V9 and protectedUnit(ps.chapter)
  local carrying = V9 and (u.state & L.US.RESCUING) ~= 0

  -- v6 policy "zone": a unit other than a lord ends its move (and attacks from) within
  -- P.zone tiles of the nearest lord or lord's carrier; one that cannot reach the
  -- zone this turn walks toward it.
  local zoneOn = false
  if P.zone and not isLord and lordPos and not rusher   -- (v6, chapter index 20 on: not a "rusher")
      and not (L.F.talkRush and L.F.talkRush.by == u.char) then   -- (coverage 12, ROUTE_TALKRUSH: nor the recruiter)
    for _, t in ipairs(tiles) do
      if dist(t.x, t.y, lordPos.x, lordPos.y) <= P.zone then zoneOn = true; break end
    end
  end
  -- Only the lord may end a move on the seize tile; anyone else there blocks the seize.
  local function ok(t)
    if not seizer and seize and t.x == seize.x and t.y == seize.y then return false end
    if unarmedBoss and not isLord and dist(t.x, t.y, unarmedBoss.x, unarmedBoss.y) == 1 then return false end
    if zoneOn and dist(t.x, t.y, lordPos.x, lordPos.y) > P.zone then return false end
    if I11.noStop(ps.chapter, t.x, t.y) then return false end   -- (I11, part 1: the event areas)
    -- (E7: nobody stands in the seize tile's holder's reach (2 tiles: Morva's Wretched Air, 36
    -- through any defence) but for the strike, L.E7.striking)
    if L.E7.on and sb and dist(t.x, t.y, sb.x, sb.y) <= 2 and not L.E7.striking then return false end
    -- (E7: Eirika's carrier and the vanguard keep to her way, L.E7.way: Franz, carrying her,
    -- attacked a Cyclops from (19,6) and went north from there alone)
    if L.E7.keepWay and not L.E7.way(t.x, t.y) and not L.E7.striking then return false end
    return not badDest[u.id .. ":" .. t.x .. ":" .. t.y]
  end

  -- (E7 on Ephraim's map, chapterIndex 0x23 with ROUTE_E7=1: the director, L.E7.fin23 above)
  if L.E7.eph(ps.chapter) then
    local p23 = L.E7.fin23(u, tiles, enemies, ok, canto)
    if p23 then return p23 end
  end
  -- (E7, the final chapter, indices 0x15 and 0x16: the director, L.E7.fin above)
  if L.E7.on and ps.chapter >= 0x15 then
    local p7 = L.E7.fin(u, tiles, enemies, ok, canto)
    if p7 then return p7 end
  end
  -- (R10, the Lagdou Ruins' last floor, index 0x37: the director, L.R10.think above)
  if L.R10.on(ps.chapter) then return L.R10.think(u, tiles, enemies, ok, canto) end

  -- Canto: move to the safest tile, then Wait.
  if canto then
    -- (v6, chapter index 20 on: a unit next to the seize tile stays: after dropping Eirika
    -- on it, her carrier keeps the only open tile next to it)
    if SACRED2 and seize and dist(u.x, u.y, seize.x, seize.y) == 1 then return { dest = { x = u.x, y = u.y }, action = "wait" } end
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) then
        local th = threatAt(u, t.x, t.y, enemies)
        if best == nil or th < best then best, bestT = th, t end
      end
    end
    bestT = bestT or { x = u.x, y = u.y }
    return { dest = bestT, action = "wait" }
  end

  -- (feature routes and the post-game walls: L.F.think; nil leaves the usual rules)
  if L.F.on or L.F.WALLS[ps.chapter] then
    local fp = L.F.think(u, tiles, enemies, ok, ps)
    if fp then return fp end
  end

  -- (I11, part 2, turn 1: the Demon King summons on four sides of himself in his first enemy
  -- phase (AiTryDKSummon, turn 1) and attacks nobody; the army holds its start tiles (rows 13-15,
  -- out of his reach) and only uses the supply and Trade (the bait's Elixir). In a first try the
  -- whole army walked up to him on turn 1 and the eight monsters landed round it.)
  if ps.chapter == 0x23 and ps.turn == 1 then
    if V3 then
      local p = support(u, tiles, enemies, ok)
      if p and (p.action == "supply" or p.action == "trade") then return p end
    end
    return { dest = { x = u.x, y = u.y }, action = "wait" }
  end

  -- Seize when the lord can stand on the seize tile and the boss is gone.
  if seizer and seize and not bossAlive then
    for _, t in ipairs(tiles) do
      if t.x == seize.x and t.y == seize.y and ok(t) then
        return { dest = t, action = "seize" }
      end
    end
  end
  -- v6, chapter index 20 on ("carry"): with the seize tile's boss gone and the tile free,
  -- Eirika's carrier drops her on it from the safest tile next to it it can reach (the unit
  -- menu's Drop, overrideId 0x64; the drop target by its position); she seizes on the next
  -- turn. A dropped unit cannot act on the turn of its drop (UnitDrop, src/bmunit.c).
  local carrier = SACRED2 and seize and carriesLord(u)
  -- (E7: the way ahead of Eirika, L.E7.front; vanguard: this unit clears it, Ephraim, and the
  -- others once they keep no weapon for the seize tile's boss)
  local fr = L.E7.on and seize and L.E7.front(seize) or nil
  local dsUses = L.E7.on and L.E7.stoneUses(u) or nil
  -- (Myrrh too, while she has more than L.E7.KEEP uses and 2 for the holder: alone, Ephraim
  -- took 50 turns to clear the southern way)
  local vanguard = fr and not carrier and not seizer and fr.c.id ~= u.id and u.weapon ~= nil
    and (isLordUnit(u) or not (sb and otherWeapon(u, sb) == nil) or (dsUses and dsUses > L.E7.KEEP + 2))
    and not (dsUses and dsUses <= L.E7.KEEP + (sb and 2 or 0))
  L.E7.keepWay = (fr ~= nil and ((carrier and fr.c.id == u.id) or vanguard)) or false
  -- (E7, the final chapter: Ephraim leads to its boss, attacking what he meets on the way)
  local lead
  if L.E7.on and not seize and isLordUnit(u) and u.char ~= L.CHAR.EIRIKA and u.weapon then
    for _, e in ipairs(enemies) do if e.char == L.E7.BOSS[ps.chapter] then lead = e end end
  end
  -- (the carrier is Ephraim, the lord who cannot seize: mounted, and Franz stays free to
  -- fight; he rescues her from the safest tile next to her he can reach, not on turn 1
  -- while she is safe; a rescue allows the canto)
  -- (E7: not Ephraim but the toughest mounted fighter that is not a lord, Franz, carries her
  -- (below): Ephraim, with Siegmund, clears the way, and a lord carrying her is two lords at risk)
  -- (and in the final chapter, where nothing is seized, for the whole chapter)
  if L.E7.on and not isLordUnit(u) and not carrier and (unitAttr(u) & 1) ~= 0 and isFighter(u) then
    local eir, other
    for _, b in ipairs(L.blue()) do
      if b.char == L.CHAR.EIRIKA then eir = b end
      if b.id ~= u.id and not isLordUnit(b) and (unitAttr(b) & 1) ~= 0 and isFighter(b) and not carriesLord(b)
          and b.maxHp + 2 * b.def > u.maxHp + 2 * u.def then other = b end
      if carriesLord(b) then other = b end
    end
    -- (once her own supply plan is done: she moves first, Supply, Wait; and not once she
    -- stands on the seize tile, where her carrier dropped her)
    local sp = eir and supplyTried[ps.chapter .. ":" .. eir.id] == nil and supplyPlanFor(equip(eir))
    -- (nor when she stands on a staging tile while the seize tile's holder lives: her carrier
    -- dropped her there for the strike; in a try Franz, moving first, picked her up again)
    local staged = eir and sb and seize and stagingTiles(eir, seize, enemies)[eir.y * 256 + eir.x] ~= nil
    if eir and not other and not sp and unitAid(u) >= eir.con and u.hp * 2 >= u.maxHp
        and not (seize and eir.x == seize.x and eir.y == seize.y) and not staged then
      local best, bestT
      for _, t in ipairs(tiles) do
        if ok(t) and dist(t.x, t.y, eir.x, eir.y) == 1 then
          local th = threatAt(u, t.x, t.y, enemies)
          if best == nil or th < best then best, bestT = th, t end
        end
      end
      if bestT then return { dest = bestT, action = "rescue", target = eir.id, targetPos = { x = eir.x, y = eir.y } } end
    end
  end
  if SACRED2 and seize and isLordUnit(u) and not seizer and not carrier and (unitAttr(u) & 1) ~= 0 and not L.E7.on then
    for _, b in ipairs(L.blue()) do
      if b.char == L.CHAR.EIRIKA and unitAid(u) >= b.con and not (ps.turn == 1 and threatAt(b, b.x, b.y, enemies) * 2 < b.hp) then
        local best, bestT
        for _, t in ipairs(tiles) do
          if ok(t) and dist(t.x, t.y, b.x, b.y) == 1 then
            local th = threatAt(u, t.x, t.y, enemies)
            if best == nil or th < best then best, bestT = th, t end
          end
        end
        if bestT then return { dest = bestT, action = "rescue", target = b.id, targetPos = { x = b.x, y = b.y } } end
      end
    end
  end
  if carrier and not sb and L.mapGet(L.ADDR.gBmMapUnit, seize.x, seize.y) == 0 then
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) and dist(t.x, t.y, seize.x, seize.y) == 1 then
        local th = threatAt(u, t.x, t.y, enemies)
        if best == nil or th < best then best, bestT = th, t end
      end
    end
    if bestT then return { dest = bestT, action = "drop", dropAt = { x = seize.x, y = seize.y } } end
  end

  -- v6 (PROTECT): the toughest mounted unit that is not a lord and can carry the
  -- unit to protect (CA_MOUNTEDAID; GetUnitAid, CanUnitRescue: aid at least its
  -- con) and can get next to it this turn rescues it from the safest tile there;
  -- Rescue allows the canto, which takes it to the safest tile.
  if guardE and not isLordUnit(u) and not isLord and (unitAttr(u) & 1) ~= 0 and unitAid(u) >= guardE.con then
    local mine, bestOther = u.maxHp + 2 * u.def, false
    for _, a in ipairs(L.blue()) do
      if a.id ~= u.id and not isLordUnit(a) and (unitAttr(a) & 1) ~= 0 and (a.state & (L.US.HAS_MOVED | L.US.UNSELECTABLE)) == 0
          and unitAid(a) >= guardE.con and dist(a.x, a.y, guardE.x, guardE.y) <= a.mov + 1
          and (a.maxHp + 2 * a.def > mine or (a.maxHp + 2 * a.def == mine and a.id < u.id)) then bestOther = true end
    end
    if not bestOther then
      local best, bestT
      for _, t in ipairs(tiles) do
        if ok(t) and dist(t.x, t.y, guardE.x, guardE.y) == 1 then
          local th = threatAt(u, t.x, t.y, enemies)
          if best == nil or th < best then best, bestT = th, t end
        end
      end
      if bestT then return { dest = bestT, action = "rescue", target = guardE.id, targetPos = { x = guardE.x, y = guardE.y } } end
    end
  end

  -- v9: a mounted unit (CA_MOUNTEDAID: aid 25 - con, at least the protected
  -- unit's con; GetUnitAid, CanUnitRescue) rescues the protected unit from the
  -- safest tile next to it; Rescue allows the canto (TryMakeCantoUnit), which
  -- then carries it off to the safest tile.
  if guard and not isLord and (unitAttr(u) & CA_MOUNTEDAID) ~= 0 and not carrying and 25 - u.con >= guard.con then
    local best, bestT
    for _, t in ipairs(tiles) do
      if ok(t) and dist(t.x, t.y, guard.x, guard.y) == 1 then
        local th = threatAt(u, t.x, t.y, enemies)
        if best == nil or th < best then best, bestT = th, t end
      end
    end
    if bestT then return { dest = bestT, action = "rescue", target = guard.id, targetPos = { x = guard.x, y = guard.y } } end
  end

  -- E7, the end of Chapter 20 (one turn before the strike): Eirika's carrier drops her on a
  -- staging tile (one move from the seize tile with its holder gone, 3 or more from the holder)
  -- once a unit that kills the holder alone (Myrrh: 56 a hit, two hits, through his 36 counter
  -- at full HP) is at full HP within its move of the holder's open side; next turn that unit
  -- strikes and Eirika, moving last, seizes (v10's strike below). The holder never moves; the
  -- turn after its fall Mogalls load on the free tile (src/events/ch20a-eventscript.h).
  if L.E7.on and carrier and fr and fr.c.id == u.id and sb then
    local eid = emu:read8(u.addr + 0x1B)
    local eir = equip(L.readUnit(L.ADDR.gUnitArrayBlue + (eid - 1) * 0x48))
    local ready = false
    for _, m in ipairs(equipAll(L.blue())) do
      if m.id ~= u.id and m.hp == m.maxHp then
        for _, ot in ipairs({ { x = seize.x, y = seize.y + 1 }, { x = seize.x - 1, y = seize.y }, { x = seize.x + 1, y = seize.y }, { x = seize.x, y = seize.y - 1 } }) do
          local ct = emu:read32(m.classPtr + 0x38)
          local W, H = L.mapSize()
          if ot.x >= 0 and ot.y >= 0 and ot.x < W and ot.y < H and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, ot.x, ot.y))) > 0
              and dist(m.x, m.y, ot.x, ot.y) <= m.mov then
            local hit = bestHitOn(m, sb, { ot })
            if hit and hit.dealt >= sb.hp then ready = true end
          end
        end
      end
    end
    if ready and eir then
      local S = stagingTiles(eir, seize, enemies)
      local W, H = L.mapSize()
      local best, bestP
      for _, t in ipairs(tiles) do
        if ok(t) then
          for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
            local sx, sy = t.x + dv[1], t.y + dv[2]
            if sx >= 0 and sy >= 0 and sx < W and sy < H and S[sy * 256 + sx] and dist(sx, sy, sb.x, sb.y) >= 3
                and (L.mapGet(L.ADDR.gBmMapUnit, sx, sy) == 0 or (sx == u.x and sy == u.y)) then
              local the = threatAt(eir, sx, sy, enemies)
              local thc = threatAt(u, t.x, t.y, enemies)
              if the < eir.hp and (best == nil or the * 10 + thc < best) then
                best, bestP = the * 10 + thc, { dest = t, action = "drop", dropAt = { x = sx, y = sy }, th = the }
              end
            end
          end
        end
      end
      if bestP then
        log("E7: drop Eirika at (%d,%d) (threat %d) from (%d,%d) for the strike next turn", bestP.dropAt.x, bestP.dropAt.y, bestP.th,
          bestP.dest.x, bestP.dest.y)
        return bestP
      end
    end
  end
  -- v10: the strike on the seize tile's holder (strikeFor). (E7 too)
  local holder = (V10 or L.E7.on) and holderOf(ps.chapter, enemies)
  if holder then
    local okTiles = {}
    L.E7.striking = true   -- (E7: the strike's tiles may be in the holder's reach)
    for _, t in ipairs(tiles) do if ok(t) then okTiles[#okTiles + 1] = t end end
    L.E7.striking = false
    local sk = strikeFor(u, okTiles, enemies, ps.chapter)
    if sk then
      return { dest = sk.t, action = "attack", target = sk.target.id, targetPos = { x = sk.target.x, y = sk.target.y }, wslot = sk.slot }
    end
    -- v10: the bait. No strike yet: a healthy fighter that outlives the
    -- holder's attack (with the margins, and every other enemy's threat on top)
    -- waits in its reach with the weapon whose counter hurts it most equipped;
    -- the holder, which never leaves its tile, attacks it in the enemy phase,
    -- the counter wears it down (the gate heals it only 10% a turn), and the
    -- next turn's strike finishes it. Darkling Woods: Morva (62 HP, def 24 with
    -- Wretched Air, 36 a hit through any defence) can only be reached from
    -- (11,12), and no two units could strike him in one turn.
    local lordBait = I11.lordBait(ps.chapter, u)   -- (I11, part 2: Ephraim may be the bait, I11.lordBait)
    if V10 and (not isLord or lordBait) and u.hp * 4 >= u.maxHp * 3 and reachSets and reachSets[holder] then   -- (not E7)
      local busy = false
      for _, b in ipairs(L.blue()) do
        if b.id ~= u.id and reachSets[holder][b.y * 256 + b.x] and (not isLordUnit(b) or lordBait) then busy = true end
      end
      -- (only when the seizer stands on a staging tile, or can reach one now, safely)
      local sz = seizerUnit(equipAll(L.blue()))
      local staged = I11.on(ps.chapter) and not seize   -- (I11: no seize tile, nothing to stage)
      if seize and sz and sz.id ~= u.id then
        local S = stagingTiles(sz, seize, enemies)
        local here = (sz.state & (L.US.UNSELECTABLE | L.US.HAS_MOVED)) ~= 0 and { { x = sz.x, y = sz.y } } or moveTiles(sz)
        for _, t in ipairs(here) do
          if S[t.y * 256 + t.x] and threatAt(sz, t.x, t.y, enemies) * 2 < sz.hp then staged = true end
        end
      end
      if not staged then busy = true end
      local best, bestP
      if not busy then
        for k, it in ipairs(u.items) do
          local w = L.item(it & 0xFF)
          if canUseWeapon(u, w) then
            for _, t in ipairs(okTiles) do
              local d = dist(t.x, t.y, holder.x, holder.y)
              if reachSets[holder][t.y * 256 + t.x] and d >= w.minRange and d <= w.maxRange then
                local mine = {}
                for kk, v in pairs(u) do mine[kk] = v end
                mine.weapon, mine.x, mine.y = w, t.x, t.y
                local hh, mh = sim(holder, mine, d)
                local left = mh - threatAt(mine, t.x, t.y, enemies, holder)
                local dealt = holder.hp - math.max(hh, 0)
                -- (I11, part 2: a counter of 10 or more; Franz's 2 a round to the Demon King held the bait for turns)
                if left > 0 and dealt > 0 and (best == nil or dealt * 10 + left > best)
                    and not (ps.chapter == 0x23 and dealt < 10) then
                  best, bestP = dealt * 10 + left, { dest = t, slot = k - 1, dealt = dealt, left = left }
                end
              end
            end
          end
        end
      end
      if bestP then
        local eq   -- the slot the game equips (the first usable weapon)
        for k, it in ipairs(u.items) do if not eq and canUseWeapon(u, L.item(it & 0xFF)) then eq = k - 1 end end
        log("bait: char %02x waits at (%d,%d) for %02x (counter %d, %d HP left at worst)%s", u.char, bestP.dest.x, bestP.dest.y,
          holder.char, bestP.dealt, bestP.left, bestP.slot ~= eq and string.format(", equips slot %d", bestP.slot) or "")
        if bestP.slot ~= eq then return { dest = bestP.dest, action = "equip", slot = bestP.slot } end
        return { dest = bestP.dest, action = "wait" }
      end
    end
    -- (I11: a hurt bait heals on its bait tile, I11.baitHeal)
    if I11.on(ps.chapter) and (not isLord or lordBait) and u.hp < u.maxHp and reachSets and reachSets[holder] then
      local okTiles2 = {}
      for _, t in ipairs(tiles) do if ok(t) then okTiles2[#okTiles2 + 1] = t end end
      local bh = I11.baitHeal(u, okTiles2, enemies, holder, lordBait)
      if bh then return bh end
    end
  end

  if V3 then
    local p = support(u, tiles, enemies, ok)
    if p then return p end
  end

  -- v10: the seizer, while the holder lives, waits on the safest staging tile
  -- it can reach (stagingTiles), ready for the seize the turn the holder falls.
  if holder and isLord and seizer and seize then
    local S = stagingTiles(u, seize, enemies)
    local best, bestT, bestTh
    for _, t in ipairs(tiles) do
      if ok(t) and S[t.y * 256 + t.x] then
        local th = threatAt(u, t.x, t.y, enemies)
        if th * 2 < u.hp then
          local sc = th * LORD_TH + S[t.y * 256 + t.x]
          if best == nil or sc < best then best, bestT, bestTh = sc, t, th end
        end
      end
    end
    if bestT then
      log("seizer: staging at (%d,%d), threat %d, %d from the seize tile", bestT.x, bestT.y, bestTh, S[bestT.y * 256 + bestT.x])
      return { dest = bestT, action = "wait", th = bestTh }
    end
  end

  -- Attacks.
  local bestScore, bestPlan = nil, nil
  local army = V3 and L.blue() or nil
  -- v5: every usable weapon is tried, and the weapon menu takes the one
  -- chosen (Ephraim's Chapter 9: Franz's Axereaver against the boss's axes,
  -- where his first lance would have been unsafe). Before v5: the equipped one.
  local choices = {}
  if V5 then
    for k, it in ipairs(u.items) do
      local d = L.item(it & 0xFF)
      if canUseWeapon(u, d) then choices[#choices + 1] = { w = d, slot = k - 1 } end
    end
  elseif u.weapon then
    choices[1] = { w = u.weapon }
  end
  -- v10: while the seize tile's holder lives, the weapon that hurts it most is
  -- kept for it (the bait's counter, the strike) once it is down to 6 uses
  -- (Darkling Woods: Seth's Audhulma, the one weapon whose counter takes Morva
  -- from 62 HP to 2, was worn out on Wights by turn 25).
  if holder and #choices > 1 then
    local best, bestC
    for _, wc in ipairs(choices) do
      local mine = {}
      for k, v in pairs(u) do mine[k] = v end
      mine.weapon = wc.w
      local s = (strike(mine, holder) or 0) * nhits(mine, holder) * perStrike(mine)
      if best == nil or s > best then best, bestC = s, wc end
    end
    -- (I11: whatever its uses, for a unit other than a lord: Seth's Audhulma, 8 uses, the one
    -- weapon that does more than 2 a hit to Lyon (def 26) but Siegmund, whose user dies to his counter)
    -- (a weapon doing 10 or more a round: Franz's Brave Lance, 4 to Lyon, stays his for the rest)
    local keep = (I11.on(ps.chapter) and not isLord and best >= 10) and 99 or 6
    if bestC and best > 0 and ((u.items[bestC.slot + 1] >> 8) <= keep) and (bestC.w.attributes & 8) == 0 then bestC.reserved = true end
  end
  -- E6: every attack this unit could make, for the attack trials (TR, below the
  -- search): { plan, score, boss }, safety left to the trial's real outcome
  local trialCands = E6 and {} or nil
  -- (v9: a unit carrying a rescued one does not fight)
  -- (E5: a lord's carrier never attacks: it fights at half skill and speed, and
  -- Gerik carrying Eirika died attacking in a trial; E6: it attacks where it is
  -- safe as a lord is, its speed halved in the estimate)
  -- (E6 PROTECT: the carrier of the unit to protect never attacks; E6, chapter index 20 on:
  -- nor does Eirika's carrier, on its way to the seize tile: in tries it fought its way
  -- there two tiles a turn, or stayed fighting in the starting corner)
  -- (Without V5, choices holds only u.weapon, so #choices > 0 is "u.weapon".)
  if #choices > 0 and not carrying and not (E5 and not isLordUnit(u) and isLord and not E6) and not carryingProt
      and not (carrier and not L.E7.on) then   -- (E7: Eirika's carrier attacks only to kill at a blow, below)
    for _, e in ipairs(enemies) do
      if V3 and spareForTalk(ps.chapter, e, army) then goto nextEnemy end
      if hiddenRed(e) then goto nextEnemy end   -- E5: under a roof, not on the map yet
      -- (E7: the seize tile's holder only by the strike (above); a dragonstone unit (Myrrh)
      -- fights only another boss, or, past the holder, an enemy on Eirika's way while she has
      -- more than L.E7.KEEP uses: by turn 7 of a first try her rush to Morva and the counters
      -- round him had spent 17 of her 24, which Lyon and the Demon King need)
      if L.E7.on and sb and e == sb then goto nextEnemy end
      if dsUses and not ((isBoss(e) and e ~= sb) or (vanguard and fr.near[e.y * 256 + e.x])) then goto nextEnemy end
      -- v5: a boss that never leaves its tile (AI1 3-5: Chapter 14's Carlyle on the
      -- throne) is attacked by any unit that survives the fight itself; the next
      -- attackers of the turn finish it (a kill leaves its threat out). With v3's
      -- "safe only" rule nobody ever attacked Carlyle (turn 60 in a trial).
      local chipBoss = E5 and not isLord and isBoss(e) and emu:read8(e.addr + 0x42) >= 3 and emu:read8(e.addr + 0x42) <= 5
      -- (v6, chapter index 20 on, while sb stands: the weapon for this target, sb's own or
      -- one not effective against sb; none: no attack. wu is the unit with it equipped)
      local wpn, wu = nil, u
      if SACRED2 and sb then
        if e == sb then wpn = bossWeapon(u, sb) else wpn = otherWeapon(u, sb, e) end
        -- (E7: Myrrh as the vanguard, her uses to spare: the Divinestone on an enemy on the way)
        if not wpn and vanguard and dsUses and fr.near[e.y * 256 + e.x] then wpn = bossWeapon(u, sb) end
        if not wpn then goto nextEnemy end
        -- (a rusher below full HP with a heal item heals before it fights anything but sb:
        -- Myrrh at 34 of 38 HP fought the mogalls round Morva turn after turn)
        if rusher and e ~= sb and u.hp < u.maxHp and healItemSlot(u) then goto nextEnemy end
        wu = {}
        for k, v in pairs(u) do wu[k] = v end
        wu.weapon = wpn.d
      elseif E6 and e.char == L.FINALBOSS[ps.chapter] then
        -- (v6, the final chapter's part 2: the weapon that hurts the Demon King most, through
        -- the weapon menu; Eirika's first weapon, a Rapier, does nothing to him, her Audhulma 17 a hit)
        wpn = bossWeapon(u, e)
        if not wpn then goto nextEnemy end
        wu = {}
        for k, v in pairs(u) do wu[k] = v end
        wu.weapon = wpn.d
      end
      for _, wc in ipairs(choices) do
      if wc.reserved and e ~= holder then goto nextChoice end
      -- (the weapon of this try: E6's choice for this target in wu, else this choice's)
      local rw = (wu ~= u) and wu.weapon or wc.w
      for _, t in ipairs(tiles) do
        local d = dist(t.x, t.y, e.x, e.y)
        if ok(t) and d >= rw.minRange and d <= rw.maxRange then   -- inRange with this weapon
          local mine = { }
          for k, v in pairs(wu) do mine[k] = v end
          if wu == u then mine.weapon = wc.w end
          mine.x, mine.y = t.x, t.y
          local uh, eh = sim(mine, e, d)
          if trialCands and e.hp - math.max(eh, 0) > 0 then
            trialCands[#trialCands + 1] = { plan = { dest = t, action = "attack", target = e.id, targetPos = { x = e.x, y = e.y },
              wslot = wpn and wpn.slot }, score = (eh <= 0 and 1000 or 0) + (e.hp - math.max(eh, 0)) * 10 - (u.hp - uh) * 4 - d,
              boss = isBoss(e) }
          end
          if uh > 0 then
            local kill = eh <= 0
            local dealt = e.hp - math.max(eh, 0)
            local taken = u.hp - uh
            -- (v10: a lord's kill in one strike counts the target gone too: Ephraim
            -- never used Siegmund, which kills most monsters at a blow)
            local oneBlow = (V10 or L.E7.on) and kill and (strike(mine, e) or 0) >= e.hp   -- (E7 too)
            -- (I11: a lord's kill by the whole fight, its counter counted, leaves the target out too:
            -- Ephraim's Siegmund kills a Draco Zombie, 68 HP, in one round, 34 twice, and the old rule
            -- counted the dead zombie's 27 and never attacked; it held the west way for 15 turns)
            local i11Kill = I11.on(ps.chapter) and kill
            local th = threatAt(mine, t.x, t.y, enemies, (kill and (not isLord or oneBlow or i11Kill)) and e or nil)
            local score = (kill and 1000 or 0) + dealt * 10 - taken * 8 - d
            -- v10: spare the weapons that are hard to replace (effective against
            -- monsters, S rank, brave) when a plain one does as well (Darkling
            -- Woods: Vidofnir's 30 uses were gone by turn 11 on Wights a Steel
            -- Lance kills, and Franz waited unarmed from turn 25 to 60).
            if V10 then
              local w = wc.w
              if w.eff == MONSTER_EFF or w.rank >= 251 or (w.attributes & 0x20) ~= 0 then score = score - 40 end
            end
            if isBoss(e) then score = score + 200 end
            -- (E7: an enemy on Eirika's way ahead, within 2 of its front, comes first)
            if vanguard and fr.near[e.y * 256 + e.x] then score = score + 300 end
            -- (v6, chapter index 20 on: killing sb comes first, whatever the threat; the
            -- attack trial and the turn's search judge the enemy phase)
            local sbKill = wpn ~= nil and e == sb and kill
            if sbKill then score = score + 5000 end
            -- (v6, the final chapter's part 2: the Demon King, L.FINALBOSS above)
            local fbHit = E6 and e.char == L.FINALBOSS[ps.chapter] and dealt > 0
              and (kill or uh > (strike(e, mine) or 0) * nhits(e, mine))
            -- (E7: the same for Lyon in part 1, L.E7.BOSS, and only from full HP)
            if L.E7.on and L.E7.BOSS[ps.chapter] then
              fbHit = e.char == L.E7.BOSS[ps.chapter] and dealt > 0 and u.hp == u.maxHp
                and (kill or uh > (strike(e, mine) or 0) * nhits(e, mine))
            end
            if fbHit then score = score + 5000 end
            local safe = th < uh
            if SIEGE and not isLord and (isBoss(e) or L.PR.siege) and u.hp * 4 >= u.maxHp * 3 then   -- (PR: L.PR.siege)
              -- v9 siege: a healthy unit attacks the boss wherever it outlives the counter.
              if bestScore == nil or score > bestScore then
                bestScore, bestPlan = score, { dest = t, action = "attack", target = e.id, targetPos = { x = e.x, y = e.y }, wslot = wc.slot }
              end
            elseif isLord then
              -- v3: a lord's death ends the game; keep the threat under half its HP.
              if V3 then safe = th * 2 < uh end
              -- (I11: a lord's kill is safe when the threat left is under the HP it keeps; the turn's
              -- search plays the turn again if he dies)
              if i11Kill and th < uh then safe = true end
              -- (E7: Ephraim, the vanguard, attacks where the threat after it stays under the HP he
              -- has left: with the half-HP rule he never engaged the wights and the Gorgon at the
              -- south-east corner's exit, and the army waited there for 25 turns; the search plays
              -- the turn again if he dies)
              if vanguard or lead then safe = th < uh end
              -- (E7: Eirika's carrier, Franz with Vidofnir, attacks only an enemy it kills at a
              -- blow, where the threat after it stays under its HP: with half its speed and skill
              -- while carrying it kills the Wights, Mogalls and Gorgons that come at it)
              if L.E7.on and carriesLord(u) then safe = oneBlow and th * 2 < uh end
              -- (E6: sbKill and fbHit, both false without E6)
              if (safe or sbKill or fbHit) and (kill or dealt > 0) then
                if bestScore == nil or score > bestScore then bestScore, bestPlan = score, { dest = t, action = "attack", target = e.id, targetPos = { x = e.x, y = e.y }, wslot = wc.slot or (wpn and wpn.slot) } end
              end
            elseif dealt > 0 and not (V3 and not safe and u.hp * 10 < u.maxHp * 4) and not (defend and not safe)
                and not ((P.cautious or P.safe[u.id]) and not safe and not chipBoss and not fbHit) then
              -- (v3: a unit under 40% HP attacks only where it is safe; v4 hold-out: every unit;
              -- E6 policies "cautious" and "safe" too)
              if not safe and not chipBoss and not fbHit then score = score - 1500 - th * 5 else score = score - th * 3 end
              -- (v7: not where it is safe and the target cannot hit back)
              if u.hp * 10 < u.maxHp * 4 and not kill and not (V7 and safe and taken == 0) then score = score - 1500 end
              score = score + noise(1, u.id, t.y * 64 + t.x, e.id) * 40   -- E6 (0 without a policy seed)
              if score > -1000 and (bestScore == nil or score > bestScore) then
                bestScore, bestPlan = score, { dest = t, action = "attack", target = e.id, targetPos = { x = e.x, y = e.y }, wslot = wc.slot or (wpn and wpn.slot) }
              end
            end
          end
        end
      end
      ::nextChoice::
      end   -- v5 weapon choices
      ::nextEnemy::
    end
  end
  if bestPlan and not P.noAttack then bestPlan.cands = trialCands; return bestPlan end
  -- (I11, part 2: the lure, I11.lure)
  if ps.chapter == 0x23 and holder and not isLord and u.weapon and u.hp * 4 >= u.maxHp * 3 then
    local okTiles3 = {}
    for _, t in ipairs(tiles) do if ok(t) then okTiles3[#okTiles3 + 1] = t end end
    local lp = I11.lure(u, okTiles3, enemies, holder)
    if lp then return lp end
  end

  -- No attack: move. Lord: toward the seize tile (boss gone) or the strongest
  -- ally, safest tile first. Others: toward the nearest enemy, unless hurt.
  local goals = {}
  local hurt = u.hp * 10 < u.maxHp * 4
  local prLead = isLord and not hurt and u.weapon and L.PR.lead(ps.chapter, u)   -- (PR: L.PR.lead)
  -- v3: a unit without a weapon, or hurt, keeps to the healers (hurt) or the
  -- fighters (no weapon), safety first.
  local keepBack = V3 and not isLord and (hurt or not u.weapon or carrying)   -- (v9: and a carrier)
    or (dsUses ~= nil and not vanguard)   -- (E7: Myrrh keeping her uses, out of reach)
  -- v9 siege: a unit under 3/4 of its HP rests on a healing tile.
  local siegeRest = SIEGE and not isLord and u.hp * 4 < u.maxHp * 3
  local function around(b)
    for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do goals[#goals + 1] = { x = b.x + dv[1], y = b.y + dv[2] } end
  end
  local talkTo = V3 and talkTargetFor(u)
  -- v5 hold-out: nobody walks off to a recruit (Ephraim's Chapter 10: Tana
  -- went for Cormag alone and died); a recruit next to a safe tile is still
  -- talked to (support).
  if V5 and defend then talkTo = nil end
  -- v5: the fighter nearest a recruit village (not a lord, not hurt) walks to it.
  local visitTo
  if V5 and not isLord and not keepBack then
    local v = recruitVillage(ps.chapter)
    if v then
      local best, bestId
      for _, b in ipairs(equipAll(L.blue())) do
        if not isLordUnit(b) and b.weapon and b.hp * 10 >= b.maxHp * 4 then
          local d = dist(b.x, b.y, v.x, v.y)
          if best == nil or d < best then best, bestId = d, b.id end
        end
      end
      if bestId == u.id then visitTo = v end
    end
  end
  if visitTo then
    goals[1] = { x = visitTo.x, y = visitTo.y }
  elseif talkTo then
    around(talkTo)            -- v3: walk to the recruit (safety as usual for its kind)
  elseif isLord then
    if defend or carryingProt then
      -- v4 hold-out: no goal; the tile score below keeps away from the enemy.
      -- (v6 PROTECT: so does the carrier of the unit to protect)
    elseif seizer and seize and not bossAlive then
      goals[1] = seize
      if SACRED2 then bossWalk = true end
    elseif unarmedBoss then
      around(unarmedBoss)       -- v7: the lord finishes a boss that cannot hit back
    elseif carrier then
      -- (v6, chapter index 20 on, "carry": Eirika's carrier walks to the tiles next to the
      -- seize tile it can stand on, the red units on the way ignored, from the start, so it
      -- is near when the boss falls: in a try where it kept away until then it was 7 turns
      -- of thickets and monsters away, and stuck; with safety first it never left the east)
      local W, H = L.mapSize()
      local ct = emu:read32(u.classPtr + 0x38)
      for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local x, y = seize.x + dv[1], seize.y + dv[2]
        if x >= 0 and y >= 0 and x < W and y < H and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0 then
          goals[#goals + 1] = { x = x, y = y }
        end
      end
      bossWalk = true
    elseif vanguard then
      -- (E7: Ephraim walks ahead of Eirika on her way, L.E7.front, fighting what he meets)
      for _, g in ipairs(fr.goals) do goals[#goals + 1] = g end
      bossWalk = true
    elseif lead then
      around(lead)              -- (E7, the final chapter: to its boss, the red units on the way ignored)
      bossWalk = true
    elseif prLead then
      for _, e in ipairs(enemies) do if not hiddenRed(e) then around(e) end end   -- (PR: L.PR.lead, to the enemies)
    elseif SACRED2 and seize and not sb and not seizer then
      -- (v6, chapter index 20 on: with sb gone the other lord keeps by Eirika or her carrier)
      for _, b in ipairs(L.blue()) do if b.char == L.CHAR.EIRIKA or carriesLord(b) then around(b) end end
    elseif (E6 or I11.on(ps.chapter)) and P.press and ps.chapter >= 0x12 and not seize then   -- (I11 too)
      -- v6, chapter index 18 on: in a stalled turn ("press") the lord too walks to
      -- the enemies left (safety first as ever), so a zone round it moves with it
      for _, e in ipairs(enemies) do if not hiddenRed(e) then around(e) end end
    else
      for _, b in ipairs(L.blue()) do
        if b.char == L.CHAR.SETH then around(b) end
      end
      -- v5: no Seth: keep by the toughest fighter (anchorUnit).
      if #goals == 0 and E5 then
        local a = anchorUnit(u)
        if a then around(a) end
      end
      -- No Seth on the map (Ephraim's chapters): head for the seize tile anyway.
      if #goals == 0 and seize and seizer then goals[1] = seize end
    end
  elseif siegeRest then
    local w, h = L.mapSize()
    for y = 0, h - 1 do for x = 0, w - 1 do
      if HEAL_TERRAIN[L.mapGet(L.ADDR.gBmMapTerrain, x, y)] and L.mapGet(L.ADDR.gBmMapUnit, x, y) < 0x80 then
        goals[#goals + 1] = { x = x, y = y }
      end
    end end
  elseif V10 and supplyLord() and supplyLord().id ~= u.id and (not u.weapon or (I11.on(ps.chapter) and hurt
      and itemCountOf(u, 0x6D) + itemCountOf(u, 0x6C) == 0 and I11.supplyHasHeal())) then
    around(supplyLord())        -- v10: a unit without a usable weapon walks to the lord (the supply); I11: a hurt one with no heal item too
  elseif keepBack then
    for _, b in ipairs(equipAll(L.blue())) do
      if b.id ~= u.id and ((hurt and healStaffSlot(b)) or (not hurt and b.weapon)) then around(b) end
    end
  end
  -- v10: the leader (the first armed fighter in the unit order, Seth) makes
  -- for the seize tile's holder (the bait, the strike), fighting on the way,
  -- and the other fighters keep with the leader (Darkling Woods: Franz took
  -- the southern way alone and died to Riev while the rest went north).
  if V10 and not isLord and #goals == 0 and holder then
    local leader   -- (Seth, else the armed non-lord with the lowest id, as unitOrder ranks them)
    for _, b in ipairs(equipAll(L.blue())) do
      if not isLordUnit(b) and b.weapon then
        if not leader or (b.char == L.CHAR.SETH and leader.char ~= L.CHAR.SETH)
            or ((b.char == L.CHAR.SETH) == (leader.char == L.CHAR.SETH) and b.id < leader.id) then leader = b end
      end
    end
    if leader and leader.id ~= u.id then around(leader) else around(holder) end
  end
  -- E5: in a seize chapter a unit with a door key walks the way to the seize
  -- tile (doors counted open), so it reaches the doors on that way (support's door rule).
  -- (enemies and roofs on the way ignored, as in the door rule)
  local keyWalk = false
  if E5 and not isLord and #goals == 0 and seize and bossAlive and canOpenDoor(u) then goals[1] = seize; keyWalk = true end
  -- v6 policy "guard": a fighter that does not attack goes to an open tile next to
  -- a healer or a lord's carrier that some enemy can reach (the healers move first
  -- in this policy, so their tiles are where they stay).
  if P.guard and not isLord and not keepBack and #goals == 0 and u.weapon and reachSets and not rusher then
    local w, h = L.mapSize()
    for _, b in ipairs(L.blue()) do
      if b.id ~= u.id and (healStaffSlot(b) or lordLike(b)) then
        local hit = false
        for _, e in ipairs(enemies) do
          if reachSets[e] and reachSets[e][b.y * 256 + b.x] then hit = true; break end
        end
        if hit then
          for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
            local nx, ny = b.x + dv[1], b.y + dv[2]
            if nx >= 0 and ny >= 0 and nx < w and ny < h
                and (L.mapGet(L.ADDR.gBmMapUnit, nx, ny) == 0 or (nx == u.x and ny == u.y)) then
              goals[#goals + 1] = { x = nx, y = ny }
            end
          end
        end
      end
    end
  end
  -- v6, chapter index 20 on: while the seize tile's boss stands, a unit other than a
  -- lord with a weapon walks to the tiles it could attack the boss from (inside its
  -- zone, under a zone policy; a rusher whatever the zone), attacking what it meets on
  -- the way
  if E6 and sb and not isLord and not keepBack and #goals == 0 and u.weapon and not (P.zone and not zoneOn and not rusher)
      and not vanguard then   -- (E7: a vanguard keeps to Eirika's way, below)
    local lo, hi = weaponRanges(u)
    if SACRED2 then   -- (the reach of its sb weapon; the red units on the way ignored)
      local bw = bossWeapon(u, sb)
      if bw then lo, hi = bw.d.minRange, bw.d.maxRange end
      bossWalk = true
    end
    if lo then
      for dy = -hi, hi do
        for dx = -hi, hi do
          local d = math.abs(dx) + math.abs(dy)
          if d >= lo and d <= hi then goals[#goals + 1] = { x = sb.x + dx, y = sb.y + dy } end
        end
      end
    end
  end
  -- v6, chapter index 20 on: with the seize tile's boss gone, the others clear the way: they
  -- walk to the tiles round the open tiles next to the seize tile (Morva's (11,12): (10,12),
  -- (12,12), (11,13)), the red units on the way ignored, and fight what they meet; the
  -- carrier comes after them
  -- (E7: a fighter that keeps no weapon for sb clears Eirika's way ahead, L.E7.front)
  if vanguard and not isLord and not keepBack and #goals == 0 then
    for _, g in ipairs(fr.goals) do goals[#goals + 1] = g end
    bossWalk = true
  end
  if SACRED2 and seize and not sb and not isLord and not keepBack and #goals == 0 then
    local W, H = L.mapSize()
    local ct = emu:read32(u.classPtr + 0x38)
    local function open(x, y)
      return x >= 0 and y >= 0 and x < W and y < H and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0
    end
    for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = seize.x + dv[1], seize.y + dv[2]
      if open(nx, ny) then
        for _, dw in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
          local fx, fy = nx + dw[1], ny + dw[2]
          if not (fx == seize.x and fy == seize.y) and open(fx, fy) then goals[#goals + 1] = { x = fx, y = fy } end
        end
      end
    end
    bossWalk = #goals > 0
  end
  if not isLord and #goals == 0 then
    if (holdU or (P.zone and not zoneOn)) and lordPos then around(lordPos)   -- v4 hold-out (v6 "hold", "zone"): to the lord
    else for _, e in ipairs(enemies) do if not hiddenRed(e) then around(e) end end end
  end
  -- v4 hold-out, the lord's tile: closeness to the enemy (each enemy within
  -- 15 tiles counts the tiles it is short of 15) costs, allies within 2 help.
  local army2 = ((defend or carryingProt) and isLord) and L.blue() or nil
  local function holdScore(t)
    local s = 0
    for _, e in ipairs(enemies) do s = s + math.max(0, 15 - dist(t.x, t.y, e.x, e.y)) * 2 end
    for _, b in ipairs(army2) do if b.id ~= u.id and dist(t.x, t.y, b.x, b.y) <= 2 then s = s - 5 end end
    return s
  end
  -- v3: doors count as open when someone can open them; a unit without a key
  -- keeps off the tiles next to a closed door, which the opener needs.
  local doorsOk = V3 and armyHasKey()
  local myKey = V3 and canOpenDoor(u)
  local D = (#goals > 0) and distanceField(u, goals, (keyWalk or bossWalk) and {} or enemies, doorsOk, keyWalk) or nil
  -- (E7: Eirika's carrier keeps to her way, L.E7.way; so does a vanguard)
  if fr and carrier and fr.c.id == u.id then D = fr.D
  elseif vanguard and #goals > 0 then D = distanceField(u, goals, {}, false, false, L.E7.wayCost) end
  -- (v6, chapter index 20 on: a rusher keeps out of the reach of the enemies that carry
  -- Stone, ITEM_MONSTER_STONE: petrified for turns, Myrrh twice lost the race to sb so)
  local stoneTiles = nil
  if rusher and reachSets then
    stoneTiles = {}
    for _, e in ipairs(enemies) do
      local has = false
      for _, it in ipairs(e.items) do if (it & 0xFF) == 0xB5 and (it >> 8) > 0 then has = true end end
      if has and reachSets[e] then for k in pairs(reachSets[e]) do stoneTiles[k] = true end end
    end
  end
  local best, bestT, bestTh
  local nearDD, nearT, nearTh   -- (v10: the seizer's log)
  for _, t in ipairs(tiles) do
    if ok(t) and not (doorsOk and not myKey and nextToDoor(t)) then
      local th = threatAt(u, t.x, t.y, enemies)
      local dd = (D and D[t.y][t.x]) or 999
      local score
      if V3 and isLord then
        -- v3: a lord rules out tiles whose threat reaches half its HP; it
        -- presses on only toward the seize tile (boss gone), else safety first.
        local deadly = (th * 2 >= u.hp) and 100000 or 0
        -- v10: the seizer makes for the seize tile as the bolder lord of v2
        -- does, and once the tile is free only a threat that could kill it
        -- rules a tile out (Darkling Woods: Morva fell to Seth's counter on
        -- turn 20 and Ephraim, keeping to quiet tiles in the north, never came).
        if V10 and seizer and seize then
          if not bossAlive then deadly = (th >= u.hp) and 100000 or 0 end
          score = deadly + th * LORD_TH + dd * LORD_DD
        elseif defend or carryingProt then score = deadly + th * 20 + holdScore(t)
        elseif carrier and fr and fr.c.id == u.id then
          -- (E7: the carrier follows the vanguard: on its way, no closer than 2 points behind the
          -- most advanced escort; with E6's rule below it walked into the monsters' reach alone
          -- or, with its threat weighted, waited at the south-east corner's exit for 25 turns)
          local dvan
          for _, b in ipairs(L.blue()) do
            local db = (b.id ~= u.id) and fr.D[b.y] and fr.D[b.y][b.x]
            if db and isFighter(b) and (dvan == nil or db < dvan) then dvan = db end
          end
          -- (a threat of two thirds of its HP rules a tile out: with "all of it" Franz, carrying her,
          -- was worn down to under 40% at the south-east exit and died there)
          score = ((th * 3 >= u.hp * 2) and 100000 or 0) + th * LORD_TH + dd * LORD_DD * 3
          if dvan and dd < dvan + 2 then score = score + (dvan + 2 - dd) * 20 end
        elseif carrier then   -- (E6, chapter index 20 on, "carry": the carrier presses on as the seizer would)
          score = ((th >= u.hp) and 100000 or 0) + th * LORD_TH + dd * LORD_DD * 3
        elseif E6 and ps.chapter >= 0x14 and seizer and seize and not bossAlive then
          -- (E6, chapter index 20 on: with the seize tile free the seizer presses on, only
          -- a tile whose threat could kill her ruled out; the search plays the turn again
          -- if she dies)
          score = ((th >= u.hp) and 100000 or 0) + th * LORD_TH + dd * LORD_DD * 3
        elseif (seizer and seize and not bossAlive) or unarmedBoss then score = deadly + th * LORD_TH + dd * LORD_DD
        elseif vanguard or lead or prLead then   -- (E7: Ephraim leads: only a threat that could kill him rules a tile out; PR: L.PR.lead)
          -- (a tile closer counts as much as for any fighter: with 3 he kept off a goal 2 tiles
          -- away that one Gorgon could reach, turn after turn)
          score = ((th >= u.hp) and 100000 or 0) + th + dd * 10
        else score = deadly + th * 20 + dd end
      elseif V2 and isLord and not hurt then
        -- v2: a bolder lord. Only a tile whose threat could kill it is ruled
        -- out; otherwise progress counts about as much as the threat.
        score = ((th >= u.hp) and 100000 or 0) + th * LORD_TH + dd * LORD_DD
      elseif V10 and not u.weapon and not hurt then
        -- v10: unarmed, it makes for the supply as any fighter goes (Darkling
        -- Woods: Franz, unarmed, kept to a quiet tile for 35 turns)
        score = dd * 10 + ((th >= u.hp) and 5000 or th * 3)
      elseif rusher and not hurt then
        -- (E6, chapter index 20 on: a "rusher" walks to sb; with safety first Myrrh, 5
        -- Divinestone uses left, waited out of reach for turns)
        -- (below full HP it heals where it stops, below; a heavier threat weight then kept
        -- Myrrh healing on one tile for turns)
        score = ((th >= u.hp) and 100000 or 0) + th * 3 + dd * 10
          + ((stoneTiles and stoneTiles[t.y * 256 + t.x]) and 5000 or 0)
      elseif isLord or hurt or keepBack or SIEGE or P.safe[u.id] then   -- (v9 siege: everyone keeps out of the boss's reach; E6 policy "safe" too)
        score = ((th >= u.hp) and 100000 or 0) + th * 50 + dd
      elseif V5 and defend then
        -- v5 hold-out: by the lord, but the threat weighs as much as the distance.
        score = dd * 3 + ((th >= u.hp) and 5000 or th * 3)
      elseif P.press then      -- E6 policy "press": closer counts most; 90% of its HP counts as deadly
        score = dd * 10 + ((th * 10 >= u.hp * 9) and 5000 or th // 4)
      elseif P.cautious then   -- E6 policy "cautious": 70% of its HP counts as deadly
        score = dd * 10 + ((th * 10 >= u.hp * 7) and 5000 or th)
      else
        score = dd * 10 + ((th >= u.hp) and 5000 or th)
      end
      score = score + noise(2, u.id, t.x, t.y) * 8   -- v6 (0 without a policy seed)
      if best == nil or score < best then best, bestT, bestTh = score, t, th end
      if V10 and isLord and seizer and (nearDD == nil or dd < nearDD) then nearDD, nearT, nearTh = dd, t, th end
    end
  end
  if nearT and bestT then
    log("seizer: to (%d,%d) th %d dd %s; nearest (%d,%d) th %d dd %d; seize tile %s", bestT.x, bestT.y, bestTh,
      tostring(D and D[bestT.y][bestT.x]), nearT.x, nearT.y, nearTh, nearDD, bossAlive and "held" or "free")
  end
  if ROUTE_DEBUG and bestT then   -- (ROUTE_DEBUG=1: why a unit moves where it does; log only)
    local nfin, mind, minT = 0, nil, nil
    for _, t in ipairs(tiles) do
      local dd = D and D[t.y][t.x]
      if dd then nfin = nfin + 1; if mind == nil or dd < mind then mind, minT = dd, t end end
    end
    log("debug move %02x: %d goals, %d tiles (%d with a way), kept (%d,%d) dd %s; nearest (%s,%s) dd %s th %s; keepBack %s lord %s weapon %s",
      u.id, #goals, #tiles, nfin, bestT.x, bestT.y, tostring(D and D[bestT.y][bestT.x]), tostring(minT and minT.x),
      tostring(minT and minT.y), tostring(mind), tostring(minT and threatAt(u, minT.x, minT.y, enemies)), tostring(keepBack),
      tostring(isLord), u.weapon and string.format("%02x", u.weapon.id) or "none")
  end
  -- (v6, chapter index 20 on: a rusher below full HP heals where it stops, to meet sb at
  -- full HP: Morva's counter takes 36 whatever the defence, and Myrrh has 38)
  if rusher and u.hp < u.maxHp then
    local hs = healItemSlot(u)
    if hs then return { dest = bestT or { x = u.x, y = u.y }, action = "item", slot = hs, th = bestTh, cands = trialCands } end
  end
  return { dest = bestT or { x = u.x, y = u.y }, action = "wait", th = bestTh, cands = trialCands }
end

local function unitOrder(units, v3, lordFirst)
  local order = {}
  local function rank(u)
    if L.F.suppLast and L.F.suppLast[u.char] then return 10 + L.F.suppLast[u.char] end   -- (coverage 12, ROUTE_SUPPORTS: after the partner)
    if L.F.trainee and L.F.trainee == u.char then return -2 end   -- (coverage 12, ROUTE_TRAINEE: first)
    if L.F.talkRush and L.F.talkRush.by == u.char then return -3 end   -- (coverage 12, ROUTE_TALKRUSH: first)
    if lordFirst and isLordUnit(u) then return -1 end   -- v4 hold-out
    if u.char == L.CHAR.SETH then return 0 end
    if lordLike(u) then return 9 end                -- (v5: a lord's carrier too)
    if v3 and healStaffSlot(u) then return P.guard and 4 or 7 end   -- v3: healers after the fighters (v6 "guard": before)
    return 5
  end
  for _, u in ipairs(units) do order[#order + 1] = u end
  table.sort(order, function(a, b)
    if rank(a) ~= rank(b) then return rank(a) < rank(b) end
    if P.order ~= 0 then   -- v6 policy "order": same rank in an order hashed from the seed
      local ha, hb = hash32(P.seed, a.id), hash32(P.seed, b.id)
      if ha ~= hb then return ha < hb end
    end
    return a.id < b.id
  end)
  return order
end
-- v7: with a boss that cannot hit back (think, "unarmedBoss"), the units next
-- to it make way first, then the lord goes, then the rest.
local function unarmedBossOrder(order)
  local ub
  for _, e in ipairs(equipAll(L.red())) do
    if ((emu:read32(emu:read32(e.addr) + 0x28) | emu:read32(e.classPtr + 0x28)) & CA_BOSS) ~= 0 and not e.weapon then ub = e end
  end
  if not ub then return order end
  local front, lord, rest = {}, {}, {}
  for _, u in ipairs(order) do
    if isLordUnit(u) then lord[#lord + 1] = u
    elseif dist(u.x, u.y, ub.x, ub.y) == 1 then front[#front + 1] = u
    else rest[#rest + 1] = u end
  end
  for _, u in ipairs(lord) do front[#front + 1] = u end
  for _, u in ipairs(rest) do front[#front + 1] = u end
  return front
end

---------------------------------------------------------------------------
-- v4 (Eirika's Chapter 11, chapterIndex 61, and every chapter from index 12;
-- README-routes.md "Tactics v4"): weapons from a world-map armory. The army
-- the v3 bot brings to Chapter 11 has worn its weapons out (Franz has none,
-- Seth's last is a 17-use Killer Edge), and Chapter 12 cannot be won
-- unarmed. At the first free world-map cursor of a run that starts from a
-- chapter save, before that run writes a game save (so a route's last 300
-- ticks after its own end save stay as they were), the bot walks the lord to
-- the nearest cleared node with an armory and no skirmish on it, picks each
-- unit that needs a weapon on the item screen, sells the weapons and staves
-- that unit cannot use when it needs the room, and buys one weapon (two
-- when it is nearly out). Then it goes on to the chapter's node as before.

SHOP_LOW_USES = 40     -- a unit whose usable weapons have fewer uses in all gets a new one
SHOP_KEEP_GOLD = 1500   -- gold left after shopping
local startedFromSave = (os.getenv("GATE0_SRAM_IN") or "") ~= ""
local shopPlan = nil          -- { node, want = { [char] = { buy = {ids}, sell = {ids}, base = {[id] = count} } }, done }

local function itemCount(u, id)
  local n = 0
  for _, it in ipairs(u.items) do if (it & 0xFF) == id then n = n + 1 end end
  return n
end
local function usableUses(u)
  local s = 0
  for _, it in ipairs(u.items) do if canUseWeapon(u, L.item(it & 0xFF)) then s = s + (it >> 8) end end
  return s
end
local function shopStock(p)
  local t = {}
  if p == 0 then return t end
  for i = 0, 19 do
    local v = emu:read16(p + 2 * i)
    if v == 0 then break end
    t[#t + 1] = v & 0xFF
  end
  return t
end
local function skirmishAt(node)   -- GetGmSkirmishUnitAtNode: gGMData.units[4..6]
  for i = 4, 6 do
    if emu:read16(L.GM.data + 0x12 + 4 * i) ~= 0 and emu:read8(L.GM.data + 0x11 + 4 * i) == node then return true end
  end
  return false
end
local function nodeXY(i) local nd = L.GM.nodeData + i * 0x20; return L.s16(emu:read16(nd + 0x18)), L.s16(emu:read16(nd + 0x1A)) end
-- (Two answers to the same snag, one per chain: reachableNodes, port-bots10i, for every
-- chapter but E5's, and gmPathClear, port-bots10e, for E5's; makeShopPlan.)
-- The nodes the lord can walk to from node `from`: over gGMData.openPaths
-- (+0xA4, s8[0x20], length +0xC4; each a gWMPathData index, struct
-- GMapPathData: 12 bytes, node[2] at +4), never through a node that holds a
-- skirmish (the route search GmSearchPath skips them, and WorldMap_MuWalkLoop
-- stops the walk in front of one), at most 16 steps. Ephraim's Chapter 13:
-- the only way back to the armories passes node 15, held by monsters, and A
-- on node 5 only played the blocked-walk animation (GmapSogu), for ever.
WM_PATHS = 0x08206674   -- gWMPathData
local function reachableNodes(from)
  local d = L.GM.data
  local adj = {}
  for i = 0, L.s8(emu:read8(d + 0xC4)) - 1 do
    local p = L.s8(emu:read8(d + 0xA4 + i))
    local a, b = L.s8(emu:read8(WM_PATHS + 12 * p + 4)), L.s8(emu:read8(WM_PATHS + 12 * p + 5))
    adj[a] = adj[a] or {}; adj[b] = adj[b] or {}
    adj[a][#adj[a] + 1] = b; adj[b][#adj[b] + 1] = a
  end
  local steps, q, h = { [from] = 0 }, { from }, 1
  while h <= #q do
    local c = q[h]; h = h + 1
    if (c == from or not skirmishAt(c)) and steps[c] < 16 then
      for _, nb in ipairs(adj[c] or {}) do
        if steps[nb] == nil then steps[nb] = steps[c] + 1; q[#q + 1] = nb end
      end
    end
  end
  return steps
end
-- E5: the lord's walk stops on the node before one with a skirmish on it
-- (WorldMap_MuWalkLoop, src/worldmap_main.c: a "Sogu" encounter, then the free
-- cursor again), so a node behind a skirmish can never be reached by walking
-- (from e14_start.sav the monsters on node 10, Teraz Plateau, cut Hamill Canyon
-- off from Port Kiris: v4 looped there for ever). A node counts as reachable
-- when a path over gGmNodeLinks passes no skirmish node on the way (GmFindPath
-- with flag 0, src/worldmap_path.c).
local function gmPathClear(from, to)
  local seen, open, head = { [from] = true }, { from }, 1
  while head <= #open do
    local n = open[head]; head = head + 1
    if n == to then return true end
    local lk = L.GM.links + 8 * n
    for k = 1, L.s8(emu:read8(lk)) do
      local c = L.s8(emu:read8(lk + k))
      if c >= 0 and not seen[c] and (c == to or not skirmishAt(c)) then seen[c] = true; open[#open + 1] = c end
    end
  end
  return false
end
-- A weapon's worth to unit u: might, less the speed its weight costs.
local function weaponScore(u, w) return w.might * 10 - math.max(0, w.weight - u.con) * 6 end

local function makeShopPlan()
  local d = L.GM.data
  local lord = emu:read8(d + 0x11)
  local lx, ly = nodeXY(lord)
  local v5e = tacticsE5(L.playSt().chapter)
  local reach = (not v5e) and reachableNodes(lord) or nil
  local best, node
  for i = 0, L.GM.nodeCount - 1 do
    local st = emu:read8(d + 0x30 + 4 * i)
    local arm = emu:read32(L.GM.nodeData + i * 0x20 + 0x0C)
    if (st & 1) ~= 0 and (st & 2) == 0 and arm ~= 0 and emu:read16(arm) ~= 0 and not skirmishAt(i)
        and ((v5e and gmPathClear(lord, i)) or (not v5e and reach[i])) then
      local x, y = nodeXY(i)
      local dd = math.abs(x - lx) + math.abs(y - ly)
      if best == nil or dd < best then best, node = dd, i end
    end
  end
  if not node then
    log("shop plan: no armory on a cleared node the lord can walk to")
    return { done = true, why = "no armory on a cleared node" }
  end
  local stock = shopStock(emu:read32(L.GM.nodeData + node * 0x20 + 0x0C))
  local gold = L.playSt().gold - (L.PR.on and L.PR.KEEP or SHOP_KEEP_GOLD)   -- (PR: L.PR.KEEP)
  local want, any = {}, false
  -- v5: a fighter with no usable 1-2 range weapon (Javelin, Hand Axe) gets one,
  -- so it can hit an enemy that only attacks next to itself (Ephraim's
  -- Chapter 9: the boss on the throne) without a counter.
  local v5 = tacticsV5(L.playSt().chapter)
  local function hasRanged(u)
    for _, it in ipairs(u.items) do
      local w = L.item(it & 0xFF)
      if canUseWeapon(u, w) and w.minRange <= 1 and w.maxRange >= 2 then return true end
    end
    return false
  end
  for _, u in ipairs(unitOrder(equipAll(L.blue()), true)) do
    local U = usableUses(u)
    local needRanged = v5 and isFighter(u) and not hasRanged(u)
    local low = L.PR.on and L.PR.LOW or SHOP_LOW_USES   -- (PR: L.PR.LOW)
    if isFighter(u) and (U < low or needRanged) then
      local cands = {}
      for _, id in ipairs(stock) do
        local w = L.item(id)
        if (w.attributes & 4) == 0 and canUseWeapon(u, w) and not (L.PR.on and w.uses * w.cost > L.PR.MAXPRICE) then
          cands[#cands + 1] = w
        end
      end
      table.sort(cands, function(a, b)
        local sa, sb = weaponScore(u, a), weaponScore(u, b)
        if sa ~= sb then return sa > sb end
        return a.id < b.id
      end)
      local n = (U < low) and ((U < 15) and 2 or 1) or 0
      if L.PR.on and U < low then n = L.PR.count(U) end
      local ranged
      if needRanged then
        for _, w in ipairs(cands) do if w.minRange <= 1 and w.maxRange >= 2 then ranged = w; break end end
      end
      local need = n + (ranged and 1 or 0)
      local free = 5 - #u.items
      local sell = {}
      for _, it in ipairs(u.items) do
        if free >= need then break end
        local w = L.item(it & 0xFF)
        local junk = ((w.attributes & 1) ~= 0 and (w.attributes & 4) == 0 and not canUseWeapon(u, w))
                  or ((w.attributes & 4) ~= 0 and not canUseStaff(u, w)) or L.PR.junk(w)
        if junk and (w.attributes & 0x10) == 0 and w.cost > 0 then sell[#sell + 1] = w.id; free = free + 1 end
      end
      local buy = {}
      for k = 1, math.min(n, free) do
        local w = cands[k] or cands[1]
        if w and w.uses * w.cost <= gold then buy[#buy + 1] = w.id; gold = gold - w.uses * w.cost end
      end
      if ranged and #buy < free and ranged.uses * ranged.cost <= gold then
        buy[#buy + 1] = ranged.id; gold = gold - ranged.uses * ranged.cost
      end
      if #buy > 0 then
        local base = {}
        for _, id in ipairs(buy) do base[id] = itemCount(u, id) end
        for _, id in ipairs(sell) do base[id] = itemCount(u, id) end
        want[u.char] = { buy = buy, sell = sell, base = base }
        any = true
        local bs, ss = {}, {}
        for _, id in ipairs(buy) do bs[#bs + 1] = string.format("%02x", id) end
        for _, id in ipairs(sell) do ss[#ss + 1] = string.format("%02x", id) end
        log("shop plan: char %02x (weapon uses %d): buy %s, sell %s", u.char, U, table.concat(bs, " "), table.concat(ss, " "))
      end
    end
  end
  -- v10: heal items from the same node's vendor (gWMNodeData +0x10), after the
  -- armory: an Elixir for each unit that has none (lords first), else a
  -- Vulnerary; a full unit sells what nobody in the army can use (gems,
  -- promotion items that fit no one, weapons it cannot use) for the room.
  -- Darkling Woods: no healer is left, and the army wore down to the end.
  local vwant, vany = {}, false
  local vstock = shopStock(emu:read32(L.GM.nodeData + node * 0x20 + 0x10))
  if (tacticsV10(L.playSt().chapter) or L.E7.chapter(L.playSt().chapter) or L.PR.on) and #vstock > 0 then   -- (E7, PR too)
    local has = {}
    for _, id in ipairs(vstock) do has[id] = true end
    local army = equipAll(L.blue())
    local order = {}
    for _, u in ipairs(army) do if isLordUnit(u) then order[#order + 1] = u end end
    for _, u in ipairs(unitOrder(army, true)) do if not isLordUnit(u) then order[#order + 1] = u end end
    local function nobodyUses(id)
      local w = L.item(id)
      for _, b in ipairs(army) do
        if (w.attributes & 1) ~= 0 and (w.attributes & 4) == 0 and canUseWeapon(b, w) then return false end
        if (w.attributes & 4) ~= 0 and canUseStaff(b, w) then return false end
        if promoFits(b, id) then return false end
        if HEAL_ITEMS[id] or (id >= 0x5B and id <= 0x63) then return false end
      end
      return true
    end
    local e7 = L.E7.chapter(L.playSt().chapter)
    for _, u in ipairs(order) do
      local pick
      if itemCount(u, 0x6D) == 0 and has[0x6D] then pick = 0x6D
      elseif itemCount(u, 0x6C) == 0 and itemCount(u, 0x6D) == 0 and has[0x6C] then pick = 0x6C
      -- (E7: a second Elixir for each unit that is not a lord, which Ephraim may take by Trade:
      -- in a try his one Elixir was spent by turn 50, and he stood at 4 HP)
      elseif e7 and not isLordUnit(u) and itemCount(u, 0x6D) == 1 and has[0x6D] then pick = 0x6D end
      if pick then
        local sell = {}
        local planned = want[u.char] and #want[u.char].buy or 0
        -- (PR: what the armory sells for this unit is gone by the vendor's turn: neither counted nor
        -- chosen again; scratch et1b sold Ephraim's one Door Key at both and bought Elixirs for ever)
        local sold = {}
        if L.PR.on and want[u.char] then
          for _, id in ipairs(want[u.char].sell) do sold[id] = (sold[id] or 0) + 1; planned = planned - 1 end
        end
        if #u.items + planned >= 5 then
          for _, it in ipairs(u.items) do
            local w = L.item(it & 0xFF)
            -- (E7: never the Hoplon Shield, which keeps its holder from critical hits)
            if (sold[w.id] or 0) > 0 then sold[w.id] = sold[w.id] - 1
            elseif (w.attributes & 0x10) == 0 and w.cost > 0 and nobodyUses(w.id) and not (e7 and w.id == 0x7C) then
              sell[1] = w.id; gold = gold + (w.cost * (it >> 8)) // 2; break
            end
          end
        end
        local price = L.item(pick).uses * L.item(pick).cost
        if price > gold and pick == 0x6D and has[0x6C] then
          pick = 0x6C; price = L.item(pick).uses * L.item(pick).cost
        end
        -- (a full unit with nothing to sell buys it into the supply: the
        -- shop's "Send it to storage?", Yes; the lord takes it on the map)
        local toSupply = not (#u.items + planned < 5 or #sell > 0)
        -- (E7: not into the supply, out of reach once Eirika is carried; nor the second Elixir
        -- for less than 6,000 gold)
        if e7 and (toSupply or (itemCount(u, 0x6D) == 1 and gold < price + 6000)) then price = gold + 1 end
        if price <= gold then
          gold = gold - price
          local base = { [pick] = itemCount(u, pick) }
          for _, id in ipairs(sell) do base[id] = itemCount(u, id) end
          vwant[u.char] = { buy = { pick }, sell = sell, base = base }
          if toSupply then
            local n = 0
            for _, id in ipairs(convoyItems()) do if id == pick then n = n + 1 end end
            vwant[u.char].supplyBase = { [pick] = n }
          end
          vany = true
          log("shop plan: char %02x at the vendor: buy %02x%s%s", u.char, pick, #sell > 0 and string.format(", sell %02x", sell[1]) or "",
            toSupply and " (into the supply)" or "")
        end
      end
    end
  end
  if not any and not vany then return { done = true, why = "nothing to buy" } end
  log("shop plan: armory at node %d, %d gold to spend", node, L.playSt().gold - SHOP_KEEP_GOLD)
  if not any then return { node = node, want = vwant, phase = "vendor" } end
  return { node = node, want = want, vwant = vany and vwant or nil }
end

-- The next item to sell or buy for unit u (nil when its part is done).
local function pendingOf(u, kind)
  local w = shopPlan and shopPlan.want[u.char]
  if not w then return nil end
  local planned = {}
  for _, id in ipairs(w[kind]) do
    planned[id] = (planned[id] or 0) + 1
    local have = itemCount(u, id)
    if kind == "buy" and w.supplyBase and w.supplyBase[id] then   -- v10: bought into the supply
      for _, cid in ipairs(convoyItems()) do if cid == id then have = have + 1 end end
      have = have - w.supplyBase[id]
    end
    if kind == "sell" and have > w.base[id] - planned[id] then return id end
    if kind == "buy" and have < w.base[id] + planned[id] then return id end
  end
  return nil
end
local function shopping() return shopPlan ~= nil and not shopPlan.done end

-- The item screen's unit grid (shop mode): go to the first unit with work
-- left (columns first, then rows), A; B when every unit is done.
local function handlePrepShop(pi)
  local n = emu:read32(L.PREPLIST + 0x100)
  local hov = emu:read8(pi.addr + 0x2A)
  local target
  for i = 0, n - 1 do
    local u = L.readUnit(emu:read32(L.PREPLIST + 4 * i))
    if u and (pendingOf(u, "sell") or pendingOf(u, "buy")) then target = i; break end
  end
  if not target then
    if shopPlan.vwant then   -- v10: the vendor next (the node menu's Shop)
      shopPlan.want, shopPlan.vwant, shopPlan.phase = shopPlan.vwant, nil, "vendor"
      log("armory done, gold %d; the vendor next", L.playSt().gold)
      push(K.B, 1, 10)
      return
    end
    shopPlan.done = true
    if shopPlan.phase == "secret" then L.F.st.shopEnd = tick end   -- (coverage 12, ROUTE_WMSHOP)
    log("shopping done, gold %d", L.playSt().gold)
    push(K.B, 1, 10)
    return
  end
  if hov == target then push(K.A, 1, 10)
  elseif target % 3 < hov % 3 then push(K.LEFT, 1, 4)
  elseif target % 3 > hov % 3 then push(K.RIGHT, 1, 4)
  elseif target < hov then push(K.UP, 1, 4)
  else push(K.DOWN, 1, 4) end
end

-- The shop: Sell first (the room), then Buy, then leave.
local function handleShop(procs, sp)
  local u = L.readUnit(emu:read32(sp.addr + 0x2C))
  local sell = u and pendingOf(u, "sell")
  local buy = u and pendingOf(u, "buy")
  local tc = L.findIdle(procs, L.IDLE.TalkChoice_OnIdle)
  if tc then
    local choices, sel = emu:read32(tc.addr + 0x34), L.s16(emu:read16(tc.addr + 0x2A))
    local want = 0
    if choices == L.TALKCHOICE.YesNo then want = 1 elseif sell then want = 2 elseif buy then want = 1 end
    if want == 0 then push(K.B, 1, 6)
    elseif sel < want then push(K.RIGHT, 1, 4)
    elseif sel > want then push(K.LEFT, 1, 4)
    else push(K.A, 1, 6) end
    return
  end
  local head = emu:read8(sp.addr + 0x5C)
  local function goLine(idx)
    if idx == nil then push(K.B, 1, 6)
    elseif head < idx then push(K.DOWN, 1, 4)
    elseif head > idx then push(K.UP, 1, 4)
    else
      log("shop: char %02x %s item %02x", u.char, sp.idle == L.SHOP.BuyLoop and "buys" or "sells",
        sp.idle == L.SHOP.BuyLoop and buy or sell)
      push(K.A, 1, 6)
    end
  end
  if sp.idle == L.SHOP.BuyLoop then
    local idx
    if buy then
      for i = 0, 19 do if (emu:read16(sp.addr + 0x30 + 2 * i) & 0xFF) == buy then idx = i; break end end
    end
    goLine(idx)
  elseif sp.idle == L.SHOP.SellLoop then
    local idx
    if sell then
      for k, it in ipairs(u.items) do if (it & 0xFF) == sell then idx = k - 1; break end end
    end
    goLine(idx)
  elseif L.findIdle(procs, L.IDLE.TalkWaitForInput) then
    push(K.A, 1, 5)
  else
    wait(1)
  end
end

---------------------------------------------------------------------------
-- Input handlers.

local function cursorTo(tx, ty)
  local cx, cy, dx, dy = L.cursor()
  if dx ~= cx * 16 or dy ~= cy * 16 then wait(1); return false end
  if cx < tx then push(K.RIGHT, 1, 1); return false end
  if cx > tx then push(K.LEFT, 1, 1); return false end
  if cy < ty then push(K.DOWN, 1, 1); return false end
  if cy > ty then push(K.UP, 1, 1); return false end
  return true
end

local function handleMainIdle()
  local ps = L.playSt()
  if L.GAL.on then L.GAL.idle() end   -- (the item gallery: the last plan's outcome, L.GAL)
  if shopPlan and shopPlan.mapVendor then shopPlan = nil end   -- v6: a map vendor's shop is closed
  local turnKey = ps.chapter .. ":" .. ps.turn
  local blue = L.blue()
  if turnKey ~= lastTurnKey then
    lastTurnKey = turnKey; skipUnits = {}; badDest = {}; endTurn = false; plan = nil
    if verbose then   -- log the units lost since the last player phase
      local now = {}
      for _, b in ipairs(blue) do now[b.id] = b.char end
      for id, ch in pairs(lastAlive or {}) do
        if not now[id] then log("lost unit %02x (char %02x) before turn %d", id, ch, ps.turn) end
        -- (coverage 12, ROUTE_TRAINEE: the trainee lost in a chapter's play ends the try; L.F.stopCheck)
        if not now[id] and L.F.trainee and ch == L.F.trainee and ps.turn > 1 then L.F.st.traineeLost = tick end
      end
      lastAlive = now
    end
  end
  if plan then
    local still = false
    for _, u in ipairs(blue) do
      if u.id == plan.uid and (u.state & (L.US.UNSELECTABLE | L.US.HIDDEN)) == 0 then still = true end
    end
    if not still then plan = nil end
  end
  if plan then
    plan.selectTries = (plan.selectTries or 0)
    if plan.selectTries > 6 then skipUnits[plan.uid] = true; plan = nil end
  end
  -- (ROUTE_SUSPEND, L.F: at turn T's first free cursor, the map menu, for its Suspend)
  if L.F.susp and not plan and not L.F.st.suspAt and not L.F.st.suspArmed and ps.turn >= L.F.susp.turn then
    L.F.st.suspArmed = true
    log("suspend: turn %d, the map menu for Suspend", ps.turn)
  end
  if not plan and not endTurn and not L.F.st.suspArmed then
    -- (E5's rout hold-out keeps the lord last: it then picks its tile with the
    -- others already in place; moving first, Eirika stood boxed in by allies who
    -- then walked away, and an enemy came through the gap)
    local order = unitOrder(blue, tacticsV3(ps.chapter), holdOut(ps.chapter))
    if tacticsV7(ps.chapter) then order = unarmedBossOrder(order) end
    -- v10: the lord first while he has a supply plan (he gives the room-making
    -- weapons he cannot use, which the others may then take). (E7 too)
    if tacticsV10(ps.chapter) or L.E7.chapter(ps.chapter) then
      L.E7.on = L.E7.chapter(ps.chapter)   -- (supplyPlanFor below: E7's own rules)
      local lord = supplyLord()
      if lord and (lord.state & (L.US.UNSELECTABLE | L.US.HIDDEN)) == 0 and supplyPlanFor(equip(lord)) then
        local o = { lord }
        for _, u in ipairs(order) do if u.id ~= lord.id then o[#o + 1] = u end end
        order = o
      elseif holderOf(ps.chapter, L.red()) then
        -- v10: while an enemy holds the seize tile, the seizer moves last (the strike)
        local sz, o = seizerUnit(blue), {}
        for _, u in ipairs(order) do if not sz or u.id ~= sz.id then o[#o + 1] = u end end
        if sz then o[#o + 1] = sz end
        order = o
      end
      -- (E7: Eirika's carrier moves after the escorts, which clear its way first)
      if L.E7.on then
        local o, last = {}, {}
        for _, u in ipairs(order) do if carriesLord(u) then last[#last + 1] = u else o[#o + 1] = u end end
        for _, u in ipairs(last) do o[#o + 1] = u end
        order = o
        -- (the final chapter: the director's order, L.E7.finOrder)
        if ps.chapter >= 0x15 then order = L.E7.finOrder(order) end
      end
      -- (E7 on map 0x23: the director's order, L.E7.order23)
      if L.E7.eph(ps.chapter) then order = L.E7.order23(order) end
    end
    if L.R10.on(ps.chapter) then order = L.R10.order(order) end   -- (R10, index 0x37: the director's order)
    if L.GAL.on then order = L.GAL.order(order) end                -- (the item gallery: the units with a job)
    for _, u in ipairs(order) do
      if (u.state & (L.US.UNSELECTABLE | L.US.HIDDEN)) == 0 and not skipUnits[u.id] then
        plan = { uid = u.id, ux = u.x, uy = u.y, selectTries = 0 }
        log("select unit %02x char %02x at (%d,%d) hp %d/%d", u.id, u.char, u.x, u.y, u.hp, u.maxHp)
        break
      end
    end
    if not plan then endTurn = true; log("end turn %d", ps.turn) end
  end
  if plan then
    local u
    for _, b in ipairs(blue) do if b.id == plan.uid then u = b end end
    if cursorTo(u.x, u.y) then
      plan.selectTries = plan.selectTries + 1
      push(K.A, 1, 3)
    end
    return
  end
  -- End turn: A on an empty tile opens the map menu.
  local cx, cy = L.cursor()
  local w, h = L.mapSize()
  local tx, ty = cx, cy
  if L.mapGet(L.ADDR.gBmMapUnit, cx, cy) ~= 0 then
    local best
    for y = 0, h - 1 do for x = 0, w - 1 do
      if L.mapGet(L.ADDR.gBmMapUnit, x, y) == 0 then
        local d = dist(x, y, cx, cy)
        if best == nil or d < best then best, tx, ty = d, x, y end
      end
    end end
  end
  if cursorTo(tx, ty) then push(K.A, 1, 3) end
end

local function activeUnit()
  local p = emu:read32(L.ADDR.gActiveUnit)
  if p == 0 then return nil end
  return L.readUnit(p)
end

local function handleRange()
  local u = activeUnit()
  if not u then push(K.B, 1, 4); return end
  local canto = (u.state & L.US.HAS_MOVED) ~= 0
  if not plan or plan.uid ~= u.id then
    if canto then
      plan = { uid = u.id, selectTries = 0 }
    else
      log("unexpected unit %02x selected, cancelling", u.id)
      push(K.B, 1, 4); return
    end
  end
  if canto and not plan.canto then plan.canto = true; plan.dest = nil end
  if not plan.dest then
    local p = think(u)
    -- v6 attack trials: against a boss, or when the chapter has stalled, the unit's
    -- best attacks (up to TRIAL_N) and its plan without an attack are each played
    -- out from a save state, and the real outcome decides (searchStep, "trial").
    if E6 and not canto and TR == nil and p.cands and #p.cands > 0 then
      local want = SS_stalled() or TRIAL_ALL
      for _, c in ipairs(p.cands) do if c.boss then want = true end end
      if want then
        table.sort(p.cands, function(a, b)
          if a.score ~= b.score then return a.score > b.score end
          if a.plan.target ~= b.plan.target then return a.plan.target < b.plan.target end
          return a.plan.dest.y * 64 + a.plan.dest.x < b.plan.dest.y * 64 + b.plan.dest.x
        end)
        local list = {}
        for k = 1, math.min(TRIAL_N, #p.cands) do list[#list + 1] = p.cands[k].plan end
        local q = p
        if p.action == "attack" then P.noAttack = true; q = think(u); P.noAttack = nil end
        if q.action ~= "attack" then list[#list + 1] = { dest = q.dest, action = q.action, target = q.target,
          targetPos = q.targetPos, slot = q.slot, takeSlot = q.takeSlot, giveSlot = q.giveSlot, buy = q.buy, take = q.take } end
        if #list >= 2 then
          TR = { want = true, uid = u.id, list = list, chapter = L.playSt().chapter, turn = L.playSt().turn }
          log("trial: unit %02x, %d plans", u.id, #list)
          wait(1)
          return
        end
      end
    end
    if TR and TR.active and TR.uid == u.id and TR.list[TR.idx] then
      p = TR.list[TR.idx]
      TR.running = true
    end
    plan.dest, plan.action, plan.target, plan.targetPos = p.dest, p.action, p.target, p.targetPos
    plan.slot, plan.takeSlot, plan.giveSlot = p.slot, p.takeSlot, p.giveSlot
    plan.wslot = p.wslot   -- v5, E6 (chapter index 20 on): the weapon chosen for an attack (inventory slot)
    plan.promote = p.promote   -- v7: the item is a promotion item
    plan.supply = p.supply     -- v10: the supply plan
    plan.buy = p.buy   -- E6: a map vendor's shopping list
    plan.take = p.take -- E6: the item to take from the supply
    plan.dropAt = p.dropAt -- E6, chapter index 20 on: where a drop puts the carried unit
    plan.wallAt = p.wallAt -- the post-game's walls (L.F.think): the cracked wall to attack
    plan.gal = p.gal       -- the item gallery's steps (L.GAL)
    plan.menuId = p.menuId -- (coverage 12, ROUTE_SHOP: the map shop's unit-menu command)
    plan.aTries = 0
    log("unit %02x (%d,%d) hp %d -> (%d,%d) %s%s%s%s%s", u.id, u.x, u.y, u.hp, plan.dest.x, plan.dest.y, plan.action,
      plan.target and string.format(" target %02x at (%d,%d)", plan.target, plan.targetPos.x, plan.targetPos.y) or "",
      (p.slot or p.takeSlot) and string.format(" slot %s give %s", tostring(p.slot or p.takeSlot), tostring(p.giveSlot)) or "",
      p.wslot and string.format(" weapon slot %d (item %02x)", p.wslot, u.items[p.wslot + 1] & 0xFF) or "",
      p.th and string.format(" (threat %d)", p.th) or "")
  end
  if cursorTo(plan.dest.x, plan.dest.y) then
    plan.aTries = plan.aTries + 1
    if plan.aTries > 3 then
      badDest[u.id .. ":" .. plan.dest.x .. ":" .. plan.dest.y] = true
      log("destination (%d,%d) refused, replanning", plan.dest.x, plan.dest.y)
      plan.dest = nil
      wait(1)
      return
    end
    push(K.A, 1, 6)
  end
end

local function menuItems(menu)
  local count = emu:read8(menu.addr + 0x60)
  local ids = {}
  for i = 0, count - 1 do
    local ip = emu:read32(menu.addr + 0x34 + 4 * i)
    ids[i] = emu:read8(emu:read32(ip + 0x30) + 0x09)
  end
  return ids, count, emu:read8(menu.addr + 0x61)
end

local function menuGo(cur, idx)
  if cur < idx then push(K.DOWN, 1, 3) elseif cur > idx then push(K.UP, 1, 3) else push(K.A, 1, 6) end
end

local function handleMenu(menu)
  local def = emu:read32(menu.addr + 0x30)
  local ids, count, cur = menuItems(menu)
  if L.GAL.on and plan and plan.gal and L.GAL.menu(menu, def, ids, count, cur) then return end   -- (the item gallery)
  -- v3: choose the entry with this overrideId, or give up the plan (B, then Wait).
  local function pick(id)
    for i = 0, count - 1 do
      if ids[i] == id then menuGo(cur, i); return end
    end
    log("menu %08x has no entry %02x, giving up %s", def, id, plan and plan.action or "-")
    if plan then plan.action = "wait" end
    push(K.B, 1, 6)
  end
  if def == L.MENUDEF.UnitAction then
    local want = L.MENUITEM.Wait
    if plan and plan.action == "attack" then want = L.MENUITEM.Attack end
    if plan and plan.action == "seize" then want = L.MENUITEM.Seize end
    if plan and plan.action == "staff" then want = L.MENUITEM.Staff end
    if plan and plan.action == "item" then want = L.MENUITEM.Item end
    if plan and plan.action == "trade" and not plan.tradeDone then want = L.MENUITEM.Trade end
    if plan and plan.action == "door" then want = L.MENUITEM.Door end
    if plan and plan.action == "talk" then want = L.MENUITEM.Talk end
    if plan and plan.action == "visit" then want = L.MENUITEM.Visit end
    if plan and plan.action == "rescue" then want = MENUITEM_RESCUE end   -- v9, E5 (L.MENUITEM.Rescue, the same 0x63)
    if plan and plan.action == "equip" and not plan.equipDone then want = L.MENUITEM.Item end     -- v10
    if plan and plan.action == "drop" then want = MENUITEM_DROP end         -- E6, chapter index 20 on
    if plan and plan.action == "vendor" then want = MENUITEM_VENDOR end      -- E6
    if plan and plan.action == "supply" then                                 -- v10, E6: Supply, then Wait
      want = plan.supplyDone and L.MENUITEM.Wait or MENUITEM_SUPPLY
    end
    if plan and L.F.ACT[plan.action] then want = L.F.ACT[plan.action] end   -- (feature routes, L.F: Arena, Support, Chest)
    if plan and plan.action == "mapshop" then want = plan.menuId end          -- (coverage 12, ROUTE_SHOP)
    local idx
    for i = 0, count - 1 do if ids[i] == want then idx = i end end
    if not idx and plan and (plan.action == "mapshop" or plan.action == "supports") then   -- (coverage 12)
      local s = {}
      for i = 0, count - 1 do s[#s + 1] = string.format("%02x", ids[i]) end
      log("%s: no menu item %02x in the unit menu (%s)", plan.action, want, table.concat(s, " "))
    end
    if not idx then
      for i = 0, count - 1 do if ids[i] == L.MENUITEM.Wait then idx = i end end
      if plan then plan.action = "wait" end
    end
    if not idx then push(K.B, 1, 6); return end
    if plan and cur == idx then plan.menuChosen = true end
    if plan and cur == idx and L.F.ACT[plan.action] and ids[idx] == want then L.F.chosen(plan.action) end
    if plan and cur == idx and ids[idx] == want and ((plan.action == "door" and L.F.door and not L.F.st.doorAt)
        or (plan.action == "visit" and L.F.visit)) then
      L.F.chosen(plan.action)   -- (coverage 12, ROUTE_DOOR, ROUTE_VISIT: st.doorAt, st.visitAt)
    end
    if plan and plan.action == "mapshop" and cur == idx and ids[idx] == want then
      -- (coverage 12, ROUTE_SHOP) the shop that opens buys and sells the route's lists (handleShop)
      local u = activeUnit()
      if u then
        local base = {}
        for _, id in ipairs(L.F.shop.buy) do base[id] = itemCount(u, id) end
        for _, id in ipairs(L.F.shop.sell) do base[id] = itemCount(u, id) end
        shopPlan = { mapVendor = true, want = { [u.char] = { buy = L.F.shop.buy, sell = L.F.shop.sell, base = base } } }
        L.F.chosen("shop")
      end
    end
    if plan and plan.action == "vendor" and cur == idx then
      -- v6: the shop that opens buys plan.buy for this unit (handleShop, as on the world map)
      local u = activeUnit()
      if u then
        local base = {}
        for _, id in ipairs(plan.buy) do base[id] = itemCount(u, id) end
        shopPlan = { mapVendor = true, want = { [u.char] = { buy = plan.buy, sell = {}, base = base } } }
        log("vendor: char %02x buys %d items", u.char, #plan.buy)
      end
    end
    menuGo(cur, idx)
  -- (coverage 12, ROUTE_PROMO_CHOICE: the n-th class of the promotion screen's list)
  elseif L.F.promoChoice and (def == MENUDEF_PROMO_SEL or def == MENUDEF_PROMOSEL) then
    local want = math.min(L.F.promoChoice, count) - 1
    if cur == want then log("promotion: class %d of %d", want + 1, count) end
    menuGo(cur, want)
  -- (E6's promotion menus; Ephraim's chapters take them by v7's rule below, plan.promote)
  elseif def == MENUDEF_PROMO_SEL and not tacticsV5(L.playSt().chapter) then
    push(K.A, 1, 10)   -- E6: the first class on offer
  elseif def == MENUDEF_PROMO_CONFIRM and not tacticsV5(L.playSt().chapter) then
    menuGo(cur, 0)     -- E6: "Change"
  elseif def == L.MENUDEF.MapMenu then
    if L.F.st.suspArmed then L.F.suspendMenu(ids, count, cur)   -- (ROUTE_SUSPEND, L.F)
    elseif endTurn then
      local idx
      for i = 0, count - 1 do if ids[i] == L.MENUITEM.EndPhase then idx = i end end
      if idx then menuGo(cur, idx) else push(K.B, 1, 6) end
    else
      push(K.B, 1, 6)
    end
  elseif def == L.MENUDEF.ItemSelect and plan and plan.action == "item" then
    pick(L.MENUITEM.ItemSelect0 + plan.slot)
  elseif def == L.MENUDEF.ItemSub and plan and plan.action == "item" then
    pick(L.MENUITEM.ItemUse)
  elseif def == L.MENUDEF.ItemSelect and plan and plan.action == "equip" then
    -- v10: Item, the weapon, Equip (the game reopens the list), then B and Wait
    if plan.equipDone then push(K.B, 1, 6) else pick(L.MENUITEM.ItemSelect0 + plan.slot) end
  elseif def == L.MENUDEF.ItemSub and plan and plan.action == "equip" then
    local idx
    for i = 0, count - 1 do if ids[i] == MENUITEM_EQUIP then idx = i end end
    if not idx then plan.equipDone = true; push(K.B, 1, 6)
    elseif cur == idx then plan.equipDone = true; log("equip: slot %d", plan.slot); push(K.A, 1, 6)
    else menuGo(cur, idx) end
  elseif def == L.MENUDEF.StaffItemSelect and plan and plan.action == "staff" then
    pick(L.MENUITEM.StaffSelect0 + plan.slot)
  elseif def == L.MENUDEF.WeaponSelect and plan and plan.action == "attack" and plan.wslot then
    pick(L.MENUITEM.WeaponSelect0 + plan.wslot)   -- v5, E6 (chapter index 20 on): the weapon the plan chose
  elseif (def == MENUDEF_PROMOSEL or def == MENUDEF_PROMOCONFIRM) and plan and plan.promote then
    log("promotion menu %08x: cursor %d of %d", def, cur, count)
    menuGo(cur, 0)                                 -- v7: the first class, then "Change"
  else
    -- Weapon choice after Attack: take the default (first) weapon.
    if plan and plan.action == "attack" then push(K.A, 1, 6) else push(K.B, 1, 6) end
  end
end

-- v3: the trade screen. Take the ally's item plan.takeSlot (column 1), then put
-- it in the empty cell the game offers on the unit's side, or swap it with
-- plan.giveSlot when the unit's five slots are full; then B leaves.
local function handleTrade(tp)
  local col, row = emu:read8(tp.addr + 0x41), emu:read8(tp.addr + 0x42)
  if not plan or plan.action ~= "trade" or plan.tradeDone then push(K.B, 1, 6); return end
  if tp.idle == L.TRADE.Unselected then
    local want = plan.takeSlot
    if col ~= 1 then push(K.RIGHT, 1, 4)
    elseif row < want then push(K.DOWN, 1, 4)
    elseif row > want then push(K.UP, 1, 4)
    else push(K.A, 1, 6) end
  elseif tp.idle == L.TRADE.Selected then
    local want = plan.giveSlot
    if want == nil or col ~= 0 or row == want then
      log("trade: item %d of %02x to slot %d (cursor %d,%d)", plan.takeSlot, plan.target, want or row, col, row)
      push(K.A, 1, 6); plan.tradeDone = true
    elseif row < want then push(K.DOWN, 1, 4)
    else push(K.UP, 1, 4) end
  else
    wait(1)
  end
end

-- v10: the supply screen (ProcScr_BmSupplyScreen, src/prep_itemsupply.c;
-- struct PrepItemSupplyProc: unit +0x2C, unitInvIdx +0x31, Give/Take cursor
-- +0x33, currentPage +0x35, idxPerPage[9] +0x3A, yOffsetPerPage[9] +0x4C).
-- Give first (the room), then Take: the item's page (GetPrepPageForItem: the
-- weapon type, items on page 8), its line in gPrepScreenItemList (pid, slot,
-- item; gUnknown_02012F56 lines), A. Then B to the map, and the unit waits.
PROC_BMSUPPLY = 0x08A192EC
SUP = { GiveTake = 0x0809DCD5, List = 0x0809E421, UnitInv = 0x0809E975 }
PREP_ITEMLIST, PREP_ITEMCOUNT = 0x020122D4, 0x02012F56
local function supplyNext(u, s, kind)
  local planned = {}
  for _, id in ipairs(s[kind]) do
    planned[id] = (planned[id] or 0) + 1
    local have = itemCountOf(u, id)
    if kind == "give" and have > s.base[id] - planned[id] then return id end
    if kind == "take" and have < s.base[id] + planned[id] and not (s.failed and s.failed[id]) then return id end
  end
end
local function handleSupply(sp)
  local u = L.readUnit(emu:read32(sp.addr + 0x2C))
  local s = plan and plan.supply
  local give = u and s and supplyNext(u, s, "give")
  local take = u and s and #u.items < 5 and supplyNext(u, s, "take")
  if sp.idle == SUP.GiveTake then
    local cur = emu:read8(sp.addr + 0x33)
    local want = give and 0 or (take and 1) or nil
    if want == nil then
      if plan then plan.supplyDone = true end
      log("supply done: items %s", (function() local t = {} for _, it in ipairs(u and u.items or {}) do t[#t + 1] = string.format("%02x", it & 0xFF) end return table.concat(t, " ") end)())
      push(K.B, 1, 10)
    elseif cur ~= want then push(K.DOWN, 1, 4)
    else push(K.A, 1, 8) end
  elseif sp.idle == SUP.UnitInv then
    local slot
    for k, it in ipairs(u and u.items or {}) do if give and (it & 0xFF) == give then slot = k - 1; break end end
    if not slot then push(K.B, 1, 8); return end
    local cur = emu:read8(sp.addr + 0x31)
    if cur < slot then push(K.DOWN, 1, 4) elseif cur > slot then push(K.UP, 1, 4)
    else log("supply: char %02x gives %02x", u.char, give); push(K.A, 1, 8) end
  elseif sp.idle == SUP.List then
    if not take then push(K.B, 1, 8); return end
    local page = emu:read8(sp.addr + 0x35)
    if (emu:read16(sp.addr + 0x4C + 2 * page) & 0xF) ~= 0 then wait(1); return end
    local wt = L.item(take).weaponType
    local want = (wt <= 7) and wt or 8
    if page < want then push(K.RIGHT, 1, 6); return end
    if page > want then push(K.LEFT, 1, 6); return end
    local n, idx = emu:read16(PREP_ITEMCOUNT), nil
    for i = 0, n - 1 do
      if (emu:read16(PREP_ITEMLIST + 4 * i + 2) & 0xFF) == take then idx = i; break end
    end
    if not idx then
      log("supply: item %02x not on page %d", take, page)
      s.failed = s.failed or {}; s.failed[take] = true
      push(K.B, 1, 8); return
    end
    local cur = emu:read16(sp.addr + 0x3A + 2 * page)
    if cur < idx then push(K.DOWN, 1, 4) elseif cur > idx then push(K.UP, 1, 4)
    else log("supply: char %02x takes %02x", u.char, take); push(K.A, 1, 8) end
  else
    wait(1)
  end
end

-- E6: the supply screen (the same proc, SUPPLY.Proc = PROC_BMSUPPLY): Take, then the page holding plan.take (Right turns the
-- page), the cursor to it, A; then B out to the unit's menu.
local function handleSupplyE(sp)
  if not plan or plan.action ~= "supply" or plan.supplyDone then push(K.B, 1, 10); return end
  plan.taken = plan.taken or 0
  local item = plan.take[plan.taken + 1]
  if not item then plan.supplyDone = true; push(K.B, 1, 10); return end
  if sp.idle == SUPPLY.GiveTake then
    if emu:read8(sp.addr + 0x33) ~= 1 then push(K.DOWN, 1, 6) else push(K.A, 1, 10) end
  elseif sp.idle == SUPPLY.Take then
    local page = emu:read8(sp.addr + 0x35)
    local cur = emu:read16(sp.addr + 0x3A + 2 * page)
    local want
    for i = 0, emu:read16(SUPPLY.ListN) - 1 do
      if (emu:read16(SUPPLY.List + 4 * i + 2) & 0xFF) == item then want = i; break end
    end
    if want == nil then
      plan.pages = (plan.pages or 0) + 1
      if plan.pages > 10 then plan.supplyDone = true; push(K.B, 1, 10) else push(K.RIGHT, 1, 12) end
    elseif cur < want then push(K.DOWN, 1, 6)
    elseif cur > want then push(K.UP, 1, 6)
    else
      log("supply: take item %02x (page %d line %d)", item, page, cur)
      push(K.A, 1, 12); plan.taken = plan.taken + 1; plan.pages = 0
    end
  else
    wait(1)
  end
end

local function handleTarget(ts)
  if L.GAL.on and plan and plan.gal and L.GAL.target(ts) then return end   -- (the item gallery)
  local cur = emu:read32(ts.addr + 0x30)
  local uid = (cur ~= 0) and emu:read8(cur + 2) or 0
  plan = plan or {}
  plan.tTries = (plan.tTries or 0) + 1
  -- (v6, chapter index 20 on: a drop's targets are positions, struct SelectTarget x +0, y +1)
  if plan.action == "drop" and plan.dropAt then
    local x, y = (cur ~= 0) and L.s8(emu:read8(cur)) or -1, (cur ~= 0) and L.s8(emu:read8(cur + 1)) or -1
    if (x ~= plan.dropAt.x or y ~= plan.dropAt.y) and plan.tTries < 12 then push(K.RIGHT, 1, 4)
    else log("confirm drop at (%d,%d)", x, y); push(K.A, 1, 6) end
    return
  end
  -- (the post-game's walls, L.F.think: a cracked wall's target is its position, uid 0)
  if plan.wallAt then
    local x, y = (cur ~= 0) and L.s8(emu:read8(cur)) or -1, (cur ~= 0) and L.s8(emu:read8(cur + 1)) or -1
    if (x ~= plan.wallAt.x or y ~= plan.wallAt.y) and plan.tTries < 12 then push(K.RIGHT, 1, 4)
    else log("confirm the wall at (%d,%d)", x, y); push(K.A, 1, 6) end
    return
  end
  if plan.target and uid ~= plan.target and plan.tTries < 12 then
    push(K.RIGHT, 1, 4)
  else
    log("confirm target %02x", uid)
    push(K.A, 1, 6)
  end
end

---------------------------------------------------------------------------
-- Feature routes (L.F): the menus and screens of their actions.

-- ROUTE_SUSPEND: the map menu's Suspend (overrideId 0x73, gMapMenuItems)
function L.F.suspendMenu(ids, count, cur)
  for i = 0, count - 1 do
    if ids[i] == 0x73 then
      if cur == i then L.F.st.suspArmed = nil; L.F.st.suspAt = tick; log("suspend: the map menu's Suspend") end
      menuGo(cur, i)
      return
    end
  end
  log("suspend: no Suspend in the map menu"); L.F.st.suspArmed = nil; L.F.st.suspAt = -1
  push(K.B, 1, 6)
end
-- ROUTE_SKIRMISH: no shopping on the way (a short file); the world map's target is the skirmish node
-- while its monsters are there (GetGmSkirmishUnitAtNode); the walk's end there starts the skirmish
-- (WorldMap_MuWalkLoop: WorldMap_IsMonsterAtNonStoryNode, label 16, src/worldmap_main.c)
function L.F.wmPre()
  if shopPlan == nil then shopPlan = { done = true, why = "skirmish route" } end
end
function L.F.wmTarget(target, ps)
  local st, sk = L.F.st, L.F.sk
  if st.skDone or not skirmishAt(sk.node) then return target end
  if not st.skStory then
    st.skStory = ps.chapter
    local lord = emu:read8(L.GM.data + 0x11)
    log("skirmish: monsters on node %d; the lord walks there from node %d (a way with no other skirmish: %s)",
      sk.node, lord, tostring(gmPathClear(lord, sk.node)))
  end
  return sk.node
end
-- The screens and talks of the feature actions; called in decide before the event engine's rules.
ARENA_MAIN, ARENA_RESULTS = 0x08A394DC, 0x08A3963C   -- gProcScr_ArenaUiMain, gProcScr_ArenaUiResults (src/uiarena.c)
function L.F.decide(procs, ps)
  local F, st = L.F, L.F.st
  -- (coverage 12) ROUTE_SUPPORTS: the conversation with A only (it is not skipped)
  if F.supps and st.supportsAt and st.supportsAt ~= st.suppDone then
    if L.findScript(procs, L.PROC.EventEngine) then st.suppEv = st.suppEv or tick; push(K.A, 1, 5); return "support-talk" end
    if L.findIdle(procs, L.IDLE.TalkWaitForInput) then push(K.A, 1, 5); return "support-talk" end
  end
  -- (coverage 12) ROUTE_LORDPROMO=no: the first two talk choices get [No] (selectedChoice 2)
  if F.lordNo and (st.lordNo or 0) < 2 then
    local tc = L.findIdle(procs, L.IDLE.TalkChoice_OnIdle)
    if tc then
      if L.s16(emu:read16(tc.addr + 0x2A)) == 2 then
        st.lordNo = (st.lordNo or 0) + 1; log("lord promotion: [No] (%d)", st.lordNo); push(K.A, 1, 6)
      else push(K.RIGHT, 1, 6) end
      return "lord-no"
    end
  end
  -- (coverage 12) ROUTE_WATCH: every event gets A from the first free cursor on (a talk choice: its default)
  if F.watch and st.watchOn and L.findScript(procs, L.PROC.EventEngine) then push(K.A, 1, 5); return "event-watch" end
  -- ROUTE_ARENA (r9-uiarena's rule): [Yes] in the arena's talk choices ([No] with ROUTE_ARENA_CHOICE=no);
  -- B held 90 ticks ROUTE_ARENA_YIELD ticks into the fight
  if F.arena then
    local am, ar = L.findScript(procs, ARENA_MAIN), L.findScript(procs, ARENA_RESULTS)
    if am or ar then
      if not st.arenaSeen then st.arenaSeen = tick; log("arena screens open at tick %d", tick) end
      local tc = L.findIdle(procs, L.IDLE.TalkChoice_OnIdle)
      if tc then
        local want = F.arena.no and 2 or 1
        local sel = emu:read8(tc.addr + 0x2A)
        if sel == want then push(K.A, 1, 6) elseif want == 1 then push(K.LEFT, 1, 6) else push(K.RIGHT, 1, 6) end
        return "arena-choice"
      end
    end
    if st.arenaSeen and not st.arenaEnd then
      local cb = emu:read32(L.ADDR.gMainCallback)
      if cb == L.CB.InBattleMainRoutine or cb == L.CB.MainUpdate_8055C68 then
        st.arenaBattle = st.arenaBattle or tick
        if F.arena.yield and tick - st.arenaBattle == F.arena.yield then
          log("arena: yield (B held 90 ticks) at tick %d", tick); push(K.B, 90, 6); return "arena-yield"
        end
        -- (coverage 12, with ROUTE_WATCH: a talk in the battle screen, such as a death quote after a loss, gets A)
        if F.watch and (L.findIdle(procs, L.IDLE.TalkWaitForInput) or L.findScript(procs, L.PROC.BattleEventEngine)) then
          push(K.A, 1, 5); return "arena-talk"
        end
        wait(1); return "arena-battle"
      end
      if not (am or ar) and (st.arenaBattle or F.arena.no) then st.arenaEnd = tick; log("arena screens closed at tick %d", tick) end
    end
  end
  -- ROUTE_SUSPEND: the prompt's [Yes] (CallSuspendPromptEvent, a talk choice in an event)
  if F.susp and st.suspAt and st.suspAt >= 0 and not st.suspTitle then
    local tc = L.findIdle(procs, L.IDLE.TalkChoice_OnIdle)
    if tc then
      if emu:read8(tc.addr + 0x2A) == 1 then push(K.A, 1, 6) else push(K.LEFT, 1, 6) end
      return "suspend-yes"
    end
  end
  -- ROUTE_SUPPORT: the support conversation with A only (it is not skipped)
  if F.supp and st.supportAt and not st.supportEnd then
    if L.findScript(procs, L.PROC.EventEngine) then st.supportEv = st.supportEv or tick; push(K.A, 1, 5); return "support-talk" end
    if L.findIdle(procs, L.IDLE.TalkWaitForInput) then push(K.A, 1, 5); return "support-talk" end
  end
  -- ROUTE_PROMO: the promotion screens (and a trainee's level-10 talk) with A
  if F.promo and L.findScript(procs, PROC_PROMO_MAIN) then
    if not st.promoSeen then st.promoSeen = tick; log("promotion screens at tick %d", tick) end
    -- (coverage 12, ROUTE_PROMO_CHOICE: on the class list, gMenuDef_PromoSel, the n-th class; the top menu,
    -- the one that is no other menu's parent, as v3 takes it)
    if F.promoChoice then
      local menus, parents, top = {}, {}, nil
      for _, p in ipairs(procs) do
        if p.idle == L.IDLE.Menu_OnIdle then menus[#menus + 1] = p; parents[emu:read32(p.addr + 0x14)] = true end
      end
      for _, m in ipairs(menus) do if not parents[m.addr] then top = m; break end end
      if top and emu:read32(top.addr + 0x30) == MENUDEF_PROMOSEL then
        local _, count, cur = menuItems(top)
        local want = math.min(F.promoChoice, count) - 1
        if cur == want then log("promotion: class %d of %d", want + 1, count) end
        menuGo(cur, want)
        return "promotion-choice"
      end
    end
    push(K.A, 1, 30)
    return "promotion-f"
  end
  return nil
end

---------------------------------------------------------------------------
-- The item gallery (coverage 12, ROUTE_GALLERY=1; port/test/inputs/gallery/README.md): test coverage
-- only, boosted and cheat-assisted (ROUTE_CHEAT=durability+funds+convoy). From gal_start (Ephraim's
-- final chapter part 1, edited: gallery/gal_prep.edit) every item of the rolling convoy's list
-- (test_cheat.lua, 177 items) is taken from the supply once and used once: a weapon in a battle with
-- an enemy, a staff on a valid target, a consumable, promotion item, stat booster or key on its user,
-- door or chest. Items with no Use command are taken and carried. Not tried: the monster-locked
-- weapons (IA_LOCK_3, only monster classes and Myrrh's: the user's decision) and Hammerne (with the
-- durability cheat no item is ever worn, so it has no target). Everything here is in L.GAL (the main
-- chunk is at Lua's 200-local limit) and runs only with ROUTE_GALLERY=1, so every other route file
-- comes out as before.
--   - The supply's lord (Ephraim on his route) stays on his tile while anyone still has items to take;
--     the four tiles next to him are the stations. A unit with an untried item it can use somewhere in
--     reach goes and uses it; else it goes to a free station, discards the items it has used (not the
--     unsellable ones, which the game will not drop), takes up to three untried items it can use (the
--     ones the fewest units can use first) and one item with no use, and uses one of them there if it
--     can; else it waits. A unit with nothing to do is not selected.
--   - Weapons: an Attack on an enemy in range (the battle animations are on: the save's option); the
--     enemies carry nothing (gal_prep.edit), so nobody counters. Staves: Heal, Mend, Recover and
--     Physic on a hurt ally, Fortify and Latona last (they heal everyone), Restore on a poisoned ally,
--     Silence, Sleep, Berserk on an enemy, Barrier, Warp and Rescue on an ally, Torch on a tile (the
--     start has fog), Unlock on a door. Keys: the Door Key next to a door, the Chest Keys and the
--     Lockpick (a thief's) on a chest. Every other usable item from the user's own tile.
--   - Each use is logged (tick it was planned, tick of the next free cursor, unit, how, result) on
--     stdout and in the key file's header ("# Item gallery"); ROUTE_STOP=gallery:N stops N ticks after
--     the last item was tried or nothing could be tried for three turns.
L.GAL = { on = os.getenv("ROUTE_GALLERY") == "1", tried = {}, fails = {}, claimed = {}, rows = {}, battle = false,
          busy = 0, doneAt = nil, idleTurns = 0, turnKey = nil, pend = nil, inited = false, usedThisTurn = 0 }
L.GAL.USE = { [0x5B] = "boost", [0x5C] = "boost", [0x5D] = "boost", [0x5E] = "boost", [0x5F] = "boost", [0x60] = "boost",
              [0x61] = "boost", [0x62] = "boost", [0x63] = "boost", [0x64] = "promo", [0x65] = "promo", [0x66] = "promo",
              [0x67] = "promo", [0x68] = "promo", [0x88] = "promo", [0x97] = "promo", [0x98] = "promo", [0x99] = "promo",
              [0x6C] = "heal", [0x6D] = "heal", [0xA2] = "heal", [0x6E] = "purewater", [0x6F] = "antitoxin", [0x70] = "torch",
              [0x89] = "metis", [0xB7] = "juna" }
L.GAL.KEY = { [0x69] = "chest", [0x79] = "chest", [0x6A] = "door", [0x6B] = "lockpick" }
L.GAL.PROMO = { [0x98] = 0x088ADFA4, [0x99] = 0x088ADFA6 }   -- gItemUseJidList_LunarBrace, _SolarBrace (with PROMO_LISTS)
L.GAL.HEALS = { 0x4B, 0x4C, 0x4D, 0x4E, 0x6C, 0x6D, 0xA2 }   -- (Fortify and Latona wait for these)
L.GAL.NAME = { [0x01] = "Eirika", [0x02] = "Seth", [0x04] = "Franz", [0x05] = "Moulder", [0x08] = "Neimi", [0x09] = "Colm",
               [0x0A] = "Garcia", [0x0B] = "Innes", [0x0C] = "Lute", [0x0D] = "Natasha", [0x0E] = "Cormag", [0x0F] = "Ephraim",
               [0x10] = "Forde", [0x11] = "Kyle", [0x12] = "Amelia", [0x13] = "Artur", [0x14] = "Gerik", [0x16] = "Marisa",
               [0x17] = "Saleh", [0x18] = "Ewan", [0x19] = "L'Arachel", [0x1A] = "Dozla", [0x1D] = "Duessel", [0x1E] = "Myrrh",
               [0x1F] = "Knoll", [0x22] = "Tana" }
function L.GAL.uname(u) return string.format("%s (char %02x)", L.GAL.NAME[u.char] or "?", u.char) end
-- the items to try: the cheat's list (ids 0x01-0xCD whose name is not MSG_403, "Dummy"), by kind
function L.GAL.kind(id)
  local d = L.item(id)
  if id == 0x57 then return "skip", "Hammerne: with the durability cheat no item is ever worn, so it has no target" end
  if id == 0x79 then
    return "skip", "Chest Key (5 uses): the map has two doors and two chests, which the Door Key, Unlock, the Chest Key and the Lockpick open; it shares the Chest Key's code (CanUnitUseChestKeyItem, ExecKeyItem)"
  end
  if id >= 0x35 and id <= 0x37 then   -- (scratch try g1, tick 88,364: Innes, a Sniper, attacked with a Ballista from his items)
    return "skip", "ballista item: an Attack with it from a unit's items makes the retail game jump to 0x01000130 (mGBA), so it is left for its own check"
  end
  if (d.attributes & 4) ~= 0 then
    if id == 0xA6 then return "carry" end   -- Nightmare: no use effect (CanUnitUseItem's default)
    return "staff"
  end
  if (d.attributes & 1) ~= 0 then
    if (d.attributes & (1 << 10)) ~= 0 and id ~= 0xAA then
      return "skip", "monster weapon (IA_LOCK_3): only monster classes use it (the user's decision: not tried)"
    end
    return "weapon"
  end
  if L.GAL.USE[id] then return "use" end
  if L.GAL.KEY[id] then return "key" end
  return "carry"
end
function L.GAL.init()
  local G = L.GAL
  if G.inited then return end
  G.inited = true
  G.list, G.skip = {}, {}
  for id = 0x01, 0xCD do
    if emu:read16(L.ADDR.gItemData + id * 0x24) ~= 0x403 then
      local k, why = G.kind(id)
      if k == "skip" then G.skip[id] = why else G.list[#G.list + 1] = id end
    end
  end
  log("gallery: %d items to try, %d not tried by rule", #G.list, (function() local n = 0 for _ in pairs(G.skip) do n = n + 1 end return n end)())
  -- how many units of the army can use each item (claim order: the rarest first)
  G.users = {}
  for _, id in ipairs(G.list) do
    local n = 0
    for _, b in ipairs(L.blue()) do if G.canClaim(b, id, true) then n = n + 1 end end
    G.users[id] = n
  end
end
function L.GAL.classRank(u, t) return emu:read8(u.classPtr + 0x2C + t) end
function L.GAL.canWeapon(u, d)
  if (d.attributes & (1 << 10)) ~= 0 then return (unitAttr(u) & (1 << 18)) ~= 0 end   -- the Divinestone: Myrrh's class
  if d.weaponType > 7 then return false end
  return L.GAL.classRank(u, d.weaponType) > 0 and canUseWeapon(u, d)              -- (the class's own animations)
end
function L.GAL.canStaff(u, d) return L.GAL.classRank(u, 4) > 0 and canUseStaff(u, d) end
function L.GAL.promoOK(u, id)
  local list = L.GAL.PROMO[id] or PROMO_LISTS[id]
  if not list then return false end
  local lordBrace = (id == 0x98 or id == 0x99) and (u.char == L.CHAR.EIRIKA or u.char == L.CHAR.EPHRAIM)
  if not lordBrace and u.level < 10 then return false end
  if id == 0x88 then   -- (the Master Seal: only a class no other promotion item fits, so it takes nobody else's)
    for _, o in ipairs({ 0x64, 0x65, 0x66, 0x67, 0x68, 0x97 }) do
      for i = 0, 63 do
        local c = emu:read8(PROMO_LISTS[o] + i)
        if c == 0 then break end
        if c == u.class then return false end
      end
    end
  end
  for i = 0, 63 do
    local c = emu:read8(list + i)
    if c == 0 then return false end
    if c == u.class then return true end
  end
  return false
end
function L.GAL.useOK(u, id)   -- CanUnitUseItem (src/bmitemuse.c) for the self-use items
  local k, a = L.GAL.USE[id], u.addr
  if k == "heal" then return u.hp < u.maxHp end
  if k == "purewater" then return (emu:read8(a + 0x31) >> 4) < 7 end
  if k == "antitoxin" then return (emu:read8(a + 0x30) & 0x0F) == 1 end
  if k == "torch" then return emu:read8(L.ADDR.gPlaySt + 0x0D) ~= 0 and (emu:read8(a + 0x31) & 0x0F) ~= 4 end
  if k == "metis" then return (u.state & 0x2000) == 0 end
  if k == "juna" then return u.level >= 10 and (emu:read32(u.classPtr + 0x28) & 0x100) ~= 0 end   -- (CA_PROMOTED: not a unit whose promotion item needs level 10)
  if k == "promo" then return L.GAL.promoOK(u, id) end
  if k == "boost" then   -- the stat below its cap (CanUnitUseStatGainItem; ClassData maxima +0x13 on)
    local c = u.classPtr
    if id == 0x5B then return u.maxHp < L.s8(emu:read8(c + 0x13)) end
    if id == 0x5C then return u.pow < L.s8(emu:read8(c + 0x14)) end
    if id == 0x5D then return u.skl < L.s8(emu:read8(c + 0x15)) end
    if id == 0x5E then return u.spd < L.s8(emu:read8(c + 0x16)) end
    if id == 0x5F then return u.lck < 30 end
    if id == 0x60 then return u.def < L.s8(emu:read8(c + 0x17)) end
    if id == 0x61 then return u.res < L.s8(emu:read8(c + 0x18)) end
    if id == 0x62 then return L.s8(emu:read8(a + 0x1D)) == 0 end
    if id == 0x63 then return u.con < L.s8(emu:read8(c + 0x19)) end
  end
  return false
end
-- may unit u take item id now (static: only what its class can ever do, for the users count)
function L.GAL.canClaim(u, id, static)
  local k, d = L.GAL.kind(id), L.item(id)
  if k == "weapon" then return L.GAL.canWeapon(u, d) end
  if k == "staff" then return L.GAL.canStaff(u, d) end
  if k == "use" then
    if static then return L.GAL.USE[id] ~= "promo" or L.GAL.promoOK(u, id) end
    return L.GAL.useOK(u, id)
  end
  if k == "key" then return id ~= 0x6B or (unitAttr(u) & (1 << 3)) ~= 0 end   -- the Lockpick: a thief (CA_THIEF)
  return k == "carry"
end
function L.GAL.lord() return supplyLord() end
function L.GAL.stations()
  local l = L.GAL.lord()
  if not l then return {} end
  return { { x = l.x, y = l.y - 1 }, { x = l.x - 1, y = l.y }, { x = l.x + 1, y = l.y }, { x = l.x, y = l.y + 1 } }
end
function L.GAL.isStation(x, y)
  for _, s in ipairs(L.GAL.stations()) do if s.x == x and s.y == y then return true end end
  return false
end
function L.GAL.remaining()   -- the items not tried yet (and not given up)
  local t = {}
  for _, id in ipairs(L.GAL.list) do
    if not L.GAL.tried[id] and (L.GAL.fails[id] or 0) < 3 then t[#t + 1] = id end
  end
  return t
end
function L.GAL.lateHealOK()
  for _, id in ipairs(L.GAL.HEALS) do
    if not L.GAL.tried[id] and (L.GAL.fails[id] or 0) < 3 then return false end
  end
  return true
end
-- the convoy items unit u could take now, in claim order
function L.GAL.claimable(u, ignoreClaims)
  local G, seen, out = L.GAL, {}, {}
  for _, it in ipairs(u.items) do seen[it & 0xFF] = true end   -- (not a second copy of what it holds)
  for _, id in ipairs(convoyItems()) do
    if not seen[id] and not G.tried[id] and (G.fails[id] or 0) < 3 and G.users[id]
        and (ignoreClaims or not G.claimed[id]) and G.canClaim(u, id) then
      seen[id] = true; out[#out + 1] = id
    end
  end
  local function pri(id) if id == 0x6F or id == 0x50 then return 0 end return G.users[id] end
  table.sort(out, function(a, b)
    if pri(a) ~= pri(b) then return pri(a) < pri(b) end
    return a < b
  end)
  return out
end
function L.GAL.lordFree()   -- nobody else needs the supply any more: the lord may move too
  local lord = L.GAL.lord()
  for _, b in ipairs(L.blue()) do
    if (not lord or b.id ~= lord.id) and #L.GAL.claimable(equip(b), true) > 0 then return false end
  end
  return true
end
-- the action for untried item id of unit u from one of `tiles`: dest, step (or nil)
function L.GAL.action(u, id, tiles)
  local G, d, k = L.GAL, L.item(id), L.GAL.kind(id)
  local blue, red = L.blue(), L.red()
  local function pickTile(f)   -- the first tile with f(t) ~= nil: the unit's own, then off the stations
    local best, bestS, bestV
    for _, t in ipairs(tiles) do
      local v = f(t)
      if v then
        local s = G.isStation(t.x, t.y) and 2 or ((t.x == u.x and t.y == u.y) and 0 or 1)
        if best == nil or s < bestS then best, bestS, bestV = t, s, v end
      end
    end
    return best, bestV
  end
  local function ally(f, t, lo, hi)
    for _, b in ipairs(blue) do
      local dd = dist(t.x, t.y, b.x, b.y)
      if b.id ~= u.id and dd >= lo and dd <= hi and f(b) then return b end
    end
  end
  local function leastHurt(t, lo, hi)   -- (a single heal on the ally it leaves least short: the deep wounds stay for Fortify, Latona)
    local best
    for _, b in ipairs(blue) do
      local dd = dist(t.x, t.y, b.x, b.y)
      if b.id ~= u.id and dd >= lo and dd <= hi and b.hp < b.maxHp and (best == nil or b.maxHp - b.hp < best.maxHp - best.hp) then best = b end
    end
    return best
  end
  local function hurt(b) return b.hp < b.maxHp end
  local r = math.max(5, u.pow // 2)   -- GetUnitMagBy2Range (src/bmunit.c)
  if k == "weapon" then
    if not G.canWeapon(u, d) then return nil end
    local t, e = pickTile(function(t)
      local best
      for _, e in ipairs(red) do
        local dd = dist(t.x, t.y, e.x, e.y)
        if dd >= d.minRange and dd <= d.maxRange and (best == nil or dd < best.dd or (dd == best.dd and e.hp > best.e.hp)) then best = { e = e, dd = dd } end
      end
      return best and best.e
    end)
    if t then return t, { k = "attack", id = id, target = e.id, tpos = { x = e.x, y = e.y }, how = string.format("attacks %s %02x (class %02x) at (%d,%d)", "the enemy", e.id, e.class, e.x, e.y) } end
    return nil
  end
  if k == "staff" then
    if not G.canStaff(u, d) then return nil end
    local function unitStep(b, what) return { k = "staff", id = id, target = b.id, tpos = { x = b.x, y = b.y }, how = string.format("%s %02x at (%d,%d)", what, b.id, b.x, b.y) } end
    if id == 0x4B or id == 0x4C or id == 0x4D then
      local t, b = pickTile(function(t) return leastHurt(t, 1, 1) end)
      if t then return t, unitStep(b, "heals the ally") end
    elseif id == 0x4E then
      local t, b = pickTile(function(t) return leastHurt(t, 1, r) end)
      if t then return t, unitStep(b, "heals the ally") end
    elseif id == 0x4F or id == 0x8C then
      if not G.lateHealOK() then return nil end
      if id == 0x8C and not G.tried[0x4F] and (G.fails[0x4F] or 0) < 3 then return nil end   -- (Latona after Fortify)
      local t = pickTile(function(t) return ally(hurt, t, 0, id == 0x8C and 99 or r) end)
      if t then return t, { k = "staff", id = id, how = id == 0x8C and "heals every ally (Latona)" or "heals the allies in range (Fortify)" } end
    elseif id == 0x50 then
      local t, b = pickTile(function(t) return ally(function(b) return (emu:read8(b.addr + 0x30) & 0x0F) ~= 0 end, t, 1, 1) end)
      if t then return t, unitStep(b, "cures the status of the ally") end
    elseif id == 0x51 or id == 0x52 or id == 0x53 then
      local t, e = pickTile(function(t)
        for _, e in ipairs(red) do
          local st, dd = emu:read8(e.addr + 0x30) & 0x0F, dist(t.x, t.y, e.x, e.y)
          if dd >= 1 and dd <= r and (st == 0 or (id == 0x51 and st == 3)) then return e end
        end
      end)
      if t then return t, { k = "staff", id = id, target = e.id, tpos = { x = e.x, y = e.y }, how = string.format("on the enemy %02x at (%d,%d)", e.id, e.x, e.y) } end
    elseif id == 0x54 or id == 0x59 then
      local lord = G.lord()
      local t, b = pickTile(function(t) return ally(function(b) return (not lord or b.id ~= lord.id)
        and (id ~= 0x59 or (emu:read8(b.addr + 0x31) >> 4) < 7) end, t, 1, 1) end)
      if t then return t, unitStep(b, id == 0x54 and "warps the ally" or "puts a barrier on the ally") end
    elseif id == 0x55 then
      local lord = G.lord()
      local t, b = pickTile(function(t)
        if t.y < 15 then return nil end
        return ally(function(b) return not lord or b.id ~= lord.id end, t, 2, r)
      end)
      if t then return t, unitStep(b, "brings next to it the ally") end
    elseif id == 0x56 then
      if emu:read8(L.ADDR.gPlaySt + 0x0D) == 0 then return nil end
      return pickTile(function() return true end), { k = "staff", id = id, torch = true, how = "lights a tile (fog)" }
    elseif id == 0x58 then
      local t, p = pickTile(function(t)
        for _, p in ipairs(G.doors()) do
          local dd = dist(t.x, t.y, p.x, p.y)
          if dd >= 1 and dd <= 2 then return p end
        end
      end)
      if t then return t, { k = "staff", id = id, pos = p, how = string.format("opens the door at (%d,%d)", p.x, p.y) } end
    end
    return nil
  end
  if k == "use" then
    if not G.useOK(u, id) then return nil end
    return pickTile(function() return true end), { k = "use", id = id, how = "uses it (" .. G.USE[id] .. ")" }
  end
  if k == "key" then
    local kk = G.KEY[id]
    if kk == "door" then
      local t, p = pickTile(function(t)
        for _, p in ipairs(G.doors()) do if dist(t.x, t.y, p.x, p.y) == 1 then return p end end
      end)
      if t then return t, { k = "use", id = id, pos = p, how = string.format("opens the door at (%d,%d)", p.x, p.y) } end
    elseif kk == "chest" or (kk == "lockpick" and (unitAttr(u) & (1 << 3)) ~= 0) then
      local t = pickTile(function(t) return L.mapGet(L.ADDR.gBmMapTerrain, t.x, t.y) == 0x21 or nil end)
      if t then return t, { k = "use", id = id, how = string.format("opens the chest at (%d,%d)", t.x, t.y) } end
    end
    return nil
  end
  return nil
end
function L.GAL.doors()
  local w, h = L.mapSize()
  local t = {}
  for y = 13, h - 1 do for x = 0, w - 1 do
    if L.mapGet(L.ADDR.gBmMapTerrain, x, y) == TERRAIN_DOOR then t[#t + 1] = { x = x, y = y } end
  end end
  return t
end
-- unit u's job this turn: { dest, steps, use = step, claims } or nil (commit: claim the items)
function L.GAL.job(u, commit)
  local G = L.GAL
  local lord = G.lord()
  local isLord = lord and u.id == lord.id
  local free = isLord and G.lordFree()
  local tiles = {}
  if isLord and not free then tiles = { { x = u.x, y = u.y } }
  else for _, t in ipairs(moveTiles(u)) do if t.y >= 14 then tiles[#tiles + 1] = t end end end
  -- 1. an untried item it holds, used somewhere in reach (off the stations if it can, so that the units
  -- with nothing left take their place); the lord takes from the supply first (2)
  local function act()
    for _, it in ipairs(u.items) do
      local id = it & 0xFF
      if not G.tried[id] and (G.fails[id] or 0) < 3 and G.kind(id) ~= "carry" and G.kind(id) ~= "skip" then
        local t, st = G.action(u, id, tiles)
        if t then st.final = true; return { dest = t, steps = { st }, use = st, why = "act" } end
      end
    end
  end
  local first = not (isLord and not free) and act()
  if first then return first end
  -- 2. the supply: a free station in reach (the lord: his own tile)
  local claim = G.claimable(u)
  -- (the lord, who stays on his tile while the others need the supply, takes only what he can use there)
  if isLord and not free then
    local c2 = {}
    for _, id in ipairs(claim) do
      local k = G.kind(id)
      if k == "carry" or k == "use" or G.action(u, id, { { x = u.x, y = u.y } }) then c2[#c2 + 1] = id end
    end
    claim = c2
  end
  -- ONE item a visit: the supply screen's take rewrites the convoy from the screen's own list
  -- (PrepItemSupply_TakeItemFromSupply, sub_80982B8: ClearSupplyItems, then the list back), so an item the
  -- rolling convoy cheat puts in while the screen is open is lost at the next take (k takes lose k(k-1)/2)
  local take = {}
  for _, id in ipairs(claim) do if G.kind(id) ~= "carry" and #take < 1 then take[#take + 1] = id end end
  if #take == 0 then for _, id in ipairs(claim) do if #take < 1 then take[#take + 1] = id end end end
  local discard = {}
  for _, it in ipairs(u.items) do
    local id = it & 0xFF
    if G.tried[id] and (L.item(id).attributes & 0x10) == 0 then discard[#discard + 1] = id end   -- (not IA_UNSELLABLE: "Treasure can't be dropped")
  end
  local room = 5 - #u.items + #discard
  while #take > room do table.remove(take) end
  local turn = L.playSt().turn
  G.supplied = G.supplied or {}
  if not isLord and G.supplied[u.id] == turn - 1 then take = {} end   -- (the stations go round: not two turns running)
  if #take > 0 then
    local sdest
    if isLord then sdest = { x = u.x, y = u.y }
    else
      for _, s in ipairs(G.stations()) do
        local occ = L.mapGet(L.ADDR.gBmMapUnit, s.x, s.y)
        for _, t in ipairs(tiles) do
          if t.x == s.x and t.y == s.y and (occ == 0 or occ == u.id) then
            if sdest == nil or occ == u.id then sdest = s end
          end
        end
      end
    end
    if sdest then
      local steps = {}
      for _, id in ipairs(discard) do steps[#steps + 1] = { k = "discard", id = id } end
      local base = {}
      for _, it in ipairs(u.items) do base[it & 0xFF] = (base[it & 0xFF] or 0) + 1 end
      for _, id in ipairs(discard) do base[id] = base[id] - 1 end
      for _, id in ipairs(take) do base[id] = base[id] or 0 end   -- (handleSupply counts the takes from these)
      steps[#steps + 1] = { k = "supply", sp = { take = take, give = {}, base = base }, take = take }
      -- then a use from the station: an untried item it holds or takes
      local use
      local cand = {}
      for _, it in ipairs(u.items) do cand[#cand + 1] = it & 0xFF end
      for _, id in ipairs(take) do cand[#cand + 1] = id end
      for _, id in ipairs(cand) do
        if not use and not G.tried[id] and (G.fails[id] or 0) < 3 and G.kind(id) ~= "carry" then
          local t, st = G.action(u, id, { sdest })
          if t then use = st end
        end
      end
      if use then use.final = true; steps[#steps + 1] = use else steps[#steps + 1] = { k = "wait", final = true } end
      if commit then
        for _, id in ipairs(take) do G.claimed[id] = u.id end
        G.supplied[u.id] = turn
      end
      return { dest = sdest, steps = steps, use = use, take = take, why = "supply" }
    end
  end
  local late = act()
  if late then return late end
  local walk = G.walk(u, tiles)
  if walk then return walk end
  if #take > 0 and not isLord then   -- (items to take, no station in reach this turn: toward the stations)
    local goals = {}
    for _, st in ipairs(G.stations()) do
      local occ = L.mapGet(L.ADDR.gBmMapUnit, st.x, st.y)
      if occ < 0x80 then goals[#goals + 1] = st end
    end
    if #goals > 0 then
      local D = distanceField(u, goals, L.red())
      local here = D[u.y] and D[u.y][u.x]
      local best
      for _, t in ipairs(tiles) do
        local d = D[t.y] and D[t.y][t.x]
        if d and (here == nil or d < here) and not G.isStation(t.x, t.y) and (best == nil or d < best.d) then best = { t = t, d = d } end
      end
      if best then return { dest = best.t, steps = { { k = "wait", final = true } }, why = "walk" } end
    end
  end
  -- 3. a unit on a station with nothing to do there leaves it free
  if not isLord and G.isStation(u.x, u.y) then
    for _, t in ipairs(tiles) do
      if not G.isStation(t.x, t.y) and (t.x ~= u.x or t.y ~= u.y) then
        return { dest = t, steps = { { k = "wait", final = true } }, why = "leave" }
      end
    end
  end
  return nil
end
-- a unit holding an untried item it cannot use from any tile in reach walks toward the nearest tile it can:
-- the tiles of the map where the use is possible (G.action on each), a distance field from them (the unit's
-- class move costs, enemies as walls), and the tile in reach nearest to them (cached for the turn)
function L.GAL.walk(u, tiles)
  local G = L.GAL
  local ps = L.playSt()
  G.walkCache = G.walkCache or {}
  local key = ps.turn .. ":" .. u.id .. ":" .. u.x .. ":" .. u.y
  if G.walkCache[key] ~= nil then return G.walkCache[key] or nil end
  local w, h = L.mapSize()
  local ct = emu:read32(u.classPtr + 0x38)
  local best
  for _, it in ipairs(u.items) do
    local id = it & 0xFF
    if not G.tried[id] and (G.fails[id] or 0) < 3 and G.kind(id) ~= "carry" and G.kind(id) ~= "skip" and G.kind(id) ~= "use" then
      local goals = {}
      for y = 14, h - 1 do for x = 0, w - 1 do
        local occ = L.mapGet(L.ADDR.gBmMapUnit, x, y)
        if (occ == 0 or occ == u.id) and L.s8(emu:read8(ct + L.mapGet(L.ADDR.gBmMapTerrain, x, y))) > 0
            and G.action(u, id, { { x = x, y = y } }) then goals[#goals + 1] = { x = x, y = y } end
      end end
      if #goals > 0 then
        local D = distanceField(u, goals, L.red())
        local here = D[u.y] and D[u.y][u.x]
        for _, t in ipairs(tiles) do
          local d = D[t.y] and D[t.y][t.x]
          if d and (here == nil or d < here) and (best == nil or d < best.d) then best = { t = t, d = d, id = id } end
        end
      end
    end
  end
  local j = best and { dest = best.t, steps = { { k = "wait", final = true } }, why = "walk",
    walkFor = best.id } or false
  G.walkCache[key] = j
  return j or nil
end
-- handleMainIdle: the units with a job, in this order: the lord, the units on stations that leave them (to act
-- elsewhere or to make room), the supply visits (those with the Antitoxin or Restore first: the poison wears
-- off), then the rest (act, walk)
function L.GAL.order(order)
  local G = L.GAL
  G.init()
  local lord = G.lord()
  local rank, keep = {}, {}
  for i, u in ipairs(order) do
    if (u.state & (L.US.UNSELECTABLE | L.US.HIDDEN)) == 0 then
      local j = G.job(equip(u), false)
      if j then
        local r = 5
        local urgent = false
        for _, id in ipairs(j.take or {}) do if id == 0x6F or id == 0x50 then urgent = true end end
        if j.use and (j.use.id == 0x6F or j.use.id == 0x50) then urgent = true end
        if j.walkFor == 0x6F or j.walkFor == 0x50 then urgent = true end
        if lord and u.id == lord.id then r = 1
        elseif G.isStation(u.x, u.y) and j.why ~= "supply" then r = 2
        elseif j.why == "supply" and urgent then r = 3
        elseif j.why == "supply" then r = 4
        elseif urgent then r = 4.5
        else r = 5 end
        rank[u.id] = r * 100 + i
        keep[#keep + 1] = u
      end
    end
  end
  table.sort(keep, function(a, b) return rank[a.id] < rank[b.id] end)
  return keep
end
function L.GAL.snap(u)
  return { hp = u.hp, maxHp = u.maxHp, pow = u.pow, skl = u.skl, spd = u.spd, def = u.def, res = u.res, lck = u.lck,
           level = u.level, class = u.class, st = emu:read8(u.addr + 0x30), sb = emu:read8(u.addr + 0x31),
           x = u.x, y = u.y, mov = L.s8(emu:read8(u.addr + 0x1D)), con = L.s8(emu:read8(u.addr + 0x1A)), state = u.state,
           items = (function() local t = {} for _, it in ipairs(u.items) do t[#t + 1] = it & 0xFF end return t end)() }
end
function L.GAL.unitAt(id)
  local base = id < 0x40 and L.ADDR.gUnitArrayBlue or (id >= 0x80 and L.ADDR.gUnitArrayRed or L.ADDR.gUnitArrayGreen)
  return L.readUnit(base + ((id & 0x3F) - 1) * 0x48)
end
-- think (handleRange): the selected unit's plan
function L.GAL.think(u)
  local G = L.GAL
  G.init()
  equip(u)
  if (u.state & L.US.HAS_MOVED) ~= 0 then   -- (canto after the action: stay)
    return { dest = { x = u.x, y = u.y }, action = "gal", gal = { steps = { { k = "wait", final = true } }, i = 1 } }
  end
  local j = G.job(u, true)
  if not j then return { dest = { x = u.x, y = u.y }, action = "gal", gal = { steps = { { k = "wait", final = true } }, i = 1 } } end
  local g = { steps = j.steps, i = 1, take = j.take }
  G.pend = { uid = u.id, char = u.char, name = G.uname(u), tick0 = tick, before = G.snap(u), step = j.use, take = j.take, g = g }
  G.battle = false
  if j.use and j.use.target then local t = G.unitAt(j.use.target); G.pend.tbefore = t and G.snap(t) end
  if j.use and j.use.pos then G.pend.terrain0 = L.mapGet(L.ADDR.gBmMapTerrain, j.use.pos.x, j.use.pos.y) end
  log("gallery: %s %s to (%d,%d): %s%s", G.uname(u), j.why, j.dest.x, j.dest.y,
    j.take and ("takes " .. table.concat((function() local t = {} for _, id in ipairs(j.take) do t[#t + 1] = string.format("%02x", id) end return t end)(), " ") .. "; ") or "",
    j.use and string.format("item %02x %s %s", j.use.id, j.use.k, j.use.how or "") or "then waits")
  return { dest = j.dest, action = "gal", gal = g }
end
function L.GAL.mark(id, row)
  local G = L.GAL
  if G.tried[id] then return end
  G.tried[id] = row
  G.rows[#G.rows + 1] = row
  G.usedThisTurn = G.usedThisTurn + 1
  print(string.format("[%d] gallery: item %02x tried: %s", tick, id, row.text)); io.stdout:flush()
end
-- at the next free cursor: what the planned use did
function L.GAL.finalize()
  local G = L.GAL
  local p = G.pend
  if not p then return end
  G.pend = nil
  local u = G.unitAt(p.uid)
  if not u then return end
  local a = G.snap(u)
  -- what it took (new items)
  local before = {}
  for _, id in ipairs(p.before.items) do before[id] = (before[id] or 0) + 1 end
  for _, id in ipairs(a.items) do
    if (before[id] or 0) > 0 then before[id] = before[id] - 1
    else
      if G.claimed[id] == p.uid then G.claimed[id] = nil end
      if G.kind(id) == "carry" then
        G.mark(id, { id = id, kind = "carry", unit = p.name, t0 = p.tick0, t1 = tick,
          text = string.format("ticks %d-%d: %s took it from the supply and carries it (no Use command: kept, sold or passive)", p.tick0, tick, p.name) })
      end
    end
  end
  for id, uid in pairs(G.claimed) do if uid == p.uid then G.claimed[id] = nil end end   -- (a take that did not happen)
  local st = p.step
  if not st or not st.id then return end
  local fired = p.g.fired
  local res = {}
  if G.battle then res[#res + 1] = "a battle (its animation)" end
  local t = st.target and G.unitAt(st.target)
  if t and p.tbefore then
    local b = p.tbefore
    if b.hp ~= t.hp then res[#res + 1] = string.format("target HP %d -> %d", b.hp, t.hp) end
    if b.st ~= emu:read8(t.addr + 0x30) then res[#res + 1] = string.format("target status %02x -> %02x", b.st, emu:read8(t.addr + 0x30)) end
    if b.sb ~= emu:read8(t.addr + 0x31) then res[#res + 1] = string.format("target torch/barrier %02x -> %02x", b.sb, emu:read8(t.addr + 0x31)) end
    if b.x ~= t.x or b.y ~= t.y then res[#res + 1] = string.format("target moved (%d,%d) -> (%d,%d)", b.x, b.y, t.x, t.y) end
    if b.hp == t.hp and G.battle and st.k == "attack" then res[#res + 1] = "no damage" end
  elseif st.target and not t then res[#res + 1] = "target gone (killed)"
  end
  local b = p.before
  for _, f in ipairs({ "hp", "maxHp", "pow", "skl", "spd", "def", "res", "lck", "level", "class", "st", "sb", "mov", "con" }) do
    if b[f] ~= a[f] then res[#res + 1] = string.format("%s %s -> %s", f, tostring(b[f]), tostring(a[f])) end
  end
  if p.terrain0 and st.pos then
    local t1 = L.mapGet(L.ADDR.gBmMapTerrain, st.pos.x, st.pos.y)
    if t1 ~= p.terrain0 then res[#res + 1] = string.format("terrain at (%d,%d) %02x -> %02x", st.pos.x, st.pos.y, p.terrain0, t1) end
  end
  local n0, n1 = 0, 0
  for _, id in ipairs(b.items) do if id == st.id then n0 = n0 + 1 end end
  for _, id in ipairs(a.items) do if id == st.id then n1 = n1 + 1 end end
  if n1 < n0 then res[#res + 1] = "used up" end
  local acted = (u.state & L.US.UNSELECTABLE) ~= 0
  local ok = fired and acted and (st.k ~= "attack" or G.battle)
  if ok then
    G.mark(st.id, { id = st.id, kind = G.kind(st.id), unit = p.name, t0 = p.tick0, t1 = tick,
      text = string.format("ticks %d-%d: %s %s: %s", p.tick0, tick, p.name, st.how or st.k, #res > 0 and table.concat(res, ", ") or "done") })
  else
    G.fails[st.id] = (G.fails[st.id] or 0) + 1
    print(string.format("[%d] gallery: item %02x NOT used by %s (try %d: fired %s, acted %s, battle %s)", tick, st.id, p.name,
      G.fails[st.id], tostring(fired), tostring(acted), tostring(G.battle))); io.stdout:flush()
  end
end
-- handleMainIdle, first: the last plan's outcome, the turn's bookkeeping, the end of the gallery
function L.GAL.idle()
  local G = L.GAL
  G.init()
  G.finalize()
  local ps = L.playSt()
  local key = ps.chapter .. ":" .. ps.turn
  if key ~= G.turnKey then
    if G.turnKey then
      if G.usedThisTurn == 0 then G.idleTurns = G.idleTurns + 1 else G.idleTurns = 0 end
    end
    G.turnKey = key; G.usedThisTurn = 0
    G.claimed = {}
    local left = G.remaining()
    print(string.format("[%d] gallery: turn %d: %d of %d items tried, %d left", tick, ps.turn,
      (function() local n = 0 for _ in pairs(G.tried) do n = n + 1 end return n end)(), #G.list, #left)); io.stdout:flush()
    if not G.doneAt and (#left == 0 or G.idleTurns >= 3) then
      G.doneAt = tick
      print(string.format("[%d] gallery: done (%s)", tick, #left == 0 and "every item tried" or "nothing tried for three turns")); io.stdout:flush()
    end
  end
end
-- handleMenu, first (plan.gal set): the gallery's steps
function L.GAL.menu(menu, def, ids, count, cur)
  local G, g = L.GAL, plan.gal
  local st = g.steps[g.i]
  if def ~= L.MENUDEF.UnitAction then while st and st.done do g.i = g.i + 1; st = g.steps[g.i] end end
  local function giveUp(why)   -- the step cannot be done: the unit waits instead
    log("gallery: %s (%s item %s): giving up the step", why, st.k, tostring(st.id))
    st.done = true
    if st.final then g.steps[#g.steps + 1] = { k = "wait", final = true } end
    push(K.B, 1, 6)
  end
  local function idx(id) for i = 0, count - 1 do if ids[i] == id then return i end end end
  local function go(id, enabled)   -- (enabled: only an entry the game shows as available, MenuItemProc +0x3D == 1)
    local i = idx(id)
    if not i then return false end
    if enabled and emu:read8(emu:read32(menu.addr + 0x34 + 4 * i) + 0x3D) ~= 1 then return false end
    if cur == i then g.pressed = id end
    menuGo(cur, i)
    return true
  end
  g.menuN = (g.menuN or 0) + 1
  if g.menuN > 400 then   -- (a menu loop the steps did not foresee: back out, and the unit waits)
    log("gallery: 400 menu decisions for one plan, the unit waits")
    g.steps = { { k = "wait", final = true } }; g.i = 1; g.menuN = 0
    push(K.B, 1, 10)
    return true
  end
  local function slotOf(id)
    local au = activeUnit()
    for k, it in ipairs(au and au.items or {}) do if (it & 0xFF) == id then return k - 1 end end
  end
  if def == L.MENUDEF.UnitAction then
    if st and st.k == "supply" and plan.supplyDone then st.done = true end
    while st and st.done do g.i = g.i + 1; st = g.steps[g.i] end
    if not st then if not go(L.MENUITEM.Wait) then push(K.B, 1, 6) end; return true end
    local want = ({ discard = L.MENUITEM.Item, use = L.MENUITEM.Item, supply = MENUITEM_SUPPLY, attack = L.MENUITEM.Attack,
                    staff = L.MENUITEM.Staff, wait = L.MENUITEM.Wait })[st.k]
    if st.k == "supply" then plan.supply = st.sp; plan.action = "supply" else plan.action = "gal" end
    if not go(want) then
      log("gallery: the unit menu has no %02x (%s item %s)", want, st.k, tostring(st.id))
      st.done = true
      if st.final then g.steps[#g.steps + 1] = { k = "wait", final = true } end
      wait(1)
    end
    return true
  end
  if not st then return false end
  if def == L.MENUDEF.ItemSelect then
    local s = (st.k == "discard" or st.k == "use") and slotOf(st.id)
    if not s or not go(L.MENUITEM.ItemSelect0 + s) then
      if st.k == "discard" then st.done = true end
      push(K.B, 1, 6)
    end
    return true
  end
  if def == L.MENUDEF.ItemSub then
    local want = st.k == "discard" and 0x37 or (st.k == "use" and L.MENUITEM.ItemUse) or nil
    if not want or not go(want, true) then
      giveUp("the item menu has no available " .. tostring(want))
    elseif st.k == "use" and cur == idx(want) then g.fired = true
    end
    return true
  end
  if def == L.MENUDEF.YesNo and st.k == "discard" then
    if cur == idx(0x32) then st.done = true; log("gallery: discards item %02x", st.id) end
    if not go(0x32) then push(K.B, 1, 6) end
    return true
  end
  if def == L.MENUDEF.WeaponSelect and st.k == "attack" then
    local s = slotOf(st.id)
    if not s or not go(L.MENUITEM.WeaponSelect0 + s) then giveUp("no such weapon in the list") end
    return true
  end
  if def == L.MENUDEF.StaffItemSelect and st.k == "staff" then
    local s = slotOf(st.id)
    if not s or not go(L.MENUITEM.StaffSelect0 + s) then giveUp("no such staff in the list")
    elseif cur == idx(L.MENUITEM.StaffSelect0 + s) and (st.id == 0x4F or st.id == 0x8C) then g.fired = true end
    return true
  end
  if def == MENUDEF_PROMOSEL or def == MENUDEF_PROMOCONFIRM then menuGo(cur, 0); return true end
  return false
end
-- handleTarget, first: the step's target (a unit by its id, or a position)
function L.GAL.target(ts)
  local g = plan.gal
  local st = g.steps[g.i]
  if not st then return false end
  local cur = emu:read32(ts.addr + 0x30)
  local uid = (cur ~= 0) and emu:read8(cur + 2) or 0
  local x, y = (cur ~= 0) and L.s8(emu:read8(cur)) or -1, (cur ~= 0) and L.s8(emu:read8(cur + 1)) or -1
  g.tTries = (g.tTries or 0) + 1
  local match = true
  if st.target then match = uid == st.target elseif st.pos then match = x == st.pos.x and y == st.pos.y end
  if not match and g.tTries < 20 then push(K.RIGHT, 1, 4); return true end
  log("gallery: confirm target %02x at (%d,%d)%s", uid, x, y, match and "" or " (not the planned one)")
  g.fired = true
  push(K.A, 1, 6)
  return true
end
-- decide, in the player phase first: the Warp and Torch tile selections, the promotion screens
SQ = { warp = 0x0859B9B8, warpIdle = 0x0802969D, torch = 0x0859BA38, torchIdle = 0x08029DF1 }
function L.GAL.decide(procs)
  local G = L.GAL
  local function square(p, mapVar, ok)
    plan = plan or {}
    if not plan.sq then
      local w, h = L.mapSize()
      local au = activeUnit()
      local ox, oy = au and au.x or 0, au and au.y or 0
      local best
      for y = 14, h - 1 do for x = 0, w - 1 do
        if ok(L.mapGet(mapVar, x, y)) and L.mapGet(L.ADDR.gBmMapUnit, x, y) == 0 and not G.isStation(x, y) then
          local d = dist(x, y, ox, oy)
          if d > 0 and (best == nil or d < best.d) then best = { x = x, y = y, d = d } end
        end
      end end
      plan.sq = best or { x = ox, y = oy }
      log("gallery: tile selection, the cursor to (%d,%d)", plan.sq.x, plan.sq.y)
    end
    if cursorTo(plan.sq.x, plan.sq.y) then
      if plan.gal then plan.gal.fired = true end
      push(K.A, 1, 6)
    end
  end
  local p = L.findScript(procs, SQ.warp)
  if p and p.idle == SQ.warpIdle then square(p, L.ADDR.gBmMapMovement, function(v) return v ~= 0xFF end); return "gal-warp" end
  p = L.findScript(procs, SQ.torch)
  if p and p.idle == SQ.torchIdle then square(p, 0x0202E4E4, function(v) return v ~= 0 end); return "gal-torch" end
  if L.findScript(procs, PROC_PROMO_MAIN) and not L.findIdle(procs, L.IDLE.Menu_OnIdle) then push(K.A, 1, 30); return "gal-promo" end
  return nil
end
-- decide's "pp-busy" (an animation or a popup that waits for a key): A after 240 ticks of it
function L.GAL.busyTick()
  local G = L.GAL
  local cb = emu:read32(L.ADDR.gMainCallback)
  if cb == L.CB.InBattleMainRoutine or cb == L.CB.MainUpdate_8055C68 then G.busy = 0; wait(1); return "pp-busy" end
  if G.lastBusy ~= tick - 1 then G.busy = 0 end
  G.lastBusy = tick
  G.busy = G.busy + 1
  if G.busy >= 240 then G.busy = 0; log("gallery: busy for 240 ticks, A"); push(K.A, 1, 6); return "gal-nudge" end
  wait(1)
  return "pp-busy"
end
-- each tick sample: a battle animation seen (the pending use's result)
function L.GAL.tick()
  local cb = emu:read32(L.ADDR.gMainCallback)
  if cb == L.CB.InBattleMainRoutine or cb == L.CB.MainUpdate_8055C68 then L.GAL.battle = true end
end
-- the key file's header: every item, what happened to it
function L.GAL.header()
  local G = L.GAL
  if not G.on or not G.inited then return "" end
  local out = { "#\n# Item gallery (ROUTE_GALLERY=1; port/test/inputs/gallery/README.md): every item of the rolling convoy's list,\n"
    .. "# taken from the supply and used once; ticks are the plan's and the next free cursor's.\n" }
  for _, id in ipairs(G.list) do
    local r = G.tried[id]
    out[#out + 1] = string.format("#   gallery item %02x (%s): %s\n", id, G.kind(id), r and r.text or
      string.format("NOT TRIED (%s)", (G.fails[id] or 0) > 0 and string.format("%d tries failed", G.fails[id]) or "never in reach of a use"))
  end
  local sk = {}
  for id in pairs(G.skip) do sk[#sk + 1] = id end
  table.sort(sk)
  for _, id in ipairs(sk) do out[#out + 1] = string.format("#   gallery item %02x: not tried by rule: %s\n", id, G.skip[id]) end
  return table.concat(out)
end

-- Coverage 12 (port-c12-scenes-e; README-routes.md "Watching the scenes"): ROUTE_WATCH=fast|slow
-- watches every story scene instead of skipping it: never Start (or B, which skips a talk too,
-- TalkSkipListener_OnIdle), the opening slides and a chapter's title left to run, and A at a
-- human's pace in every event, battle quote, talk and the ending. "fast": A every 4 to 12 ticks,
-- mostly one tick, sometimes held 2 to 4 or 15 to 45 ticks (a press while a talk prints takes
-- its instant-print path; A held runs an event's stalls 4 times as fast, Event0E_STAL, and the
-- ending's records twice as fast); "slow": never while a talk or CG text prints, A 20 to 80
-- ticks after it waits for A or a choice opens, and with no talk on screen (moves, maps, the
-- ending's screens) A every 90 to 180 ticks. The pace comes from a hash of the tick and
-- ROUTE_WATCH_SEED (default 1), not the game's RNG, so a file is made again byte for byte.
-- Unset (every older file), nothing here runs.
L.W = { mode = os.getenv("ROUTE_WATCH") or "", seed = tonumber(os.getenv("ROUTE_WATCH_SEED") or "1") }
if L.W.mode == "1" then L.W.mode = "" end   -- (ROUTE_WATCH=1 is the features' rule, L.F.watch; coverage 12's merge)
L.W.on = L.W.mode ~= ""
-- ROUTE_WATCH_FROM=T (coverage 12, scenes-i): the bot as without ROUTE_WATCH (scenes skipped) until
-- tick T, then watching. Its first T ticks are those of the file made without ROUTE_WATCH (the bot is
-- deterministic), so a chapter whose fully watched play goes another way (watching consumes RNs, and
-- Ephraim's chapters have no turn search) can still be watched from its first free cursor or its ending.
L.W.from = tonumber(os.getenv("ROUTE_WATCH_FROM") or "0")
assert(L.W.mode == "" or L.W.mode == "fast" or L.W.mode == "slow", "ROUTE_WATCH must be unset, fast or slow")
function L.W.rand(n)
  local x = (tick * 0x9E3779B1 + L.W.seed * 0x7F4A7C15 + 0x2545F491) & 0xFFFFFFFF
  x = ((x ~ (x >> 16)) * 0x045D9F3B) & 0xFFFFFFFF
  x = ((x ~ (x >> 16)) * 0x045D9F3B) & 0xFFFFFFFF
  return (x ~ (x >> 16)) % n
end
function L.W.reset() L.W.wAddr, L.W.wAt, L.W.wDelay, L.W.nAt, L.W.nDelay, L.W.pAt = nil, nil, nil, nil, nil, nil end
-- ROUTE_PREFIX=FILE [ROUTE_PREFIX_TICKS=N] (coverage 12): the first N ticks (default all) play FILE's keys
-- open-loop, as MONKEY_PREFIX does, and the bot takes over from tick N; the key file written holds both.
-- For the end of a chapter whose watched play the bot cannot win: an older route's keys to the chapter's
-- last turn, then its ending watched (inputs/watch/README.md). Unset, nothing changes.
L.W.prefixPath = os.getenv("ROUTE_PREFIX") or ""
if L.W.prefixPath:match(":%d+$") then L.W.prefixPath = "" end   -- (FILE:N is the features' rule, L.F.prefix; the merge)
L.W.prefix = L.W.prefixPath ~= "" and L.readKeys(L.W.prefixPath) or {}
L.W.prefixTicks = tonumber(os.getenv("ROUTE_PREFIX_TICKS") or "") or #L.W.prefix
assert(L.W.prefixTicks <= #L.W.prefix, "ROUTE_PREFIX_TICKS is longer than the prefix key file")
function L.W.header()
  return (L.W.on and string.format(", ROUTE_WATCH=%s, ROUTE_WATCH_SEED=%d%s", L.W.mode, L.W.seed,
      L.W.from > 0 and string.format(", ROUTE_WATCH_FROM=%d", L.W.from) or "") or "")
    .. (L.W.prefixTicks > 0 and string.format(", ROUTE_PREFIX=%s, ROUTE_PREFIX_TICKS=%d", L.W.prefixPath, L.W.prefixTicks) or "")
end
function L.W.pace(procs, name)
  if L.W.mode == "fast" then
    local r = L.W.rand(100)
    local hold = (r < 70 and 1) or (r < 90 and 2 + L.W.rand(3)) or (15 + L.W.rand(31))
    push(K.A, hold, 3 + L.W.rand(9))
    return name .. "-fast"
  end
  local w = L.findIdle(procs, L.IDLE.TalkWaitForInput) or L.findIdle(procs, L.IDLE.TalkChoice_OnIdle)
    or L.findIdle(procs, L.SCENE.YesNoChoice)
  if w then
    L.W.nAt, L.W.pAt = nil, nil
    if L.W.wAddr ~= w.addr or not L.W.wAt or L.W.wAt > tick then L.W.wAddr, L.W.wAt, L.W.wDelay = w.addr, tick, 20 + L.W.rand(61) end
    if tick - L.W.wAt >= L.W.wDelay then L.W.wAddr = nil; push(K.A, 1 + L.W.rand(2), 2); return name .. "-read" end
    wait(1); return name .. "-reading"
  end
  L.W.wAddr = nil
  -- (a talk or CG text printing: no key; after 1,500 ticks of it with no wait, A anyway)
  if L.findScript(procs, L.SCENE.Talk) or L.findScript(procs, L.SCENE.CgTextMain) then
    L.W.nAt = nil
    if not L.W.pAt or L.W.pAt > tick then L.W.pAt = tick end
    if tick - L.W.pAt < 1500 then wait(1); return name .. "-print" end
    L.W.pAt = nil; push(K.A, 1, 2); return name .. "-print-nudge"
  end
  L.W.pAt = nil
  if not L.W.nAt or L.W.nAt > tick then L.W.nAt, L.W.nDelay = tick, 90 + L.W.rand(91) end
  if tick - L.W.nAt >= L.W.nDelay then L.W.nAt = nil; push(K.A, 1, 2); return name .. "-nudge" end
  wait(1); return name .. "-wait"
end

local idleTicks = 0
local lastSig = nil
local gameOver = nil   -- v9: set by decide when the game-over procs run; the tick callback stops

local function decide()
  if L.W.from > 0 then L.W.on = L.W.mode ~= "" and tick >= L.W.from end   -- (ROUTE_WATCH_FROM, L.W)
  local procs = L.procs()
  local ps = L.playSt()

  local dp = L.findIdle(procs, L.IDLE.DifficultySelect_KeyHandler)
  if dp then
    local sel = emu:read8(dp.addr + 0x30)
    if sel < wantDiff then push(K.DOWN, 1, 6) elseif sel > wantDiff then push(K.UP, 1, 6) else push(K.A, 1, 10) end
    return "difficulty"
  end

  -- (ROUTE_WATCH: the opening slides and a chapter's title run to their end, L.W)
  if L.W.on and L.findScript(procs, L.PROC.OpSubtitle) then wait(1); return "opening-watch" end
  if L.W.on and (L.findScript(procs, L.SCENE.ChapterIntro) or L.findScript(procs, L.SCENE.ChapterIntroTitleOnly)) then
    wait(1); return "intro-watch"
  end
  if L.findScript(procs, L.PROC.OpSubtitle) then push(K.START, 1, 10); return "opening-skip" end

  if tacticsV9(ps.chapter) and (L.findScript(procs, PROC_BMGAMEOVER) or L.findScript(procs, PROC_GAMEOVERSCREEN)) then
    gameOver = string.format("game over in chapter %d, turn %d", ps.chapter, ps.turn)
    wait(1)
    return "game-over"
  end

  -- v10: the supply screen (handleSupply). (E7 too)
  if tacticsV10(ps.chapter) or L.E7.chapter(ps.chapter) then
    local sp = L.findScript(procs, PROC_BMSUPPLY)
    if sp then handleSupply(sp); return "supply" end
  end

  -- (ROUTE_POSTGAME=1, from a cleared game save: Continue would play the ending again
  -- (GameCtrl_CheckGameCompleteAndBranch); the post-game's world map is Extras, then Map
  -- (EXTRA_MENU_OPTION_MAP: PostSaveMenuHandler sets PLAY_FLAG_POSTGAME), then the cleared
  -- slot. ProcScr_SaveMenu: main_select +0x2B over the set bits of main_options +0x30
  -- (SameMenu_CtrlLoop), extra_select +0x34 over extra_options +0x32 (SaveMenu_ExtraMenuLoop))
  if os.getenv("ROUTE_POSTGAME") == "1" then
    local sm = L.findScript(procs, 0x08A200B8)
    if sm and (sm.idle == 0x080A9025 or sm.idle == 0x080A9B91) then
      local main = sm.idle == 0x080A9025
      local bits = emu:read8(sm.addr + (main and 0x30 or 0x32)) & (main and 0x1F or 0x0F)
      local want = 0
      while bits ~= 0 do want = want + (bits & 1); bits = bits >> 1 end
      menuGo(emu:read8(sm.addr + (main and 0x2B or 0x34)), want)
      return main and "postgame-title" or "postgame-extras"
    end
  end

  -- v4: the world-map shop (and its last screens once the plan is done).
  if shopPlan then
    local sp = L.findScript(procs, L.SHOP.Proc)
    if sp and shopping() then handleShop(procs, sp); return "shop" end
    if L.findScript(procs, L.PROC.GmapBaseMenu) then
      local pi = L.findIdle(procs, L.IDLE.PrepItemScreen_UnitSelect)
      if pi then
        if shopping() then handlePrepShop(pi) else push(K.B, 1, 10) end
        return "shop-units"
      end
      if L.findIdle(procs, L.IDLE.TalkWaitForInput) then push(K.A, 1, 5); return "shop-talk" end
      wait(1)
      return "shop-wait"
    end
  end

  -- Battle preparations (first in Chapter 4): Start begins the chapter with
  -- the default units and items. The chapter's event engine waits under it.
  if L.findScript(procs, L.PROC.AtMenu) then push(K.START, 1, 20); return "prep-start" end

  -- v2: a unit with a full inventory got an item (an enemy's drop, in either
  -- phase): the menu lists its five items and the new one last. The new one
  -- goes to the supply (gSendToConvoyMenuItems' last entry), so the unit keeps
  -- its weapons.
  if tacticsV2(ps.chapter) and L.findScript(procs, L.PROC.ConvoyMenu) then
    local menu = L.findIdle(procs, L.IDLE.Menu_OnIdle)
    if menu then
      local _, count, cur = menuItems(menu)
      log("full inventory: menu of %d, cursor %d, sending the new item", count, cur)
      menuGo(cur, count - 1)
      return "convoy-menu"
    end
    wait(1)
    return "convoy-wait"
  end

  -- The route split after Chapter 8 (EventScr_SplitMenu, CallRouteSplitMenu,
  -- src/events/ch8-eventscript.h): a menu over the event engine. Without
  -- ROUTE_SPLIT the event branch's A takes its first entry (Eirika); with
  -- ROUTE_SPLIT=ephraim, Down to the second entry, A. The Yes/No question that
  -- follows is the event engine's, and its A takes Yes as before.
  if splitSpec == "ephraim" then
    for _, p in ipairs(procs) do
      if p.idle == L.IDLE.Menu_OnIdle and emu:read32(p.addr + 0x30) == L.MENUDEF.RouteSplit then
        local _, _, cur = menuItems(p)
        if cur == 1 then log("route split: Ephraim") end
        menuGo(cur, 1)
        return "split-menu"
      end
    end
  end

  -- (the post-game: a dungeon floor's "Proceed to the next floor?" while more nodes are wanted:
  -- B, the retreat; L.PG above)
  if (ps.stateBits & 0x04) ~= 0 and L.PG.idx < #L.PG.list and ps.chapter >= 0x24 and ps.chapter <= 0x37
      and (L.findIdle(procs, L.IDLE.TalkChoice_OnIdle) or L.findIdle(procs, 0x08090015))   -- (YesNoChoice_Loop_KeyHandler, the
      and not L.PG.done[ps.chapter] then                                                    -- tutorial text box's [Yes] [No])
    L.PG.done[ps.chapter] = true
    L.PG.idx = L.PG.idx + 1
    log("post-game: retreat from chapter %d, next node %d", ps.chapter, L.PG.list[L.PG.idx])
    push(K.B, 1, 20)
    return "postgame-retreat"
  end
  -- (coverage 11: a dungeon's last floor won, the dungeon record screen,
  -- sProcScr_DisplayDungeonRecord_AfterDungeonClear, under EventScr_WholeTowerClear: its key listener
  -- takes A (DungeonRecordUi_KeyListener) where the event branch's Start does nothing; no older route
  -- finishes a dungeon)
  if ps.chapter >= 0x24 and ps.chapter <= 0x37 and L.findScript(procs, 0x0859E6EC) then push(K.A, 1, 10); return "dungeon-record" end
  -- (feature routes: their screens and talks, L.F.decide)
  if L.F.on then
    local fs = L.F.decide(procs, ps)
    if fs then return fs end
  end
  local ev = L.findScript(procs, L.PROC.EventEngine)
  -- (the game's ending, once the final chapter is won, PLAY_FLAG_COMPLETE set and not
  -- the post-game's PLAY_FLAG_POSTGAME: A only, never Start, so the epilogue, the
  -- character endings and the credits play in full; no older route gets there)
  if ev and (ps.stateBits & 0x24) == 0x20 then
    if L.W.on then return L.W.pace(procs, "ending-watch") end   -- (ROUTE_WATCH, L.W)
    push(K.A, 1, 5); return "ending-advance"
  end
  if ev and L.W.on then return L.W.pace(procs, "event-watch") end   -- (ROUTE_WATCH: never Start, L.W)
  if ev then
    local bits = emu:read16(ev.addr + 0x3C)
    if (bits & 0x04) ~= 0 then wait(1); return "event-skipping" end
    if (bits & 0x02) ~= 0 and (bits & 0x10) == 0 then push(K.START, 1, 4); return "event-skip" end
    push(K.A, 1, 5)
    return "event-advance"
  end
  -- Battle quotes run in their own event engine and cannot be skipped; any
  -- other talk box waiting for a key is advanced with A too.
  if L.findScript(procs, L.PROC.BattleEventEngine) or L.findIdle(procs, L.IDLE.TalkWaitForInput) then
    if L.W.on then return L.W.pace(procs, "talk-watch") end   -- (ROUTE_WATCH, L.W)
    push(K.A, 1, 5)
    return "talk-advance"
  end
  -- (ROUTE_WATCH: a talk or CG text printing outside an event engine gets the same pace, L.W)
  if L.W.on and (L.findScript(procs, L.SCENE.Talk) or L.findScript(procs, L.SCENE.CgTextMain)) then
    return L.W.pace(procs, "talk-watch")
  end

  if L.GAL.on then local gs = L.GAL.decide(procs); if gs then return gs end end   -- (the item gallery: Warp, Torch, promotion)
  local pp = L.findScript(procs, L.PROC.PlayerPhase)
  if pp then
    local menu = L.findIdle(procs, L.IDLE.Menu_OnIdle)
    if menu and tacticsV3(ps.chapter) then
      -- v3: with a menu open over another (the Item list and its Use/Equip
      -- menu), take the one that is no other menu's parent.
      local menus, parents = {}, {}
      for _, p in ipairs(procs) do
        if p.idle == L.IDLE.Menu_OnIdle then menus[#menus + 1] = p; parents[emu:read32(p.addr + 0x14)] = true end
      end
      for _, m in ipairs(menus) do if not parents[m.addr] then menu = m; break end end
    end
    if menu then handleMenu(menu); return "menu" end
    if tacticsV3(ps.chapter) then
      local tp = L.findScript(procs, L.TRADE.Proc)
      if tp then handleTrade(tp); return "trade" end
    end
    -- v6: the promotion screens between their menus (A goes on); the supply screen
    if tacticsE6(ps.chapter) and L.findScript(procs, PROC_PROMO_MAIN) then push(K.A, 1, 30); return "promotion" end
    if tacticsE6(ps.chapter) then
      local sp = L.findScript(procs, SUPPLY.Proc)
      if sp then handleSupplyE(sp); return "supply" end
    end
    local ts = L.findScript(procs, L.PROC.TargetSelection)
    if ts then handleTarget(ts); return "target" end
    if pp.idle == L.IDLE.PlayerPhase_MainIdle then handleMainIdle(); return "main" end
    if pp.idle == L.IDLE.PlayerPhase_RangeDisplayIdle then handleRange(); return "range" end
    if L.GAL.on then return L.GAL.busyTick() end   -- (the item gallery: a popup that waits for a key)
    wait(1)
    return "pp-busy"
  end

  if L.findScript(procs, L.PROC.BMapMain) and ps.faction ~= 0 then
    wait(1)
    return "other-phase"
  end

  -- World map with a free cursor: put the cursor on the node of the chapter
  -- in gPlaySt.chapterIndex (L jumps between valid nodes) and press A there.
  -- A on the lord's own, already cleared node would open the base menu
  -- (shops, items) instead.
  local wm = L.findIdle(procs, L.GM.Loop)
  if wm then
    local d = L.GM.data
    if L.F.sk then L.F.wmPre() end   -- (ROUTE_SKIRMISH: no shopping, L.F)
    -- (coverage 12, ROUTE_WMSHOP: the world map's secret shop at node n, once)
    if L.F.wmshop and not L.F.st.wmshopSet then
      local ws = L.F.wmshop
      for _, u in ipairs(L.blue()) do
        if u.char == ws.char then
          local base = {}
          for _, id in ipairs(ws.buy) do base[id] = itemCount(u, id) end
          for _, id in ipairs(ws.sell) do base[id] = itemCount(u, id) end
          shopPlan = { node = ws.node, phase = "secret", want = { [u.char] = { buy = ws.buy, sell = ws.sell, base = base } } }
          L.F.st.wmshopSet = tick
          log("wmshop: node %d's secret shop for char %02x", ws.node, u.char)
        end
      end
    end
    -- v4: plan the shopping at the run's first free cursor, if no game save
    -- was written yet (see "v4" above).
    if shopPlan == nil and startedFromSave and tacticsV4(ps.chapter) then
      local saved = false
      for k in pairs(milestones.seen) do if k:match("^save_to") then saved = true end end
      if not saved then shopPlan = makeShopPlan() end
    end
    local cx, cy = emu:read32(d + 8) >> 8, emu:read32(d + 12) >> 8
    local target, cur, best
    for i = 0, L.GM.nodeCount - 1 do
      local st = emu:read8(d + 0x30 + 4 * i)
      if (st & 1) ~= 0 then
        local nd = L.GM.nodeData + i * 0x20
        if emu:read8(nd + 4) == ps.chapter and target == nil then target = i end
        local dd = math.abs(L.s16(emu:read16(nd + 0x18)) - cx) + math.abs(L.s16(emu:read16(nd + 0x1A)) - cy)
        if best == nil or dd < best then best, cur = dd, i end
      end
    end
    -- v7: a node of both routes holds Ephraim's chapter at +5 (chapteridx_ephram,
    -- struct GMapNodeData; +4 is Eirika's): Scorched Sand, index 28.
    if target == nil and tacticsV7(ps.chapter) then
      for i = 0, L.GM.nodeCount - 1 do
        if (emu:read8(d + 0x30 + 4 * i) & 1) ~= 0 and emu:read8(L.GM.nodeData + i * 0x20 + 5) == ps.chapter and target == nil then target = i end
      end
    end
    if shopping() then target = shopPlan.node end   -- A there walks the lord to it, A again opens its menu
    -- (the post-game, PLAY_FLAG_POSTGAME, with ROUTE_WM_NODE=N: walk the lord to node N, a dungeon
    -- (Tower of Valni 0x1A, Lagdou Ruins 0x1B); the walk's end there enters it, WorldMap_MuWalkLoop)
    -- (PR: the shopping first)
    if (ps.stateBits & 0x04) ~= 0 and #L.PG.list > 0 and not (L.PR.on and shopping()) then target = L.PG.list[L.PG.idx] end
    if L.F.sk then target = L.F.wmTarget(target, ps) end   -- (ROUTE_SKIRMISH: the skirmish node, L.F)
    log("world map cursor (%d,%d) on node %s (%s px), target node %s, lord at node %d", cx, cy, tostring(cur),
      tostring(best), tostring(target), emu:read8(d + 0x11))
    if target ~= nil and cur == target and best <= 16 then push(K.A, 1, 20) else push(K.L, 1, 20) end
    return "worldmap"
  end
  if L.findScript(procs, L.PROC.WorldMapMain) and L.findIdle(procs, L.IDLE.Menu_OnIdle) then
    local menu = L.findIdle(procs, L.IDLE.Menu_OnIdle)
    if shopping() and emu:read32(menu.addr + 0x30) == L.MENUDEF.WMNode then   -- v4: Enter Armory
      local ids, count, cur = menuItems(menu)
      local wantId = (shopPlan.phase == "vendor") and 1 or 0   -- (v10: the vendor, Shop)
      if shopPlan.phase == "secret" then wantId = 2 end        -- (coverage 12, ROUTE_WMSHOP: "? Shop")
      for i = 0, count - 1 do
        if ids[i] == wantId then menuGo(cur, i); return "worldmap-armory" end
      end
    end
    log("world map menu open, closing")
    push(K.B, 1, 20)
    return "worldmap-menu"
  end
  if L.findScript(procs, L.PROC.GmapBaseMenu) or L.findScript(procs, L.PROC.PrepItemScreen)
      or L.findScript(procs, L.PROC.PrepItemTradeScreen) then
    push(K.B, 1, 20)
    return "worldmap-back"
  end

  push(K.A, 1, 20)
  return "generic"
end

-- A route started from a save (GATE0_SRAM_IN, port/test/README.md "Chapter
-- saves") names it in its header: the first 32 KiB's FNV-1a 32, as the Gate 0
-- trace headers give it ("sram_fnv").
local function sramLine()
  local path = os.getenv("GATE0_SRAM_IN")
  if not path or path == "" then return "" end
  local f = assert(io.open(path, "rb"))
  local s = f:read(0x8000); f:close()
  local h = 0x811C9DC5
  for i = 1, #s do h = ((h ~ s:byte(i)) * 0x01000193) & 0xFFFFFFFF end
  -- (a save made with the test cheat, or after one: its .sav.json, from sram_capture.lua or
  -- sram_boost.lua, says "cheat", which they copy on into every later save of a chain;
  -- README-routes.md, "Cheat-assisted routes")
  local j = io.open(path .. ".json", "r")
  local made = j and j:read("a"):match('"cheat":"([%w+]+)"')
  if j then j:close() end
  return string.format("# Start: boot both sides with GATE0_SRAM_IN=%s (the save's first 32 KiB: FNV-1a %08x).\n",
    path:match("[^/]*$"), h) .. (made and string.format("# Start save made with the test cheat (its .sav.json: "
    .. "\"cheat\":\"%s\"): the army this route starts with comes from a run with the test cheat %s, at some save "
    .. "before this one (test coverage only; README-routes.md, \"Cheat-assisted routes\").\n", made, made) or "")
end
-- (a cheat-assisted route from a boosted start, whose .sav.json, from sram_boost.lua, says "what":"boosted
-- start ...": its header says both, "boosted, cheat-assisted" (README-routes.md, "Cheat-assisted routes").
-- Only with ROUTE_CHEAT, so every older header, boosted or not, stays as it was.)
function L.F.boostedLine()
  local path = os.getenv("GATE0_SRAM_IN")
  local j = path and path ~= "" and io.open(path .. ".json", "r")
  local what = j and j:read("a"):match('"what":"([^"]*boosted[^"]*)"')
  if j then j:close() end
  return what and string.format("# Boosted, cheat-assisted (test coverage only, not story play): the start save is a boosted "
    .. "start (its .sav.json: \"what\":\"%s\") and the route was made with the test cheat (below).\n", what) or ""
end

local finishHook = nil   -- v6: the search's totals
local function finish(reason)
  if finishHook then finishHook() end
  local header = string.format(
    "# FE8 Gate 0 key file (format: port/test/README.md, \"Input file\").\n" ..
    "# Generated by port/test/oracle/route_bot.lua (ROUTE_STOP=%s, ROUTE_DIFF=%d%s).\n" ..
    "%s%s# Milestones and how to replay: port/test/inputs/README-routes.md.\n" ..
    "# %d ticks. Stop reason: %s\n#\n# Milestones seen while generating (route_check.lua re-derives them):\n",
    stopSpec, wantDiff, (tickRule ~= "frame" and (", ROUTE_TICK=" .. tickRule) or "")
      .. (splitSpec ~= "" and (", ROUTE_SPLIT=" .. splitSpec) or "") ..
      (os.getenv("ROUTE_SEARCH") == "1" and string.format(
        ", ROUTE_SEARCH=1, ROUTE_SEARCH_K=%s, ROUTE_SEARCH_BACK=%s, ROUTE_SEARCH_BASE=%s, ROUTE_SEARCH_LOOKBACK=%s",
        os.getenv("ROUTE_SEARCH_K") or "12", os.getenv("ROUTE_SEARCH_BACK") or "3", os.getenv("ROUTE_SEARCH_BASE") or "4",
        os.getenv("ROUTE_SEARCH_LOOKBACK") or "1.5") or "") .. (L.E7.env and ", ROUTE_E7=1" or "") .. L.F.header() .. L.PR.header()
      .. L.W.header() .. (L.cheat and (", ROUTE_CHEAT=" .. L.CHEAT) or ""),
    sramLine(), L.cheat and L.F.boostedLine() .. string.format("# Cheat-assisted (test coverage only, not fair play): made with the test cheat "
      .. "ROUTE_CHEAT=%s (%s); replay it with GATE0_CHEAT=%s on both sides "
      .. "(README-routes.md, \"Cheat-assisted routes\").\n", L.CHEAT, L.cheat.describe(), L.CHEAT) or "", #masks, reason)
  for _, line in ipairs(mlog) do header = header .. "#   " .. line .. "\n" end
  header = header .. L.GAL.header()   -- (the item gallery's list, ROUTE_GALLERY=1; empty otherwise)
  L.writeKeys(outPath, masks, header)
  print(string.format("route_bot: %s; %d ticks in %d frames -> %s", reason, #masks, frame, outPath))
  io.stdout:flush()
  os.exit(0)
end

-- Feature routes (L.F): what their stops wait for, and the stops ROUTE_STOP=arena:N, support:N,
-- chest:N, resume:N, promo:N, skirmish:N; and wm:N, N ticks after the world map's first free
-- cursor (a dungeon's last floor won). Called each tick sample after the other stops.
function L.F.stopCheck()
  local F, st = L.F, L.F.st
  local procs = L.procs()
  local function past(at, what) if stopKind == what and at and tick - at >= stopArg then finish("passed " .. what .. ":" .. stopArg) end end
  if stopKind == "wm" then
    if not st.wmAt and L.findIdle(procs, L.GM.Loop) then st.wmAt = tick; log("the world map's free cursor") end
    past(st.wmAt, "wm")
  end
  if F.arena then past(st.arenaEnd, "arena") end
  if F.supp then
    if st.supportEv and not st.supportEnd and not L.findScript(procs, L.PROC.EventEngine) then
      st.supportEnd = tick; log("support: the conversation ended")
    end
    past(st.supportEnd, "support")
  end
  if F.chest then past(st.chestAt, "chest") end
  -- (coverage 12)
  if F.watch and not st.watchOn then
    local pp = L.findScript(procs, L.PROC.PlayerPhase)
    if pp and pp.idle == L.IDLE.PlayerPhase_MainIdle then st.watchOn = tick; log("watch: the events from here on get A") end
  end
  if F.supps then
    if st.supportsAt and st.supportsAt ~= st.suppDone and st.suppEv and not L.findScript(procs, L.PROC.EventEngine) then
      st.suppDone, st.suppEv = st.supportsAt, nil
      st.suppEnds = st.suppEnds or {}
      st.suppEnds[#st.suppEnds + 1] = tick
      log("supports: conversation %d ended", #st.suppEnds)
    end
    if st.suppEnds and #st.suppEnds >= F.suppN then past(st.suppEnds[F.suppN], "support") end
  end
  if F.visit then
    if st.visitAt and L.findScript(procs, L.PROC.EventEngine) then st.visitEv = true
    elseif st.visitAt and st.visitEv then
      st.visitAt, st.visitEv = nil, nil
      local n = #L.F.visitGoals((F.rawPlaySt or L.playSt)().chapter)
      st.visitCount = (st.visitCount or 0) + 1
      log("visit: an event ended (%d so far), %d to go", st.visitCount, n)
      if (n == 0 or (F.visitN and st.visitCount >= F.visitN)) and not st.visitEnd then st.visitEnd = tick end
    end
    past(st.visitEnd, "visit")
  end
  if F.door then past(st.doorAt, "door") end
  if F.shop and st.shopAt and not st.shopEnd then
    if L.findScript(procs, L.SHOP.Proc) then st.shopSeen = true
    elseif st.shopSeen then st.shopEnd = tick; log("shop: closed, gold %d", L.playSt().gold) end
  end
  if F.shop or F.wmshop then past(st.shopEnd, "shop") end
  if F.susp and st.suspAt and st.suspAt >= 0 then
    if not st.suspTitle and L.findScript(procs, L.END.title) then st.suspTitle = tick; log("suspend: the title screen") end
    local pp = L.findScript(procs, L.PROC.PlayerPhase)
    if st.suspTitle and not st.suspBack and pp and pp.idle == L.IDLE.PlayerPhase_MainIdle then
      st.suspBack = tick; log("suspend: the chapter resumed, free cursor")
    end
    past(st.suspBack, "resume")
  end
  -- (coverage 12, ROUTE_TRAINEE: the trainee's class when the try starts; the trainee lost ends the try)
  if F.trainee and not st.traineeClass then
    for _, u in ipairs(L.blue()) do if u.char == F.trainee then st.traineeClass = u.class end end
  end
  if st.traineeLost then finish(string.format("the trainee (char %02x) was lost", F.trainee)) end
  if F.promo then
    if st.promoSeen and not st.promoEnd and not L.findScript(procs, PROC_PROMO_MAIN) then
      st.promoEnd = tick; log("promotion screens closed")
      if F.trainee then   -- (coverage 12, ROUTE_TRAINEE: only the trainee's own promotion counts for promo:N)
        local cls
        for i = 0, 61 do   -- (any blue unit slot, deployed or not)
          local u = L.readUnit(L.ADDR.gUnitArrayBlue + i * 0x48)
          if u and u.char == F.trainee then cls = u.class end
        end
        if cls == nil or cls == st.traineeClass then
          log("promotion screens closed: not the trainee's (char %02x class %s)", F.trainee, cls and string.format("%02x", cls) or "-")
          st.promoSeen, st.promoEnd = nil, nil
        else
          log("trainee: char %02x class %02x -> %02x", F.trainee, st.traineeClass, cls)
        end
      end
    end
    past(st.promoEnd, "promo")
  end
  if F.sk then
    local raw = F.rawPlaySt()
    if st.skStory and not st.skFought and raw.chapter ~= st.skStory and L.findScript(procs, L.PROC.BMapMain) then
      st.skFought = tick; log("skirmish: its map (chapterIndex %d), played with the rules of chapter index %d", raw.chapter, F.sk.rule)
    end
    if st.skFought and not st.skDone and L.findIdle(procs, L.GM.Loop) then st.skDone = tick; log("skirmish: the world map again") end
    past(st.skDone, "skirmish")
  end
end

---------------------------------------------------------------------------
-- v6: a search over policies, turn by turn, with save states (ROUTE_SEARCH=1,
-- chapter index 15 on; README-routes.md "Tactics v6"). At the first free
-- cursor of each player phase (a "branch point") the bot saves the emulator
-- (emu:saveStateBuffer, with the save data) and its own state, and plays the
-- turn with policy 0 (the plain v5 bot) through the enemy phase to the next
-- branch point. If a unit died there (or a lord, seen at once), the bot loads
-- the save state again and plays the same turn with another policy (P above):
-- first with the units that died playing safe, then with more knobs; at most
-- ROUTE_SEARCH_K policies a turn. It keeps the first policy with no death, else
-- the best end (fewest deaths, a healer's counting 1.5; then the army's HP, the
-- enemies left and their HP). A try that has already lost more than the best
-- one so far is cut short. When every policy loses a lord it goes back a turn
-- (up to ROUTE_SEARCH_BACK turns) and tries that turn's next policy. The key
-- file holds only the kept turns' keys, so it replays open-loop like any other:
-- mGBA is deterministic from a save state, and the save states are taken and
-- loaded only at a tick sample (the capture rule's gate0_wait, with the rule's
-- own state put back too).
SEARCH_K = tonumber(os.getenv("ROUTE_SEARCH_K") or "12")
SEARCH_BACK = tonumber(os.getenv("ROUTE_SEARCH_BACK") or "3")
SS ={ cur = nil, lastBranch = nil, variants = 0, restores = 0, backtracks = 0, deathsKept = 0, pruned = 0 }
assert(not SEARCH or tickRule == "capture", "ROUTE_SEARCH=1 needs ROUTE_TICK=capture")

local function slog(fmt, ...)
  print(string.format("[%d] search: " .. fmt, tick, ...)); io.stdout:flush()
end

local function copyT(t)
  if t == nil then return nil end
  local c = {}
  for k, v in pairs(t) do c[k] = v end
  return c
end
local function deepT(t)
  if type(t) ~= "table" then return t end
  local c = {}
  for k, v in pairs(t) do c[k] = deepT(v) end
  return c
end

-- A policy spec: { safe = {[unit id] = true}, hold, cautious, seed, amp, order }.
local function newSpec() return { safe = {}, hold = false, cautious = false, seed = 0, amp = 0, order = 0, zone = nil, guard = false,
                                   press = false, bold = false } end
local function copySpec(s) local c = copyT(s); c.safe = copyT(s.safe); return c end
local function specKey(s)
  local ids = {}
  for id in pairs(s.safe) do ids[#ids + 1] = id end
  table.sort(ids)
  local t = {}
  for _, id in ipairs(ids) do t[#t + 1] = string.format("%02x", id) end
  return "safe[" .. table.concat(t, ",") .. "]" .. (s.hold and " hold" or "") .. (s.cautious and " cautious" or "")
    .. (s.zone and (" zone " .. s.zone) or "") .. (s.guard and " guard" or "") .. (s.press and " press" or "")
    .. (s.bold and " bold" or "") .. (s.amp ~= 0 and string.format(" noise %d/%d", s.seed, s.amp) or "")
end
local function setSpec(s, v)
  P = { v = v, seed = s.seed, amp = s.amp, cautious = s.cautious, hold = s.hold, order = s.order, safe = copyT(s.safe),
        zone = s.zone, guard = s.guard, press = s.press, bold = s.bold }
end

SAVESTATE_ALL = 31   -- mGBA's SAVESTATE_* flags: screenshot, save data, cheats, RTC, metadata
local function snapshot()
  return {
    emu = emu:saveStateBuffer(SAVESTATE_ALL),
    tick = tick, frame = frame, nMasks = #masks, nMlog = #mlog,
    queue = copyT(queue), plan = deepT(plan), endTurn = endTurn, skipUnits = copyT(skipUnits),
    lastTurnKey = lastTurnKey, badDest = copyT(badDest), lastAlive = copyT(lastAlive),
    idleTicks = idleTicks, lastSig = lastSig, shopPlan = deepT(shopPlan), lastTraced = lastTraced,
    ms = { seen = copyT(milestones.seen), lastSbi = milestones.lastSbi, lastChapter = milestones.lastChapter,
           lastPlayedChapter = milestones.lastPlayedChapter },
    cap = L.captureState(),
    pg = { idx = L.PG.idx, done = copyT(L.PG.done) },
    fst = deepT(L.F.st),   -- (feature routes, L.F)
    -- (I11: v10's supply visits, in Ephraim's final chapter only; nil elsewhere, so E6's saves are as before)
    supplyTried = I11.on(L.playSt().chapter) and copyT(supplyTried) or nil,
    sup = L.E7.env and copyT(supplyTried) or nil,   -- (E7: v10's supply, whose plans the search replays)
    cheat = L.cheat and L.cheat.save() or nil,      -- (the test cheat's state at this sample, route_lib.lua)
  }
end

-- Load snapshot s. For a turn's end state, base is the turn's start snapshot and
-- the slices are the keys and milestone lines made since it.
local function restore(s, base, maskSlice, mlogSlice)
  assert(emu:loadStateBuffer(s.emu, SAVESTATE_ALL), "search: the save state did not load")
  -- (the runner ends its frame loop when the frame counter changes and fires one
  -- "frame" callback for it; frame counts it back)
  tick, frame = s.tick, s.frame - 1
  local n0 = base and base.nMasks or s.nMasks
  for i = #masks, n0 + 1, -1 do masks[i] = nil end
  if maskSlice then table.move(maskSlice, 1, #maskSlice, n0 + 1, masks) end
  assert(#masks == s.nMasks, "search: key list out of step")
  local m0 = base and base.nMlog or s.nMlog
  for i = #mlog, m0 + 1, -1 do mlog[i] = nil end
  if mlogSlice then table.move(mlogSlice, 1, #mlogSlice, m0 + 1, mlog) end
  queue = copyT(s.queue); plan = deepT(s.plan); endTurn = s.endTurn; skipUnits = copyT(s.skipUnits)
  lastTurnKey = s.lastTurnKey; badDest = copyT(s.badDest); lastAlive = copyT(s.lastAlive)
  idleTicks = s.idleTicks; lastSig = s.lastSig; shopPlan = deepT(s.shopPlan); lastTraced = s.lastTraced
  milestones.seen = copyT(s.ms.seen); milestones.lastSbi = s.ms.lastSbi
  milestones.lastChapter = s.ms.lastChapter; milestones.lastPlayedChapter = s.ms.lastPlayedChapter
  L.setCaptureState(s.cap)
  L.PG.idx, L.PG.done = s.pg.idx, copyT(s.pg.done)
  L.F.st = deepT(s.fst)
  if s.supplyTried then supplyTried = copyT(s.supplyTried) end
  if s.sup then supplyTried = copyT(s.sup) end
  if s.cheat then L.cheat.load(s.cheat) end
  L.W.reset()   -- (ROUTE_WATCH: the pace's timers start again)
  SS.restores = SS.restores + 1
end

-- The army at a branch point: blue slots (alive: on the map or carried), and
-- the red units left with their HP.
local function blueAlive(base)
  local st, hp = emu:read32(base + 0x0C), emu:read8(base + 0x13)
  return (st & (L.US.DEAD | L.US.NOT_DEPLOYED | L.US.BIT16)) == 0 and hp > 0, hp
end
local function armyInfo()
  local info = { blue = {}, redN = 0, redHP = 0 }
  for i = 0, 61 do
    local base = L.ADDR.gUnitArrayBlue + i * 0x48
    if emu:read32(base) ~= 0 then
      local alive, hp = blueAlive(base)
      info.blue[i] = { alive = alive, hp = hp, id = emu:read8(base + 0x0B), base = base,
                       healer = emu:read8(base + 0x28 + 4) > 0 }
    end
  end
  for _, e in ipairs(L.red()) do info.redN = info.redN + 1; info.redHP = info.redHP + e.hp end
  return info
end
-- Deaths since the turn's start (a healer counts 1.5) and the unit ids that died.
local function deathsSince(F)
  local d, victims = 0, {}
  for _, b0 in pairs(F.info0.blue) do
    if b0.alive and not blueAlive(b0.base) then
      d = d + (b0.healer and 1.5 or 1)
      victims[#victims + 1] = b0.id
    end
  end
  return d, victims
end

local function stateHash()
  local s = emu:readRange(L.ADDR.gUnitArrayBlue, 0x48 * 62) .. emu:readRange(L.ADDR.gUnitArrayRed, 0x48 * 50)
    .. emu:readRange(L.ADDR.gRNSeeds, 6)
  local h = 0x811C9DC5
  for i = 1, #s do h = ((h ~ s:byte(i)) * 0x01000193) & 0xFFFFFFFF end
  return h
end

if SEARCH then
  finishHook = function()
    slog("totals: %d extra policies played (%d cut short), %d save states loaded, %d turns gone back, %d look-backs, %d turns kept with a death, %d attack trials",
      SS.variants, SS.pruned, SS.restores, SS.backtracks, SS.lookbacks or 0, SS.deathsKept, SS.trials or 0)
    local seeds, lcg = L.rng()
    slog("end state: tick %d, gRNSeeds %s, gLCGRNValue %s, units hash %08x", tick, seeds, lcg, stateHash())
  end
end

local function lordDeadNow(F)
  for _, base in ipairs(F.lordSlots) do
    local st, hp = emu:read32(base + 0x0C), emu:read8(base + 0x13)
    if (st & L.US.DEAD) ~= 0 or (hp == 0 and (st & L.US.NOT_DEPLOYED) == 0) then return true end
  end
  -- (PROTECT: the game over after the protected unit's death counts as a lost lord;
  -- the title's Resume Chapter would replay the same enemy phase with the same RNG)
  if protectOn(F.chapter) then
    local procs = L.procs()
    if L.findScript(procs, PROC_BMGAMEOVER) or L.findScript(procs, PROC_GAMEOVERSCREEN) then return true end
  end
  return false
end

local function newFrame(key, parent, startSnap)
  TR = nil
  local ps = L.playSt()
  local F = { key = key, chapter = ps.chapter, turn = ps.turn, parent = parent, start = startSnap or snapshot(),
              info0 = armyInfo(), results = {}, lordSlots = {}, tried = {}, victimsSeen = {} }
  for i = 0, 61 do
    local base = L.ADDR.gUnitArrayBlue + i * 0x48
    local cp = emu:read32(base)
    if cp ~= 0 and (emu:read8(cp + 4) == L.CHAR.EIRIKA or emu:read8(cp + 4) == L.CHAR.EPHRAIM) then
      F.lordSlots[#F.lordSlots + 1] = base
    -- (E7: Eirika's carrier counts as a lord: the turn is played again, or the one before, when
    -- it dies; a kept death of Franz at the south-east exit left her alone on the map)
    elseif cp ~= 0 and L.E7.chapter(ps.chapter) and carriesLord(L.readUnit(base)) then
      F.lordSlots[#F.lordSlots + 1] = base
    end
  end
  -- the turns too far back to go back to drop their save states
  local p, n = parent, 1
  while p do
    if n > SEARCH_BACK then p.results = nil; p.start = nil; p.parent = nil; break end
    p = p.parent; n = n + 1
  end
  -- stalled: the enemies left have not gone down for three turns (press policies first)
  local q, k = parent, 0
  F.stalled = true
  while k < 3 do
    if not q or not q.info0 or q.info0.redN ~= F.info0.redN then F.stalled = false; break end
    q = q.parent; k = k + 1
  end
  -- (v6, chapter index 20 on, a seize chapter: no fewer enemies than two turns ago;
  -- reinforcements keep the count from standing still. F.sb0: the seize tile's boss
  -- stands at the turn's start)
  if F.chapter >= 0x14 and seizeTile(F.chapter) then
    local gp = parent and parent.parent
    F.stalled = gp ~= nil and gp.info0 ~= nil and F.info0.redN >= gp.info0.redN
    F.sb0 = seizeBoss(F.chapter) ~= nil
  end
  -- (E7, the final chapter: the baseline of L.E7.finScore)
  if L.E7.chapter(F.chapter) and F.chapter >= 0x15 then F.e7 = L.E7.finBase() end
  if L.E7.eph(F.chapter) then F.e23 = L.E7.base23() end   -- (E7 on map 0x23: L.E7.score23)
  local s = newSpec()
  if F.stalled then s.press = true end
  F.tried[specKey(s)] = true
  F.spec = s
  SS.cur, SS.lastBranch = F, key
  setSpec(s, 0)
  return F
end

local function describe(r)
  local vs = {}
  for _, id in ipairs(r.victims or {}) do vs[#vs + 1] = string.format("%02x", id) end
  local who = #vs > 0 and (" (" .. table.concat(vs, ",") .. ")") or ""
  if r.fatal then return "a lord died" .. who end
  if r.pruned then return string.format("cut short at %s deaths%s", tostring(r.deaths), who) end
  local cs = {}
  for _, id in ipairs(r.critIds or {}) do cs[#cs + 1] = string.format("%02x", id) end
  return string.format("%s deaths%s, army HP %d%s, %d enemies left, score %.0f%s", tostring(r.deaths), who, r.hpSum,
    #cs > 0 and (", low " .. table.concat(cs, ",")) or "", r.redN, r.score, r.bad and ", led to a lost lord" or "")
end

-- The next policy to try after result lastR: first the units that died play
-- safe too; when protected units die anyway (or a lord died), every unit seen
-- dying this turn plays safe, with hold, cautious, both, then noise seeds.
local function nextSpec(F, lastR)
  local function fresh(s) if not F.tried[specKey(s)] then return s end end
  local base = lastR.spec or newSpec()
  local hit = {}
  for _, id in ipairs(lastR.victims or {}) do hit[#hit + 1] = id end
  for _, id in ipairs(lastR.critIds or {}) do hit[#hit + 1] = id end
  if #hit > 0 then
    local s, new = copySpec(base), false
    for _, id in ipairs(hit) do if not s.safe[id] then s.safe[id] = true; new = true end end
    if new and fresh(s) then return s end
  end
  local all = newSpec()
  all.zone = base.zone
  for id in pairs(F.victimsSeen) do all.safe[id] = true end
  if fresh(all) then return all end
  for _, mod in ipairs({ { zone = 2, guard = true }, { zone = 2 }, { zone = 3, guard = true, cautious = true },
                         { zone = 2, cautious = true }, { hold = true }, { cautious = true },
                         { zone = 4, guard = true }, { hold = true, cautious = true } }) do
    local s = copySpec(all)
    for k, v in pairs(mod) do s[k] = v end
    if fresh(s) then return s end
  end
  for seed = 1, 200 do
    local s = copySpec(all)
    s.seed, s.amp, s.order = seed, 1 + seed % 3, 1
    s.zone = ({ 2, 3, nil, 4 })[seed % 4 + 1]
    s.hold, s.cautious, s.guard = false, (seed % 2 == 0), (seed % 3 ~= 0)
    if fresh(s) then return s end
  end
end

local backtrack
local function startVariant(F, s)
  TR = nil
  restore(F.start)
  SS.lastBranch = F.key
  F.tried[specKey(s)] = true
  F.spec = s
  local v = #F.results
  setSpec(s, v)
  SS.variants = SS.variants + 1
  slog("chapter %d turn %d: policy %d: %s (back to tick %d)", F.chapter, F.turn, v, specKey(s), F.start.tick)
end
-- A save state with the whole key list and milestone log (the look-back's
-- fallback below: it is not relative to any turn's start).
local function snapshotFull()
  local s = snapshot()
  s.fullMasks = table.move(masks, 1, #masks, 1, {})
  s.fullMlog = table.move(mlog, 1, #mlog, 1, {})
  return s
end
local function restoreFull(s)
  TR = nil
  for i = #masks, 1, -1 do masks[i] = nil end
  table.move(s.fullMasks, 1, #s.fullMasks, 1, masks)
  for i = #mlog, 1, -1 do mlog[i] = nil end
  table.move(s.fullMlog, 1, #s.fullMlog, 1, mlog)
  restore(s)
end
local function useIncumbent(B)
  local inc = B.incumbent
  B.incumbent = nil
  restoreFull(inc.snap)
  inc.bAccepted.bad = nil
  B.accepted = inc.bAccepted
  inc.frame.accepted = inc.result
  SS.deathsKept = SS.deathsKept + 1
  newFrame(inc.key, inc.frame, inc.snap)
end
local function accept(F, r, isCurrent)
  if not isCurrent then restore(r.snap, F.start, r.maskSlice, r.mlogSlice) end
  -- the end of a look-back (decideFrame): keep the better of this way and the old one
  local B = F.parent
  if B and B.incumbent and B.incumbent.turn == F.turn and B.incumbent.frame ~= F then
    local inc = B.incumbent
    B.incumbent = nil
    local newScore = r.score - 100000 * (B.accepted.deaths or 0)
    local oldScore = inc.score - 100000 * (inc.bAccepted.deaths or 0)
    if oldScore >= newScore then
      slog("chapter %d turn %d: the look-back from turn %d did not help (%s); back to the old way", F.chapter, F.turn, B.turn, describe(r))
      B.incumbent = inc
      return useIncumbent(B)
    end
    slog("chapter %d turn %d: the look-back from turn %d helped", F.chapter, F.turn, B.turn)
  end
  F.accepted = r
  if r.deaths > 0 then SS.deathsKept = SS.deathsKept + 1 end
  slog("chapter %d turn %d: keep policy %d %s (%s; %d tried)", F.chapter, F.turn, r.v, specKey(r.spec), describe(r), #F.results)
  -- (ROUTE_PARTIAL=1: the keys kept so far, for a look at a long run; not a route file)
  if os.getenv("ROUTE_PARTIAL") == "1" then
    L.writeKeys(outPath .. ".partial", masks, string.format("# partial key file: the kept turns to tick %d\n", tick))
  end
  newFrame(r.key, F, r.snap)
end
-- Every turn plays the SEARCH_BASE first policies (the plain bot, hold, hold
-- and cautious) and keeps the best of them if it lost nobody and left nobody
-- under 40% HP; else it goes on with nextSpec up to SEARCH_K.
SEARCH_BASE = tonumber(os.getenv("ROUTE_SEARCH_BASE") or "4")
LOOKBACK = tonumber(os.getenv("ROUTE_SEARCH_LOOKBACK") or "1.5")   -- deaths that make a turn look back (0: never)
local function baseSpec(i)
  local s = newSpec()
  if i == 1 then s.zone = 3 end
  if i == 2 then s.zone = 3; s.cautious = true end
  if i == 4 then s.zone = 3; s.guard = true end
  return s   -- (i = 3: the plain v5 bot)
end
local function decideFrame(F, lastR)
  local usable = not lastR.fatal and not lastR.pruned and not lastR.bad
  if lastR.victims then for _, id in ipairs(lastR.victims) do F.victimsSeen[id] = true end end
  if usable and lastR.critIds then for _, id in ipairs(lastR.critIds) do F.victimsSeen[id] = true end end
  if usable and (F.bestDeaths == nil or lastR.deaths < F.bestDeaths) then F.bestDeaths = lastR.deaths end
  local function bestOf()
    local best
    for i, r in ipairs(F.results) do
      -- (in a look-back only the turn's new tries count: the old ones led to the fallback)
      if not r.fatal and not r.pruned and not r.bad and not (F.incumbent and i <= F.lookFrom)
          and (best == nil or r.score > best.score) then best = r end
    end
    return best
  end
  for i = 1, SEARCH_BASE do
    local s = baseSpec(i)
    if F.stalled then s.press = true end   -- (a stalled turn tries its base policies with "press")
    if not F.tried[specKey(s)] then return startVariant(F, s) end
  end
  if F.stalled then
    for i = 1, SEARCH_BASE do
      local s = baseSpec(i)
      if not F.tried[specKey(s)] then return startVariant(F, s) end
    end
    -- (and, from chapter index 16 on, "bold" attack trials: the threat after an
    -- attack left to this search; route_e15.keys was made before this rule)
    for i = 1, (F.chapter >= 0x10) and SEARCH_BASE or 0 do
      local s = baseSpec(i)
      s.press, s.bold = true, true
      if not F.tried[specKey(s)] then return startVariant(F, s) end
    end
  end
  local best = bestOf()
  if best and best.deaths == 0 and best.crit == 0 then return accept(F, best, best == lastR) end
  -- (a look-back gets up to 6 new tries of its own)
  if #F.results < SEARCH_K or (F.incumbent and #F.results < F.lookFrom + 6) then
    local s = nextSpec(F, lastR)
    if s then return startVariant(F, s) end
  end
  -- Look-back: every policy of this turn loses a healer or two units. Play the
  -- turn before once more with this turn's victims playing safe there too
  -- (each turn looks back at most once); the way kept so far (this turn's
  -- best end) stays as the fallback, and accept() keeps the better one.
  local B = F.parent
  if best and LOOKBACK > 0 and best.deaths >= LOOKBACK and B and B.start and B.results and B.accepted
      and not B.lookedBack and not B.incumbent and not F.incumbent then
    local s = copySpec(B.accepted.spec or newSpec())
    for id in pairs(F.victimsSeen) do s.safe[id] = true end
    if not B.tried[specKey(s)] then
      if best ~= lastR then restore(best.snap, F.start, best.maskSlice, best.mlogSlice) end
      B.incumbent = { snap = snapshotFull(), key = best.key, frame = F, turn = F.turn, score = best.score, bAccepted = B.accepted,
                      result = best }
      B.lookedBack = true
      B.lookFrom = #B.results
      -- (its tries are cut short only past the old way's deaths over both turns)
      B.bestDeaths = (B.accepted.deaths or 0) + best.deaths
      B.accepted.bad = true
      SS.cur = B
      SS.lookbacks = (SS.lookbacks or 0) + 1
      slog("chapter %d turn %d: every policy loses %s; look back to turn %d", F.chapter, F.turn, tostring(best.deaths), B.turn)
      return startVariant(B, s)
    end
  end
  if best then return accept(F, best, best == lastR) end
  if F.incumbent then
    slog("chapter %d turn %d: the look-back found nothing; back to the old way", F.chapter, F.turn)
    return useIncumbent(F)
  end
  return backtrack(F)
end
backtrack = function(F)
  local B = F.parent
  SS.backtracks = SS.backtracks + 1
  if not B or not B.start or not B.results then
    finish(string.format("search: every policy loses a lord in chapter %d, turn %d", F.chapter, F.turn))
  end
  slog("chapter %d turn %d: every policy loses a lord; back to turn %d", F.chapter, F.turn, B.turn)
  B.accepted.bad = true
  B.bestDeaths = nil
  for _, r in ipairs(B.results) do
    if not r.fatal and not r.pruned and not r.bad and (B.bestDeaths == nil or r.deaths < B.bestDeaths) then B.bestDeaths = r.deaths end
  end
  SS.cur = B
  return decideFrame(B, B.accepted)
end

local function searchBranch(key)
  local F = SS.cur
  if F == nil then
    newFrame(key, nil)
    slog("chapter %d turn %d: first branch point", SS.cur.chapter, SS.cur.turn)
    return
  end
  local now = armyInfo()
  local deaths, victims = deathsSince(F)
  -- (a unit left under 40% of its HP costs up to 3,000, a healer 4,500: the next
  -- enemy phase may take it; those units play safe in the next policies)
  local hpSum, crit, critIds = 0, 0, {}
  for _, b1 in pairs(now.blue) do
    if b1.alive then
      hpSum = hpSum + b1.hp
      local mx = emu:read8(b1.base + 0x12)
      if b1.hp * 10 < mx * 4 then
        crit = crit + 3000 * (1 - b1.hp * 10 / (mx * 4)) * (b1.healer and 1.5 or 1)
        critIds[#critIds + 1] = b1.id
      end
    end
  end
  local r = { v = P.v, spec = F.spec, key = key, deaths = deaths, victims = victims, hpSum = hpSum, redN = now.redN,
              crit = crit, critIds = critIds,
              score = -100000 * deaths - crit + 3 * hpSum - 300 * now.redN - 2 * now.redHP, hash = stateHash() }
  for _, o in ipairs(F.results) do if o.hash == r.hash and o.bad then r.bad = true end end
  -- (chapter index 18 on: in a stalled turn, getting closer to the enemies left
  -- counts too, 3 a tile of each unit's distance to its nearest enemy)
  if F.chapter >= 0x12 and F.stalled then
    local red, sumd = L.red(), 0
    for _, b in ipairs(L.blue()) do
      local md
      for _, e in ipairs(red) do
        local d = dist(b.x, b.y, e.x, e.y)
        if md == nil or d < md then md = d end
      end
      sumd = sumd + (md or 0)
    end
    r.score = r.score - 3 * sumd
  end
  -- (chapter index 20 on, a seize chapter: the seize tile's boss killed this turn
  -- counts 150,000 (more than a death); every unit's distance to the seize tile costs
  -- 20 a tile, 100 for a rusher (every usable weapon kept for the boss) and for Eirika's
  -- carrier while it stands, 300 for Eirika or her carrier once the boss is gone)
  if F.chapter >= 0x14 and seizeTile(F.chapter) then
    local s, sbu, md = seizeTile(F.chapter), seizeBoss(F.chapter), 0
    if F.sb0 and not sbu then r.score = r.score + 150000 end
    -- (E7: Eirika's or her carrier's distance on her way, L.E7.front, not in a straight line)
    local fr7 = L.E7.chapter(F.chapter) and L.E7.front(s) or nil
    for _, b in ipairs(equipAll(L.blue())) do
      local wt = 20
      if (b.char == L.CHAR.EIRIKA or carriesLord(b)) and not sbu then wt = 300 end   -- (or her carrier)
      if sbu and not isLordUnit(b) and b.weapon and otherWeapon(b, sbu) == nil and not L.E7.chapter(F.chapter) then wt = 100 end   -- (not E7: no rusher)
      if sbu and carriesLord(b) then wt = 100 end   -- (her carrier while the boss stands)
      local dd = dist(b.x, b.y, s.x, s.y)
      if fr7 and fr7.c.id == b.id then dd = fr7.dc0 end
      md = md + dd * wt
    end
    r.score = r.score - md
  end
  -- (E7, the final chapter: Myrrh's uses, Stone, the boss's HP, the ways to the boss; L.E7.finScore)
  if F.e7 and L.E7.chapter(F.chapter) and F.chapter >= 0x15 then r.score = r.score + L.E7.finScore(F.e7) end
  if F.e23 and L.E7.eph(F.chapter) then r.score = r.score + L.E7.score23(F.e23) end   -- (E7 on map 0x23)
  r.snap = snapshot()
  r.maskSlice = table.move(masks, F.start.nMasks + 1, #masks, 1, {})
  r.mlogSlice = table.move(mlog, F.start.nMlog + 1, #mlog, 1, {})
  F.results[#F.results + 1] = r
  slog("chapter %d turn %d: policy %d: %s", F.chapter, F.turn, r.v, describe(r))
  decideFrame(F, r)
end

local function searchCut(fatal)
  local F = SS.cur
  local d, victims = deathsSince(F)
  local r = { v = P.v, spec = F.spec, fatal = fatal, pruned = not fatal, score = -math.huge, deaths = d, victims = victims }
  F.results[#F.results + 1] = r
  if not fatal then SS.pruned = SS.pruned + 1 end
  slog("chapter %d turn %d: policy %d: %s", F.chapter, F.turn, r.v, describe(r))
  decideFrame(F, r)
end

SS_stalled = function() return SS.cur ~= nil and SS.cur.stalled == true end

-- v6 attack trials (handleRange starts one). At the next tick sample the bot
-- saves the state, then plays TR.list[1], TR.list[2], ... in turn for the unit,
-- each to the end of its action (the unit is done, or dead, or the phase or the
-- chapter ends), loading the save state between them; it keeps the best real
-- outcome by its end state (the target's HP lost, a kill, the unit's HP lost,
-- the heuristic enemy-phase threat on the unit there against the HP it has left)
-- and goes on from there. mGBA's RNG is part of the state, so the outcome kept
-- is exactly the one the key file replays.
local function trialValue(reason)
  if reason == "chapter" then return 1e9 end
  if reason == "lord" or reason == "dead" then return -1e9 end
  local base = L.ADDR.gUnitArrayBlue + (TR.uid - 1) * 0x48
  local u = L.readUnit(base)
  if not u then return -1e9 end
  local pl = TR.list[TR.idx]
  local v = -(TR.hp0 - u.hp) * 4
  if pl.target then
    local tb = L.ADDR.gUnitArrayRed + (pl.target - 0x81) * 0x48
    local t0 = TR.thp0[TR.idx] or 0
    local alive = emu:read32(tb) ~= 0 and emu:read8(tb + 0x13) > 0
    local now = alive and emu:read8(tb + 0x13) or 0
    v = v + (t0 - now) * 10 + (alive and 0 or 1000)
    -- (v6, chapter index 20 on: the seize tile's boss killed outweighs any danger; the
    -- turn's search judges the enemy phase)
    if not alive and TR.sbId and pl.target == TR.sbId then v = v + 300000 end
  end
  -- the enemy-phase threat on the unit where it stands now
  local ps = L.playSt()
  V2, V3, E6 = tacticsV2(ps.chapter), tacticsV3(ps.chapter), tacticsE6(ps.chapter)
  SACRED2 = E6 and ps.chapter >= 0x14
  L.E7.on = L.E7.chapter(ps.chapter)
  local enemies = equipAll(L.red())
  reachSets = {}
  for _, e in ipairs(enemies) do reachSets[e] = enemyReach(e, nil) end
  equip(u)
  local th = threatAt(u, u.x, u.y, enemies)
  -- (v6, the final chapter's part 2: an attack on the Demon King is judged by his next attack
  -- alone, exact; the turn's search judges the rest of the enemy phase)
  if pl.target and L.FINALBOSS[ps.chapter] then
    for _, e in ipairs(enemies) do
      if e.id == pl.target and e.char == L.FINALBOSS[ps.chapter] then
        local hit = (strike(e, u) or 0) * nhits(e, u)
        if hit >= u.hp then v = v - 200000 end
        return v - hit * 2
      end
    end
  end
  if lordLike(u) then
    if th * 2 >= u.hp then v = v - 200000 end
  elseif th >= u.hp and not P.bold then v = v - 50000 end   -- (policy "bold": the turn's search judges the enemy phase)
  return v - (P.bold and 0 or th * 2)
end

local function trialStep()
  local ps = L.playSt()
  if TR.want then
    TR.want, TR.active, TR.idx, TR.results = false, true, 1, {}
    TR.snap = snapshot()
    local base = L.ADDR.gUnitArrayBlue + (TR.uid - 1) * 0x48
    TR.hp0 = emu:read8(base + 0x13)
    TR.thp0 = {}
    for k, pl in ipairs(TR.list) do
      if pl.target then TR.thp0[k] = emu:read8(L.ADDR.gUnitArrayRed + (pl.target - 0x81) * 0x48 + 0x13) end
    end
    local sbu = seizeBoss(ps.chapter)   -- (v6, chapter index 20 on: the seize tile's boss)
    TR.sbId = sbu and sbu.id
    return
  end
  if not TR.running then return end
  local reason
  local base = L.ADDR.gUnitArrayBlue + (TR.uid - 1) * 0x48
  if ps.chapter ~= TR.chapter then reason = "chapter"
  elseif SS.cur and lordDeadNow(SS.cur) then reason = "lord"
  elseif not blueAlive(base) then reason = "dead"
  elseif ps.faction ~= 0 then reason = "phase"
  else
    local pp = L.findScript(L.procs(), L.PROC.PlayerPhase)
    if pp and pp.idle == L.IDLE.PlayerPhase_MainIdle and (emu:read32(base + 0x0C) & L.US.UNSELECTABLE) ~= 0 then reason = "done" end
  end
  if not reason then return end
  local r = { idx = TR.idx, value = trialValue(reason), reason = reason }
  r.snap = snapshot()
  r.maskSlice = table.move(masks, TR.snap.nMasks + 1, #masks, 1, {})
  r.mlogSlice = table.move(mlog, TR.snap.nMlog + 1, #mlog, 1, {})
  TR.results[#TR.results + 1] = r
  local pl = TR.list[TR.idx]
  slog("trial: unit %02x plan %d (%s%s at (%d,%d)): %s, value %.0f", TR.uid, TR.idx, pl.action,
    pl.target and string.format(" %02x", pl.target) or "", pl.dest.x, pl.dest.y, reason, r.value)
  SS.trials = (SS.trials or 0) + 1
  if TR.idx < #TR.list and reason ~= "chapter" then
    TR.idx = TR.idx + 1
    TR.running = false
    restore(TR.snap)
    return
  end
  local best
  for _, x in ipairs(TR.results) do if best == nil or x.value > best.value then best = x end end
  slog("trial: unit %02x keeps plan %d", TR.uid, best.idx)
  local snap0 = TR.snap
  TR = nil
  if best ~= r then restore(best.snap, snap0, best.maskSlice, best.mlogSlice) end
end

local function branchKey(ps)
  if ps.faction ~= 0 then return nil end
  local procs = L.procs()
  local pp = L.findScript(procs, L.PROC.PlayerPhase)
  if not pp or pp.idle ~= L.IDLE.PlayerPhase_MainIdle then return nil end
  if L.findScript(procs, L.PROC.EventEngine) then return nil end
  return ps.chapter .. ":" .. ps.turn
end

-- Called at the start of every tick sample, before anything else looks at it.
local function searchStep()
  local ps = L.playSt()
  -- (the game won: PLAY_FLAG_COMPLETE, from the final chapter's end on; the ending's scenes
  -- clear and reload the units, which the search would take for deaths. No search after it)
  if (ps.stateBits & 0x24) == 0x20 then   -- (not in the post-game, PLAY_FLAG_POSTGAME)
    if SS.cur then slog("the game is won (PLAY_FLAG_COMPLETE), search ends"); SS.cur = nil; setSpec(newSpec(), 0) end
    TR = nil
    return
  end
  -- (I11: Ephraim's final chapter too, I11.search)
  local searchOn = tacticsE6(ps.chapter) or I11.search(ps.chapter)
  if not searchOn or (SS.cur and SS.cur.chapter ~= ps.chapter) then
    if SS.cur then slog("chapter %d left, search ends", SS.cur.chapter); SS.cur = nil; setSpec(newSpec(), 0) end
    if not searchOn then return end
  end
  if TR then trialStep(); if TR then return end end   -- (no turn-level checks while an attack trial runs)
  if SS.cur then
    if lordDeadNow(SS.cur) then searchCut(true); return end
    if SS.cur.bestDeaths ~= nil and tick % 16 == 0 and deathsSince(SS.cur) > SS.cur.bestDeaths then searchCut(false); return end
  end
  local key = branchKey(ps)
  if key and key ~= SS.lastBranch then searchBranch(key) end
end

if L.W.prefixTicks > 0 then masks[1] = L.W.prefix[1] end   -- (ROUTE_PREFIX, L.W)
emu:setKeys(masks[1])
L.onTick(tickRule, function() frame = frame + 1 end, function()
  -- v6: may load a save state (this tick is then the one it was taken at); (ROUTE_PREFIX: not while the
  -- prefix plays, whose keys no policy changes: the search starts at the bot's first turn, L.W)
  if SEARCH and tick + 1 >= L.W.prefixTicks then searchStep() end
  for _, line in ipairs(milestones.step(tick)) do
    mlog[#mlog + 1] = line
    print(line); io.stdout:flush()
  end

  local ps = L.playSt()
  if stopKind == "pp" and milestones.seen["pp" .. stopArg] then finish("reached pp:" .. stopArg) end
  if stopKind == "save" then
    -- Chapter N's end save is written after gPlaySt.chapterIndex moves on;
    -- run 300 more ticks after it.
    local at
    for k, v in pairs(milestones.seen) do
      local to = k:match("^save_to(%d+)$")
      if to and tonumber(to) > stopArg then at = v end
    end
    if at and tick - at >= 300 then finish("passed save:" .. stopArg) end
  end
  -- "saveto:K": 300 ticks after the first game save written while
  -- gPlaySt.chapterIndex is K. For a chapter whose ending passes through
  -- scene maps with higher indices (Chapter 8: Castle Frelia, 56), save:N
  -- would stop at the scene's save.
  if stopKind == "saveto" then
    local at = milestones.seen["save_to" .. stopArg]
    if at and tick - at >= 300 then finish("passed saveto:" .. stopArg) end
  end
  if stopKind == "tick" and tick + 1 >= stopArg then finish("reached tick:" .. stopArg) end
  -- "title:N": N ticks after the title screen that follows the ending's Fin screen
  -- (gProcScr_FinScreen, then gProcScr_TitleScreen; src/gamecontrol.c label 16 on)
  if stopKind == "title" then
    local procs, es = L.procs(), L.END.seen
    if L.findScript(procs, L.END.fin) and not es.fin then es.fin = tick; log("the Fin screen") end
    if es.fin and not es.title and L.findScript(procs, L.END.title) then es.title = tick; log("the title screen") end
    if es.title and tick - es.title >= stopArg then finish("passed title:" .. stopArg .. " after the ending") end
  end
  if L.F.on or stopKind == "wm" then L.F.stopCheck() end   -- (feature routes, L.F: their stops; wm:N)
  if L.GAL.on then   -- (the item gallery: a battle seen; ROUTE_STOP=gallery:N, N ticks after it is done)
    L.GAL.tick()
    if stopKind == "gallery" and L.GAL.doneAt and tick - L.GAL.doneAt >= stopArg then finish("passed gallery:" .. stopArg) end
  end
  -- (coverage 12, ROUTE_PREFIX=FILE:N: the first N entries are that key file's, played as they are)
  if L.F.prefix and tick + 2 <= #L.F.prefix then
    masks[tick + 2] = L.F.prefix[tick + 2]
    tick = tick + 1
    emu:setKeys(masks[tick + 1])
    return
  end

  local state = "queued"
  local inPrefix = tick + 1 < L.W.prefixTicks   -- (ROUTE_PREFIX: tick t+1's keys come from the prefix file, L.W)
  if inPrefix then
    state = "prefix"
  elseif #queue == 0 then
    state = decide()
    if gameOver then finish(gameOver) end
    if traceStates and state ~= lastTraced then
      print(string.format("[%d] state %s (chapter %d turn %d phase %02x mode %d)", tick, state, ps.chapter, ps.turn, ps.faction, ps.mode))
      lastTraced = state
    end
  end
  local m
  if inPrefix then m = L.W.prefix[tick + 2] else m = table.remove(queue, 1) or 0 end

  -- Watchdog: same coarse state for too long.
  local sig = state .. ":" .. ps.chapter .. ":" .. ps.turn .. ":" .. ps.faction
  if state ~= "queued" and state ~= "prefix" then
    if sig == lastSig then idleTicks = idleTicks + 1 else idleTicks = 0; lastSig = sig end
  end
  -- Long enemy phases (many battles) stay in one state for thousands of ticks.
  if idleTicks > 40000 then finish("stuck in " .. sig) end
  -- v3: a lord died. The game-over screen leads back to the title, and the
  -- bot's A presses take "Resume Chapter", which replays the same enemy phase
  -- from its suspend save with the same RNG: the same death, forever. Stop.
  -- (v6: the search sees a lord's death first and plays the turn again)
  if tacticsV3(ps.chapter) and not tacticsE6(ps.chapter) and not I11.search(ps.chapter) and tick % 30 == 0 then
    for i = 0, 61 do
      local b = L.readUnit(L.ADDR.gUnitArrayBlue + i * 0x48)
      if b and isLordUnit(b) and ((b.state & L.US.DEAD) ~= 0 or (b.hp == 0 and (b.state & L.US.NOT_DEPLOYED) == 0)) then
        finish(string.format("game over: lord (char %02x) died in chapter %d, turn %d", b.char, ps.chapter, ps.turn))
      end
    end
  end
  -- A chapter the tactics cannot win (units that only wait): stop, and keep
  -- the keys so far for a look.
  if ps.turn > MAX_TURN then finish("stuck: chapter " .. ps.chapter .. " past turn " .. MAX_TURN) end

  masks[tick + 2] = m
  tick = tick + 1
  emu:setKeys(m)
end)
