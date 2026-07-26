# The live-debugging rig

Static disassembly of `1000:3a67` hit its limit (see `docs/worklog.md` - five
self-corrections in one session, every one from a tool rather than the binary).
This is the replacement technique: watch the game run.

Nothing here lives in this repository. The rig is machine-local and its drive
holds copyrighted game data, so it sits outside:

    ~/Dev/tubes-tooling/
      dosbox-x-remotedebug/     lokkju's DOSBox-X fork: GDB stub + QMP server
      dosbox-mcp/               jdmichaud's MCP wrapper, 24 tools
      tubes.conf                the DOSBox-X config used for all experiments
      gamedrive/                what DOS sees as C:
      capture/                  screenshots and save states land here
      bringup_tubes.py          end-to-end check; also locates DGROUP
      exp0_input_bits.py        Experiment 0: scancode -> bitmask, solved
      exp1_atom_array.py        Experiment 1: locate the live atom array
      peek_startup.py           run the game and dump the text screen
      sweep_env.py              try conf variants against a startup guard
      holdkey.py                hold/release one key via QMP
      build.log                 the fork's build transcript

`gamedrive/` is symlinks to the game files in `..`, plus **copies** of the two
files the game writes (`SETUP.CFG`, `TUBES.SAV`) so debugging cannot corrupt
the user's real save. No game data is duplicated and none is in this repo.

## What was built

    cd ~/Dev/tubes-tooling/dosbox-x-remotedebug
    ./build-debug --enable-remotedebug \
                  --disable-libfluidsynth --disable-mt32 --disable-avcodec

Fork base is DOSBox-X **2025.12.01** (the distro package on this machine is
2022.12.26 and does *not* have the stub - the fork is a separate binary at
`src/dosbox-x`, and the system `dosbox-x` is left alone).

`--disable-avcodec` is not cosmetic: this machine has ffmpeg 8.1, and the AUR
recipe needed an ffmpeg-4.4 shim even for the 2022 release. Video recording is
irrelevant here; screenshots go through libpng and are unaffected.
`--disable-libfluidsynth --disable-mt32` follow the MCP's own instructions.

`build-debug` implies `--enable-debug=heavy`, so `C_HEAVY_DEBUG=1` - this
matters, see the watchpoint section below.

## The one setting without which nothing runs

    [dos]
    dos idle api = false

Leave it at DOSBox-X's default of `true` and the game prints

    This game requires complete control of your computer.  Please run from DOS!

and stops. At `21ea:0249` it issues `INT 2Fh AX=1680h` - the Windows/DPMI
cooperative-yield call - and bails if `AL` comes back `0`. DOSBox-X services
that call so it can idle the host CPU (`src/dos/dos_misc.cpp:443` sets
`reg_al = 0`); with the setting off, `AL` stays `0x80` and the check passes.
`docs/reversing-notes.md` has the full disassembly.

Two things this cost, worth not repeating:

- It is **not** about `SETUP.EXE`. The game's first check does name `SETUP.EXE`
  in its error text, but tests for **`SETUP.CFG`**. Copying the executable
  achieves nothing; the config file must exist in the current directory.
  `gamedrive/` has it.
- It is **not** an unpacking artifact. The shipped packed binary fails
  identically from the same drive - which is how it was ruled out. One A/B run
  with a single variable beat a round of speculation about `unlzexe` fidelity.

Also worth knowing: `dos idle api` is read into a function-level `static` on
first call, so it is effectively boot-time. A runtime `CONFIG -set` will not
take effect.

## Two interfaces to the same emulator

| Port | Protocol | Used for |
|---|---|---|
| 2159 | GDB remote serial | registers, memory read/write, breakpoints, stepping |
| 4444 | QMP (QEMU monitor subset) | key/mouse injection, screendump, save states |

Both are enabled in `tubes.conf`. Addresses at both interfaces are **linear**:
real mode `(seg << 4) + offset`. The `eip` reported back is already linearised.

Two ways to drive it:

- **`bringup_tubes.py`** - a plain Python script against the fork's own
  `tests/integration/dosbox_debug.py`. No Claude Code restart needed; best for
  iterating on an experiment.
- **the MCP** - 24 `dosbox_*` tools, registered local-scope to this project
  directory. Best for exploratory work driven conversationally.

## Config choices that are load-bearing

`tubes.conf` is deliberately deterministic, because every experiment in
`PLAN.md` is "save state, change one variable, re-run":

| Setting | Why |
|---|---|
| `core = normal` | the dynamic/dynrec JIT cores defeat breakpoints |
| `cycles = fixed 20000` | `auto` retunes mid-run; fixed keeps two runs comparable |
| `cputype = 386` | period-appropriate, and one less variable |
| `machine = vgaonly` | Tubes is Mode X; `vgaonly` gives true VGA 256 KiB behaviour |
| `scaler = none`, `aspect = false` | captures come out a true 320x200, pixel-comparable with the port's own `--screenshot` |
| `nosound = true` | hardware still emulated, no host audio device to fail headless |
| `captures = .../capture` | otherwise screenshots land wherever cwd happens to be - possibly in this repo |
| `[autoexec]` stops at `C:\>` | does **not** launch the game, so a debugger can attach first |

## Debug the unpacked image

`assets-extracted/TUBES_UNP.EXE` is what Ghidra analysed and what every address
in the notes refers to, so that is what the rig runs. `gamedrive/TUBES.EXE`
symlinks to it; the shipped LZEXE-packed original is present as
`TUBESPKD.EXE` for reference only.

This matters for the entry breakpoint. Run the *packed* binary and
break-on-exec stops in LZEXE's decompressor stub, whose `CS` belongs to the
packer - so `CS` at entry would not be the game's load segment. Run the
unpacked one and it is, because its entry `CS:IP` is `0000:aaba`,
image-relative segment 0.

### The segment mapping, proven statically

Ghidra's base segment is an arbitrary `0x1000`; DGROUP is Ghidra segment
`0x2785`. So DGROUP is `0x1785` paragraphs into the image, at file offset
`0x1785 * 16 + 0x2200` (header) `= 0x19a50`. Reading the waypoint-target table
at `DS:0x26` straight out of the file gives `104, 122, 140, 158, 176, 194` -
byte-exact against the measured values. Six values agreeing by chance is not
credible, so the DGROUP segment number and the offset formula are confirmed
with no emulator involved.

Only the load segment `L` needs measuring at runtime:

    live_segment = L + (ghidra_segment - 0x1000)
    DGROUP       = L + 0x1785

`bringup_tubes.py` establishes it two independent ways and requires agreement:
**A** breaks at entry, checks `EIP - CS*16 == 0xaaba` (proof we stopped in the
right program), and reads back `DS:0x26`; **B** free-runs, then scans
conventional RAM for the 24 bytes the file holds at `DS:0x1a` - verified to
occur exactly once in the image - and checks the hit lands where A predicted.
A alone could be a wrong-but-consistent guess about the load address; B alone
finds an address without proving what it is.

### Do not invent a signature - read the notes first

The obvious signature is wrong. `DS:0x1a` does **not** hold the columns
ascending. It holds two *descending* triples, `143, 125, 107, 197, 179, 161`,
which columns 1..6 index in the order 3, 2, 1, 6, 5, 4;
`docs/reversing-notes.md` documents this. The familiar
`107, 125, 143, 161, 179, 197` is the mapping after that permutation, not a byte
run. A scan for it finds nothing. The real 24 bytes at `DS:0x1a` are:

    8f 00 7d 00 6b 00 c5 00 b3 00 a1 00 68 00 7a 00 8c 00 9e 00 b0 00 c2 00

Cost of learning this the slow way: one bring-up script written against a
signature that does not exist. It was caught by checking the guess against the
file before running the emulator - which is the cheaper order of operations.

## The watchpoint gap - read this before planning Experiment 3

`PLAN.md`'s highest-value experiment is "set a memory *write* breakpoint on
atom record 0's `+0x00`; whatever traps is the mover". The GDB stub **cannot do
that**. From `src/debug/gdbserver.cpp`, `handle_breakpoint`:

        if (bp_type != 0) {  // Only software breakpoints supported
            send_packet("");
            return;
        }

So `Z0` (software execution breakpoint) only; `Z2`/`Z3`/`Z4` - write, read and
access watchpoints - are silently declined. `qSupported` advertises `hwbreak+`,
which is misleading. The MCP inherits this: `dosbox_set_breakpoint` hardcodes
`Z0` in `dosbox_debug.py:281`.

DOSBox-X's *internal* debugger does have it - `BPM [seg]:[off]`, "set memory
breakpoint (memory change)", `src/debug/debug.cpp:2517`, backed by
`CBreakpoint::AddMemBreakpoint`. `C_HEAVY_DEBUG=1` in this build, so it is
compiled in. It is simply not exposed over the wire.

Three routes, in order of preference:

1. **Patch the stub.** Map `Z2` onto `AddMemBreakpoint(0, linear)` - the
   internal debugger already calls it with `seg = 0` and a flat offset
   (`debug.cpp:2538`), so the linear address the stub receives goes straight
   in. Needs a `DEBUG_SetMemBreakpoint` pair in `debug.cpp`/`debug.h`
   mirroring the existing `DEBUG_SetBreakpoint` (`debug.cpp:6255`), the type
   check above relaxed, then a `type=` argument threaded through
   `dosbox_debug.py` and `tools.py`. Small, and it makes the whole plan
   agentic. Both checkouts are ours to patch.
2. **The curses debugger TUI.** `BPM` works today, no code changes. But it is
   interactive - it cannot be driven by an agent, which is the whole point of
   installing this rig.
3. **Step and diff.** Break on a known per-frame address, then single-step
   while re-reading the four bytes. Works with what exists; thousands of RSP
   round-trips per frame, so slow but bounded.

Experiments 1, 2 and 4 in `PLAN.md` need none of this - they are memory reads,
execution breakpoints and save states, all supported today.

## Verified working

`bringup_tubes.py` passes end to end. What it establishes:

    CS at entry  = 0x0824,  EIP - CS*16 = 0xaaba   (matches the EXE header)
    load segment = 0x0824
    DGROUP       = 0x0824 + 0x1785 = 0x1fa9
    DS:0x26 readback = 104 122 140 158 176 194     (matches the file)
    signature found at 0x1faaa = 0x1fa90 + 0x1a    (A and B agree)
    BIOS video mode 0x13 -- the game reaches its splash screens

**Do not hardcode `L = 0x0824`.** It is a function of the DOS memory layout, so
it moves if the conf, the DOS version or anything resident changes. Measure it
at the entry breakpoint every session; the script does.

Mode 13h is what the BIOS reports even though the game runs Mode X - Mode X is
reached by reprogramming the CRTC out of mode 13h, which does not change the
BIOS mode byte. So `0x449 == 0x13` is the right "game is up" signal.

### Screenshot quirks

- **The capture always succeeds; the path reporting races.** The PNG reliably
  lands in the `captures` directory as `tubes_NNN.png`, but
  `CAPTURE_GetLastScreenshotPath()` is read on a different thread from the write,
  so the reply - and the copy to a requested `path` - intermittently fails with
  "Screenshot capture failed - no file created" *even though the file was
  written*. Confirmed against the DOSBox-X log. **Rule: take the shot, then read
  the newest file in `captures`.** Do not trust the returned path or a
  `path=` argument.
- **A screenshot needs the guest running.** The handler polls until the render
  thread completes the capture, so calling it while the CPU is halted blocks for
  its full 5 s and then times out. Continue first, then capture.
- Captures come out **640x400**, not 320x200, despite `scaler = none`: mode 13h
  is double-scanned to 400 lines and DOSBox-X doubles horizontally too. Halve
  both axes before comparing against the port's own `--screenshot` output.

### Other things learned driving it

- **A timed-out tool call desynchronises the GDB stream.** After the screenshot
  timeout above, every register came back as `0` - the RSP framing was out of
  step, not the guest. There is no resync; `dosbox_restart` is the fix. Treat
  all-zero registers as "the connection is broken", never as guest state.
- **Mode X planes are not visible at `0xa0000`.** Reading there through the stub
  returned all zeros mid-cutscene, so framebuffer content cannot be used as a
  cheap screen-state test. Use `dosbox_screenshot` and look at the image.
- **Held-key state is not observable through the MCP.** `dosbox_press_key`
  blocks for its whole `hold_ms` and returns only after the release, and both
  the GDB stub *and* the QMP server are single-client - a second connection to a
  running instance is accepted but never gets a QMP greeting. Anything needing a
  key held across a memory read must be a standalone script using
  `key_down`/`key_up`. See `exp0_input_bits.py`.
- **Menu navigation should not be timed.** The blackboard cutscene is multi-page
  and its length varies between runs, so fixed waits are flaky. Pressing Enter
  repeatedly walks cutscene -> title -> menu -> Start Game, and once a
  breakpoint fires the CPU is halted so overshooting is harmless. Crude, but it
  is the only approach here that worked repeatably.

## Footguns

- **`pkill -9 -f dosbox-x` on startup.** `DOSBoxInstance.start()` calls
  `_kill_existing()`, which kills *every* DOSBox-X on the machine, not just its
  own. Do not start a run while a manual DOSBox-X session matters.
- **One client at a time.** The GDB stub services a single connection.
  Two MCP instances, or the MCP plus a `gdb`, will fight.
- **Listener up != DOS ready.** The TCP ports open about a second after launch;
  DOS needs several more. Wait for an explicit signal - the `C:\>` prompt, or
  the BDA video-mode byte at linear `0x449` leaving `0x03`.
- **Halting stops everything.** No timer ticks, no keyboard polling while
  halted. Fine for inspection, wrong for anything that expects time to pass.
- **Sticky keystrokes.** A partial line left in the input buffer gets committed
  by the next `\r`. Reset input, or restart, if unsure.

## Provenance

- fork: <https://github.com/lokkju/dosbox-x-remotedebug> (branch `remotedebug`)
- MCP: <https://github.com/jdmichaud/dosbox-mcp>
- protocol reference: the fork's `docs/REMOTEDEBUG.md`

Neither is vendored into this repo; both are upstream clones, unmodified so
far. If route 1 above is taken, the patch should be kept as a tracked diff
somewhere in `~/Dev/tubes-tooling/` so a fresh clone can be brought back up.
