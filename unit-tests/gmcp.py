#!/usr/bin/env python3
"""
unit-tests/gmcp.py — GMCP protocol validation suite for NewCirMUD
==================================================================

REQUIREMENTS
    The MUD must be running and accepting connections.
    The login character must already exist (create it in-game first).
    The character should be at least level 2 so gossip/shout channels work.

USAGE
    python3 unit-tests/gmcp.py --user NAME --password PASS [options]

OPTIONS
    --host HOST       MUD hostname or IP           (default: 127.0.0.1)
    --port PORT       MUD port                     (default: 4000)
    --user NAME       Character name to log in as  (required)
    --password PASS   Character password            (required)
    --slow            Include tests that take >10 s (ping waits 65 s)
    --verbose         Print every received GMCP packet as it arrives
    --test NAMES      Comma-separated subset of tests to run
    --timeout SECS    Per-packet wait timeout       (default: 5.0)
    --list            Print available test names and exit

AVAILABLE TESTS
    negotiation     IAC WILL/DO exchange produces Core.Hello with correct fields.
    login_burst     All expected GMCP modules are present after login.
    vitals          Char.Vitals JSON has required integer fields in valid ranges.
    status          Char.Status JSON has required fields with correct types.
    room_info       Room.Info JSON has num/name/zone/terrain and a full exits object.
    room_players    Room.Players is a JSON array.
    afflictions     Char.Afflictions.List is a JSON array.
    discord         External.Discord.Status has game/details/state fields.
    move            Moving a character re-sends Room.Info, Room.Players, Discord.
    channel         Gossiping produces a Comm.Channel.Text packet back to sender.
    goodbye         Quitting sends Core.Goodbye before the TCP connection closes.
    ping            Core.Ping is received within 65 s (requires --slow).

CONNECTION SHARING
    One persistent connection is used for: negotiation, login_burst, vitals,
    status, room_info, room_players, afflictions, discord, move, channel.
    "goodbye" and "ping" each open a fresh connection/login so they do not
    affect the shared session.

EXAMPLES
    # Run all standard tests against a local MUD:
    python3 unit-tests/gmcp.py --user Testplayer --password secret

    # Run schema checks only:
    python3 unit-tests/gmcp.py --user Testplayer --password secret \\
        --test vitals,status,room_info

    # Include the 65-second ping test:
    python3 unit-tests/gmcp.py --user Testplayer --password secret --slow

    # Watch packets arrive while running against a remote host:
    python3 unit-tests/gmcp.py --host mud.example.com --port 4000 \\
        --user Testplayer --password secret --verbose
"""

import argparse
import json
import socket
import sys
import threading
import time

# ---------------------------------------------------------------------------
# Telnet / GMCP constants
# ---------------------------------------------------------------------------
IAC  = 0xFF
WILL = 0xFB
WONT = 0xFC
DO   = 0xFD
DONT = 0xFE
SB   = 0xFA
SE   = 0xF0
GMCP = 0xC9   # Telnet option 201

# IAC parser states
_ST_NORM    = 0
_ST_GOT_IAC = 1
_ST_GOT_CMD = 2   # waiting for option byte after WILL/WONT/DO/DONT
_ST_SB_DATA = 3
_ST_SB_IAC  = 4


# ---------------------------------------------------------------------------
# IAC / GMCP byte-stream splitter
# ---------------------------------------------------------------------------

class _IACSplitter:
    """Incrementally parses a raw Telnet byte stream.

    Call feed(data) with bytes from the socket.  After each call, drain:
        self.text     — bytearray of non-IAC printable bytes
        self.packets  — list of (module_str, json_str) GMCP tuples
    Both lists must be cleared by the caller after inspection.

    Negotiation replies (IAC DO GMCP, IAC DONT X, IAC WONT X) are sent
    automatically via send_fn.
    """

    def __init__(self, send_fn):
        self._send   = send_fn
        self._st     = _ST_NORM
        self._cmd    = 0
        self._sb     = bytearray()
        self.text    = bytearray()
        self.packets = []          # [(module, json_str), ...]

    def feed(self, data: bytes):
        for b in data:
            self._byte(b)

    def _byte(self, b: int):
        st = self._st
        if st == _ST_NORM:
            if b == IAC:
                self._st = _ST_GOT_IAC
            else:
                self.text.append(b)

        elif st == _ST_GOT_IAC:
            if b == IAC:                           # escaped 0xFF in text
                self.text.append(0xFF)
                self._st = _ST_NORM
            elif b == SB:
                self._sb.clear()
                self._st = _ST_SB_DATA
            elif b in (WILL, WONT, DO, DONT):
                self._cmd = b
                self._st  = _ST_GOT_CMD
            else:
                self._st = _ST_NORM               # single-byte command (NOP, GA …)

        elif st == _ST_GOT_CMD:
            if self._cmd == WILL and b == GMCP:
                self._send(bytes([IAC, DO, GMCP]))   # accept GMCP
            elif self._cmd == WILL:
                self._send(bytes([IAC, DONT, b]))    # reject other WILLs
            elif self._cmd == DO:
                self._send(bytes([IAC, WONT, b]))    # we offer nothing
            self._st = _ST_NORM

        elif st == _ST_SB_DATA:
            if b == IAC:
                self._st = _ST_SB_IAC
            else:
                self._sb.append(b)

        elif st == _ST_SB_IAC:
            if b == SE:
                self._dispatch(bytes(self._sb))
                self._sb.clear()
                self._st = _ST_NORM
            elif b == IAC:                         # escaped 0xFF inside SB
                self._sb.append(0xFF)
                self._st = _ST_SB_DATA
            else:
                self._sb.clear()
                self._st = _ST_NORM

    def _dispatch(self, payload: bytes):
        if not payload or payload[0] != GMCP:
            return
        try:
            text = payload[1:].decode('utf-8', errors='replace')
        except Exception:
            return
        parts    = text.split(' ', 1)
        module   = parts[0]
        json_str = parts[1] if len(parts) > 1 else '{}'
        self.packets.append((module, json_str))


# ---------------------------------------------------------------------------
# MUD connection with background reader thread
# ---------------------------------------------------------------------------

class MUDConnection:
    """TCP connection to a MUD with GMCP parsing and a background reader.

    Thread-safe.  All received GMCP packets are appended to an indexed list;
    tests search that list by index range so they cannot accidentally match
    packets from a previous action.
    """

    def __init__(self, host: str, port: int, verbose: bool = False):
        self.verbose  = verbose
        self._sock    = socket.create_connection((host, port), timeout=15)
        self._sock.settimeout(None)             # background thread uses blocking recv
        self._splitter = _IACSplitter(self._raw_send)
        # Packet store — append-only, protected by _lock
        self._lock    = threading.Lock()
        self._pkts    = []                      # [(module, json_str), …]
        self._pkts_ev = threading.Event()       # set whenever a packet arrives
        # Text buffer (prompt/output bytes after IAC stripping)
        self._text    = bytearray()
        self._text_ev = threading.Event()
        # Connection state
        self.closed   = threading.Event()
        self._thread  = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    # ------------------------------------------------------------------
    # Low-level I/O
    # ------------------------------------------------------------------

    def _raw_send(self, data: bytes):
        try:
            self._sock.sendall(data)
        except OSError:
            pass

    def send_line(self, text: str):
        """Send text followed by CR LF."""
        self._raw_send((text + '\r\n').encode('latin-1', errors='replace'))

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass

    def _reader(self):
        while True:
            try:
                chunk = self._sock.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            self._splitter.feed(chunk)
            with self._lock:
                if self._splitter.packets:
                    for pkt in self._splitter.packets:
                        if self.verbose:
                            print(f'  [GMCP] {pkt[0]}  {pkt[1]}', flush=True)
                        self._pkts.append(pkt)
                    self._splitter.packets.clear()
                    self._pkts_ev.set()
                if self._splitter.text:
                    self._text.extend(self._splitter.text)
                    self._splitter.text.clear()
                    self._text_ev.set()
        self.closed.set()
        self._pkts_ev.set()   # wake any blocked wait_for_gmcp
        self._text_ev.set()

    # ------------------------------------------------------------------
    # GMCP packet access
    # ------------------------------------------------------------------

    def current_idx(self) -> int:
        """Number of packets received so far; use as start= before an action."""
        with self._lock:
            return len(self._pkts)

    def wait_for_gmcp(self, module: str, *,
                      start: int = 0, timeout: float = 5.0):
        """Block until a packet with this module name appears at index >= start.

        Returns (index, module, json_str).  Raises TimeoutError on failure.
        Call current_idx() BEFORE the triggering action and pass the result
        as start= to avoid matching stale packets.
        """
        deadline = time.monotonic() + timeout
        while True:
            with self._lock:
                for i in range(start, len(self._pkts)):
                    m, j = self._pkts[i]
                    if m == module:
                        return i, m, j
            if self.closed.is_set():
                raise TimeoutError(
                    f'Connection closed before receiving GMCP {module!r}')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'Timeout waiting for GMCP {module!r}')
            self._pkts_ev.wait(timeout=min(remaining, 0.25))
            self._pkts_ev.clear()

    def modules_received(self, *, start: int = 0) -> set:
        """Set of module names received from index start onward."""
        with self._lock:
            return {self._pkts[i][0] for i in range(start, len(self._pkts))}

    def get_gmcp(self, module: str, *, start: int = 0):
        """Return json_str for the first matching packet at index >= start, or None."""
        with self._lock:
            for i in range(start, len(self._pkts)):
                m, j = self._pkts[i]
                if m == module:
                    return j
        return None

    # ------------------------------------------------------------------
    # Text-output helpers
    # ------------------------------------------------------------------

    def wait_for_text(self, pattern: str, *, timeout: float = 15.0):
        """Block until pattern (case-insensitive) appears in the text stream."""
        pat      = pattern.lower()
        deadline = time.monotonic() + timeout
        while True:
            with self._lock:
                buf = self._text.decode('latin-1', errors='replace').lower()
            if pat in buf:
                return
            if self.closed.is_set():
                raise TimeoutError(
                    f'Connection closed before text {pattern!r}')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'Timeout waiting for text {pattern!r}')
            self._text_ev.wait(timeout=min(remaining, 0.25))
            self._text_ev.clear()

    def _text_snapshot(self, cursor: int) -> tuple[str, int]:
        """Return (new_text_since_cursor, new_cursor)."""
        with self._lock:
            raw = self._text[cursor:]
            return raw.decode('latin-1', errors='replace').lower(), len(self._text)


# ---------------------------------------------------------------------------
# Login helper
# ---------------------------------------------------------------------------

def login(conn: MUDConnection, username: str, password: str):
    """Drive the CircleMUD login sequence into CON_PLAYING.

    Raises RuntimeError if the character does not exist, the password is
    wrong, or the login sequence times out.
    """
    # Name prompt
    conn.wait_for_text('known?', timeout=15)
    conn.send_line(username)

    # Wait for password prompt; detect new-character confirmation along the way
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        conn._text_ev.wait(timeout=0.3)
        conn._text_ev.clear()
        with conn._lock:
            buf = conn._text.decode('latin-1', errors='replace').lower()
        if 'assword' in buf:
            break
        if ('did i get that right' in buf or 'are you new' in buf
                or 'new character' in buf):
            raise RuntimeError(
                f"Character {username!r} does not exist. "
                "Create it in-game before running the test suite.")
    else:
        raise RuntimeError("Timed out waiting for password prompt")

    conn.send_line(password)

    # Navigate MOTD / character menu using a moving text cursor to avoid
    # repeatedly responding to the same prompt text.
    text_cursor   = 0
    motd_handled  = False
    menu_handled  = False
    deadline      = time.monotonic() + 30

    while time.monotonic() < deadline:
        # Success: Char.StatusVars proves we're in CON_PLAYING
        if 'Char.StatusVars' in conn.modules_received():
            return

        new_text, text_cursor = conn._text_snapshot(text_cursor)

        if 'incorrect password' in new_text or 'wrong password' in new_text:
            raise RuntimeError("Login failed: incorrect password")

        if not motd_handled and ('press return' in new_text
                                 or 'press enter' in new_text):
            conn.send_line('')
            motd_handled = True

        if not menu_handled and ('make your choice' in new_text
                                 or 'enter the game' in new_text):
            conn.send_line('1')
            menu_handled = True

        conn._text_ev.wait(timeout=0.3)
        conn._text_ev.clear()

    raise RuntimeError("Login timed out: never reached CON_PLAYING")


# ---------------------------------------------------------------------------
# JSON parse helper
# ---------------------------------------------------------------------------

def _parse(json_str: str):
    try:
        return json.loads(json_str)
    except json.JSONDecodeError as e:
        raise AssertionError(f'Invalid JSON ({e}): {json_str!r}') from e


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_negotiation(conn: MUDConnection, args):
    """IAC WILL/DO exchange; Core.Hello received with correct fields."""
    _, _, j = conn.wait_for_gmcp('Core.Hello', start=0, timeout=args.timeout)
    d = _parse(j)
    assert 'name'    in d,                       f'Core.Hello missing "name": {j}'
    assert 'version' in d,                       f'Core.Hello missing "version": {j}'
    assert d['name'] == 'NewCirMUD',             f'Core.Hello.name: {d["name"]!r}'
    assert isinstance(d['version'], str),        f'Core.Hello.version must be str'
    assert d.get('auth') is False,               f'Core.Hello.auth should be false'


def test_login_burst(conn: MUDConnection, args):
    """All expected GMCP modules are present after login."""
    expected = {
        'Char.StatusVars',
        'Char.Status',
        'Char.Vitals',
        'Room.Info',
        'Room.Players',
        'Char.Items.List',
        'Char.Defences.List',
        'Char.Afflictions.List',
        'External.Discord.Status',
    }
    received = conn.modules_received()
    missing  = expected - received
    assert not missing, f'Missing from login burst: {sorted(missing)}'


def test_vitals(conn: MUDConnection, args):
    """Char.Vitals has required integer fields within valid ranges."""
    j = conn.get_gmcp('Char.Vitals')
    assert j is not None, 'Char.Vitals not received'
    d = _parse(j)
    int_fields = ('hp', 'hpmax', 'mp', 'mpmax', 'mv', 'mvmax', 'gold')
    for f in int_fields:
        assert f in d,                      f'Char.Vitals missing {f!r}'
        assert isinstance(d[f], int),       f'Char.Vitals.{f} must be int'
        assert d[f] >= 0,                   f'Char.Vitals.{f} negative: {d[f]}'
    assert d['hp']  <= d['hpmax'],          f'hp > hpmax: {d["hp"]} > {d["hpmax"]}'
    assert d['mp']  <= d['mpmax'],          f'mp > mpmax: {d["mp"]} > {d["mpmax"]}'
    assert d['mv']  <= d['mvmax'],          f'mv > mvmax: {d["mv"]} > {d["mvmax"]}'
    assert 0 <= d.get('hungry',  -1) <= 24, f'hungry out of 0-24: {d.get("hungry")}'
    assert 0 <= d.get('thirsty', -1) <= 24, f'thirsty out of 0-24: {d.get("thirsty")}'


def test_status(conn: MUDConnection, args):
    """Char.Status has required fields with correct types."""
    j = conn.get_gmcp('Char.Status')
    assert j is not None, 'Char.Status not received'
    d = _parse(j)
    for f in ('name', 'class', 'level', 'align', 'xp', 'xp_next', 'ac'):
        assert f in d, f'Char.Status missing {f!r}'
    assert isinstance(d['name'],    str), 'Char.Status.name must be str'
    assert isinstance(d['level'],   int), 'Char.Status.level must be int'
    assert isinstance(d['xp'],      int), 'Char.Status.xp must be int'
    assert isinstance(d['xp_next'], int), 'Char.Status.xp_next must be int'
    assert isinstance(d['ac'],      int), 'Char.Status.ac must be int'
    assert d['align'] in ('good', 'neutral', 'evil'), \
        f'Char.Status.align unexpected: {d["align"]!r}'


def test_room_info(conn: MUDConnection, args):
    """Room.Info has required fields and a complete boolean exits object."""
    j = conn.get_gmcp('Room.Info')
    assert j is not None, 'Room.Info not received'
    d = _parse(j)
    for f in ('num', 'name', 'zone', 'terrain', 'exits'):
        assert f in d, f'Room.Info missing {f!r}'
    assert isinstance(d['num'],  int), 'Room.Info.num must be int'
    assert isinstance(d['name'], str), 'Room.Info.name must be str'
    exits = d['exits']
    assert isinstance(exits, dict), 'Room.Info.exits must be an object'
    for direction in ('n', 'e', 's', 'w', 'u', 'd'):
        assert direction in exits, f'Room.Info.exits missing direction {direction!r}'
        assert isinstance(exits[direction], bool), \
            f'Room.Info.exits.{direction} must be bool, got {exits[direction]!r}'


def test_room_players(conn: MUDConnection, args):
    """Room.Players is a JSON array."""
    j = conn.get_gmcp('Room.Players')
    assert j is not None, 'Room.Players not received'
    d = _parse(j)
    assert isinstance(d, list), \
        f'Room.Players must be array, got {type(d).__name__}: {j}'


def test_afflictions(conn: MUDConnection, args):
    """Char.Afflictions.List is a JSON array."""
    j = conn.get_gmcp('Char.Afflictions.List')
    assert j is not None, 'Char.Afflictions.List not received'
    d = _parse(j)
    assert isinstance(d, list), \
        f'Char.Afflictions.List must be array, got {type(d).__name__}: {j}'


def test_discord(conn: MUDConnection, args):
    """External.Discord.Status has game, details, and state fields."""
    j = conn.get_gmcp('External.Discord.Status')
    assert j is not None, 'External.Discord.Status not received'
    d = _parse(j)
    assert d.get('game') == 'NewCirMUD', \
        f'game should be "NewCirMUD": {d.get("game")!r}'
    assert 'details' in d and d['details'], \
        'External.Discord.Status.details missing or empty'
    assert 'state' in d and d['state'], \
        'External.Discord.Status.state missing or empty'


def test_move(conn: MUDConnection, args):
    """Moving triggers Room.Info, Room.Players, and External.Discord.Status."""
    j = conn.get_gmcp('Room.Info')
    assert j is not None, 'No Room.Info available to inspect exits'
    d    = _parse(j)
    exits = d.get('exits', {})
    dir_names = {'n': 'north', 'e': 'east',  's': 'south',
                 'w': 'west',  'u': 'up',     'd': 'down'}
    open_dirs = [dir_names[k] for k, v in exits.items() if v]
    if not open_dirs:
        raise AssertionError(
            'No open exits in starting room; cannot test movement. '
            'Log in to a room with at least one open exit.')

    start = conn.current_idx()
    conn.send_line(open_dirs[0])

    try:
        conn.wait_for_gmcp('Room.Info', start=start, timeout=args.timeout)
    except TimeoutError:
        raise AssertionError(
            f'Room.Info not re-sent after moving {open_dirs[0]!r}. '
            'The character may lack movement points or the exit blocked it.')
    conn.wait_for_gmcp('Room.Players',            start=start, timeout=args.timeout)
    conn.wait_for_gmcp('External.Discord.Status', start=start, timeout=args.timeout)


def test_channel(conn: MUDConnection, args):
    """Gossip sends Comm.Channel.Text back to the sender with correct fields."""
    start = conn.current_idx()
    msg   = 'GMCP-autotest'
    conn.send_line(f'gossip {msg}')
    try:
        _, _, j = conn.wait_for_gmcp('Comm.Channel.Text', start=start,
                                     timeout=args.timeout)
    except TimeoutError:
        raise AssertionError(
            'Comm.Channel.Text not received after gossip. '
            'Ensure the character is level 2+ (level_can_shout setting).')
    d = _parse(j)
    assert d.get('channel') == 'gossip', \
        f'Expected channel "gossip", got {d.get("channel")!r}'
    assert 'talker' in d and d['talker'], \
        'Comm.Channel.Text missing "talker"'
    assert msg.lower() in d.get('text', '').lower(), \
        f'{msg!r} not found in Comm.Channel.Text.text: {d.get("text")!r}'


def test_goodbye(conn: MUDConnection, args):
    """Core.Goodbye is sent before the connection closes after quit."""
    start = conn.current_idx()
    conn.send_line('quit')
    conn.send_line('0')
    try:
        conn.wait_for_gmcp('Core.Goodbye', start=start, timeout=args.timeout)
    except TimeoutError:
        raise AssertionError(
            'Core.Goodbye not received after quit command. '
            'Verify gmcp_send_goodbye() is called in close_socket().')
    # TCP connection should close shortly after
    conn.closed.wait(timeout=5.0)
    assert conn.closed.is_set(), \
        'Connection did not close within 5 s after Core.Goodbye'


def test_ping(conn: MUDConnection, args):
    """Core.Ping is received within 65 s (server pings every 60 s)."""
    start = conn.current_idx()
    try:
        conn.wait_for_gmcp('Core.Ping', start=start, timeout=65.0)
    except TimeoutError:
        raise AssertionError(
            'Core.Ping not received within 65 s. '
            'Verify the 60 RL_SEC heartbeat block in comm.c heartbeat().')


# ---------------------------------------------------------------------------
# Test registry  (name, function, requires_own_connection, slow)
# ---------------------------------------------------------------------------

TESTS = [
    ('negotiation',  test_negotiation,  False, False),
    ('login_burst',  test_login_burst,  False, False),
    ('vitals',       test_vitals,       False, False),
    ('status',       test_status,       False, False),
    ('room_info',    test_room_info,    False, False),
    ('room_players', test_room_players, False, False),
    ('afflictions',  test_afflictions,  False, False),
    ('discord',      test_discord,      False, False),
    ('move',         test_move,         False, False),
    ('channel',      test_channel,      False, False),
    ('goodbye',      test_goodbye,      True,  False),  # own connection: closes it
    ('ping',         test_ping,         True,  True),   # own connection + slow
]

TEST_NAMES = [name for name, *_ in TESTS]


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def _make_conn(host, port, verbose, username, password):
    """Open a fresh connection and log in.  Returns MUDConnection or raises."""
    conn = MUDConnection(host, port, verbose)
    login(conn, username, password)
    return conn


def run_suite(args) -> int:
    # Validate requested test names
    if args.test:
        requested = {t.strip() for t in args.test.split(',')}
        unknown   = requested - set(TEST_NAMES)
        if unknown:
            print(f'Unknown tests: {sorted(unknown)}.  '
                  f'Use --list to see available names.', file=sys.stderr)
            return 1
    else:
        requested = set(TEST_NAMES)

    # Shared connection for tests that do not close the socket
    print(f'Connecting to {args.host}:{args.port} …', flush=True)
    try:
        main_conn = MUDConnection(args.host, args.port, args.verbose)
    except OSError as e:
        print(f'Connection failed: {e}', file=sys.stderr)
        return 1

    print(f'Logging in as {args.user!r} …', flush=True)
    try:
        login(main_conn, args.user, args.password)
    except (RuntimeError, TimeoutError) as e:
        print(f'Login failed: {e}', file=sys.stderr)
        main_conn.close()
        return 1

    print('In game.  Running tests …\n', flush=True)

    results = []

    for name, fn, own_conn, slow in TESTS:
        if name not in requested:
            continue

        if slow and not args.slow:
            results.append((name, 'SKIP', 'requires --slow'))
            print(f'  SKIP    {name}  (requires --slow)', flush=True)
            continue

        # Tests that close the connection need their own session
        if own_conn:
            try:
                conn = _make_conn(args.host, args.port,
                                  args.verbose, args.user, args.password)
            except (OSError, RuntimeError, TimeoutError) as e:
                msg = f'Setup failed: {e}'
                results.append((name, 'FAIL', msg))
                print(f'  FAIL    {name}  —  {msg}', flush=True)
                continue
        else:
            conn = main_conn

        try:
            fn(conn, args)
            results.append((name, 'PASS', ''))
            print(f'  PASS    {name}', flush=True)
        except (AssertionError, TimeoutError) as e:
            results.append((name, 'FAIL', str(e)))
            print(f'  FAIL    {name}  —  {e}', flush=True)
        finally:
            if own_conn:
                conn.close()

    main_conn.close()

    # Summary
    passed  = sum(1 for _, s, _ in results if s == 'PASS')
    failed  = sum(1 for _, s, _ in results if s == 'FAIL')
    skipped = sum(1 for _, s, _ in results if s == 'SKIP')
    total   = passed + failed

    parts = [f'{passed}/{total} passed']
    if skipped:
        parts.append(f'{skipped} skipped')
    print('\n' + ', '.join(parts))

    return 0 if failed == 0 else 1


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(
        description='GMCP validation test suite for NewCirMUD',
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--host',     default='127.0.0.1',
                   help='MUD hostname or IP (default: 127.0.0.1)')
    p.add_argument('--port',     default=4000, type=int,
                   help='MUD port (default: 4000)')
    p.add_argument('--user',     default=None,
                   help='Character name to log in as (required except with --list)')
    p.add_argument('--password', default=None,
                   help='Character password (required except with --list)')
    p.add_argument('--slow',     action='store_true',
                   help='Include slow tests (ping waits up to 65 s)')
    p.add_argument('--verbose',  action='store_true',
                   help='Print every received GMCP packet')
    p.add_argument('--test',     metavar='NAMES',
                   help='Comma-separated subset of tests to run')
    p.add_argument('--timeout',  default=5.0, type=float,
                   help='Per-packet wait timeout in seconds (default: 5.0)')
    p.add_argument('--list',     action='store_true',
                   help='Print available test names and exit')
    args = p.parse_args()

    if args.list:
        for name, _, own, slow in TESTS:
            tags = []
            if own:  tags.append('own-conn')
            if slow: tags.append('slow')
            suffix = f'  [{", ".join(tags)}]' if tags else ''
            print(f'  {name}{suffix}')
        return

    if not args.user or not args.password:
        p.error('--user and --password are required (use --list to see tests)')

    sys.exit(run_suite(args))


if __name__ == '__main__':
    main()
