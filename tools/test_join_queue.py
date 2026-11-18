"""The join queue without the game (server/plasma.py QUEUE, 0.4.7.4): a backend of its own on 127.0.0.5 (ports 18641,
18646, 18650; nothing of a running backend is touched), servers registered by hand, players from loopback addresses of
their own. Checks a full server's queue for games that say "queue": 1 (places in order, the same place asked again, its
turn, nobody past a waiting player, Close, a place given up when its game stops asking, a pick of another server, a
server that registers again), games without the queue (still "full", never a word read as a yes), the list's queue
counts, and the slot held for a player who got the server's address already (server_full, in process).

    python -I tools/test_join_queue.py
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'server'))
import plasma  # noqa: E402

PY = sys.executable
HOST, FESL, THEATER, REG = '127.0.0.5', 18641, 18646, 18650
NEW = {'version': '0.4.7.4', 'platform': 'windows', 'notice': 1, 'queue': 1}  # a game with the queue
OLD = {'version': '0.4.7.3', 'platform': 'windows', 'notice': 1}              # a 0.4.7.3 game
FAILS = []


def check(what, ok, detail=''):
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:400]))
    if not ok:
        FAILS.append(what)


def ask(obj, source):
    """One registry request from `source` (a loopback address of its own): the raw reply line."""
    s = socket.socket()
    s.settimeout(5)
    s.bind((source, 0))
    s.connect((HOST, REG))
    s.sendall((json.dumps(obj) + '\n').encode())
    line = s.makefile().readline()
    s.close()
    return line


def askj(obj, source):
    return json.loads(ask(obj, source))


A, B, C, D, E = '127.0.0.11', '127.0.0.12', '127.0.0.13', '127.0.0.14', '127.0.0.15'


def in_process():
    """server_full's count of players on their way (no backend: plasma's own tables)."""
    now = time.time()
    plasma.GAMES.clear(), plasma.PICKS.clear(), plasma.ARRIVED.clear(), plasma.QUEUE.clear()
    plasma.GAMES['x'] = {'GID': 'x', 'MAX-PLAYERS': 2, 'AP': 1, 'status_at': now}
    # A picked 5 s ago with one player in; its game got the address 4 s ago and the host has counted it since; then
    # another player left (still 1). Until 0.4.7.4 this counted A on its way: full for nobody.
    plasma.PICKS['1.0.0.1'] = ('x', now - 5, 1, plasma.JOIN_WINDOW)
    plasma.ARRIVED['1.0.0.1'] = ('x', now - 4)
    full, players, arriving, most = plasma.server_full('x', '1.0.0.9')
    check('a pick whose game has the address and was counted is not on its way', not full and arriving == 0,
          (full, players, arriving, most))
    plasma.GAMES['x']['status_at'] = now - 3.5  # the host's last status came before the player could be in its count
    full, players, arriving, most = plasma.server_full('x', '1.0.0.9')
    check('a pick whose game has the address but no status since is still on its way', full and arriving == 1,
          (full, players, arriving, most))
    plasma.ARRIVED.clear()
    plasma.GAMES['x']['status_at'] = now
    full, _, arriving, _ = plasma.server_full('x', '1.0.0.9')
    check('a pick still without the address is on its way', full and arriving == 1, arriving)
    plasma.PICKS['1.0.0.1'] = ('x', now - 40, 1, plasma.JOIN_WINDOW)
    check('a pick older than its window is not', not plasma.server_full('x', '1.0.0.9')[0])
    plasma.PICKS['1.0.0.1'] = ('x', now - 40, 1, plasma.ADMIT_WINDOW)
    check('a player let in from the queue is on its way for its longer window', plasma.server_full('x', '1.0.0.9')[0])
    plasma.PICKS.clear()
    plasma.QUEUE.append({'token': 't', 'gid': 'x', 'address': '1.0.0.2', 'server': ('h', 1), 'since': now, 'seen': now})
    check('a waiting player takes the free place from a player not waiting', plasma.server_full('x', '1.0.0.9')[0])
    check('and not from itself (first in the queue)', not plasma.server_full('x', '1.0.0.2', 0)[0])
    plasma.GAMES.clear(), plasma.QUEUE.clear()


def main():
    in_process()
    tmp = tempfile.mkdtemp(prefix='bf-queue-')
    backend = subprocess.Popen([PY, '-I', os.path.join(ROOT, 'server', 'plasma.py'), '--host', HOST, '--port', str(FESL),
                                '--theater-port', str(THEATER), '--registry-port', str(REG), '--login-delay', '0',
                                '--no-create', '--log', os.path.join(tmp, 'plasma.log')],
                               cwd=ROOT, stdout=open(os.path.join(tmp, 'plasma.out'), 'w'), stderr=subprocess.STDOUT)
    servers = {}
    try:
        for _ in range(50):
            try:
                socket.create_connection((HOST, REG), timeout=1).close()
                break
            except OSError:
                time.sleep(0.2)

        def register(name, port, most, players):
            s = socket.create_connection((HOST, REG), timeout=5)
            s.sendall((json.dumps({'op': 'register', 'name': name, 'port': port, 'map': 'Wake Island',
                                   'max_players': most, 'tick': 60}) + '\n').encode())
            f = s.makefile()
            reply = json.loads(f.readline())
            servers[name] = (s, f)
            status(name, players)
            return reply['gid']

        def status(name, players):
            servers[name][0].sendall((json.dumps({'op': 'status', 'players': players, 'map': 'Wake Island'}) +
                                      '\n').encode())
            time.sleep(0.3)

        def drop(name):  # the server's link ends (its file too, or the socket stays open)
            s, f = servers.pop(name)
            f.close(), s.close()
            time.sleep(0.5)

        full = register('Full one', 18660, 2, 2)
        other = register('Other one', 18661, 24, 3)
        time.sleep(0.3)

        raw = ask(dict(OLD, op='reserve', gid=full), C)
        check('a game without the queue: full, as before', json.loads(raw)['op'] == 'full', raw)
        a = askj(dict(NEW, op='reserve', gid=full), A)
        check('a game with the queue: a place, 1 of 1', a['op'] == 'queued' and (a['position'], a['total']) == (1, 1), a)
        raw = ask(dict(NEW, op='reserve', gid=full), B)
        b = json.loads(raw)
        check('the next one: 2 of 2', b['op'] == 'queued' and (b['position'], b['total']) == (2, 2), b)
        check('no queue reply holds the word a 0.4.6 game reads as a yes', 'reserved' not in raw, raw)
        again = askj(dict(NEW, op='reserve', gid=full), A)
        check('asking again keeps the place', again['op'] == 'queued' and again['token'] == a['token'] and
              again['position'] == 1, again)
        p = askj({'op': 'queue', 'gid': full, 'token': b['token']}, B)
        check('the queue op says the place', p['op'] == 'queued' and (p['position'], p['total']) == (2, 2), p)
        p = askj({'op': 'queue', 'gid': full, 'token': b['token']}, A)
        check('a token from another address is no place', p['op'] == 'queue_lost', p)
        listed = askj(dict(NEW, op='list'), C)
        row = [s for s in listed['servers'] if s['gid'] == full]
        check('the list says how many wait', row and row[0].get('queue') == 2, row)
        raw = ask(dict(OLD, op='reserve', gid=full), C)
        check('a game without the queue is told players wait', json.loads(raw)['op'] == 'full' and
              '2 players are waiting' in raw and 'reserved' not in raw, raw)

        status('Full one', 1)  # a place opens
        p = askj({'op': 'queue', 'gid': full, 'token': b['token']}, B)
        check('the second in the queue does not get the first one\'s place', p['op'] == 'queued' and p['position'] == 2, p)
        raw = ask(dict(OLD, op='reserve', gid=full), C)
        check('nor does a game without the queue', json.loads(raw)['op'] == 'full', raw)
        raw = ask(dict(NEW, op='reserve', gid=full), D)
        d = json.loads(raw)
        check('nor a new pick with the queue: it waits at the end, 3 of 3', d['op'] == 'queued' and
              (d['position'], d['total']) == (3, 3), d)
        p = askj({'op': 'queue', 'gid': full, 'token': a['token']}, A)
        check('the first one\'s turn: reserved', p['op'] == 'reserved' and p['gid'] == full, p)
        p = askj({'op': 'queue', 'gid': full, 'token': b['token']}, B)
        check('the others move up while it is on its way (1 of 2)', p['op'] == 'queued' and
              (p['position'], p['total']) == (1, 2), p)
        status('Full one', 2)  # it arrived

        p = askj({'op': 'queue_leave', 'token': b['token']}, B)
        check('Close leaves the queue', p['op'] == 'queue_left', p)
        p = askj({'op': 'queue', 'gid': full, 'token': d['token']}, D)
        check('the one behind moves up (1 of 1)', p['op'] == 'queued' and (p['position'], p['total']) == (1, 1), p)
        p = askj({'op': 'queue', 'gid': full, 'token': b['token']}, B)
        check('a place left is gone', p['op'] == 'queue_lost', p)

        e = askj(dict(NEW, op='reserve', gid=full), E)
        check('E waits 2 of 2', e['op'] == 'queued' and e['position'] == 2, e)
        p = askj(dict(NEW, op='reserve', gid=other), E)
        check('a pick of another server with room: reserved, and the queue left', p['op'] == 'reserved', p)
        p = askj({'op': 'queue', 'gid': full, 'token': e['token']}, E)
        check('its old place is gone', p['op'] == 'queue_lost', p)

        # A server whose link drops and registers again (a new game id): its queue waits on for it.
        drop('Full one')
        p = askj({'op': 'queue', 'gid': full, 'token': d['token']}, D)
        check('while its server is away the place stays', p['op'] == 'queued' and p['position'] == 1, p)
        full2 = register('Full one', 18660, 2, 2)
        p = askj({'op': 'queue', 'gid': full, 'token': d['token']}, D)
        check('the server registered again: the place is in its new game', p['op'] == 'queued' and p['gid'] == full2, p)

        # A place whose game stops asking: given up after QUEUE_TIMEOUT.
        print('waiting %d s for a place to time out' % (plasma.QUEUE_TIMEOUT + 2))
        time.sleep(plasma.QUEUE_TIMEOUT + 2)
        listed = askj(dict(NEW, op='list'), C)
        row = [s for s in listed['servers'] if s['gid'] == full2]
        check('the game that stopped asking lost its place', row and row[0].get('queue') == 0, row)
        p = askj({'op': 'queue', 'gid': full2, 'token': d['token']}, D)
        check('and is told so (it asks with reserve again)', p['op'] == 'queue_lost', p)

        drop('Other one')
        p = askj({'op': 'queue', 'gid': other, 'token': 'nope'}, D)
        check('asking for a place in a server that is gone: its words', p['op'] == 'gone' and p.get('message'), p)
        log = open(os.path.join(tmp, 'plasma.log'), encoding='utf-8').read()
        for words in ('queued for game', 'let in to game', 'left the queue of game', 'gave up its place'):
            check('the log says "%s"' % words, words in log)
    finally:
        for s, f in servers.values():
            f.close(), s.close()
        backend.kill()
    print('backend log:', os.path.join(tmp, 'plasma.log'))
    print('%d failed' % len(FAILS) if FAILS else 'all passed')
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
