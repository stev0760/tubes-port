# Tubes — reverse engineering notes

Running notes on the original DOS build. Facts here are derived from the
binary and the shipped documentation; no original game data lives in this
repository.

## Original files

| File | Size | What it is |
|---|---|---|
| `TUBES.EXE` | 46,037 | Main game, LZEXE v0.91 compressed |
| `TUBES.RES` | 524,020 | Custom asset container ("Absolute Magic Resource File!") |
| `DRIVERS.RES` | 7,452 | Same container format; holds 8086 sound-driver blobs |
| `SETUP.EXE` | 13,406 | Config tool, also LZEXE compressed |
| `SETUP.CFG` | 65 | Hardware selections written by SETUP |
| `TUBES.SAV` | 960 | Saved game (zero-filled when empty) |

## TUBES.EXE — LZEXE v0.91

Signature `LZ91` at offset `0x1C`. Nothing in the file is analyzable until
it is decompressed; run `tools/unpack.sh`.

Packed MZ header:

| Field | Value |
|---|---|
| `e_cparhdr` | 2 (32-byte header) |
| `e_cs:e_ip` | `0A9A:000E` |
| `e_crlc` | 0 (stub rebuilds relocations) |

LZEXE stores the original entry state in the 14 bytes immediately below the
stub entry point, which is why `e_ip` is `0x0E`. At file offset `0xA9C0`:

| Offset | Value | Meaning |
|---|---|---|
| +0x00 | `AABA` | original IP |
| +0x02 | `0000` | original CS |
| +0x04 | `2000` | original SP |
| +0x06 | `1A70` | original SS |
| +0x08 | `0A9A` | packed size in paragraphs (43,424 bytes) |
| +0x0A | `0DD1` | size increase in paragraphs |
| +0x0C | `0A15` | decompressor length (2,581 bytes) |

Unpacked result: 99,728-byte image, 2,150 relocations, entry `0000:AABA`,
stack `1A70:2000`. The recovered `SS:SP` and `CS:IP` match the info block
above exactly, which is a good end-to-end correctness check.

### Format details worth recording

Forward LZ77 over an LSB-first bit stream buffered as little-endian words:

- `1` — literal byte
- `00` + 2 length bits — short match, length 2–5, 1-byte distance (−1..−256)
- `01` — long match, 13-bit distance (−1..−8192), 3-bit length

When the 3-bit length field is zero, an extra byte follows: `0` ends the
stream and `1` is a segment-normalization no-op; any other value is
`length + 1`.

Two traps, both of which cost time here:

1. Some online descriptions have the two escape values swapped. The
   authoritative behavior is `0` = end, `1` = segment change.
2. The bit reader must sample the bit *before* decrementing the counter, and
   must not shift on the refill step. Getting this wrong yields 16 bits from
   the first flag word instead of 15 and desynchronizes the stream
   immediately — with symptoms that look like a correct decoder, since the
   first 15 bytes still decode into valid x86.

We use the upstream `unlzexe` rather than a reimplementation.

## Toolchain identification

The binary is **Borland Pascal 7.0**, not C. Evidence:

- The in-game credits screen states Tubes was written in Borland Pascal v7
  and uses a planar 320x200x256 mode with multiple pages (i.e. Mode X).
- `Portions Copyright (c) 1983,92 Borland` plus `Runtime error ` / ` at ` are
  the Borland Pascal 7.0 RTL error handler.
- `Copyright 1994 Absolute Magic`.
- The binary refuses to run below an 80286, consistent with the `enter`
  prologues seen at the entry point.

Consequences for decompilation:

- BP7 is a non-optimizing, highly deterministic single-pass compiler. Code
  generation is regular and maps back to source structure closely.
- Calling convention is Pascal, not cdecl: arguments pushed left-to-right,
  callee cleans the stack (`retf N`). Ghidra must be told this or every
  signature will be wrong.
- Programs are built from units, which are linked as contiguous blocks. Unit
  boundaries should be recoverable, allowing the binary to be attacked one
  unit at a time.
- Smart linking means only referenced routines are present.
- A large fraction of the image is stock RTL (System, Crt, Dos). These should
  be identified and left alone rather than decompiled.

Because BP7 itself runs under DOSBox, a **matching decompilation** is
realistic: recompile candidate Pascal source and diff the generated code
against the original image. Byte equality is a much stronger correctness
signal than playtesting, and does not require reaching late-game content.

## Toolchain reproduction

Turbo Pascal 7.0 (BIN dated 1992-10-30) runs headless under DOSBox and
compiles successfully, which makes an automated compile-and-diff loop
possible:

    SDL_VIDEODRIVER=dummy dosbox -noconsole \
      -c "mount c <dir>" -c "c:" -c "cd work" \
      -c "..\\tp7\\bin\\tpc.exe HELLO.PAS" -c "exit"

`TPC.EXE` is the command-line compiler and the one to automate; `TURBO.EXE`
is the IDE and cannot be scripted. Note the shipped `SOURCE/` directory is
Turbo Vision and WinDos only - the core System / Crt / Dos units exist solely
as compiled code inside `TURBO.TPL`.

### Version confirmation

Compiling a trivial program and byte-comparing its image against
`TUBES_UNP.EXE` (see `tools/rtl_match.py`) gives **79.4% of the reference
image matching**, with 21 runs totalling 1,326 bytes sharing a single delta
of `0x016810`. A whole linked unit relocates as one block, so a shared delta
across many runs means the System unit sits contiguously in Tubes at that
offset.

This is strong evidence the toolchain version is correct, and therefore that
a matching decompilation is achievable. The residual mismatch is expected:
bytes holding segment values differ because relocation targets differ between
the two programs.

Caveats worth keeping in mind:

- Tubes is dated 1994 and TP 7.01 exists; if individual routines refuse to
  match later, a point-release codegen difference is the first suspect.
- The reference was a hello-world, so it only pulls in the slice of System
  that `WriteLn` needs. Real RTL usage in Tubes (Crt, Dos, more of System)
  is certainly larger than what this first pass found.
- Single-run deltas are likely coincidental matches on common byte patterns,
  not real RTL. Only multi-run clusters should be trusted.

## Graphics

Mode X (unchained VGA, 320x200x256, planar) with multiple video pages for
page flipping. Expect writes to the VGA sequencer (`3C4h`/`3C5h` map mask)
and CRTC (`3D4h`/`3D5h`) for page start address.

## Ghidra

Import settings that work:

    ghidra-headless <project-dir> tubes -import TUBES_UNP.EXE \
        -processor "x86:LE:16:Real Mode" -cspec default

Ghidra selects the "Old-style DOS Executable (MZ)" loader and runs its
Segmented X86 Calling Conventions analyzer. Result: **297 functions**,
75,832 bytes covered, entry at `1000:aaba`.

Notes:

- Ghidra 12.2 ships Jython only as an uninstalled extension, so `.py` scripts
  do not run out of the box and PyGhidra wants an interactive venv install.
  Java `GhidraScript` files compile on the fly with no setup - use those.
- `-scriptPath` must be absolute, as must the project directory (a leading
  `./` is rejected outright).
- Available conventions include `__stdcall16far` / `__stdcall16near`. Pascal
  is callee-cleans, so `__stdcall16far` is the right default for game code.

### Segment layout = unit layout

Borland Pascal links each unit as its own segment, so Ghidra's 24 `CODE_*`
blocks correspond to program units. Base segment `0x1000` maps to image
offset 0.

| Block | Segment | Size | Notes |
|---|---|---|---|
| `CODE_0` | `1000` | 45,792 | contains entry `1000:aaba` - main program |
| `CODE_1` | `1b2e` | 25,760 | largest unit after the main block |
| `CODE_2`..`CODE_15` | `2178`..`2407` | ~12,300 total | game units |
| `CODE_16`..`CODE_23` | `2475`..`2785` | ~15,900 total | RTL |
| `DATA` | `2785:0d40` | 16,752 | |

Cross-checking against `rtl_match.py` confirms the split. The largest RTL
cluster spans image `0x160a9`..`0x16690`, which coincides almost exactly with
`CODE_19` (`0x16070`..`0x16690`); the remaining clusters all fall inside
`CODE_22` (`0x16850`..`0x17850`), which is therefore the System unit.

So roughly **84KB of game code across 16 units, and 16KB of RTL** that can be
ignored. RTL sits at the end of the image, after the game units.

### Pascal strings are not C strings

Ghidra's auto-analysis found only 50 strings and zero references to them,
despite the binary being full of text. Turbo Pascal uses `ShortString`: a
single length byte followed by up to 255 characters, with no terminator.
Ghidra's string analyzer searches for NUL-terminated data and misses almost
all of it.

A custom scanner is needed - look for a length byte `n` followed by exactly
`n` printable bytes - after which cross-references become usable and the
string-to-subsystem mapping should fall out.

## Resource containers

Both `.RES` files begin with the ASCII banner `Absolute Magic Resource File!`
followed by `\r\n\x1a` (32 bytes total), then what appears to be a table.

`TUBES.RES` bulk data shows a strongly regular pattern of `FF` followed by
8-byte groups, suggesting a run/literal marker scheme over VGA pixel data.
Unconfirmed.

`DRIVERS.RES` contains real 8086 code — `55 8B EC ... CA 02 00`
(`push bp; mov bp,sp; ... retf 2`), i.e. far-called driver entry points for
the PC speaker / Sound Blaster / Adlib backends. **These are not needed for
the port**; SDL replaces them entirely.

## Game design (from TUBES.DOC)

Falling-atom puzzler in the Columns lineage. Catch atoms from dispenser
tubes with a player-controlled test tube and place them into a beaker;
match 3+ of a colour horizontally, vertically, or diagonally.

- Two modes: **Wave** (clear a task count) and **Endurance**
- Difficulty 101 / 201 / 301 → 9 / 6 / 3 drops allowed; also sets atom
  fall speed and spawn rate
- Loss conditions: exceeding the drop limit, or filling the beaker
- Registered version adds ANTI-MATTER and BONUS atom types
- In-game keys: `ESC` abort, `F1` help, `F2` save, `F3` music toggle,
  `F4` sound toggle, `F5` pause
- HUD tracks drops remaining, tasks remaining, and chains made

Strings recovered from the unpacked binary corroborate all of the above and
additionally reference `DEMO.SCR` and a `GAMEBG` resource name.

## Open questions

- `TUBES.RES` directory structure and compression scheme
- Scoring tables, chain multipliers, wave progression curves
- Atom spawn RNG and distribution
- `SETUP.CFG` field layout (65 bytes)
- `TUBES.SAV` layout (960 bytes)
