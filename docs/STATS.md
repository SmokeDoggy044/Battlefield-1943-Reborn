# Stats, leaderboards and achievements

TekRantGaming's request of 16 Nov 2026: new Stats & Rewards and Leaderboards pages with more stats, such as kills and
K/D, and achievements that pop up in game with a sound and are listed as in King Kong Recompiled. He chose these three
of six mockups (`runs/ui-designs/build_progress.py`, pictures in `runs/ui-designs/out/`):

- **S1 Service Record** for Stats & Rewards;
- **L1 Roll of Honour** for Leaderboards;
- **M1 Medal Case** for Achievements, with its popup.

## Where the numbers come from

Before this work, each player's own game added up its score events (`stat.<label>` in the settings) and sent its
totals (`stats_put`): score, time and captures, as the player's game said. Those totals cannot be checked, and the game
does not see deaths, wins or losses this way.

Now the dedicated server counts every player from the game's own bookkeeping (`bf/src/server_stats.cpp`). The functions
it reads were found in the generated code on 16 Nov 2026:

| Function | When the server calls it | What it gives |
|---|---|---|
| `sub_823F8960` | Every score award. r4 is the player, f1 the points, r6 the label. | Score, melee kills, kill assists, flag captures, defends, repairs, heals |
| `sub_82409708` | The kill handler. r4 is the killer (0 for the world), r5 the victim, r9 & 0xFF a headshot. | Kills, deaths, team kills, suicides, headshots, kills made from a vehicle |
| `sub_823F7518` | Each player's round result. r5 & 0xFF is set when the player lost. | Rounds, wins, losses |
| `sub_824144A0` | The event dispatcher. The type is at r4+136, the player at r4+144, a name at r4+156. | Spawn with its kit (17), into a vehicle (15), out of one (16), a player gone (1): the time in each kit and vehicle |

Other details:

- A player the game marks at +532 gets no score record, so it is left out too.
- The best kill streak is counted here, because the game keeps none.
- The first time the server sees each kit name, vehicle name and award label, it writes it to its log as `BF server
  stats:`, so how each one counts can be checked.

### How the counts reach the backend

Every 5 seconds the server sends one `player_stats` message holding each player's new counts. It goes one of two ways:

- **Official servers (the server manager):** the game writes the message as one line into `stats.txt` in the server's
  folder (`--bf_stats_file`). The host, `server/gmhost.py --stats-file`, sends each new line on its backend link.
- **The built in host** (`gm_host.cpp`) sends it with its status.

The backend (`server/player_stats.py`) keeps the counts only from official servers, so a community server cannot make
up anyone's stats. A test backend can accept every server with `--player-stats-from all`.

For each name it keeps totals for all time, this month and this week, plus results by map and the best streak. They
are saved to `player_stats.json` in the server package's data folder (`--player-stats`).

When an account moves to another name, its stats move with it.

### Checked in a match

TekRantGaming's kill test of 16 Nov 2026 used a local Wake Island server and the target soldier TargetDummy from this
session. The server counted all of these, and they reached the backend and the boards:

- three kills, each a headshot;
- the game's own awards, "kill" (+15) and "headshot" (+5);
- a best streak of 3;
- the target's three deaths;
- the time in each kit.

Earlier test runs, on the same local setup, counted these as well:

- deaths and suicides;
- the time in the landing boat (LCVP counts as Boat);
- spawns, with the kit.

The kits go by the engine's names:

| Engine name | Kit |
|---|---|
| Support | Infantry |
| Assault | Rifleman |
| Recon | Scout |

Not seen in a match yet: flag captures, round wins and losses, kills from vehicles, heals and repairs.

### What is not counted

- **Revives:** Battlefield 1943 has none.
- **Shots fired and hit:** not counted, so there is no accuracy.
- **Heals and repairs:** counted as the game's award events. They may come one per amount healed or repaired rather
  than one per action. This has not been checked in a match yet.

### Backend requests

| Request | Reply |
|---|---|
| `{"op": "leaderboard", "sort", "period", "me"}` | The top 50 by `score`, `kills`, `kd` (50 kills or more), `captures`, `seconds`, `headshots`, `vk_plane`, `vk_tank` or `wins`, for `all`, `month` or `week`, with the asker's own row and place. Players the servers have not counted yet fill in from their own totals, all time only. Without these fields the reply is the old one. |
| `{"op": "player_card", "name"}` | One player's record: every count, by map, the best streak, and their place on each board. |
| `{"op": "achievement_share"}` | How many players with an account have each achievement, counts only. |

## The pages

All three are in `bf/src/game_menu_progress.inc`.

### Stats & Rewards (S1)

- The soldier, their rank, the next rank and the rank ladder.
- Eight numbers: kills, deaths, K/D, score, flags, rounds won, headshots and repairs.
- Time by kit and by vehicle, and results by map.
- The latest rewards, the achievement total, and the next achievements to earn.

Each number is the larger of the servers' count and this PC's own score events, because the servers began counting
later. Deaths, K/D and rounds won show a dash until the servers have counted the player.

### Leaderboards (L1)

- Folder tabs for Score, Kills, K/D, Flags and Time, switched with LB and RB.
- X switches between all time, this month and this week.
- Every column is shown. Your own row is pinned under the table, and the chosen soldier's card is on the right.

### Achievements (M1)

- The game's 12 achievements and Reborn's own 3 in two columns. Unlocked ones are first, with when they were unlocked.
  With more than 12 the columns scroll (the chosen row stays in sight; the mouse wheel scrolls too). Ours are marked
  REBORN.
- Locked ones are grey with a padlock. Where our counts can tell, they show how far along you are. These are our own
  counts: the game decides the unlock.
- Each one shows how many players have it.
- X filters all, unlocked or locked. Y shows a test popup.
- It works on Home before the game runs too. The SDK only has its catalog once the game starts, at the first join on
  a normal start, so each run with the game keeps a copy of the game's list in
  `user/cache/extracted/achievements/catalog.txt` (beside the pictures). Before the game runs, the page reads that copy,
  Reborn's own 3 and the SDK's unlock file. Until the game has run once on a user folder, only Reborn's own 3 show,
  with a note. The popup is made with the menus at startup (not at the game's launch), so Y works there as well.

## Achievements and the popup

The game unlocks an achievement through XGIUserWriteAchievements, which the SDK's AchievementManager handles: it saves
the unlock to `user/achievements/5841097E.toml` and tells the app. `bf/src/achievement_toast.cpp` replaces the SDK's
toast and copies King Kong's behaviour:

- A paper slip slides in at the top right in 0.35 s, holds for 5 s, then slides out.
- It shows the achievement's own picture, its name in the game's headline font, its words and its gamerscore.
- Several unlocks queue one after another.
- A chime plays through SDL: the Battlefield theme's hook, made from the player's own game files (below). King
  Kong's plain chime, made at run time, plays when those give none. A player's own `user/sounds/achievement.wav`
  (16 bit PCM) plays in place of either.

### The chime from the Battlefield theme

TekRantGaming, 18 Nov 2026: "the iconic Battlefield tone play as like a kind of esque achievement kind of sound ...
when the user provides the game files, we can still accurately recreate this on everyone's systems". No sound
ships: `bf/src/theme_chime.h` holds a recipe, and the chime is made on the player's PC from the menu music the
first start extracts from their game files (`user/cache/extracted/music/menu.wav`).

- The cut is the theme's first bar, its hook "Da, da, da-daaa" (0.25 to 2.30 s), played a whole tone up.
- Before it, a reversed reverb of its first hit swells in, with a run of bells; a bell chord marks the hit.
- After it, bells play the hook again on the same rhythm over a softer, darker echo of the orchestra's bar. The
  last note rings into a longer hall and fades out. 5.7 s in all, as long as the popup shows.
- Everything in `theme_chime.cpp` is integer arithmetic (its own sine, filters, reverbs and bells; no floating
  point, no libm), so the same game files give the same bytes on every PC, compiler and system.
- The first start's worker makes it once, after the menu music, into `user/cache/extracted/sounds/achievement-1.wav`
  (`MakeAchievementChime`); an unlock only reads that file and sets the volume. The number in the name goes up when
  the recipe changes, so the next start makes the new one.
- `tools/theme_chime_render.cpp` renders the recipe from an extracted `menu.wav` with the game's own code: to
  listen to a change, and to compare two builds or systems byte for byte.

The pictures come from the game's title data and are written once to `user/cache/extracted/achievements/<id>.png`.

There are two settings under Settings > Audio: Achievement popups (`bf_achievement_popups`) and Achievement sound volume
(`bf_achievement_volume`).

A protected name already sends its unlocked ids with its signed stats, and a recovery code brings them back on a new PC.

## Reborn's own achievements

TekRantGaming asked on 16 Nov 2026 for achievements of our own, with gamerscore and pictures of their own. There are
eight, in `bf/src/reborn_achievements.cpp`:

| Id | Name | Gamerscore | Unlocked when |
|---|---|---|---|
| 101 | I am reborn | 10 G | you load into your first multiplayer match (any server, a custom match too) |
| 102 | I am forever | 20 G | you load into a match on a community server |
| 103 | I am inevitable | 30 G | you load into a custom match that you are hosting |
| 104 | Steel Behemoth | 10 G | you get into a tank for the first time |
| 105 | Road Warrior | 10 G | you get into a jeep for the first time |
| 106 | Naval Warfare | 10 G | you get into a boat for the first time |
| 107 | Air Superiority | 10 G | you get into a fighter plane for the first time |
| 108 | Death From Above | 20 G | you call in the bombers from an airfield's air raid bunker |

TekRantGaming asked for 104 to 108 on 18 Nov 2026. With the game's 200 G the total is 320 G.

- **Ids.** The game's title data has 1 to 12. Ours are 101 and up, so they never clash, and the backend already counts
  every id from 1 to 999 (`achievement_share`), so it needs no change for them.
- **Where they live.** They are put into the SDK's own catalog (AchievementManager::RegisterAchievement, which is made
  for a recomp's own entries) once the game's 12 are loaded. So they work like the 12: the unlock is saved in the same
  user/achievements/5841097E.toml, the popup and chime show it, the Achievements page and Stats & Rewards list it, a
  protected name sends it with its signed stats, and the backend counts it for "how many players have it".
- **Loading into.** Join notes what kind of match it is. The unlock comes once the level has loaded and the soldier
  is in it (on foot or in a seat) for 1.5 s, so not when the join is picked and not on the deploy screen. A community
  server is one that is not official, not this PC's own custom match, and not a LAN match. A friend's listed custom
  match counts, as it is on the browser's Community tab. Your own custom match gives I am reborn and I am inevitable,
  not I am forever.
- **Vehicles.** Any seat counts (driver, pilot, gunner or passenger), on either side, once. Which vehicle it is comes
  from the game itself, never from the model. Each time the soldier's seat changes (`seat_info.h`), the seat's vehicle
  object is read: its data at +0x14 holds the vehicle's own name at +0xC (the name the server counts time per vehicle
  by) and its class at +0x80. `ForVehicle` in `reborn_achievements.cpp` turns the name, or else the class, into the
  achievement. The seat's own control scheme decides the planes and the air raid: a plane's pilot gives Air
  Superiority and flying the air raid's bombers gives Death From Above. Sitting in the bunker before the bombers fly
  gives nothing. Guns give none. The log says each seat once: "BF achievements: seat #n in the vehicle ...".

  | Side | Vehicle | Name | Class |
  |---|---|---|---|
  | USMC | tank | Sherman | Medium_Tank |
  | Japan | tank | chi-ha | Medium_Tank |
  | USMC | jeep | Willys | Light_Jeep |
  | Japan | jeep | Kurogane | Light_Jeep |
  | both | landing boat | LCVP | Light_Boat |
  | USMC | fighter | Corsair | Aircraft_Airframe_corsair |
  | Japan | fighter | Zero | Aircraft_Airframe |
  | both | air raid bunker | bombraid2 | Stationary_Metal |

  Tested on 18 Nov 2026 in matches on a local server: each one on both sides, a gunner's seat, each unlocking once
  (also with a profile that already had them), the popup with its picture in a boat and in the bomber view, and the
  Achievements page with all 20. Dev builds: `BF_DEV_VEHICLESCAN=1` logs the text near a seat's vehicle in memory,
  and `BF_DEV_FIND_SPAWNS=<x>,<z>,<r>` logs a server's spawn points near a place (for `BF_DEV_MOVE_SPAWNS`: Japan's
  carrier on Wake Island is -164.2, 231.0, -554.3 and its neighbours).
- **Pictures.** python tools/make_reborn_achievements.py draws them as 64 x 64 RGBA PNGs, the size and format of the
  game's own pictures, into `bf/data/achievements/<id>.png`. They ship next to the exe in data/achievements. They
  are perforated stamps like the game's, but every scene is our own drawing: a phoenix rising from its flames, a
  golden endless loop in laurel, a command flag with a star planted on a hill by the sea, a tank in its own dust, a
  jeep on a dirt road past a palm, a landing boat throwing spray, a fighter climbing over the clouds, and bombers over
  an island at dusk with their bombs falling.

## Tests

- `python -I tools/test_player_stats.py` checks the module and the backend's requests. It runs on 127.0.0.5.
- Dev switches:
  - `BF_DEV_ACHIEVEMENT_TEST="<s>:<id>,..."` shows a test popup at that time.
  - `BF_DEV_ACHIEVEMENT_WAV=<file>` writes the chime to that file instead of playing it, so tests stay silent.
  - `BF_DEV_NO_GAME_ART=1` keeps the theme's chime from being made too: the plain chime plays.
