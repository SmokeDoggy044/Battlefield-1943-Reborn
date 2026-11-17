# Muting a player's voice

TekRantGaming, 17 Nov 2026: "a new feature that allows users to mute individual users so that there coms dont come
across on voice chat anymore. This needs to be intergrated into the scoreboard in game and on the endgame score
screen too and should have a speaker or mic icon and an icon for being muted too to visualy show the status of that
players mute status".

## What players get

- **The game's own scoreboards** (held with Back or Tab in a match, and the end of round screen's team scoreboard
  and Top 5 players): each row's **V** column (the console's voice column, empty on
  PC until now) shows a speaker:
  - dim: the player is quiet;
  - lit, with two green waves moving out: the player is heard now;
  - struck through in red: you muted them.
  Your own row's speaker lights while your own voice goes out, and is never muted.
- The scoreboards' **Y** prompt, "SEE GAMER CARD" on the console, reads **MUTE / UNMUTE**: move the game's own row
  cursor (D-pad or stick; the arrow keys) to a player and press Y (F on the keyboard by default) to mute them, again to
  unmute. The prompt keeps the game's own look and follows the Button prompts setting (an Xbox or PlayStation
  button, or the keyboard key).
- **Our pause menu's Scoreboard**: the same speaker in each row, a row cursor (D-pad, stick, arrow keys or the mouse),
  "MUTED" beside a muted name, and X MUTE / UNMUTE (F on the keyboard, a click on the prompt, or a click on the
  speaker itself).

A mute is yours alone: nothing is sent anywhere, the server and the muted player are not told, and it changes nothing
about your own microphone.

## How it works

- **Dropped on your own game** (comms.cpp): a muted player's voice packets are dropped as they arrive, before they
  are decoded or mixed, and anything of theirs already waiting to play is dropped at once. No server change, so it
  works on every server: official, community, private matches, and servers on older packages.
- **Kept by name, across matches, servers and restarts**: `muted-players.txt` in the user folder, one name a line,
  written at each change. The name is the account's (the backend keeps one name per account); a clan tag is not part
  of it ("[TAG]Name" and "Name" are the same player), and case does not matter.
- **The game's scoreboards are its own Flash movies** (igm_mp_cq). Their Y calls the movie's seeGamerCard, which
  sends `FSCommand:showGamerCard <the selected row's name>`; auto_join.cpp's command hook hands it to
  voice_mute.cpp (TakeGamerCard), which toggles that player's mute, and the command goes no further (the PC has no
  gamer card). The prompt's words are rewritten in the text hook (RewriteGameText) as each frame draws them, and put
  back after.
- **Where the rows are**: every run of the game's text passes clan_tag_colours.cpp's hook on sub_826AB588.
  voice_mute.cpp keeps the runs that can be a heading ("V", "Gamertag") or a row ("Name . . . . ."), with where
  their first glyph lands: its pen (sub_826AB240: x at r7, y in r8, 1280 x 720 stage units, the y at the top of the
  line) through the run's matrix at renderer + 128 (clip x = x m0 + y m4 + m12, clip y = x m1 + y m5 + m13). A table
  is a "V" heading with a "Gamertag" heading on its line; its rows start where "Gamertag" starts, 20 units each. The
  overlay (comms_ui.cpp, our UI) draws each row's speaker in the V column.
- **The icon** (voice_mute_icon.cpp) is drawn from shapes, so it is sharp at any resolution: a speaker, two waves, a
  slash, with a thin edge in the paper's colour so it reads on the game's green (selected) and black (your own) row
  bars as well as on its paper.

## Settings

| setting | default | what |
| --- | --- | --- |
| `bf_scoreboard_mute` | true | the game's scoreboards show the speakers and their Y mutes; false: the game's own scoreboards as before (Y does nothing on PC, as before) |

The mutes themselves are only in `muted-players.txt` (delete a line, or the file, to unmute from outside the game).

## Not done

- The game's scoreboards cannot tell us which row the game's cursor is on (its rows' text colour words are all the
  same), so the prompt says MUTE / UNMUTE for any row rather than the one word that fits the selected player.
- A click on a speaker in the game's own scoreboards: those screens have no mouse pointer in a match. Our pause's
  Scoreboard takes clicks.
- Other languages: the prompt is rewritten only where the game's text is the English "SEE GAMER CARD"; Y mutes in
  every language.
- Text chat is not muted (not asked for).
- The scoreboard the game shows after a death (with "TIME TO SPAWN") is the same movie with the same headings and
  should show the speakers too; it was not caught on screen in the tests.
- On the end of round screen nobody is heard (voice goes out only from a soldier in the world), so its speakers show
  quiet or muted, never talking.

## Tests (17 Nov 2026, F:\bf047 worktree player-mute, runs\<name>)

- **fn1**: three clients; peer talks a 440 Hz test voice, peer2 1000 Hz; the main client records what it hears
  (BF_DEV_VOICE_OUT; no real microphone or speakers). Y on the game's scoreboard muted peer2 at 28.7 s into the
  recording and unmuted it at 54.7 s: the 1000 Hz voice fell from -9.6 dB to the -65 dB floor between the two and
  came back, the 440 Hz voice stayed at -9.6 dB throughout; the comms log counted 250 frames a 5 s window dropped
  from the muted player and none from the other.
- **eor1**: the round ended (BF_DEV_TICKETS 120,84,1 on the server and a suicide): the end of round screen's team
  scoreboard and Top 5 list both show the speakers, and its Y unmuted and muted again.
- **kb1** (a restart): started from eor1's user folders, whose muted-players.txt held peer2: "1 muted player" at the
  start and peer2's voice dropped from the first second. Keyboard and mouse on, keyboard prompts: Tab held, Right, F
  unmuted, muted and unmuted peer2; the prompt reads "F MUTE / UNMUTE".
- **pz1**: our pause's Scoreboard: X (controller) muted two players, F (keyboard) unmuted one; prompts X MUTE and the
  F key, no mute prompt on your own row. **p720**: clicks on another player's speaker there unmuted, muted and muted.
- **eor2**: the end of round screen with keyboard and mouse and keyboard prompts in a real 1280 x 720 window (this PC
  runs at 250%, so --window_width=512 --window_height=288): "F MUTE / UNMUTE", "R NEXT INFO SCREEN"; F muted peer2 in
  the match, then unmuted and muted it on the end screen; the speakers and the slash read at 720p as at 3200 x 1800.
