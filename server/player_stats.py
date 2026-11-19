"""Each player's stats as the servers count them (TekRantGaming, 16 Nov 2026: "expand upon what stats we collect like
kills and K/D"), for Stats & Rewards and the Leaderboards.

The dedicated servers count them from the game's own bookkeeping (bf/src/server_stats.cpp) and send the new counts
every few seconds over their backend link: {"op": "player_stats", "rows": [{"name", "kills", ...}, ...]}. Only servers
we trust count (official ones, or every server on a test backend with --player-stats-from all): a player's own game
never sends these, so nobody types their own K/D.

Kept by name (a protected name is its account's), all time, this month and this week, in one JSON file.
    python -I tools/test_player_stats.py   (the checks)
"""
import datetime
import json
import os
import time

# The counters a server sends, each a whole number to add (at most MAX_STEP in one message).
COUNTS = ('kills', 'deaths', 'teamkills', 'suicides', 'headshots', 'melee', 'assists', 'captures', 'defends',
          'repairs', 'heals', 'score', 'rounds', 'wins', 'losses', 'seconds',
          'kit_infantry', 'kit_rifleman', 'kit_scout',              # seconds as each kit
          'veh_plane', 'veh_tank', 'veh_jeep', 'veh_boat',          # seconds in each vehicle
          'vk_plane', 'vk_tank', 'vk_jeep', 'vk_boat')              # kills from each vehicle
MAPS = ('wake', 'iwo', 'guadalcanal', 'coral')
MAP_COUNTS = ('rounds', 'wins', 'kills', 'deaths')
MAX_STEP = 100000  # one message: a few seconds of play, never more than this
MAX_NAMES = 20000
PERIODS = ('all', 'month', 'week')
# Leaderboard columns that can be sorted on (kd: kills over deaths, 50 kills at least).
SORTS = ('score', 'kills', 'kd', 'captures', 'seconds', 'headshots', 'vk_plane', 'vk_tank', 'wins')


def period_ids(now=None):
    d = datetime.datetime.fromtimestamp(time.time() if now is None else now, datetime.timezone.utc)
    year, week, _ = d.isocalendar()
    return {'month': '%04d-%02d' % (d.year, d.month), 'week': '%04d-W%02d' % (year, week)}


TM = '\u2122'  # the trademark sign a soldier name may end with (since v0.4.7.4: ZALIAS plays as "ZALIAS\u2122")


def clean_name(name):
    """A name as players' games and the backend keep it: letters, digits, spaces and underscores, and one trademark
    sign kept at its end, at most 15 bytes in UTF-8 (the game's own name buffers: bf/src/player_name.h)."""
    raw = str(name)
    core = ''.join(c for c in raw if c.isalnum() or c in ' _')[:15].strip()
    if TM in raw and core:
        return core[:12].rstrip() + TM
    return core


def empty():
    return {k: 0 for k in COUNTS}


class PlayerStats:
    def __init__(self, path=None):
        self.path = path
        self.rows = {}  # name lower -> {'name', 'all': {...}, 'month': {'id', ...}, 'week': {'id', ...}, 'maps', 'best_streak', 'at'}
        self.dirty = False
        self.saved_at = 0.0
        if path and os.path.exists(path):
            try:
                with open(path, encoding='utf-8') as f:
                    data = json.load(f)
                for row in data.get('rows', []):
                    if isinstance(row, dict) and clean_name(row.get('name', '')):
                        self.rows[clean_name(row['name']).lower()] = self._fix(row)
            except (OSError, ValueError):
                pass

    @staticmethod
    def _fix(row):
        out = {'name': clean_name(row.get('name', '')), 'at': int(row.get('at', 0) or 0),
               'best_streak': int(row.get('best_streak', 0) or 0)}
        for p in PERIODS:
            src = row.get(p) if isinstance(row.get(p), dict) else {}
            out[p] = {k: int(src.get(k, 0) or 0) for k in COUNTS}
            if p != 'all':
                out[p]['id'] = str(src.get('id', ''))
        maps = row.get('maps') if isinstance(row.get('maps'), dict) else {}
        out['maps'] = {m: {k: int((maps.get(m) or {}).get(k, 0) or 0) for k in MAP_COUNTS} for m in MAPS}
        return out

    def _row(self, name):
        key = name.lower()
        row = self.rows.get(key)
        if row is None:
            if len(self.rows) >= MAX_NAMES:
                return None
            row = self._fix({'name': name})
            self.rows[key] = row
        row['name'] = name
        ids = period_ids()
        for p in ('month', 'week'):
            if row[p].get('id') != ids[p]:
                row[p] = dict(empty(), id=ids[p])
        return row

    def add(self, rows):
        """A server's message: each player's new counts since its last. Returns how many rows counted."""
        if not isinstance(rows, list):
            return 0
        counted = 0
        for r in rows[:64]:
            if not isinstance(r, dict):
                continue
            name = clean_name(r.get('name', ''))
            if not name:
                continue
            step = {}
            for k in COUNTS:
                v = r.get(k, 0)
                if type(v) is not int or not 0 <= v <= MAX_STEP:
                    step = None
                    break
                if v:
                    step[k] = v
            if step is None:
                continue
            m = str(r.get('map', ''))
            row = self._row(name)
            if row is None:
                continue
            for k, v in step.items():
                for p in PERIODS:
                    row[p][k] += v
            if m in MAPS:
                for k in MAP_COUNTS:
                    v = step.get(k, 0)
                    row['maps'][m][k] += v
            streak = r.get('streak', 0)
            if type(streak) is int and 0 < streak <= 1000:
                row['best_streak'] = max(row['best_streak'], streak)
            row['at'] = int(time.time())
            counted += 1
        if counted:
            self.dirty = True
            self.save()
        return counted

    def rename(self, old, new):
        """An account moved to another name (plasma.py move_name): its stats move with it."""
        row = self.rows.pop(old.lower(), None)
        if not row:
            return
        other = self.rows.get(new.lower())
        if other and other['all'].get('score', 0) > row['all'].get('score', 0):
            row = other
        row['name'] = new
        self.rows[new.lower()] = row
        self.dirty = True
        self.save(force=True)

    def save(self, force=False):
        if not self.path or not self.dirty:
            return
        if not force and time.time() - self.saved_at < 10:
            return
        try:
            with open(self.path + '.tmp', 'w', encoding='utf-8') as f:
                json.dump({'rows': list(self.rows.values())}, f)
            os.replace(self.path + '.tmp', self.path)
            self.dirty = False
            self.saved_at = time.time()
        except OSError:
            pass

    # ---- reading

    def totals(self, name, period='all'):
        row = self.rows.get(clean_name(name).lower())
        if not row:
            return None
        if period != 'all':
            ids = period_ids()
            if row[period].get('id') != ids[period]:
                return empty()
        return {k: row[period].get(k, 0) for k in COUNTS}

    @staticmethod
    def kd(t):
        return round(t['kills'] / max(1, t['deaths']), 2)

    def board(self, sort='score', period='all', count=50, me='', fallback=None):
        """The leaderboard: rows by `sort` for the period; `fallback` (name lower -> {'score', 'seconds', 'captures'},
        the players' own totals from before the servers counted) fills in the players the servers have not seen."""
        sort = sort if sort in SORTS else 'score'
        period = period if period in PERIODS else 'all'
        ids = period_ids()
        table = {}
        for key, row in self.rows.items():
            if period != 'all' and row[period].get('id') != ids[period]:
                continue
            t = {k: row[period].get(k, 0) for k in COUNTS}
            if not any(t[k] for k in ('score', 'kills', 'deaths', 'seconds', 'captures')):
                continue
            table[key] = dict(t, name=row['name'])
        if period == 'all' and fallback:
            for key, e in fallback.items():
                old = table.get(key)
                if old is None:
                    table[key] = dict(empty(), name=e.get('name', key), score=int(e.get('score', 0)),
                                      seconds=int(e.get('seconds', 0)), captures=int(e.get('captures', 0)))
                else:  # the larger of the two, as the servers began counting after the players' own totals
                    for k in ('score', 'seconds', 'captures'):
                        old[k] = max(old[k], int(e.get(k, 0)))
        rows = []
        for key, t in table.items():
            if sort == 'kd' and t['kills'] < 50:
                continue
            t['kd'] = self.kd(t)
            rows.append(t)
        rows.sort(key=lambda t: (-t[sort], t['name'].lower()))
        out = {'rows': [self.public(t, i + 1) for i, t in enumerate(rows[:count])], 'total': len(rows),
               'sort': sort, 'period': period}
        me_key = clean_name(me).lower()
        if me_key:
            for i, t in enumerate(rows):
                if t['name'].lower() == me_key:
                    out['me'] = self.public(t, i + 1)
                    break
        return out

    @staticmethod
    def public(t, place):
        keep = ('name', 'score', 'kills', 'deaths', 'kd', 'captures', 'seconds', 'headshots', 'wins', 'rounds',
                'vk_plane', 'vk_tank')
        return dict({k: t.get(k, 0) for k in keep}, place=place)

    def card(self, name, fallback=None):
        """One player's full record (Stats & Rewards, and a leaderboard row's card): every count, by map, the best
        streak and the player's place on each board."""
        key = clean_name(name).lower()
        row = self.rows.get(key)
        out = {'name': clean_name(name), 'found': bool(row)}
        t = {k: row['all'].get(k, 0) for k in COUNTS} if row else empty()
        if fallback and key in fallback:
            for k in ('score', 'seconds', 'captures'):
                t[k] = max(t[k], int(fallback[key].get(k, 0)))
        out.update(t)
        out['kd'] = self.kd(t)
        out['best_streak'] = row['best_streak'] if row else 0
        out['maps'] = row['maps'] if row else {m: {k: 0 for k in MAP_COUNTS} for m in MAPS}
        week = self.totals(name, 'week') or empty()
        out['week_kd'] = self.kd(week) if week['kills'] or week['deaths'] else None
        places = {}
        for sort in ('score', 'kills', 'kd', 'captures', 'headshots', 'vk_plane', 'vk_tank'):
            b = self.board(sort, 'all', 0, name, fallback if sort in ('score', 'captures') else None)
            places[sort] = [b['me']['place'], b['total']] if 'me' in b else None
        out['places'] = places
        return out
