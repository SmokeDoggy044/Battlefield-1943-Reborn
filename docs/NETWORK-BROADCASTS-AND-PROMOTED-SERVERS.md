# Network broadcasts and promoted servers

Two tools for the owner of the Project-Official Servers (TekRantGaming), both run on the official machine with the
official server package's manager:

- **Network broadcasts**: a message box to the players on every official server (this machine's and the other
  official machines', US Central included) or, when you choose, on every server registered with the main backend,
  community servers too.
- **Promoted servers**: a community server you pick shows on the **Reborn** tab with the official servers, marked
  **TRUSTED COMMUNITY SERVER**, until you take it back. It never says it is run by TRG Studios. The official servers
  are marked **REBORN OFFICIAL SERVER** next to them. (Players see "Trusted Community Server", never "promoted";
  the commands and the backend keep the word promoted.)

All commands below are typed in the official set's folder on the official machine, as the other manager commands:

```
python\python.exe server\manager.py <command>
```

## Network broadcasts

### Sending one

| What you want | Command |
|---|---|
| This machine's own servers only (as before) | `manager.py broadcast --kind custom --text "..."` |
| Every official server, on every official machine | `manager.py broadcast --kind custom --text "..." --to official` |
| Everyone: official and community servers | `manager.py broadcast --kind custom --text "..." --to everyone` |

Without `--to`, a broadcast goes to this machine's servers exactly as it always has. `--to official` is the usual
choice for news; `--to everyone` is for the times the whole player base must hear something.

Every kind works with `--to`, with or without a countdown (`--minutes`):

- `--kind custom --text "..."`: your own words, 200 characters at most. Put `{time}` in the text to count it down:
  `--kind custom --text "New maps arrive in {time}" --minutes 30`.
- `--kind maintenance --minutes 15`: "The Reborn servers go down for maintenance in 15 minutes", said again at
  10, 5, 2 and 1 minutes, 30 and 10 seconds.
- `--kind restart --minutes 5`: "The Reborn servers restart in 5 minutes".
- `--kind update --minutes 10`: "A new version of Battlefield 1943 Reborn is out. The Reborn servers update in
  10 minutes".

A network broadcast **only tells** the players: it never restarts or updates a server, on this machine or anywhere
else. Restart or deploy the servers yourself when the countdown ends, as now. (Without `--to`, a restart or update
countdown still restarts this machine's servers at its end, as before.)

A countdown runs in the manager. The manager's page shows it under **Counting down**, with a **Cancel** button. The
page's **Messages** section has the same choices: **Send to** lists "Every official server (all machines)" and
"Everyone: official and community servers" on the official package, and the **Maintenance** message.

The players see the usual box at the top of the screen with its chime. A maintenance message has its own amber
label, MAINTENANCE (games from before v0.4.7 show it as a server message).

### Who gets it

- `--to official`: every server registered with the official key. Promoted community servers do not get these:
  they are someone else's machines.
- `--to everyone`: every server registered with the main backend, listed or not.
- A community server shows network broadcasts from its package v0.4.7 on. Older community servers (0.4.6.7 and
  before) ignore the message and keep running as before; nothing breaks.
- The manager's log and the page's **Sent** list say how many official and community servers it reached.

### Why nobody else can send one

- The manager signs each broadcast with a key made from `official.key`, which only the official machine has.
- The main backend passes on only a broadcast signed with that key, and each server checks the signature again
  before its players see it. A forged one, a changed text, or one sent twice is refused, and so is an old one: a
  broadcast is good for one minute, plus one minute for a server whose clock is a little off. A server whose clock
  is further off refuses them and says so in its host log.
- At most 6 network broadcasts a minute, 200 characters each. The backend and each server count for themselves.

### Before the v0.4.7 release: one step on the official machine

Community servers need the public half of that key in their copy of `server/gmhost.py`. It is not secret. On the
official machine, run once:

```
python\python.exe server\manager.py broadcast-key
```

It prints one line of 64 letters and digits. That line goes into `OFFICIAL_BROADCAST_KEYS` in `server/gmhost.py`
before the release is built. Until it is there, official servers already show network broadcasts (they make the key
from their own `official.key`), and community servers show none. If `official.key` is ever changed, the printed key
changes too and needs a new release.

## Promoted servers

### See the community servers registered now

```
python\python.exe server\manager.py community
```

One line per community server: its host key (64 letters and digits), game number, name, address, version, players,
and whether it is listed, hidden by the build check, or promoted. A host key is the server's identity: it stays the
same when its address or name changes, and nobody else can use it without that host's own key file. All servers of
one host share its key.

### Promote

```
python\python.exe server\manager.py promote <host key> [--note "what it is"]
```

The start of the key is enough when it matches only one registered server (8 characters or more), and a whole key
works even while that server is offline. Within a few seconds that host's servers move to the **Reborn** tab, marked
**TRUSTED COMMUNITY SERVER** under the name in the list and on their card ("A trusted community server, listed with
ours by TRG Studios."), on Home's Quick Play line, as TRUSTED in the news window's Reborn servers box, in the box
before joining, and on the website's server list. The official servers say **REBORN OFFICIAL SERVER** in the same
places.

A promoted server must still pass the build check: one that is hidden (not an official package) or on the block list
is never shown, promoted or not. Blocking a promoted host takes it off the list within a minute.

### Demote

```
python\python.exe server\manager.py demote <host key>
```

The start of the key (8 characters or more) is enough. Within a few seconds its servers are back on the Community
tab.

### List

```
python\python.exe server\manager.py promoted
```

Each promoted host with the date you promoted it, your note, and what its servers are doing now.

### Where the list lives

`promoted_servers.json` in the official set's folder, beside `blocked_servers.json` and `server.json`. The manager's
commands write it; the backend reads it again within a few seconds of a change, without a restart. **A deploy to a
new folder must copy it** with `server.json` and `blocked_servers.json`, or the promoted servers go back to the
Community tab. It looks like this, and can be edited by hand:

```json
{"servers": [{"key": "<host key>", "note": "Sugma's EU server", "since": "2026-11-20 18:00:00 UTC"}]}
```

### What promotion does not change

A promoted server stays a community server inside the backend: its players' stats are not counted as an official
server's, and `--to official` broadcasts do not go to it. Games before v0.4.7 show it on the Reborn tab like the
official servers, without the Trusted Community Server mark (once v0.4.7 is out, the version gate keeps those games
off the servers).

## Tests

`python -I tools/test_network.py` runs both features on this PC's loopback addresses with a test key made for the
run (never the real one): promote, demote, the website feed, broadcasts to official servers and to everyone, forged,
changed, old, replayed and too many broadcasts, a community server with no key, and a 0.4.6.7 server ignoring them.
