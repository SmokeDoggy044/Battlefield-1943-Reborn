"""Network broadcasts and promoted community servers without the game (server/gmhost.py NetworkCheck, sign_network;
server/plasma.py Promoted, Registry.relay; server/manager.py broadcast --to, promote, demote, promoted, community).

Everything on this PC's loopback: a backend of its own on 127.0.0.76 (FESL 18241, Theater 18246, registry 18250 on
that address only; nothing of another backend is touched) with a TEST official key made here (never the real one),
the build check's manifests for two fake server packages, and the promoted and blocked lists in a data folder of its
own. Hosts (gmhost.py, no game behind them): an official one, community A (its package an official build, trusts the
test broadcast key), community B (an official build, knows no broadcast key: what a community server does until the
key is in gmhost.py), community C (a build with no manifest: hidden), and community O, the released v0.4.6.7 host
(origin/main's gmhost.py). Then a tiny registry of our own feeds host F forged, replayed and old messages directly.

    python -I tools/test_network.py [--keep] [--hold=<s>]   (--keep: leave the folder for a look; --hold: keep
        everything running that long at the end, for the manager's page; about 6 minutes in all)
"""
import json
import re
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'server'))
import gmhost  # noqa: E402

PY = sys.executable
HOST = '127.0.0.76'
REG = 18250
NEW = {'version': '0.4.7', 'platform': 'windows', 'notice': 1}
FAILS = []


def check(what, ok, detail=''):
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:500]), flush=True)
    if not ok:
        FAILS.append(what)


def ask(obj, host=HOST, port=REG):
    s = socket.create_connection((host, port), timeout=10)
    s.sendall((json.dumps(obj) + '\n').encode())
    line = s.makefile('rb').readline()
    s.close()
    return json.loads(line) if line else None


def listed():
    return {s['name']: s for s in ask(dict(NEW, op='list'))['servers']}


def wait_for(what, test, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            if test():
                return True
        except (OSError, ValueError, KeyError, TypeError):
            pass
        time.sleep(0.5)
    return False


def read(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def package(base, name, version, server_files):
    """A fake server package: bin, python, server (these gmhost.py files), version.txt."""
    root = os.path.join(base, name)
    for sub in ('bin', 'python', 'server'):
        os.makedirs(os.path.join(root, sub), exist_ok=True)
    with open(os.path.join(root, 'bin', 'bf1943.exe'), 'w') as f:
        f.write('not a game: %s\n' % name)
    with open(os.path.join(root, 'python', 'python.txt'), 'w') as f:
        f.write('python stand in\n')
    for target, text in server_files.items():
        with open(os.path.join(root, 'server', target), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    with open(os.path.join(root, 'version.txt'), 'w') as f:
        f.write(version + '\n')
    return root


def manifest(root, out):
    with open(os.path.join(root, 'version.txt')) as f:
        version = f.read().strip()
    files = gmhost.build_files(root)
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'version': version, 'platform': 'windows', 'digest': gmhost.build_digest(files), 'files': files}, f)


def main():
    keep = '--keep' in sys.argv
    base = tempfile.mkdtemp(prefix='bf_network_')
    data = os.path.join(base, 'data')
    os.makedirs(os.path.join(data, 'attest'))
    os.makedirs(os.path.join(data, 'logs'))
    official_key = os.urandom(32).hex().encode()  # a TEST key, made here
    with open(os.path.join(data, 'official.key'), 'wb') as f:
        f.write(official_key)
    test_public = gmhost.broadcast_public(official_key)
    promoted_file = os.path.join(data, 'promoted_servers.json')
    blocked_file = os.path.join(data, 'blocked_servers.json')

    here = {n: read(os.path.join(ROOT, 'server', n)) for n in ('gmhost.py', 'ed25519.py')}
    # The released v0.4.6.7 server package's host (BF_TEST_OLD_SERVER: its server folder; by default the release
    # folder of the dev checkout beside this one). Its gmhost.py has the build check and host key, no network broadcasts.
    old_dir = os.environ.get('BF_TEST_OLD_SERVER') or os.path.join(
        ROOT, '..', '..', '..', '..', 'BF-1942-MP-Decomp', 'runs', 'release', '0.4.6.7',
        'BF1943-Reborn-Server-0.4.6.7-windows', 'server')
    old, old_ed = read(os.path.join(old_dir, 'gmhost.py')), read(os.path.join(old_dir, 'ed25519.py'))
    check('the released 0.4.6.7 host is there to test with (%s)' % os.path.abspath(old_dir),
          'host-key-file' in old and 'network_broadcast' not in old, 'set BF_TEST_OLD_SERVER')
    new_pkg = package(base, 'pkg047', '0.4.7-test', here)
    old_pkg = package(base, 'pkg0467', '0.4.6.7', {'gmhost.py': old, 'ed25519.py': old_ed})
    odd_pkg = package(base, 'pkgodd', '0.4.7-test', dict(here, **{'extra.py': '# a changed build\n'}))
    manifest(new_pkg, os.path.join(data, 'attest', '0.4.7-test-windows.json'))
    manifest(old_pkg, os.path.join(data, 'attest', '0.4.6.7-windows.json'))

    procs = []

    def start(args, out, env=None):
        p = subprocess.Popen(args, stdout=open(out, 'w'), stderr=subprocess.STDOUT, env=env or dict(os.environ),
                             creationflags=0x08000000 if os.name == 'nt' else 0)
        procs.append(p)
        return p

    backend_log = os.path.join(base, 'backend.log')
    start([PY, '-I', os.path.join(ROOT, 'server', 'plasma.py'), '--host', HOST, '--theater-host', HOST,
           '--log', backend_log, '--official-key-file', os.path.join(data, 'official.key'),
           '--check-servers', os.path.join(data, 'attest'), '--blocked-file', blocked_file,
           '--promoted-file', promoted_file], os.path.join(base, 'backend.out'))
    check('the test backend answers', wait_for('backend', lambda: ask({'op': 'list'}) is not None, 20))

    keys = {}

    def host(tag, pkg, port, official=False, trust=True):
        seed = os.urandom(32).hex()
        key_file = os.path.join(base, 'host_%s.key' % tag)
        with open(key_file, 'w') as f:
            f.write(seed)
        keys[tag] = gmhost._ed25519().public_key(bytes.fromhex(seed)).hex()
        env = dict(os.environ, BF_SERVER_ROOT=pkg)
        env.pop('BF_DEV_BROADCAST_KEY', None)
        if trust:
            env['BF_DEV_BROADCAST_KEY'] = test_public
        args = [PY, '-I', os.path.join(pkg, 'server', 'gmhost.py'), '--backend', '%s:%d' % (HOST, REG),
                '--listen', '127.0.0.%d:%d' % (77, port), '--game-server', '127.0.0.77:9',
                '--name', 'Test %s' % tag, '--host-key-file', key_file, '--log', os.path.join(base, 'host_%s.log' % tag),
                '--status-file', os.path.join(base, 'status_%s.json' % tag)]
        if official:
            args += ['--official-key-file', os.path.join(data, 'official.key')]
        start(args, os.path.join(base, 'host_%s.out' % tag), env)

    host('official', new_pkg, 18771, official=True, trust=False)
    host('A', new_pkg, 18772)
    host('B', new_pkg, 18773, trust=False)
    host('C', odd_pkg, 18774)
    host('O', old_pkg, 18775)
    ok = wait_for('hosts', lambda: all('registered with the backend' in read(os.path.join(base, 'host_%s.log' % t))
                                       for t in ('official', 'A', 'B', 'C', 'O')), 40)
    check('five hosts registered (official, A, B, C, and O on the released 0.4.6.7 host)', ok,
          [t for t in ('official', 'A', 'B', 'C', 'O') if 'registered' not in read(os.path.join(base, 'host_%s.log' % t))])
    time.sleep(2)

    # ---- the list before anything is promoted
    s = listed()
    check('official listed as Reborn, not promoted', s.get('Test official', {}).get('official') is True and
          not s['Test official'].get('promoted'), s.get('Test official'))
    check('A, B and O listed as community', all(s.get('Test %s' % t, {}).get('official') is False for t in 'ABO'), s)
    check('C (a changed build) hidden by the build check', 'Test C' not in s, list(s))

    # ---- the manager's commands, with a manager of the official package (no servers started)
    mgr_root = os.path.join(base, 'mgr')
    shutil.copytree(os.path.join(ROOT, 'server'), os.path.join(mgr_root, 'server'),
                    ignore=shutil.ignore_patterns('__pycache__'))
    with open(os.path.join(mgr_root, 'package.json'), 'w') as f:
        json.dump({'official': True}, f)
    with open(os.path.join(mgr_root, 'version.txt'), 'w') as f:
        f.write('0.4.7-test\n')
    menv = dict(os.environ, BF_SERVER_ROOT=mgr_root, BF_SERVER_DATA=data, BF_SERVER_BACKEND_BIND=HOST,
                BF_SERVER_NO_BROWSER='1', BF_SERVER_LOOPBACK_BASE='80')

    def mgr(*args, timeout=60):
        r = subprocess.run([PY, '-I', os.path.join(mgr_root, 'server', 'manager.py')] + list(args), env=menv,
                           capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    out = mgr('broadcast-key')
    check('manager.py broadcast-key prints the public key made from official.key', out == test_public, out)
    out = mgr('community')
    check('manager.py community lists A, B, C and O with their host keys',
          all(keys[t] in out for t in 'ABCO') and keys['official'] not in out, out)
    check('... C marked hidden, the others listed', 'hidden:' in [l for l in out.splitlines() if keys['C'] in l][0], out)

    out = mgr('promote', keys['A'][:12], '--note', 'Test host A')
    check('manager.py promote <start of the key> writes the promoted list', keys['A'] in read(promoted_file), out)
    print('      ' + out)
    check('A moves to the Reborn tab marked promoted within 10 s',
          wait_for('promoted', lambda: listed()['Test A'].get('promoted') is True and listed()['Test A']['official'], 10),
          listed().get('Test A'))
    check('B and O stay community', all(listed()['Test %s' % t]['official'] is False for t in 'BO'))
    found = ask(dict(NEW, op='find', address='127.0.0.77', port=18772))
    check('Direct connect (find) says promoted too', (found.get('server') or {}).get('promoted') is True, found)
    check('the backend log says so', 'promoted: listed with the Reborn servers' in read(backend_log))
    out = mgr('promoted')
    check('manager.py promoted lists A as shown', keys['A'] in out and 'shown as trusted' in out, out)

    out = mgr('promote', keys['C'])
    check('promoting C (hidden) is taken, and the manager says it is not shown', 'Not shown' in out, out)
    time.sleep(6.5)
    check('C stays off the list though promoted (the build check first)', 'Test C' not in listed())
    out = mgr('community')
    check('manager.py community: C promoted, not shown', 'promoted, not shown' in out, out)

    # ---- the website feed against this backend
    feed = os.path.join(ROOT, '..', 'website', 'feed', 'bf1943_server_feed.py')
    feed = os.environ.get('BF_TEST_FEED') or feed
    if os.path.exists(feed):
        feed_out = os.path.join(base, 'servers.json')
        r = subprocess.run([PY, '-I', feed, '--out', feed_out, '--host', HOST, '--once', '--log',
                            os.path.join(base, 'feed.log')], capture_output=True, text=True, timeout=60)
        rows = {x['name']: x for x in json.loads(read(feed_out) or '{}').get('servers', [])}
        check('the website feed: A promoted, official, B community', rows.get('Test A', {}).get('promoted') is True and
              rows.get('Test B', {}).get('promoted') is False and rows.get('Test official', {}).get('promoted') is False,
              (r.stdout, r.stderr, rows))
        with open(os.path.join(base, 'feed_promoted.json'), 'w') as f:
            json.dump(rows, f, indent=1)
    else:
        print('SKIP  the website feed (set BF_TEST_FEED to bf1943_server_feed.py)')

    # ---- network broadcasts through the manager (a manager running, no servers)
    mgr_out = os.path.join(base, 'manager_run.out')
    start([PY, '-I', os.path.join(mgr_root, 'server', 'manager.py'), 'run', '--no-browser', '--no-start', '--port',
           '18293'], mgr_out, menv)
    check('the test manager runs', wait_for('manager', lambda: 'started' in read(os.path.join(data, 'logs',
                                                                                           'manager.log')), 20))
    time.sleep(1)

    def host_log(t):
        return read(os.path.join(base, 'host_%s.log' % t))

    out = mgr('broadcast', '--kind', 'custom', '--text', 'Official only: test one', '--to', 'official')
    check('manager.py broadcast --to official is sent', out.startswith('sent'), out)
    check('the official host shows it', wait_for('o', lambda: 'Official only: test one' in host_log('official'), 10),
          host_log('official')[-400:])
    time.sleep(1.5)
    check('no community host gets an official only message',
          all('Official only: test one' not in host_log(t) for t in 'ABO'))
    check('the backend says 1 official and 0 community servers',
          '1 official and 0 community servers: Official only: test one' in read(backend_log))

    out = mgr('broadcast', '--kind', 'custom', '--text', 'Everyone: test two', '--to', 'everyone')
    check('manager.py broadcast --to everyone is sent', out.startswith('sent'), out)
    check('official and community host A show it',
          wait_for('e', lambda: all('comms: broadcast' in host_log(t) and 'Everyone: test two' in host_log(t)
                                    for t in ('official', 'A')), 10), (host_log('A')[-400:]))
    time.sleep(1.5)
    check('promoted A got it as a community server (everyone), not as official',
          'network broadcast' in host_log('A') and '(to everyone)' in host_log('A'))
    # Since the official key is in gmhost.py (OFFICIAL_BROADCAST_KEYS), B knows that key only: a message signed with
    # this test's key fails its signature check. Before it, B knew no key at all. Either way it must refuse.
    check('B, without the test broadcast key, refuses it (fails closed)',
          'refused: no official broadcast key is known' in host_log('B')
          or 'refused: the signature does not check' in host_log('B'), host_log('B')[-400:])
    check('the released 0.4.6.7 host ignores it and stays registered',
          'Everyone: test two' not in host_log('O') and 'backend link lost' not in host_log('O') and
          'Test O' in listed(), host_log('O')[-400:])
    check('the backend counted 1 official and 4 community servers', '1 official and 4 community servers: Everyone'
          in read(backend_log), read(backend_log)[-600:])

    out = mgr('broadcast', '--kind', 'maintenance', '--minutes', '2', '--to', 'everyone')
    check('a maintenance countdown to everyone starts', 'countdown' in out, out)
    check('its first line reaches A as a maintenance message',
          wait_for('m', lambda: '(maintenance) to 0 players: The Reborn servers go down for maintenance in 2 minutes'
                   in host_log('A'), 10), host_log('A')[-500:])
    st = json.loads(mgr('status') and '{}')
    state = subprocess.run([PY, '-I', '-c', 'import sys,json;sys.path.insert(0,%r);import manager;print(json.dumps('
                            'manager.ask("/api/state")["countdowns"]))' % os.path.join(mgr_root, 'server')],
                           env=menv, capture_output=True, text=True).stdout
    check('the manager shows the countdown, for everyone', '"network": "everyone"' in state, state)
    for cd in json.loads(state or '[]'):
        subprocess.run([PY, '-I', '-c', 'import sys;sys.path.insert(0,%r);import manager;manager.ask('
                        '"/api/broadcast/cancel",{"id":%r})' % (os.path.join(mgr_root, 'server'), cd['id'])], env=menv)
    out = mgr('broadcast', '--kind', 'custom', '--text', 'here only', '--to', 'everyone', '--server', '1')
    check('a network broadcast refuses a server number', 'leave out the server number' in out, out)

    # ---- forged, replayed, old and too many, at the backend
    other = os.urandom(32).hex().encode()
    r = ask(gmhost.sign_network(other, 'everyone', 'custom', 'Forged by someone else'))
    check('a broadcast signed with another key is refused by the backend',
          r.get('ok') is False and 'does not check' in r.get('why', ''), r)
    good = gmhost.sign_network(official_key, 'everyone', 'custom', 'Replay me')
    r1, r2 = ask(good), ask(good)
    check('the same signed broadcast twice: the second refused (a replay)', r1.get('ok') is True and r2.get('ok') is
          False and 'seen before' in r2.get('why', ''), (r1, r2))
    r = ask(gmhost.sign_network(official_key, 'everyone', 'custom', 'Old one', now=time.time() - 400))
    check('a broadcast made 400 s ago is refused (too old)', r.get('ok') is False and 'too old' in r.get('why', ''), r)
    bad = dict(good, signed=good['signed'].replace('Replay me', 'Changed text'))
    r = ask(bad)
    check('a changed text under a good signature is refused', r.get('ok') is False and 'does not check' in r.get('why'), r)
    long_text = 'x' * 250
    signed = 'bf1943-broadcast|1|%s|%d|%d|everyone|custom|10|%s' % (os.urandom(8).hex(), time.time(), time.time() + 60,
                                                                     long_text)
    r = ask({'op': 'network_broadcast', 'signed': signed,
             'sig': gmhost._ed25519().sign(gmhost.broadcast_seed(official_key), signed.encode()).hex()})
    check('a signed text of 250 characters is refused (200 at most)', r.get('ok') is False and 'too long' in r.get('why'), r)
    r = ask(gmhost.sign_admin(other, 'hosts'))
    check('the host key list asked with another key is refused', r.get('ok') is False, r)
    r = ask({'op': 'admin', 'signed': 'bf1943-admin|1|00|0|hosts', 'sig': '00'})
    check('an unsigned host key question is refused', r.get('ok') is False, r)
    time.sleep(1)
    oks = [ask(gmhost.sign_network(official_key, 'official', 'custom', 'Burst %d' % i)).get('ok') for i in range(8)]
    check('more than 6 network broadcasts in a minute: the rest refused', oks.count(False) >= 2, oks)

    # ---- demote and block
    out = mgr('demote', keys['A'][:10])
    check('manager.py demote takes A off the promoted list', keys['A'] not in read(promoted_file), out)
    check('A is back on the Community tab within 10 s', wait_for('d', lambda: listed()['Test A']['official'] is False
                                                                 and not listed()['Test A'].get('promoted'), 10))
    mgr('promote', keys['A'])
    check('promoted again', wait_for('p2', lambda: listed()['Test A'].get('promoted') is True, 10))
    with open(blocked_file, 'w') as f:
        json.dump({'keys': [keys['A']]}, f)
    check('blocked while promoted: off the list within 70 s', wait_for('b', lambda: 'Test A' not in listed(), 70))

    # ---- the host's own check, with a registry of our own feeding it
    fake = socket.socket()
    fake.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    fake.bind(('127.0.0.78', 18779))
    fake.listen(1)
    sends = []

    def registry():
        conn, _ = fake.accept()
        f = conn.makefile('rb')
        f.readline()  # register
        conn.sendall((json.dumps({'op': 'registered', 'gid': '999', 'uid': 1, 'pid': 1, 'official': False,
                                  'listed': True, 'address': '127.0.0.78:18778'}) + '\n').encode())
        time.sleep(1)
        for m in sends:
            conn.sendall((json.dumps(m) + '\n').encode())
            time.sleep(0.4)
        time.sleep(30)
        conn.close()

    valid = gmhost.sign_network(official_key, 'everyone', 'custom', 'Direct valid')
    sends += [gmhost.sign_network(other, 'everyone', 'custom', 'Direct forged'),
              valid, valid,
              gmhost.sign_network(official_key, 'everyone', 'custom', 'Direct old', now=time.time() - 400),
              gmhost.sign_network(official_key, 'official', 'custom', 'Direct official only'),
              dict(valid, signed=valid['signed'].replace('Direct valid', 'Direct changed'))]
    threading.Thread(target=registry, daemon=True).start()
    seed_file = os.path.join(base, 'host_F.key')
    with open(seed_file, 'w') as f:
        f.write(os.urandom(32).hex())
    start([PY, '-I', os.path.join(new_pkg, 'server', 'gmhost.py'), '--backend', '127.0.0.78:18779', '--listen',
           '127.0.0.78:18778', '--game-server', '127.0.0.78:9', '--name', 'Test F', '--host-key-file', seed_file,
           '--log', os.path.join(base, 'host_F.log')], os.path.join(base, 'host_F.out'),
          dict(os.environ, BF_SERVER_ROOT=new_pkg, BF_DEV_BROADCAST_KEY=test_public))
    wait_for('f', lambda: host_log('F').count('network broadcast') >= 6, 25)
    log = host_log('F')
    lines = [l for l in log.splitlines() if 'network broadcast' in l]
    print('      host F: ' + '\n      host F: '.join(l.split(' ', 1)[-1] for l in lines))
    check('host F shows the valid one once', log.count('Direct valid') == 1 and 'comms: broadcast' in log, log[-800:])
    check('host F refuses the forged one', 'refused: the signature does not check' in log and 'Direct forged' not in log)
    check('host F refuses the replay', 'refused: seen before (a replay)' in log)
    check('host F refuses the old one', 'refused: too old' in log and 'Direct old' not in log)
    check('host F (community) refuses an official only one', 'refused: meant for the official servers only' in log)
    check('host F refuses a changed text', log.count('refused: the signature does not check') >= 2)

    # ---- phase 2: the host's own limits, an official host taking official only ones, a clock ahead
    fake2 = socket.socket()
    fake2.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    fake2.bind(('127.0.0.78', 18789))
    fake2.listen(1)
    now = time.time()
    sends2 = [gmhost.sign_network(official_key, 'everyone', 'custom', 'Future one', now=now + 200),
              gmhost.sign_network(official_key, 'official', 'custom', 'Official only, to an official host')]
    sends2 += [gmhost.sign_network(official_key, 'everyone', 'custom', 'Quick %d' % i) for i in range(6)]

    def registry2():
        conn, _ = fake2.accept()
        conn.makefile('rb').readline()
        conn.sendall((json.dumps({'op': 'registered', 'gid': '998', 'uid': 1, 'pid': 1, 'official': True,
                                  'listed': True, 'address': '127.0.0.78:18788'}) + '\n').encode())
        time.sleep(1)
        for m in sends2:
            conn.sendall((json.dumps(m) + '\n').encode())
            time.sleep(0.2)
        time.sleep(20)
        conn.close()

    threading.Thread(target=registry2, daemon=True).start()
    with open(os.path.join(base, 'host_G.key'), 'w') as f:
        f.write(os.urandom(32).hex())
    start([PY, '-I', os.path.join(new_pkg, 'server', 'gmhost.py'), '--backend', '127.0.0.78:18789', '--listen',
           '127.0.0.78:18788', '--game-server', '127.0.0.78:9', '--name', 'Test G', '--host-key-file',
           os.path.join(base, 'host_G.key'), '--log', os.path.join(base, 'host_G.log')],
          os.path.join(base, 'host_G.out'), dict(os.environ, BF_SERVER_ROOT=new_pkg, BF_DEV_BROADCAST_KEY=test_public))
    wait_for('g', lambda: host_log('G').count('network broadcast') >= 8, 25)
    log = host_log('G')
    check('host G refuses one made 200 s in the future (its clock)', re.search(r'refused: made 19[0-9] s in the future', log) is not None, log[-900:])
    check('host G (registered as official) shows an official only one', 'Official only, to an official host' in log)
    check('host G shows 6 in a minute, then refuses the next (too many)',
          log.count('comms: broadcast') == 6 and 'refused: too many in a minute' in log and 'Quick 5' not in log,
          log[-900:])

    # ---- phase 3: countdowns and the network wording, through the running manager (a fresh minute first)
    time.sleep(61)
    with open(blocked_file, 'w') as f:
        json.dump({}, f)
    out = mgr('broadcast', '--kind', 'custom', '--text', 'Custom countdown test, {time} left', '--minutes', '0.7',
              '--to', 'everyone')
    check('a custom network countdown of 42 s starts', 'countdown' in out, out)
    check('it is said at the start, at 30 s and at 10 s, on the official host and on community host B',
          wait_for('cd', lambda: all(host_log('official').count('Custom countdown test') == 3 for _ in [0]), 60),
          [l for l in host_log('official').splitlines() if 'Custom countdown' in l])
    lines = [l.split(': ', 2)[-1] for l in host_log('official').splitlines() if 'comms: broadcast' in l and 'Custom countdown' in l]
    check('... with the time in words: 42 seconds, 30 seconds, 10 seconds',
          [x.split(', ')[-1] for x in lines] == ['42 seconds left', '30 seconds left', '10 seconds left'], lines)
    check('... and the backend counted community servers for it too',
          'community servers: Custom countdown test, 30 seconds left' in read(backend_log))
    def manager_state():
        return json.loads(subprocess.run([PY, '-I', '-c', 'import sys,json;sys.path.insert(0,%r);import manager;print('
                                          'json.dumps(manager.ask("/api/state")))' % os.path.join(mgr_root, 'server')],
                                         env=menv, capture_output=True, text=True).stdout or '{}')

    wait_for('end', lambda: not manager_state().get('countdowns'), 20)  # the 10 s mark came about 11 s before its end
    st = manager_state()
    check('the countdown is gone when its time is up, and nothing was restarted',
          not st.get('countdowns') and 'restarting' not in read(os.path.join(data, 'logs', 'manager.log')), st.get('countdowns'))
    check('the page state says the manager can send network broadcasts', st.get('network') is True)
    check('the Sent list names the network message', any(s.get('network', '').startswith('everyone: 1 official')
                                                          for s in st.get('sent', [])), st.get('sent', [])[:2])
    for kind, words in (('restart', 'The Reborn servers restart in 5 minutes'),
                        ('update', 'A new version of Battlefield 1943 Reborn is out. The Reborn servers update in 5 minutes')):
        mgr('broadcast', '--kind', kind, '--minutes', '5', '--to', 'official')
        check('--kind %s --to official: "%s"' % (kind, words),
              wait_for(kind, lambda: words in host_log('official'), 10), host_log('official')[-300:])
    state = json.loads(subprocess.run([PY, '-I', '-c', 'import sys,json;sys.path.insert(0,%r);import manager;print(json.dumps('
                                       'manager.ask("/api/state")["countdowns"]))' % os.path.join(mgr_root, 'server')],
                                      env=menv, capture_output=True, text=True).stdout or '[]')
    check('a network restart countdown never restarts (restart false)', state and all(not c['restart'] and not c['update']
                                                                                     for c in state), state)
    for cd in state:
        subprocess.run([PY, '-I', '-c', 'import sys;sys.path.insert(0,%r);import manager;manager.ask('
                        '"/api/broadcast/cancel",{"id":%r})' % (os.path.join(mgr_root, 'server'), cd['id'])], env=menv)
    check('the countdowns can be cancelled', 'countdown cancelled' in read(os.path.join(data, 'logs', 'manager.log')))
    out = mgr('broadcast', '--kind', 'custom', '--text', '', '--to', 'official')
    check('an empty network text is refused', 'Type the message first' in out, out)

    # ---- phase 4: the promotion commands' edges
    out = mgr('promote', keys['B'][:4])
    check('promote with 4 characters of a key is refused', 'No community server' in out, out)
    out = mgr('promote', 'ffffffffff')
    check('promote with an unknown start is refused', 'No community server' in out, out)
    gid_b = [l.split()[2] for l in mgr('community').splitlines() if keys['B'] in l][0]
    out = mgr('promote', gid_b)
    check('promote by the game number (from manager.py community)', keys['B'] in read(promoted_file), out)
    out = mgr('promote', gid_b)
    check('promoting it twice says so', 'promoted already' in out, out)
    offline = os.urandom(32).hex()
    out = mgr('promote', offline, '--note', 'Offline host')
    check('a whole key of a host that is not online is taken', offline in read(promoted_file), out)
    out = mgr('promoted')
    check('manager.py promoted: the offline one "not registered now", its note kept',
          'Offline host; not registered now' in out, out)
    check('B shows promoted', wait_for('pb', lambda: listed()['Test B'].get('promoted') is True, 10))
    out = mgr('demote', 'abcd')
    check('demote with 4 characters is refused', 'No promoted host' in out, out)
    mgr('demote', offline)
    with open(promoted_file, 'w') as f:
        f.write('{not json')
    time.sleep(6.5)
    check('a broken promoted file: the backend keeps the last list', listed()['Test B'].get('promoted') is True and
          'not read' in read(backend_log))
    out = mgr('promote', keys['A'])
    check('the manager will not write over a broken file', 'not valid JSON' in out, out)
    with open(promoted_file, 'w') as f:
        json.dump({'servers': [{'key': keys['B']}, {'key': keys['A']}]}, f)

    # ---- phase 5: the backend starts again: the promoted list and the hosts come back
    procs[0].kill()
    procs[0].wait(5)
    time.sleep(1)
    start([PY, '-I', os.path.join(ROOT, 'server', 'plasma.py'), '--host', HOST, '--theater-host', HOST,
           '--log', backend_log, '--official-key-file', os.path.join(data, 'official.key'),
           '--check-servers', os.path.join(data, 'attest'), '--blocked-file', blocked_file,
           '--promoted-file', promoted_file], os.path.join(base, 'backend2.out'))
    check('after a backend restart the hosts register again and A and B are promoted again',
          wait_for('re', lambda: listed()['Test A'].get('promoted') and listed()['Test B'].get('promoted')
                   and 'Test O' in listed() and listed()['Test official']['official'], 40))
    r = ask(gmhost.sign_network(official_key, 'everyone', 'custom', 'After the restart'))
    check('a broadcast after the restart reaches the hosts again', r.get('ok') and
          wait_for('ar', lambda: 'After the restart' in host_log('A') and 'After the restart' in host_log('official'), 10),
          r)

    # ---- phase 6: other packages: a second official machine, a community package
    us = os.path.join(base, 'us')
    shutil.copytree(mgr_root, us, ignore=shutil.ignore_patterns('__pycache__'))
    with open(os.path.join(us, 'package.json'), 'w') as f:
        json.dump({'official': True, 'main_backend': False}, f)
    us_data = os.path.join(base, 'us_data')
    os.makedirs(us_data)
    shutil.copy(os.path.join(data, 'official.key'), us_data)
    with open(os.path.join(us_data, 'server.json'), 'w') as f:
        json.dump({'backend': HOST}, f)
    uenv = dict(menv, BF_SERVER_ROOT=us, BF_SERVER_DATA=us_data)
    uenv.pop('BF_SERVER_BACKEND_BIND')
    u = subprocess.run([PY, '-I', os.path.join(us, 'server', 'manager.py'), 'community'], env=uenv, capture_output=True,
                       text=True, timeout=60)
    check('a second official machine (main_backend false) asks the main backend of its server.json',
          keys['A'] in u.stdout, u.stdout + u.stderr)
    cm = os.path.join(base, 'cm')
    shutil.copytree(mgr_root, cm, ignore=shutil.ignore_patterns('__pycache__'))
    os.remove(os.path.join(cm, 'package.json'))
    cm_data = os.path.join(base, 'cm_data')
    os.makedirs(cm_data)
    cenv = dict(menv, BF_SERVER_ROOT=cm, BF_SERVER_DATA=cm_data)
    for args, want in ((['promote', keys['A']], 'only works on the official server package'),
                       (['community'], 'only works on the official server package'),
                       (['broadcast-key'], 'only works on the official server package')):
        c = subprocess.run([PY, '-I', os.path.join(cm, 'server', 'manager.py')] + args, env=cenv, capture_output=True,
                           text=True, timeout=60)
        check('a community package refuses manager.py %s' % args[0], want in c.stdout, c.stdout + c.stderr)
    c = subprocess.run([PY, '-I', '-c', 'import sys;sys.path.insert(0,%r);import manager;m=manager.Manager();'
                        'm.broadcast("custom",0,"x",network="everyone")' % os.path.join(cm, 'server')], env=cenv,
                       capture_output=True, text=True, timeout=60)
    check('a community package refuses a network broadcast', 'only works on the official server package' in c.stderr,
          c.stderr[-300:])

    hold = [a for a in sys.argv if a.startswith('--hold=')]
    if hold:
        print('HOLD: manager page http://127.0.0.1:18293/ for %s s, folder %s' % (hold[0][7:], base), flush=True)
        time.sleep(float(hold[0][7:]))

    for p in procs:
        p.kill()
    for p in procs:
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            pass
    fake.close()
    fake2.close()
    print('\n%d failed%s' % (len(FAILS), ': ' + ', '.join(FAILS) if FAILS else ''))
    print('folder: %s' % base if keep else '')
    if not keep:
        shutil.rmtree(base, ignore_errors=True)
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
