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

`ghidra_scripts/FindPascalStrings.java` handles this: pass 1 finds a length
byte `n` followed by exactly `n` printable bytes and defines it as
`PascalString255`; pass 2 synthesises cross-references. Result: **811 strings
and 709 references across 75 functions**, versus 50 strings and 0 references
from stock analysis.

Two things that matter if you modify the script:

- String constants live in each unit's **code** segment, not `DATA`, so
  references are CS-relative. Match on
  `SegmentedAddress.getSegmentOffset()`, not `getOffset()` - the latter
  returns the flat linear address, which never appears in an instruction
  operand. Restricting matches to the referring instruction's own segment
  also suppresses most coincidental scalar hits.
- The script is re-runnable, so it must *adopt* strings a previous run
  already defined rather than skipping them as "existing data", or pass 2
  sees an empty set.

## Subsystem map

Derived from string references. This is the current best guess at what the
major functions do:

| Function | Size | Role |
|---|---|---|
| `1b2e:2d63` | 4,510 | Slideshow / cutscene player (prev-slide, next-slide, instructions) |
| `1000:aaba` | 2,096 | Entry: SETUP check, VGA check, memory check, resource load |
| `1b2e:1651` | 2,323 | Text/font rendering (`WRITE0.GFX`..`WRITE7.GFX`) |
| `1000:9e53` | 2,173 | Playfield rendering (test tubes, beaker, foreground) |
| `1000:2dd0` | 3,064 | In-game help / key list screen |

### Resource naming

String references expose the asset names inside `TUBES.RES`, which is a
strong lead for the container format work. Two extensions are in use:

- `*.GFX` - graphics; includes `STAR1`/`STAR2`, `WRITE0`..`WRITE7` (font
  glyph pages), `GAMEFG` (and the `GAMEBG` seen earlier)
- `*.CSP` - appears to be a sprite format; includes `TESTUBE1`..`TESTUBE3`,
  `TESTUBES`, `BEAKER`, `BEAKERS`, `TUBEH`

The paired plain/`S` naming (`BEAKER`/`BEAKERS`, `TESTUBE1`/`TESTUBES`)
suggests a shadow or mask variant alongside each sprite. Unconfirmed.

## Resource containers

Both `.RES` files begin with the ASCII banner `Absolute Magic Resource File!`
followed by `\r\n\x1a` (32 bytes total), then what appears to be a table.

The container format is **solved**, recovered by decompiling the loader at
`2407:0146`. See `tools/res_extract.py`.

    header (39 bytes)
      [0..31]   "Absolute Magic Resource File!\r\n\x1a"
      [32]      format version (always 1)
      [33..34]  entry count            u16
      [35..38]  directory file offset  u32

    directory: `count` x 26-byte entries at the directory offset,
    running exactly to end of file
      [0]       name length (Pascal ShortString)
      [1..12]   name, NUL-padded to 12 bytes (8.3)
      [13]      type / flags (always 1 in both shipped files)
      [14..17]  uncompressed size      u32
      [18..21]  stored size            u32
      [22..25]  file offset            u32

Validated against both shipped containers with zero discrepancies: payloads
start at offset 39 immediately after the header, are laid out contiguously
with no gaps, end exactly where the directory begins, and the directory ends
exactly at EOF.

| | `TUBES.RES` | `DRIVERS.RES` |
|---|---|---|
| entries | 228 | 11 |
| directory at | 518,092 | 7,166 |
| stored -> raw | 518,053 -> 1,437,240 (36.0%) | 7,127 -> 8,952 (79.6%) |

Content of `TUBES.RES` by extension:

| Ext | Count | Likely |
|---|---|---|
| `CSP` | 108 | sprites |
| `GFX` | 73 | images / bitmaps |
| `SFX` | 24 | digital sound effects |
| `MUS` | 10 | FM/Adlib music |
| `816`, `88` | 5 | fonts? (8x16 / 8x8) |
| `PAL` | 3 | palettes |
| `SPR` | 2 | sprites, second form |
| `SCR` | 1 | `DEMO.SCR` - cutscene script |
| `ANM` | 1 | animation |
| `BIN` | 1 | raw data |

`DRIVERS.RES` holds 11 `.DRV` payloads, compressing far less well (79.6%),
consistent with them being 8086 code rather than pixel data.

### Payload compression - solved: LZSS

The `FF`-plus-8-byte-groups guess was wrong. The codec is **Okumura's classic
LZSS**, recovered from `2475:115c` (setup) and `2475:10dc` (main loop).

The fetch path is `2407:0471`. It branches on the directory entry's flags
byte: `0` reads the payload raw in `0x2000` chunks, `1` decompresses. It
allocates two 8KB buffers, stores the destination pointer and remaining input
count in globals, and passes a far callback (`0x2407:004e`) used to refill
the input - i.e. the decompressor is streaming, not whole-buffer.

Parameters, all read directly out of the disassembly:

| Constant | Value | Seen as |
|---|---|---|
| ring buffer `N` | 4096 | offsets masked with `0xfff` |
| longest match `F` | 18 | `(b & 0xf) + 3` |
| `THRESHOLD` | 2 | shortest encoded match is 3 |
| ring pre-fill | `0x20` (space) | fill of `0x1011` = `N + F - 1` bytes |
| start position `r` | `0xfee` = `N - F` | |

Encoding: a flag byte supplies 8 control bits, LSB first. A set bit means one
literal byte. A clear bit means a two-byte match reference - offset is
`lo | ((hi & 0xf0) << 4)` (12 bits) and length is `(hi & 0x0f) + 3`.

Implemented in `tools/res_extract.py` as `lzss_decompress()`.

**Validation: 239/239 payloads across both containers decompress to exactly
their recorded uncompressed size** (228 in `TUBES.RES`, 11 in `DRIVERS.RES`).

Independent structural confirmation, which does not depend on the size
oracle: all three `.PAL` resources decompress to exactly 768 bytes = 256 x 3
VGA palette entries. The `.DRV` payloads begin with ascending 16-bit values
(`0x22, 0x26, 0x2b, 0x38 ...`), consistent with an entry/jump table at the
head of each driver.

`tools/res_extract.py EXTRACT <container> <outdir>` now writes fully
decompressed assets.

## .CSP - compiled sprites (solved)

`CSP` stands for **Compiled Sprite**. A `.CSP` is not a bitmap: it is
generated 16-bit x86 code that draws the sprite with unrolled stores and
returns via `retf`. Transparency is implicit - undrawn pixels simply have no
instruction. This was a common DOS speed technique: no per-pixel loop, no
mask test.

The entire instruction grammar:

    c6 44 dd ii              mov byte [si+disp8],  imm8
    c7 44 dd ii ii           mov word [si+disp8],  imm16
    c6 84 dddd ii            mov byte [si+disp16], imm8
    c7 84 dddd ii ii         mov word [si+disp16], imm16
    d0 c0                    rol al,1
    83 d6 00                 adc si,0
    ee                       out dx,al
    cb                       retf

`si` is the destination byte offset, the immediate is the palette index, and
the last three are the Mode X plane switch. They are **independent
instructions, not a fixed block** - `rol`/`adc` may repeat with no
intervening `out` to skip planes containing no pixels, so a decoder has to
track the carry flag across instructions rather than pattern-match a group.
Five shadow sprites (`TUBEVS`, `TUBEVRS`, `TUBEVLS`, ...) do exactly this.

`al` holds the VGA sequencer map mask and starts at `0x11`. Rotating left
gives `0x11 -> 0x22 -> 0x44 -> 0x88 -> 0x11`, so carry-out is set exactly
once per four planes and `adc si,0` advances one byte per four pixels; the
sequencer only reads the low nibble, making the plane `bit_index(al & 0x0f)`.

Screen mapping with a Mode X plane stride of 80 bytes (320 / 4):

    x = (byte_offset % 80) * 4 + plane
    y =  byte_offset // 80

`tools/csp_decode.py` implements this. **All 108 sprites parse cleanly** with
no unknown opcodes and each `retf` landing exactly at end of file. Rendering
them against `TUBES.PAL` produces correct, recognisable artwork with sane
shading gradients, which independently confirms the palette decode and the
plane mapping.

Contents: atoms in eight colours (matching the eight elements of the story)
with multi-frame fade/destruction sequences, lettered special atoms
(`B`, `C`, `X`, `F`, `M`, `?`), test tube and beaker outlines, and the
dispenser tube segments.

    tools/csp_decode.py INFO   <file.CSP>...
    tools/csp_decode.py RENDER <palette.PAL> <outdir> <file.CSP>...

## .GFX - raster images (solved)

    u16   width
    u16   height
    u8[]  width * height pixels, 8-bit palette indices

Pixel data is **planar**, in the same Mode X layout the compiled sprites draw
into. All of plane 0 comes first as a contiguous `(width / 4) x height`
block, then planes 1, 2 and 3:

    plane = x % 4
    index = plane * (width // 4) * height + y * (width // 4) + x // 4

Decoding it as chunky produces the image tiled 4x horizontally and squashed
4x vertically - a useful symptom to recognise if this regresses.

### The 0xE5 variant

Three resources - `SOFT.GFX`, `CLOUD.GFX`, `AMWRITE.GFX` - carry an extra
leading `0xE5` byte before the header. These are exactly the three fetched
through a different routine (`21ea:045f` rather than `21ea:03c4` /
`21ea:035b`), which is what flagged them in the first place.

The prefix marks **chunky** storage: their pixels are already linear and must
not be de-planarized. Confirmed visually - `SOFT.GFX` decodes to the
publisher title card only when treated as chunky.

### Palettes are per-scene

There is no single global palette. `SOFT.GFX` requires `SOFT.PAL`,
`CLOUD.GFX` requires `INTRO.PAL`, and the in-game art uses `TUBES.PAL`.
Rendering with the wrong one yields structurally correct but wildly
miscoloured output, which is easy to mistake for a layout bug.

All 73 `.GFX` resources parse and render correctly:
backgrounds (`GAMEBG1`..`GAMEBG10`, fractal artwork behind the playfield),
the `GAMEFG` overlay, a chemistry blackboard, the Nobel prize trophy,
explosion frames, and the `WRITE*` scientist animation cells.

    tools/gfx_decode.py INFO   <file.GFX>...
    tools/gfx_decode.py RENDER <palette.PAL> <outdir> <file.GFX>...

## .SCR - recorded demo (solved)

**Correction to an earlier assumption**: `DEMO.SCR` is not a cutscene script.
It is a recording of player input for attract mode, replayed through the
normal game loop. The giveaway was that its referencing function is
`1000:5f4b`, not the slideshow player at `1b2e:2d63`, and the binary also
carries a `Recording` string.

`1000:5f4b` is the attract-mode setup: it calls `Random(10)` in a loop that
rejects a repeat of the previous value, picking one of `GAMEBG1`..`GAMEBG10`
as the backdrop, loads it plus `DEMO.SCR`, and stores the demo pointer at
`DS:0xd24`. That pointer is read by `1000:3a67` - at 9,382 bytes the largest
function in the binary, i.e. the main game loop.

    u16   frame count (bytes following this field)
    u32   RNG seed          [inferred]
    u8[]  one input bitmask per frame

| Bit | Control | Evidence in the shipped demo |
|---|---|---|
| `0x01` | up | 20 frames, never held |
| `0x02` | down | 1357 frames, runs to 30 - held to drop faster |
| `0x04` | left | 280 frames, 146 runs, mean 1.92 |
| `0x08` | right | 258 frames, 125 runs, mean 2.06 |
| `0x10` | button A | 134 frames, never held |
| `0x20` | button B | unused |

Bit assignments are **inferred from behaviour**, not yet read out of the
input handler. The reasoning: `0x04` and `0x08` have near-identical run
statistics as a left/right pair should; only `0x02` is held for long
stretches; and across 11,970 frames the impossible combinations never occur -
`0x03` (up+down) and `0x0c` (left+right) are entirely absent - while the four
combinations that do appear (`0x05`, `0x06`, `0x0a`, `0x18`) are all legal
diagonals or direction+button. This matches the manual's cursor-keys plus
Button A / Button B scheme.

The 4 header bytes are read as an RNG seed on the grounds that a replay must
reproduce the same atom sequence to stay in sync, and Turbo Pascal's
`System.RandSeed` is a 32-bit LongInt. **Not confirmed** against the playback
code - worth verifying before relying on it.

Decoded, the demo reads as ordinary play: nudge left, hold down to descend,
tap right to line up, press A, repeat. 83.1% of frames are idle.

    tools/scr_decode.py INFO <file.SCR>
    tools/scr_decode.py DUMP <file.SCR> [maxframes]

## .816 / .88 - bitmap fonts (solved)

Standard VGA glyph tables. No header at all: 256 characters, one byte per
scanline, most significant bit leftmost. The extension encodes the cell size
and the file size follows from it.

| Ext | Cell | Size | Files |
|---|---|---|---|
| `.816` | 8 x 16 | 4096 | `FUTURE`, `SCRIPT`, `STARTREK`, `THIN8X8` |
| `.88` | 8 x 8 | 2048 | `TINY6X8` |

Cell width is always 8 because a scanline is exactly one byte. Fonts that are
narrower in practice leave the right-hand columns clear, so advance width is
a renderer decision rather than a property of the file - `THIN8X8` uses 7
columns, `TINY6X8` uses 7 of its 8.

`THIN8X8.816` is worth noting: despite the name it is stored in 8 x 16 cells
with the lower 8 rows blank, i.e. a genuine 8x8 font padded into the larger
cell. The extension describes the storage, not the design.

Glyph coverage: the four `.816` fonts define 253 of 256 codes; `TINY6X8`
defines only 97, essentially the printable ASCII range.

    tools/fnt_decode.py INFO   <font>...
    tools/fnt_decode.py RENDER <outdir> <font>...
    tools/fnt_decode.py SHOW   <font> <char>

## .SFX - digital sound effects (solved)

    [0]        0xf1 marker
    [1..0x1f]  Pascal ShortString name, zero-padded (1 length + 30 chars)
    [0x20..23] u32 sample rate
    [0x24]     unknown, 0 in every shipped sound
    [0x25..26] u16 sample count
    [0x27..]   unsigned 8-bit PCM, silence at 0x80

Verified across all 24 resources: the marker is always `0xf1`, the rate is
always 8000 Hz, and the count at `0x25` equals `filesize - 0x27` exactly
every time. Durations run 0.03s to 2.4s.

Two independent confirmations that the PCM interpretation is right:

- Every file has a mean sample value of 125-130, i.e. centred on `0x80`.
  A wrong sign convention or offset would scatter these.
- Plotted waveforms show real acoustic structure: `HITATOM` is a single
  sharp transient decaying to silence, `CLAP` is six discrete evenly spaced
  spikes separated by true silence (hence its 85.6% silent measurement),
  `BUBBLE` is repeated bursts.

The sounds carry human-readable names in the header, which conveniently
document themselves - "Smack!", "Glass Clink", "Bubbles!", "Wooosshh!",
"Switch On/Off", "Lightning". `GLDFADE.SFX` has a zero-length name.

The byte at `0x24` is zero everywhere, so the shipped data cannot constrain
its meaning. It may be a format flag or the high byte of a wider length
field; nothing distinguishes those when every sound is under 64KB.

Conversion to WAV is direct - WAV 8-bit is also unsigned, so the sample data
is copied verbatim.

    tools/sfx_decode.py INFO <file.SFX>...
    tools/sfx_decode.py WAV  <outdir> <file.SFX>...

### Format status

| Format | Count | Status |
|---|---|---|
| container + LZSS | - | solved |
| `.CSP` compiled sprites | 108 | solved, all 108 render |
| `.GFX` raster images | 73 | solved, all 73 render |
| `.SFX` digital audio | 24 | solved, all 24 convert to WAV |
| `.MUS` FM/Adlib music | 10 | solved, all 10 decode |
| `.816` / `.88` fonts | 5 | solved |
| `.PAL` palettes | 3 | solved |
| `.SPR` sprites | 2 | not examined |
| `.SCR` demo recording | 1 | solved |
| `.ANM` animation | 1 | not examined |
| `.BIN` raw data | 1 | not examined |

Every format the game actually loads is now decoded. `.SPR`, `.ANM` and
`.BIN` are one or two resources each and have not been looked at yet.


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

## Porting notes

Traps encountered building the C++ renderer that the Python tools did not hit:

- **Negative offsets need floor division.** A `.CSP` addresses pixels either
  side of its base pointer, so plane offsets go negative. Python's `//` and
  `%` floor toward negative infinity; C++ truncates toward zero. Using the
  native operators maps `off = -128` to row -1, column -48 instead of row -2,
  column 32, which silently produces a 334x12 sprite with origin (-190,-1)
  instead of 16x13 at (128,-2). The pixel *count* stays correct, so only the
  geometry is wrong - it renders as a flat sliver rather than failing.
- **Sprite origin is provenance, not placement.** `originX` is around 128 for
  these resources because it reflects the base pointer the original game
  passed in. Adding it to a draw position pushes sprites off-screen. Keep it
  as metadata and place by the bounding box top-left.

Cross-checking a new decoder against the Python tools (`csp_decode.py INFO`
prints dimensions, origin and pixel count) catches both immediately.

## Gameplay constants - NOT reverse engineered

The engine's playfield logic follows TUBES.DOC, not the binary. These values
are hand-tuned to play sensibly and should be replaced with real ones:

| Constant | Current | Source |
|---|---|---|
| cell size | 16 x 13 | **real** - measured from atom sprites |
| playfield x range | 74..245 | **real** - gap between tube walls in `GAMEFG.GFX` |
| grid columns / rows | 7 x 10 | guess |
| spawn interval | 1.6 s | guess |
| fall speed | 30 / 42 / 56 px/s | guess, scaled by difficulty |
| score per atom | 10 | guess |
| chain bonus | 25 per extra round | guess |
| drop limits | 9 / 6 / 3 | **real** - stated in TUBES.DOC |

The playfield renderer at `1000:9e53` should yield the true grid dimensions
and cell origin; the scoring tables live somewhere in the main loop at
`1000:3a67`.

Input uses the same bit layout as a recorded `.SCR` demo (`0x01` up, `0x02`
down, `0x04` left, `0x08` right, `0x10` A, `0x20` B) so a recording can later
be fed into the same update path as live input.

## .MUS - FM/Adlib music (solved)

Recovered by disassembling `DRIVERS.RES:FMMUSIC.DRV` and cross-checking every
command against `GMMUSIC.DRV`, which consumes the identical byte stream and
emits General MIDI. Two independent consumers agreeing on the meaning of each
byte is what made this provable rather than plausible.

All offsets below are file offsets into `FMMUSIC.DRV`. Decoder:
`tools/mus_decode.py`.

### Container

32-byte header: marker `0xf5` followed by 31 reserved bytes, zero in every
resource. The event stream starts at `0x20`.

This is not inferred. The driver's play entry at `0x996` does exactly:

        80 3c f5    cmp byte [si], 0xf5
        75 26       jne <reject>
        83 c6 20    add si, 0x20

There is no length field because the stream is self-delimiting - see `0xf0`.

### Event stream

`<delta:u8> <cmd:u8> <args...>`, dispatched on the high nibble at `0x83d`.
The low nibble is the channel.

| cmd | args | handler | meaning |
|---|---|---|---|
| `0x1c` | 12 | `0x1e8` | set instrument on channel *c* |
| `0x2c` | 1 | `0x378` | set volume / velocity, 0..127 |
| `0x3c` | 1 | `0x388` | note on, MIDI note number |
| `0x4c` | 0 | `0x420` | note off |
| `0x5c` | 2 | `0x454` | pitch bend, MSB first, centre `40 00` |
| `0xf0` | 0 | inline | end of song |

`delta` counts timer ticks. A delta of `0` means "still this tick", so any
number of events can share a timestamp; the tick routine loops back to `0x835`
until it reads a non-zero delta.

`0xf0` sets the read offset back to `0` and does **not** stop playback, so
every song loops. It also sets a flag at `cs:0x32` which the game can poll to
learn that a one-shot has finished. An unrecognised command clears the
playing flag - a defensive stop, not a documented feature.

### Channels: 11, not 9

The driver enables OPL2 **rhythm mode** permanently - `reg 0xBD = 0xE0` at
`0x1b2` - and never turns it off. So the channel map is 6 melodic voices plus
5 percussion voices:

| channel | 0..5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|
| voice | melodic | BD | SD | TT | CY | HH |

The operator table at `cs:0x3a` is `00 01 02 08 09 0a 10 14 12 15 11`, which
is the standard AdLib slot map: melodic modulators, then bass drum, snare,
tom, cymbal, hi-hat. Channels 0..6 are two-operator; 7..10 are single
operator, which is why the loader guards every carrier write with
`cmp bh,6 / ja`.

Channel indices reaching 10 is what blocked the previous attempt - they look
impossible for a 9-voice chip until you find the rhythm-mode table.

### Instrument payload (12 bytes)

- `[0]` **General MIDI program number** for melodic channels, or a **GM
  percussion note number** for channels 6..10. The FM driver skips this byte
  outright (`inc si` at `0x847` before calling the loader); `GMMUSIC.DRV`
  sends it as a Program Change at `0xf4`.
- `[1..11]` the 11 classic AdLib registers, written in operator pairs:
  `20/23`, `40/43`, `60/63`, `80/83`, `E0/E3`, then `C0`. For single-operator
  channels the carrier half is read and discarded, so the record is always
  the same size.

The loader also caches each Total Level byte at `cs:0x4d`, indexed by operator
slot, because volume changes have to re-derive TL from the patch.

### Volume

`0x2c` stores the value at `cs:0x6f[channel]` and immediately recomputes TL at
`0x31a`: `TL = min((patchTL & 0x3f) + attenuation[vol], 0x3f)`, preserving the
KSL bits. `attenuation` is a 128-entry table at `cs:0x29a` running 63 down to
0, i.e. MIDI velocity range with 0.75 dB steps and no attenuation at 127.

### Pitch

The note byte is a MIDI note number; the driver subtracts 12, so its note 0 is
MIDI 12 = C0. Frequency comes from three tables:

| table | at | size | contents |
|---|---|---|---|
| F-number | `0x458` | 192 words | 12 pitch classes x 16 bend steps |
| block | `0x5d8` | 96 bytes | note -> OPL block |
| pitch class | `0x638` | 96 bytes | note -> F-number table row |

The sign bit of an F-number entry is a flag, not magnitude: a negative entry
means "use the block as-is", a positive one means "halve me and drop a block".
Only the low 10 bits reach the chip (`and ah,0x3` at `0x71c`).

Verified against equal temperament: note 0 -> 16.36 Hz (C0), note 69 -> 879.9
Hz (A5).

**Pitch bend is dead code on AdLib.** The note-on at `0x3a2` hardcodes the
centre value `0x2000`, so the bend arithmetic at `0x69e` collapses to
`note * 16`, and the `0x5c` handler at `0x454` is literally `add si,2 / ret`.
The interpolation machinery exists and is unreachable in this build.

Percussion is asymmetric, and faithfully so:

- Note on for channels 6 and 8 (BD, TT) writes a frequency with the key-on bit
  **clear** - in rhythm mode the `0xBD` bit triggers the voice instead.
  Channels 7, 9 and 10 get no frequency write at all.
- Note off for channel 6 goes through the `0xBD` bit, not `reg 0xB0`, because
  the handler at `0x424` branches on `>= 6` while note-on branches on `< 6`.

### Tempo: fixed at 72.827 Hz

Not guessed. `init` at `0x92a` hooks INT 8 and programs PIT channel 0 with the
divisor stored at `0x8de`:

        b0 34       mov al, 0x34      ; channel 0, mode 2, LSB/MSB
        e6 43       out 0x43, al
        a1 de 08    mov ax, [0x8de]   ; = 0x4000
        e6 40 ...   out 0x40, al / ah

1193182 / 16384 = **72.8271 Hz**, so one delta unit is 13.7319 ms. Nothing
ever writes to `0x8de`, so there is no tempo control.

The ISR at `0x8e0` runs a Bresenham divider - it accumulates its own divisor
and chains to the previous INT 8 handler on overflow, so the BIOS still sees
its usual 18.2 Hz. It also checks for the signature `0xccbb` four bytes before
the existing handler's entry, which is how these drivers detect and cooperate
with each other.

### Verification

- All 10 resources parse to **exactly** EOF, zero trailing bytes.
- Durations match what the filenames imply: 3.1 s logo sting, 5.5 s death,
  4.0 s stats, 8.8 s briefing, 87 s title.
- Instrument byte `[0]` on percussion channels decodes to 36, 38 and 42 -
  Bass Drum, Acoustic Snare and Closed Hi-Hat in the standard GM drum map -
  landing on precisely the channels the operator table assigns to BD, SD and
  HH. That map is an external standard, so it cannot be an artifact of the
  decoding.
- `AMTHEME` channel 0 gets GM program 32, Acoustic Bass, and plays E2/G2.
- Rendered to WAV through the MIDI export and confirmed by ear.

### Exports

`tools/mus_decode.py` reads the frequency and attenuation tables out of the
user's own `FMMUSIC.DRV` rather than hardcoding them, so no game data enters
this repository.

- `dump` - event listing with decoded note names and GM program names.
- `midi` - a transcription of `GMMUSIC.DRV`'s output, not an approximation of
  it. Portable, but retimbred by whatever synth plays it.
- `dro` - a DRO v2 OPL2 register log, transcribed from `FMMUSIC.DRV`
  write-for-write. This is what the hardware actually received.

The DRO writer drops writes that do not change chip state. The driver's reset
sweep at `0x142` blanks registers `0x01..0xf5`, which is 245 of them - more
than DRO's 127-entry codemap can name. OPL registers are state rather than
triggers, so rewriting an identical value never retriggers an envelope and the
pruning is lossless from reset. The C++ player keeps the full sweep, since it
must also do the right thing on a chip with leftover state.
