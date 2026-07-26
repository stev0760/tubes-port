#!/usr/bin/env python3
"""Decode Tubes .MUS music.

The format was recovered by disassembling DRIVERS.RES:FMMUSIC.DRV and
cross-checking every command against GMMUSIC.DRV, which consumes the same
byte stream and emits General MIDI.  Driver offsets cited below are file
offsets into FMMUSIC.DRV.

Container
    32-byte header: 0xf5 marker + 31 reserved bytes.  The driver's play
    entry (0x996) checks byte 0 == 0xf5 and then does `add si,0x20`.

Event stream
    <delta:u8> <cmd:u8> <args...>, dispatched on the high nibble at 0x83d.
    delta counts timer ticks; 0 means "still this tick, keep going".

    0x1c  12 bytes  set instrument on channel c
    0x2c   1 byte   set volume / velocity, 0..127
    0x3c   1 byte   note on, MIDI note number
    0x4c   0 bytes  note off
    0x5c   2 bytes  pitch bend, MSB first, centre = 40 00
    0xf0   0 bytes  end of song - rewinds to offset 0, so songs loop

Channels
    11 of them, because the driver runs OPL2 rhythm mode permanently
    (reg 0xBD = 0xE0, written at 0x1b2).  0..5 are melodic; 6..10 are
    bass drum, snare, tom, cymbal, hi-hat.  The operator table at 0x3a
    is 00 01 02 08 09 0a 10 14 12 15 11, the standard AdLib slot map.

Instrument payload
    [0]     General MIDI program (melodic) or GM percussion note
            (channels 6..10).  Ignored by the FM driver; GMMUSIC.DRV
            sends it as a Program Change at 0xf4.
    [1..11] the 11 classic AdLib registers, in pairs:
            20/23, 40/43, 60/63, 80/83, E0/E3, then C0.
            Channels 7..10 are single-operator, so the driver reads but
            discards the carrier half (the `cmp bh,6 / ja` at 0x1fd etc).

Tempo
    Fixed.  init (0x92a) programs PIT channel 0 with the divisor stored
    at 0x8de = 0x4000, giving 1193182/16384 = 72.8271 Hz.  One delta unit
    is 13.7319 ms.  The ISR runs a Bresenham divider so the BIOS handler
    still sees its usual 18.2 Hz.

Usage
    mus_decode.py dump  FILE.MUS [--driver FMMUSIC.DRV]
    mus_decode.py midi  FILE.MUS OUT.mid
    mus_decode.py dro   FILE.MUS OUT.dro [--driver FMMUSIC.DRV]
"""

import argparse
import os
import struct
import sys

# ---------------------------------------------------------------------------
# constants read off the driver

PIT_HZ = 1193182.0
TIMER_DIVISOR = 0x4000          # word at FMMUSIC.DRV:0x8de
TICK_HZ = PIT_HZ / TIMER_DIVISOR
TICK_MS = 1000.0 / TICK_HZ

HEADER_LEN = 32
MARKER = 0xF5

CMD_INSTRUMENT = 0x10
CMD_VOLUME = 0x20
CMD_NOTE_ON = 0x30
CMD_NOTE_OFF = 0x40
CMD_PITCH_BEND = 0x50
CMD_END = 0xF0

ARG_LEN = {
    CMD_INSTRUMENT: 12,
    CMD_VOLUME: 1,
    CMD_NOTE_ON: 1,
    CMD_NOTE_OFF: 0,
    CMD_PITCH_BEND: 2,
}

CHANNELS = 11
PERCUSSION_BASE = 6             # channels >= this are rhythm-mode voices
RHYTHM_NAMES = ['BD', 'SD', 'TT', 'CY', 'HH']

# Driver table locations.  Sizes are pinned by the code that indexes them
# and by each table ending exactly where the next begins.
TBL_OP_OFFSET = (0x3a, 11)      # cs:[bx+0x3a], channel -> operator slot
TBL_RHYTHM_MASK = (0x45, 5)     # cs:[bx+0x45], channel-6 -> reg 0xBD bit
TBL_DEFAULT_INSTR = (0x85, 11)  # six default instruments, 11 bytes each
TBL_ATTENUATION = (0x29a, 128)  # cs:[bx+0x29a], volume -> TL attenuation
TBL_FNUM = (0x458, 192)         # 12 pitch classes x 16 bend steps, words
TBL_BLOCK = (0x5d8, 96)         # cs:[bx+0x5d8], note -> OPL block
TBL_PITCH_CLASS = (0x638, 96)   # cs:[bx+0x638], note -> fnum table row


class Event(object):
    __slots__ = ('offset', 'delta', 'tick', 'cmd', 'channel', 'args')

    def __init__(self, offset, delta, tick, cmd, channel, args):
        self.offset = offset
        self.delta = delta
        self.tick = tick
        self.cmd = cmd
        self.channel = channel
        self.args = args


def parse(data):
    """Split a .MUS into events.  Raises ValueError on anything unexpected."""
    if len(data) < HEADER_LEN or data[0] != MARKER:
        raise ValueError('not a .MUS: missing 0x%02x marker' % MARKER)

    events = []
    pos = HEADER_LEN
    tick = 0
    while True:
        if pos + 2 > len(data):
            raise ValueError('event stream ran off the end at %d' % pos)
        start = pos
        delta = data[pos]
        cmd = data[pos + 1]
        pos += 2
        tick += delta

        if cmd == CMD_END:
            events.append(Event(start, delta, tick, cmd, None, b''))
            return events, pos

        cls = cmd & 0xF0
        chan = cmd & 0x0F
        if cls not in ARG_LEN:
            raise ValueError('unknown command 0x%02x at %d' % (cmd, start + 1))
        if chan >= CHANNELS:
            raise ValueError('channel %d out of range at %d' % (chan, start + 1))
        n = ARG_LEN[cls]
        if pos + n > len(data):
            raise ValueError('arguments run past EOF at %d' % pos)
        events.append(Event(start, delta, tick, cls, chan, data[pos:pos + n]))
        pos += n


# ---------------------------------------------------------------------------
# driver tables


class Driver(object):
    """The lookup tables FMMUSIC.DRV keeps in its own code segment."""

    def __init__(self, blob):
        if len(blob) < 0x698:
            raise ValueError('FMMUSIC.DRV is too short to hold its tables')
        off, n = TBL_OP_OFFSET
        self.op_offset = list(blob[off:off + n])
        off, n = TBL_RHYTHM_MASK
        self.rhythm_mask = list(blob[off:off + n])
        off, n = TBL_ATTENUATION
        self.attenuation = list(blob[off:off + n])
        off, n = TBL_FNUM
        self.fnum = list(struct.unpack('<%dH' % n, blob[off:off + n * 2]))
        off, n = TBL_BLOCK
        self.block = list(blob[off:off + n])
        off, n = TBL_PITCH_CLASS
        self.pitch_class = list(blob[off:off + n])

        off, n = TBL_DEFAULT_INSTR
        self.default_instr = [blob[off + i * n:off + (i + 1) * n]
                              for i in range(6)]

        # Cheap sanity checks against what the code requires of each table.
        if self.op_offset[:6] != [0x00, 0x01, 0x02, 0x08, 0x09, 0x0a]:
            raise ValueError('operator table is not the AdLib slot map')
        if self.rhythm_mask != [0x10, 0x08, 0x04, 0x02, 0x01]:
            raise ValueError('rhythm mask table is not BD/SD/TT/CY/HH')
        if self.attenuation[127] != 0:
            raise ValueError('attenuation table does not bottom out at 0')

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as fh:
            return cls(fh.read())


def find_driver(explicit):
    """Locate FMMUSIC.DRV.  It is game data, so it is never in the repo."""
    if explicit:
        return explicit
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in ('../assets-extracted/drivers/FMMUSIC.DRV',
                '../../assets-extracted/drivers/FMMUSIC.DRV'):
        cand = os.path.normpath(os.path.join(here, rel))
        if os.path.exists(cand):
            return cand
    raise SystemExit('cannot find FMMUSIC.DRV; pass --driver')


# ---------------------------------------------------------------------------
# OPL2 register generation - a transcription of FMMUSIC.DRV


class FmPlayer(object):
    """Reproduces the exact register writes FMMUSIC.DRV would make.

    Every method below is a line-by-line transcription of the routine at
    the cited driver offset, including its asymmetries: note-on writes a
    frequency for channels 6 and 8 but not 7, 9 or 10, and note-off for
    channel 6 goes through the rhythm bit rather than reg 0xB0.
    """

    def __init__(self, drv):
        self.drv = drv
        self.writes = []        # (tick, register, value)
        self.tick = 0
        self.saved_tl = [0] * 0x16   # cs:0x4d, indexed by operator slot
        self.saved_b0 = [0] * CHANNELS   # cs:0x64, reg 0xB0 without key-on
        self.volume = [0] * CHANNELS     # cs:0x6f
        self.rhythm = 0                  # cs:0x4a, shadow of reg 0xBD

    def write(self, reg, val):
        self.writes.append((self.tick, reg & 0xFF, val & 0xFF))

    # -- 0x142: reset the chip and install default instruments -------------
    def init(self):
        for reg in range(0x01, 0xF6):
            self.write(reg, 0x00)
        self.write(0x01, 0x20)          # enable waveform select
        for ch in range(6):
            self.load_instrument(ch, self.drv.default_instr[0])
        for ch in range(6, CHANNELS):
            self.load_instrument(ch, self.drv.default_instr[ch - 5])
        self.rhythm = 0xE0              # AM depth + vibrato depth + rhythm on
        self.write(0xBD, self.rhythm)

    # -- 0x1e8 -------------------------------------------------------------
    def load_instrument(self, ch, regs):
        off = self.drv.op_offset[ch]
        two_op = ch <= 6

        self.write(0x20 + off, regs[0])
        if two_op:
            self.write(0x23 + off, regs[1])

        self.write(0x40 + off, regs[2])
        self.saved_tl[off] = regs[2]
        if two_op:
            self.write(0x43 + off, regs[3])
            self.saved_tl[off + 3] = regs[3]

        self.write(0x60 + off, regs[4])
        if two_op:
            self.write(0x63 + off, regs[5])

        self.write(0x80 + off, regs[6])
        if two_op:
            self.write(0x83 + off, regs[7])

        self.write(0xE0 + off, regs[8])
        if two_op:
            self.write(0xE3 + off, regs[9])
            self.write(0xB0 + ch, 0x00)
            self.write(0xC0 + ch, regs[10])

    # -- 0x378 / 0x31a -----------------------------------------------------
    def set_volume(self, ch, vol):
        self.volume[ch] = vol
        atten = self.drv.attenuation[vol & 0x7F]
        off = self.drv.op_offset[ch]

        def scaled(slot):
            v = self.saved_tl[slot]
            level = (v & 0x3F) + atten
            return (v & 0xC0) | min(level, 0x3F)

        self.write(0x40 + off, scaled(off))
        if ch <= 6:
            self.write(0x43 + off, scaled(off + 3))

    # -- 0x698: F-number and block from the driver's tables ----------------
    def set_frequency(self, ch, note, key_on):
        # Pitch bend is dead code on AdLib: 0x388 hardcodes the centre value
        # 0x2000, so the bend arithmetic at 0x69e collapses to note * 16.
        ax = note * 16
        ax = max(0, min(ax, 0x5FF))

        row = self.drv.pitch_class[ax >> 4]
        frac = ((ax << 1) & 0x1F) >> 1
        fnum = self.drv.fnum[row * 16 + frac]

        block = self.drv.block[ax >> 4] - 1
        signed = fnum - 0x10000 if fnum & 0x8000 else fnum
        if signed < 0:
            block += 1
        if block < 0:
            block += 1
            signed >>= 1               # arithmetic shift, as `sar ax,1`

        low = signed & 0xFF
        high = (signed >> 8) & 0x03
        self.write(0xA0 + ch, low)
        val = high + (block << 2) + key_on
        self.write(0xB0 + ch, val)
        self.saved_b0[ch] = val - key_on

    # -- 0x388 -------------------------------------------------------------
    def note_on(self, ch, midi_note):
        note = midi_note - 12
        if note < 0:
            note = 0
        if ch < PERCUSSION_BASE:
            self.set_frequency(ch, note, 0x20)
        elif ch == 6 or ch == 8:
            # Bass drum and tom are pitched, but key-on must stay clear in
            # rhythm mode - the 0xBD bit triggers them instead.
            self.set_frequency(ch, note, 0x00)
        if ch >= PERCUSSION_BASE:
            self.rhythm |= self.drv.rhythm_mask[ch - PERCUSSION_BASE]
            self.write(0xBD, self.rhythm)

    # -- 0x420 -------------------------------------------------------------
    def note_off(self, ch):
        if ch < PERCUSSION_BASE:
            self.write(0xB0 + ch, self.saved_b0[ch])
        else:
            self.rhythm &= ~self.drv.rhythm_mask[ch - PERCUSSION_BASE] & 0xFF
            self.write(0xBD, self.rhythm)

    # -- 0x810: the tick routine's dispatch --------------------------------
    def run(self, events):
        self.init()
        for ev in events:
            self.tick = ev.tick
            if ev.cmd == CMD_INSTRUMENT:
                self.load_instrument(ev.channel, ev.args[1:])
            elif ev.cmd == CMD_VOLUME:
                self.set_volume(ev.channel, ev.args[0])
            elif ev.cmd == CMD_NOTE_ON:
                self.note_on(ev.channel, ev.args[0])
            elif ev.cmd == CMD_NOTE_OFF:
                self.note_off(ev.channel)
            elif ev.cmd == CMD_PITCH_BEND:
                pass            # 0x454 is `add si,2 / ret` - a no-op
            elif ev.cmd == CMD_END:
                break
        return self.writes


# ---------------------------------------------------------------------------
# exports


def prune(writes):
    """Drop writes that do not change chip state.

    The driver's reset sweep at 0x142 blanks registers 0x01..0xf5, which is
    245 of them - more than DRO's 127-entry codemap can name.  On a chip
    that starts at zero those writes are no-ops, and OPL registers are
    state rather than triggers, so rewriting an identical value never
    retriggers an envelope.  Removing them is therefore lossless from
    reset, and it is only ever done for the log; the C++ player keeps the
    sweep so it also does the right thing on real state.
    """
    shadow = [0] * 256
    out = []
    for tick, reg, val in writes:
        if shadow[reg] == val:
            continue
        shadow[reg] = val
        out.append((tick, reg, val))
    return out


def write_dro(writes, total_ticks, path):
    """DRO v2 - an OPL register log, playable by AdPlug (adplay, VLC)."""
    writes = prune(writes)
    regs_used = sorted({reg for _, reg, _ in writes})
    if len(regs_used) > 127:
        raise ValueError('too many distinct registers for a DRO codemap')
    codemap = {reg: i for i, reg in enumerate(regs_used)}

    SHORT_DELAY, LONG_DELAY = 0x00, 0x01
    body = bytearray()
    pairs = 0
    now = 0.0
    emitted = 0.0

    def delay(ms):
        nonlocal pairs
        ms = int(ms)
        while ms > 0:
            if ms > 256:
                step = min(ms, 256 * 256) // 256 * 256
                body.append(LONG_DELAY)
                body.append((step // 256) - 1)
                ms -= step
            else:
                body.append(SHORT_DELAY)
                body.append(ms - 1)
                ms = 0
            pairs += 1

    for tick, reg, val in writes:
        now = tick * TICK_MS
        if now - emitted >= 1.0:
            delay(now - emitted)
            emitted += int(now - emitted)
        body.append(codemap[reg])
        body.append(val)
        pairs += 1

    tail = total_ticks * TICK_MS - emitted
    if tail >= 1.0:
        delay(tail)

    length_ms = int(total_ticks * TICK_MS)
    header = bytearray(b'DBRAWOPL')
    header += struct.pack('<HH', 2, 0)          # version 2.0
    header += struct.pack('<II', pairs, length_ms)
    header += bytes([0,                          # hardware: OPL2
                     0,                          # format: interleaved
                     0,                          # compression: none
                     SHORT_DELAY, LONG_DELAY,
                     len(regs_used)])
    header += bytes(regs_used)
    with open(path, 'wb') as fh:
        fh.write(bytes(header) + bytes(body))
    return pairs, length_ms


def _vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def write_midi(events, path, ppq=96):
    """Export via the same mapping GMMUSIC.DRV uses.

    This is not an approximation of the FM output - it is a transcription
    of the game's own General MIDI driver, which consumes this identical
    byte stream and emits these identical messages.
    """
    perc_note = [35] * (CHANNELS - PERCUSSION_BASE)
    program = [0] * PERCUSSION_BASE
    velocity = [100] * CHANNELS
    sounding = [None] * CHANNELS

    track = bytearray()
    tempo = int(round(1e6 / TICK_HZ * ppq))
    track += _vlq(0) + b'\xff\x51\x03' + struct.pack('>I', tempo)[1:]

    last_tick = 0

    def emit(tick, msg):
        nonlocal last_tick
        track.extend(_vlq(tick - last_tick))
        track.extend(msg)
        last_tick = tick

    for ev in events:
        if ev.cmd == CMD_END:
            break
        ch = ev.channel
        percussive = ch >= PERCUSSION_BASE
        midi_ch = 9 if percussive else ch

        if ev.cmd == CMD_INSTRUMENT:
            if percussive:
                perc_note[ch - PERCUSSION_BASE] = ev.args[0]
            else:
                program[ch] = ev.args[0]
                emit(ev.tick, bytes([0xC0 | midi_ch, ev.args[0] & 0x7F]))
        elif ev.cmd == CMD_VOLUME:
            velocity[ch] = ev.args[0] & 0x7F
        elif ev.cmd == CMD_NOTE_ON:
            note = (perc_note[ch - PERCUSSION_BASE] if percussive
                    else ev.args[0]) & 0x7F
            if sounding[ch] is not None:
                emit(ev.tick, bytes([0x80 | midi_ch, sounding[ch], 0]))
            emit(ev.tick, bytes([0x90 | midi_ch, note, velocity[ch] or 1]))
            sounding[ch] = note
        elif ev.cmd == CMD_NOTE_OFF:
            if sounding[ch] is not None:
                emit(ev.tick, bytes([0x80 | midi_ch, sounding[ch], 0]))
                sounding[ch] = None
        elif ev.cmd == CMD_PITCH_BEND:
            if not percussive:
                msb, lsb = ev.args[0] & 0x7F, ev.args[1] & 0x7F
                emit(ev.tick, bytes([0xE0 | midi_ch, lsb, msb]))

    for ch, note in enumerate(sounding):
        if note is not None:
            midi_ch = 9 if ch >= PERCUSSION_BASE else ch
            emit(last_tick, bytes([0x80 | midi_ch, note, 0]))
    track += _vlq(0) + b'\xff\x2f\x00'

    with open(path, 'wb') as fh:
        fh.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, ppq))
        fh.write(b'MTrk' + struct.pack('>I', len(track)) + bytes(track))
    return len(track)


# ---------------------------------------------------------------------------
# reporting


def describe(ev):
    if ev.cmd == CMD_END:
        return 'end of song (rewinds to start)'
    ch = ev.channel
    tag = ('ch%-2d' % ch if ch < PERCUSSION_BASE
           else '%-4s' % RHYTHM_NAMES[ch - PERCUSSION_BASE])
    if ev.cmd == CMD_INSTRUMENT:
        kind = 'GM prog' if ch < PERCUSSION_BASE else 'GM perc note'
        return '%s instrument  %s %-3d  opl %s' % (
            tag, kind, ev.args[0],
            ' '.join('%02x' % b for b in ev.args[1:]))
    if ev.cmd == CMD_VOLUME:
        return '%s volume      %d' % (tag, ev.args[0])
    if ev.cmd == CMD_NOTE_ON:
        n = ev.args[0]
        names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
        return '%s note on     %-3d  %s%d' % (
            tag, n, names[n % 12], n // 12 - 1)
    if ev.cmd == CMD_NOTE_OFF:
        return '%s note off' % tag
    if ev.cmd == CMD_PITCH_BEND:
        bend = (ev.args[0] << 7) | ev.args[1]
        centre = ' (centre, ignored by the FM driver)' if bend == 0x2000 else ''
        return '%s pitch bend  %d%s' % (tag, bend, centre)
    return '%s ?' % tag


def cmd_dump(args):
    data = open(args.path, 'rb').read()
    events, end = parse(data)
    total = events[-1].tick

    print('%s: %d bytes, %d events, stream ends at %d (EOF %d)'
          % (os.path.basename(args.path), len(data), len(events), end, len(data)))
    print('%.2f seconds at %.4f Hz (%d ticks)'
          % (total / TICK_HZ, TICK_HZ, total))

    used = sorted({e.channel for e in events if e.channel is not None})
    print('channels: %s' % ', '.join(
        str(c) if c < PERCUSSION_BASE else RHYTHM_NAMES[c - PERCUSSION_BASE]
        for c in used))
    print()

    limit = len(events) if args.all else min(len(events), args.limit)
    for ev in events[:limit]:
        print('%6d  t%-7d +%-4d  %s'
              % (ev.offset, ev.tick, ev.delta, describe(ev)))
    if limit < len(events):
        print('... %d more events (--all to see them)' % (len(events) - limit))


def decode_patch(regs, two_op=True):
    """Spell out an 11-byte AdLib patch as OPL2 register fields."""
    def op(av, tl, ad, sr, wf):
        return dict(
            trem=bool(av & 0x80), vib=bool(av & 0x40),
            sustain=bool(av & 0x20), ksr=bool(av & 0x10), mult=av & 0x0F,
            ksl=tl >> 6, level=tl & 0x3F,
            attack=ad >> 4, decay=ad & 0x0F,
            sus_level=sr >> 4, release=sr & 0x0F,
            wave=wf & 0x07)
    out = {'mod': op(regs[0], regs[2], regs[4], regs[6], regs[8])}
    if two_op:
        out['car'] = op(regs[1], regs[3], regs[5], regs[7], regs[9])
        out['feedback'] = (regs[10] >> 1) & 0x07
        out['connection'] = 'AM' if regs[10] & 1 else 'FM'
    return out


def fmt_op(o):
    return ('mult %2d  lvl %2d  ksl %d  atk %2d dec %2d sus %2d rel %2d  '
            'wave %d %s%s%s%s' % (
                o['mult'], o['level'], o['ksl'], o['attack'], o['decay'],
                o['sus_level'], o['release'], o['wave'],
                'T' if o['trem'] else '-', 'V' if o['vib'] else '-',
                'S' if o['sustain'] else '-', 'K' if o['ksr'] else '-'))


def collect_bank(paths):
    """Gather every distinct OPL patch, keyed by its own bytes.

    Deliberately not keyed by GM program: the game reuses program 0 for
    several unrelated patches, so a GM-keyed bank would silently merge them.
    """
    bank = {}
    for path in paths:
        events, _ = parse(open(path, 'rb').read())
        for ev in events:
            if ev.cmd != CMD_INSTRUMENT:
                continue
            patch = bytes(ev.args[1:])
            entry = bank.setdefault(patch, {
                'gm': set(), 'percussive': ev.channel >= PERCUSSION_BASE,
                # The driver's two-operator test is `ch <= 6`, so the bass
                # drum is percussive *and* two-operator. Do not conflate.
                'two_op': ev.channel <= 6, 'uses': []})
            entry['gm'].add(ev.args[0])
            entry['uses'].append((os.path.basename(path), ev.channel))
    return bank


def cmd_bank(args):
    paths = sorted(args.paths)
    bank = collect_bank(paths)
    melodic = [(p, e) for p, e in bank.items() if not e['percussive']]
    drums = [(p, e) for p, e in bank.items() if e['percussive']]

    print('%d distinct OPL patches across %d files '
          '(%d melodic, %d percussion)\n'
          % (len(bank), len(paths), len(melodic), len(drums)))

    for label, group in (('MELODIC', melodic), ('PERCUSSION', drums)):
        print('== %s ==' % label)
        for i, (patch, e) in enumerate(group):
            gm = ','.join(str(g) for g in sorted(e['gm']))
            songs = sorted({s for s, _ in e['uses']})
            chans = sorted({c for _, c in e['uses']})
            tag = 'GM note' if e['percussive'] else 'GM prog'
            print('  [%d] %s %-8s  channels %s' % (i, tag, gm, chans))
            print('      bytes %s' % ' '.join('%02x' % b for b in patch))
            d = decode_patch(patch, two_op=e['two_op'])
            print('      mod  %s' % fmt_op(d['mod']))
            if 'car' in d:
                print('      car  %s' % fmt_op(d['car']))
                print('      %s, feedback %d' % (d['connection'], d['feedback']))
            print('      used by %s' % ', '.join(songs))
        print()


def cmd_midi(args):
    data = open(args.path, 'rb').read()
    events, _ = parse(data)
    n = write_midi(events, args.out)
    print('%s -> %s (%d bytes of track data, %.2f s)'
          % (os.path.basename(args.path), args.out, n,
             events[-1].tick / TICK_HZ))


def cmd_dro(args):
    data = open(args.path, 'rb').read()
    events, _ = parse(data)
    drv = Driver.load(find_driver(args.driver))
    writes = FmPlayer(drv).run(events)
    pairs, ms = write_dro(writes, events[-1].tick, args.out)
    print('%s -> %s (%d register writes, %d DRO pairs, %.2f s)'
          % (os.path.basename(args.path), args.out, len(writes), pairs,
             ms / 1000.0))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd')

    p = sub.add_parser('dump', help='human-readable event listing')
    p.add_argument('path')
    p.add_argument('--limit', type=int, default=60)
    p.add_argument('--all', action='store_true')
    p.add_argument('--driver')
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser('midi', help='export .mid the way GMMUSIC.DRV would')
    p.add_argument('path')
    p.add_argument('out')
    p.add_argument('--driver')
    p.set_defaults(func=cmd_midi)

    p = sub.add_parser('bank', help='list every distinct OPL patch')
    p.add_argument('paths', nargs='+')
    p.add_argument('--driver')
    p.set_defaults(func=cmd_bank)

    p = sub.add_parser('dro', help='export an OPL2 register log (DRO v2)')
    p.add_argument('path')
    p.add_argument('out')
    p.add_argument('--driver')
    p.set_defaults(func=cmd_dro)

    args = ap.parse_args()
    if not getattr(args, 'func', None):
        ap.print_help()
        return 1
    args.func(args)
    return 0


if __name__ == '__main__':
    sys.exit(main())
