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

## `SETUP.CFG` - partly decoded (65 bytes)

Read straight out of the user's own file. Two blocks stand out of the zeros;
everything else is still unread.

        offset  bytes          reading
        0x00    01 02 01 02    four device selections           guessed
        0x04    20 02          SB base port 0x220               measured
        0x06    07 00          IRQ 7                            measured
        0x08    01 00          DMA 1                            measured
        0x0a    00 x 6         zero
        0x10    48 00          scancode 0x48  Up
        0x12    4b 00          scancode 0x4b  Left
        0x14    4d 00          scancode 0x4d  Right
        0x16    50 00          scancode 0x50  Down
        0x18    1d 00          scancode 0x1d  Left Ctrl
        0x1a    38 00          scancode 0x38  Left Alt
        0x1c    00 x 18        zero
        0x2e    01 01 01 00    three more selections            guessed
        0x32    00 x 15        zero

The hardware triple is solid: 0x220 / IRQ 7 / DMA 1 is a stock Sound Blaster
set, sitting as three consecutive 16-bit fields exactly where a hardware block
belongs. It is also, usefully, DOSBox-X's default - so no `[sblaster]` tuning
is needed to run the game under the debugger.

The six scancodes are solid as a *set*: they are the standard XT codes for the
four arrows plus left Ctrl and left Alt, six consecutive words in an otherwise
zeroed span, and the game takes exactly six inputs - four directions plus
buttons A and B. So the remappable controls are arrows + Ctrl + Alt.

### Slot order is not bit order - and that resolves an apparent contradiction

`SETUP.CFG` lists the six scancodes as up/**left**/**right**/down/Ctrl/Alt,
while the `.SCR` bits were read as `0x02`=down, `0x04`=left, `0x08`=right. Those
look incompatible, and a session was nearly spent "resolving" it. They are not
incompatible: **the driver does not map slot *n* to bit *n*.**

`KEYBOARD.DRV` is the second consumer of these scancodes, and it settles it
outright. Its INT 9 handler compares the scancode - break bit masked off with
`and bh, 0x7f` - against six slots at driver offsets `0x24`, `0x26`, `0x28`,
`0x2a`, `0x2c`, `0x2e`, and ORs a bit into a mask byte at offset `0x1f`:

        cmp  bh, [0x24]        ; slot 1
        jne  next
        and  ah, 0xfe          ; clear the bit
        or   al, al
        js   done              ; break code -> leave it cleared
        or   ah, 0x01          ; make code -> set it
        ...                    ; slot 2 -> 0x04, slot 3 -> 0x08,
                               ; slot 4 -> 0x02, slot 5 -> 0x10, slot 6 -> 0x20

So a bit is set only *while* the key is held, and the slot-to-bit map is
`1, 4, 8, 2, 16, 32` - deliberately not the identity. Feed `SETUP.CFG`'s order
through it and both readings come out right at once.

**Measured live** (`exp0_input_bits.py`, six key injections, every bit matching
the prediction from the disassembly):

| bit | control | default key | scancode |
|---|---|---|---|
| `0x01` | up | Up arrow | `0x48` |
| `0x02` | down | Down arrow | `0x50` |
| `0x04` | left | Left arrow | `0x4b` |
| `0x08` | right | Right arrow | `0x4d` |
| `0x10` | **button A** | **Left Ctrl** | `0x1d` |
| `0x20` | **button B** | **Left Alt** | `0x38` |

This **confirms the `.SCR` bit table**, which had only ever been inferred from
run-length statistics - so `DEMO.SCR` is safe to use as the correctness oracle
`PLAN.md` §5 plans for. It also names the buttons, which was open.

Two details worth keeping:

- The game **patches the table at load time** from `SETUP.CFG`. The shipped
  driver has the slots zeroed with its own defaults sitting in the *high* byte
  of each word; live memory has the `SETUP.CFG` values in the low bytes. The
  shipped defaults also have Ctrl and Alt the other way round, so `SETUP.CFG`
  is what governs.
- The driver's live base is not fixed: take it from the **INT 9 vector**
  (linear `0x24`). The handler sits at driver offset `0x3c`, matching its offset
  in the extracted `KEYBOARD.DRV`, so file offsets and memory offsets coincide.
  The old vector is saved at driver `+0x18` and reads `F000:E987`.

## Startup checks (solved)

Found while getting the game to run under the debugger. Five Pascal-framed
messages sit together at file `0x0c8d0`, and a sixth in the driver-install unit:

| message | what failed |
|---|---|
| `Run SETUP.EXE to configure Tubes for your system!` | `SETUP.CFG` could not be opened |
| `Tubes requires ` *n* ` bytes of free memory with this SetUp!` | not enough free conventional memory - the requirement depends on the chosen sound config, hence "with this SetUp" |
| `Tubes Requires VGA!` | video adapter check |
| `Tubes Resource File Error!` | `TUBES.RES` missing or malformed |
| `Drivers Resource File Error!` | `DRIVERS.RES` missing or malformed |
| `This game requires complete control of your computer.  Please run from DOS!` | a multitasker answered the DPMI/Windows yield call - see below |

Note the first one names `SETUP.EXE` but tests for `SETUP.CFG` (opened at
`0x149b1`). Copying `SETUP.EXE` next to the game does nothing; the *config
file* is what has to exist.

### The multitasker guard, at `21ea:0249`

The one that actually blocks emulation. Read straight off the disassembly:

        mov  ax, 0x1680          ; INT 2Fh "release current VM time-slice"
        int  0x2f
        not  al                  ; AL = 0x00 if something serviced the call,
        mov  cl, 7               ;      0x80 (unchanged AH) if nothing did
        shr  al, cl              ; flag = (~AL) >> 7
        mov  [0xd3e], al
        cmp  byte [0xd3e], 0
        je   +9                  ; flag 0 -> continue into driver install
        mov  di, 0x167           ; "This game requires complete control..."

`INT 2Fh AX=1680h` is the Windows/DPMI cooperative-yield call. Under Windows 3.x
enhanced mode, a DOS task switcher or a DPMI host, it returns `AL = 0`; on bare
DOS nothing claims the interrupt so `AL` keeps the `0x80` it came in with. So
the game is asking "is anything time-slicing me?" and refuses if so - reasonable
for a program that is about to hook interrupts and reprogram the VGA into Mode X.

This is why it will not start under a default DOSBox-X: DOSBox-X *does* service
that call (`src/dos/dos_misc.cpp:443`, setting `reg_al = 0`) so it can idle the
host CPU. `dos idle api = false` disables it. See `docs/debug-rig.md`.

The guard reads and writes `DGROUP:0x0d3e`, which is worth remembering: the
neighbourhood `0xd14`..`0xd44` is used as scratch by this unit, and `DS:0xd24`
is already recorded above as the `DEMO.SCR` pointer.

## Open questions

- `TUBES.RES` directory structure and compression scheme
- Scoring tables, chain multipliers, wave progression curves
- Atom spawn RNG and distribution
- `SETUP.CFG`: the selection bytes at `0x00` and `0x2e`; and the
  scancode-order-vs-`.SCR`-bit-order contradiction above
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

## Gameplay constants - partly reverse engineered

The engine's playfield logic still follows TUBES.DOC rather than the binary
for most values. What has been measured, and what has not:

| Constant | Current | Source |
|---|---|---|
| cell size | 16 x 13 | **real** - measured from atom sprites |
| playfield x range | 74..245 | **real** - confirmed twice, see below |
| drop limits | 9 / 6 / 3 | **real** - stated in TUBES.DOC |
| difficulty seed values | see below | **real** - read off `1000:9e53` |
| difficulty progression | see below | **real** - read off `1000:9e53` |
| grid columns / rows | 7 x 10 | still a guess |
| spawn interval | 1.6 s | still a guess |
| fall speed | 30 / 42 / 56 px/s | still a guess |
| score per atom / chain bonus | 10 / 25 | still a guess |

### Playfield extent

`GAMEFG.GFX` is the play-area overlay: nested tube outlines down both sides.
From row 60 to the bottom its drawn runs are constant, the left bundle ending
at x=73 and the right beginning at x=246. So the clear interior is
**x 74..245**, 172 pixels. This now agrees with an earlier measurement taken
by a different route.

172 does not divide by the 16-pixel atom width, so the grid does not span the
full interior and the column count cannot be inferred from this alone.

### `BEAKER.CSP` is not the playfield container

Worth recording as a dead end. Its walls sit at sprite columns 4-5, 22-26 and
108-109, which do not divide into 16-pixel cells on any reading. It is 114x65
with a plain rectangular outline - probably the "beakers remaining" indicator
or a cutscene prop, not the thing atoms fall into.

### `1000:9e53` is the game session, not the playfield renderer

An earlier note called this the playfield renderer. It is not. It:

1. loads the play-area art - `TESTUBE1..3`, `GAMEFG`, `BEAKER`, `BEAKERS`,
   `TUBEH`, and the `GAMEBG1..10` series, the last built by concatenating
   `GAMEBG` with a loop counter into a 19-entry-per-index pointer table;
2. seeds the difficulty state, either from a saved game or from defaults;
3. runs the frame loop, calling `1000:3a67`.

`1000:3a67` takes no arguments yet reads its caller's stack frame, which
means it is a **nested Pascal procedure** of `9e53` and shares its locals.
That is why the difficulty variables are locals rather than globals, and it
is the reason to decompile the two together rather than separately.

### Program structure - the whole interface

Recovered with `ghidra_scripts/MapProgram.java`, which pairs the call graph
with the ShortStrings each function references. Borland Pascal emits one code
segment per unit and leaves literals in plain sight, so "which function shows
the menu" is usually answerable by asking what text and resource names it
touches.

23 units, 297 functions. `entry @ 1000:aaba` is the Pascal main program; it
loads the shared sprites (the eight balls, `ANTIBALL`, `GOLDBALL`, `XENBALL`,
`STAR1..4`) and then calls each stage in turn:

| Function | Size | Stage | Identified by |
|---|---|---|---|
| `1b2e:11b0` | 55 | splash sequencer | calls the two splash screens |
| `21d5:007b` | 198 | Software Creations splash | `SOFT.PAL/GFX/ANM` |
| `2178:00eb` | 1173 | Absolute Magic splash | `AMLOGO.SPR`, `LIGHTN.SPR`, `AMTHEME.MUS`, `WOOSH.SFX` |
| `1b2e:52bf` | 3782 | title / main menu | `TUBESBG.GFX`, `TUBESFG.GFX`, `TUBES.MUS`, `SELECT.SFX` |
| `1b2e:1651` | 2323 | blackboard cutscene | `WRITE0..9.GFX`, `EXPLOD1..4.GFX` |
| `1b2e:2d63` | 4510 | (test-tube screen) | `TESTUBE1.CSP`, `TESTUBES.CSP` |
| `1000:9e53` | 2173 | game session | the whole playfield set |
| `1000:3a67` | 9382 | the game loop - **not** a per-frame call; entered once and loops internally | nested inside `9e53` |

Two useful consequences:

- `SOFT.ANM` is consumed by the developer splash. That is the one `.ANM` in
  the game and it had never been examined; now there is a reason to.
- `1b2e:1651` is the teacher-at-the-blackboard sequence, which is where the
  instructions and inter-level story live.

### The playfield array is 6 x 5

From `1000:3a67`. Three parallel arrays - `abStack_27` (the colours, 30 uses),
`acStack_62` (12) and `acStack_45` - are all indexed `[i * 6 + j]`, with the
inner loop running `j = 1..6` and the outer `i = 1..5`:

        local_1b6[0] = 1;
        while (true) {
          local_1b6[1] = 1;
          while (true) {
            ... abStack_27[local_1b6[0] * 6 + local_1b6[1]] ...
            if (local_1b6[1] == 6) break;
            local_1b6[1]++;
          }
          if (local_1b6[0] == 5) break;
          local_1b6[0]++;
        }

Row stride 6, five rows: a Pascal `array[1..5, 1..6]`. The guess of 7 x 10
taken from the manual was wrong in both dimensions.

Cell values are 0 for empty and 1..7 for colours - the loop above increments a
cell and wraps 8 back to 1, which is a colour-cycling effect.

### Cell to pixel mapping (solved)

The draw loop in `1000:3a67` settles it. At instruction level:

        mov  al, [bp-0x1b3]      ; row
        xor  ah, ah
        imul ax, ax, 0xd         ; * 13
        add  ax, 0x79            ; + 121
        push word ds:0x1e        ; x for column 1
        push ax                  ; y
        ...
        push [di+0x1da8]         ; sprite segment
        push [di+0x1da6]         ; sprite offset
        call 1321:0905           ; Draw(x, y, sprite)

So **y = row * 13 + 121** for row 1..5, giving y = 134, 147, 160, 173, 186.
The bottom row ends at y = 199, the last scanline.

x does not come from arithmetic at all - it is a six-entry table, one push per
column, unrolled. The column-to-slot order is 3, 2, 1, 6, 5, 4:

| column | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| slot | `ds:0x1e` | `ds:0x1c` | `ds:0x1a` | `ds:0x24` | `ds:0x22` | `ds:0x20` |
| x | 107 | 125 | 143 | 161 | 179 | 197 |

**Column pitch is 18, not 16.** The sprites are 16 wide, so there is a 2-pixel
gap between columns. Assuming pitch equalled cell size is why nothing lined
up.

Two independent checks that this is right:

- The six values are stored in memory as two *descending* triples
  (143, 125, 107, 197, 179, 161). Reading them through the 3,2,1,6,5,4
  mapping yields a perfectly uniform arithmetic progression. A wrong mapping
  would produce a jumbled sequence.
- The grid spans x 107..212, centred on 160 - exactly the centre of the
  x 74..245 gap between the tube walls in `GAMEFG.GFX`, which was measured
  from completely different data.

The cell value indexes a far-pointer table at `DS:0x1da6` (offset) and
`DS:0x1da8` (segment), stride 4, which `1000:9e53` fills with the loaded
compiled sprites. Colour 0 draws nothing.

#### Finding the values: two segment traps

The six x words live at `DS:0x1a`..`0x25`, and a byte search for writes to
those offsets finds only one site - which turned out to be **a different
segment entirely**, a function that does `mov ax, cs / mov ds, ax` and stores
far-pointer pairs at the same offsets. Same numbers, unrelated storage.

The values are not written at runtime at all; they are initialised data.
DGROUP is Ghidra segment `2785`, load-relative `0x1785`, so `DS:0x1a` is at
image offset `0x1785 * 16 + 0x1a`. Reading there gives the table directly.

Note also that `SS:SP = 1a70:2000` from the EXE header points *past* the
99,728-byte image. SS is not DGROUP here, and assuming it was is what sent the
first search to the wrong address.

#### `BEAKER.CSP` is the container after all - a measurement error, corrected

This was written off twice as "not the playfield container" on the strength of
an 82-pixel interior clear span. That measurement was wrong: it took the
widest gap between drawn pixels at three sample rows, and the sprite has an
internal vertical line, so it was measuring between structures rather than the
interior.

Measuring column occupancy over the full height instead gives walls at x 4-5
and 108-109:

- interior **106 px** - exactly the grid span, x 107..212
- height **65** - exactly the grid height, y 134..198
- the gap from the wall at 5 to the internal line at 23 is **18**, the column
  pitch

So it is drawn at `(kGridX - 4, kGridY)` = (103, 134). Confirmed against a
reference screenshot of the original running under DOSBox-X.

The lesson generalises: "widest gap at a few sample rows" is a fragile way to
measure a shape. Occupancy over the whole extent is not much more work and
does not depend on picking lucky rows.

#### Test tube sizes

`TESTUBE1/2/3` are 22x65, 20x42 and 20x27 - roughly 5, 3 and 2 cells tall at
the 13-pixel row pitch. Three sizes for three capacities, which fits the drop
limits of 9/6/3 stated in TUBES.DOC. The `S` suffix throughout the sprite set
means shadow, not "small": `TESTUBES`, `BEAKERS`, `TUBEVS`, `TUBEHS` are all
thin slivers 1-10 px wide.

The tube therefore **holds several atoms stacked**, which the port does not
model - it holds at most one.

### Difficulty seed and progression

Read directly off the new-game branch. The variables are not yet named - what
each one controls still has to be traced into `3a67`:

        seed:  3, 30, 2, 0, 3, 8      plus globals [0x1d49]=50, [0x1d4a]=25

        every 15 levels:  one counter += 32, another += 11
        every 20 levels:  three counters += 1, and one += 10

A saved game restores these from globals at `0x1d10`..`0x1d19` instead.

**Caution recorded deliberately:** this function compares a level counter
against `0x4a` = 74, which is also the playfield's left edge in pixels. The
two are unrelated. Grepping decompiler output for a known constant will find
coincidences, so every hit needs its surrounding context read before it is
believed.

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

### The instrument bank - 12 patches, and why a GM bank cannot hold them

`tools/mus_decode.py bank` gathers every distinct patch across all ten songs.
The entire soundtrack uses **12**: 7 melodic and 5 percussion.

The obvious next step - export a standard OPL bank (DMX `OP2`, `WOPL`) so the
music plays faithfully in an external MIDI player - **does not work**, and the
reason is worth recording because it is not obvious:

- Standard OPL banks are keyed by **GM program number**.
- The game reuses **program 0 for four unrelated patches**. The composer
  evidently treated it as "unlabelled" rather than "grand piano".

So a GM-keyed bank silently merges four distinct instruments into one slot.
Any faithful export must key patches by their own bytes, not by the GM number,
and remap the program changes to match.

This also means the *engine* needs no bank at all: patches arrive inside each
`.MUS` as `0x1c` events and are loaded at runtime. A bank is only ever an
interchange artifact for external tools.

Other observations from the bank dump:

- Every patch uses **waveform 0** (pure sine) on both operators, even though
  init sets `reg 0x01 = 0x20` to unlock the other three OPL2 waveforms. The
  composer never used them.
- All five percussion patches sit on their own channels, and the bass drum is
  the only two-operator one - matching the driver's `ch <= 6` test rather than
  the `ch < 6` melodic/percussion split. The two boundaries differ by one, and
  conflating them is an easy mistake to make.
- Percussion patch reuse is musically coherent: one patch serves GM notes 35
  and 36 (both bass drums), another serves 38 and 40 (both snares).


## The atom array, located live

Measured under the debugger rather than inferred, and it settles the shape.

**Found by signature, not by frame arithmetic.** The 12 records initialise to
`(303, 186)`, and x=303 is off the right of the play area (which ends at 245), so
it cannot be a mid-play coordinate - it is a parked/spawn marker. Searching
conventional RAM for `2f 01` followed by a plausible y found **ten** hits at an
exact **28-byte stride**, spanning precisely twelve slots:

        base + 11*28 == last hit          (0x2419e + 0x134 == 0x242d2)

Ten of twelve parked, and the two missing slots were records 1 and 2. The
screenshot taken during the same halt shows **exactly two atoms on screen**. That
correspondence is the confirmation: the twelve records are the atoms, parked at
`(303, 186)` while unused, and a record leaves the marker exactly when its atom
is in transit.

So `12 records x 28 bytes` is confirmed, and `(303, 186)` is the spawn marker
rather than the "parked sentinel" it was first read as.

**Still open: the frame-relative offset.** `PLAN.md` records the base as
`parent - 0x163`; that is *not* verified. The obstacle is that
**`1000:3a67`'s first call is the difficulty-selection screen**, not the game
loop - its parent frame at that call holds the strings `Tubes 101`, `Tubes 201`,
`Tubes 301`, `ifficu`(lty) and `Exit`. So breaking at `3a67` and reading the
static link yields a frame with no atoms in it, and `parent - 0x163` scores 3/12
there, i.e. noise. To pin the offset down, re-arm the breakpoint *after*
difficulty selection and read the static link during actual play.

Mechanics for that, all verified:

- `3a67`'s prologue is `enter 0x3c6, 0`, and `mov di,[bp+4]` at `3a70` loads the
  static link. So break at **`3a73`** and read `DI`: it is the parent frame
  offset within `SS`, with no stack walking needed.
- The three difficulty levels are named **`Tubes 101` / `Tubes 201` /
  `Tubes 301`** - presumably what the 9/6/3 drop limits and the three `TESTUBE`
  sprites are selected by.

### The atom record is 28 bytes, and two offsets were misattributed

`+0x1e` and `+0x1f` cannot belong to it: `0x1f` = 31 > 28. Those were measured on
the **test tube** struct (direction `+0x04`, waypoint index `+0x1e`, target x
`+0x1f`) and got merged into the atom table. The atom record's highest offset is
`0x1b`, which fits exactly:

| offset | field |
|---|---|
| +0x00 | x |
| +0x02 | y |
| +0x0b | colour / sprite index |
| +0x14, +0x16 | saved x, one per video page |
| +0x18, +0x1a | saved y, one per video page |

## The eight elements are named

From the blackboard cutscene: Dr. Lanny B. Brilliant created "eight new elements
not yet included on the periodic table".

| name | colour |
|---|---|
| Redium | red |
| Greenium | green |
| Bluium | blue |
| Cyanium | cyan |
| Purplium | purple |
| Yellowium | yellow |
| Pinkium | magenta |
| **Flashium** | violet |

Eight named elements, against grid cells that cycle **1..7** with 0 empty. The
odd one out is most likely `Flashium` - the name suggests a flashing or wildcard
atom rather than an eighth ordinary colour, which would fit the `ANTIBALL` /
`GOLDBALL` / `XENBALL` sprites that `entry` loads and nothing uses. Not proven;
the cell encoding is what to check.

The in-play HUD reads **`Chains`** top-left and **`Drops`** top-right, with two
counters between them.


## The ball inventory - 25 sprites, measured

Dimensions from the rendered `.CSP` output. The split is exact and informative:

**16 x 13 - seventeen playfield balls**

| group | sprites |
|---|---|
| the seven ordinary colours | `REDBALL` `GRENBALL` `BLUEBALL` `CYANBALL` `PURPBALL` `YELWBALL` `PINKBALL` |
| specials without a letter | `ANTIBALL` `GOLDBALL` `XENBALL` `OBSTBALL` |
| **letter balls** | `MYSTBALL` **?** - `EVILBALL` **X** - `MULTBALL` **M** - `BLOCBALL` **B** - `CONVBALL` **C** - `FILLBALL` **F** |

**8 x 7 - eight half-size balls**

`SRBALL` `SGBALL` `SBBALL` `SCBALL` `SPBALL` `SYBALL` `SPNKBALL` `SWBALL` -
exactly half the playfield size, so `S` = small. Seven match the ordinary
colours; the eighth, `SWBALL`, is pale/white with no full-size counterpart.
Where they are drawn is **not established** - a next-ball preview in the HUD and
the stack inside the test tube are both plausible.

The letter balls are grey spheres with a dark red character on them. Rendering
them is what identified the set; the names alone are ambiguous.

### Special ball behaviour (reported from play, not yet read from the binary)

Marked as such deliberately - this is play observation, and belongs on the
guessed side of the line until the code confirms it.

| ball | behaviour |
|---|---|
| `ANTIBALL` | antimatter. Destroys **any** balls in the beaker on contact. |
| `GOLDBALL` | bonus ball, orange/gold. Travels the tube **very fast**, awards bonus score when caught, and is believed to become a random ordinary ball once captured (uncertain). |
| `XENBALL` | unknown. A featureless grey sphere. **Not** the "X ball" - that is `EVILBALL`. |
| letter balls | each performs some function; `?` mystery, `X` evil, `M`, `B`, `C`, `F` unidentified. |

### Flashium explains the 1..7 cell cycling

This reconciles a code observation that was recorded without an explanation.
The playfield notes say cell values are 0 for empty and 1..7 for colours, and
that the draw loop *increments a cell and wraps 8 back to 1* - written up as "a
colour-cycling effect" with no reason given.

The reason: **Flashium is a wildcard that matches any colour, and has no static
sprite of its own.** Settled in the beaker it continuously displays the other
colours in turn - so a Flashium cell is rendered by cycling its value through
1..7, which is exactly what that loop does. Only three Flashiums matched
together produce a distinct animation.

Two independent sources agreeing, from opposite directions: the increment-and-
wrap loop in the binary, and the behaviour visible in play. That also explains
why the settled-cell far-pointer table at `DS:0x1da6`/`DS:0x1da8` needs only
**seven** entries - there is no eighth sprite to point at.

It raises a concrete follow-up. If a Flashium's cell value is being cycled
through the ordinary colours, the cell cannot also be what marks it *as* a
Flashium, or the three-Flashium match would be impossible to detect. The beaker
grid is already known to be **three parallel arrays of stride 6**, so the natural
place for a type/flag marker is one of the other two. That is the thing to read
next, and it is cheap now: dump all three arrays with a Flashium settled.


## The sprite type table - measured, 19 ball types (solved)

This supersedes every earlier attempt to map fade families to balls by their
initials. It is read out of live memory, with each pointer identified by matching
the **entire** `.CSP` payload against guest RAM - safe because a compiled sprite
must load verbatim to execute. `exp_sprite_tables2.py`.

Two contiguous tables in DGROUP, both **19 entries**, stride 4, Pascal 1-based:

        balls:  DS:0x1da6 + 4*type                       type 1..19
        fades:  DS:0x1df6 + 76*(frame-1) + 4*(type-1)    frame 1..6

`76 = 19*4`, and `0x1da6 + 19*4 = 0x1df2`, immediately before the fade block -
so the two are adjacent and the same length. This confirms the `DS:0x1da6` base
already recorded for the settled-cell table, and gives its length.

| type | ball sprite | fade family | what it is |
|---|---|---|---|
| 1 | `REDBALL` | `RFADE` | Redium |
| 2 | `GRENBALL` | `GFADE` | Greenium |
| 3 | `BLUEBALL` | `BFADE` | Bluium |
| 4 | `CYANBALL` | `CFADE` | Cyanium |
| 5 | `PURPBALL` | `PFADE` | Purplium |
| 6 | `YELWBALL` | `YFADE` | Yellowium |
| 7 | `PINKBALL` | `PNKFADE` | Pinkium |
| 8 | *borrowed* - read as `BLUEBALL` | `FFADE` | **Flashium**, the wildcard |
| 9 | `ANTIBALL` | `AFADE` | AntiMatter |
| 10 | `GOLDBALL` | `GLDFADE` | Bonus |
| 11 | `XENBALL` | none | **Xenon** |
| 12 | `MULTBALL` | none | Multiplier - fills the tube with **random** balls |
| 13 | `EVILBALL` | none | Evil Multiplier |
| 14 | `CONVBALL` | none | Convertor |
| 15 | `BLOCBALL` | none | Blocker |
| 16 | `FILLBALL` | none | Blocker/Filler |
| 17 | `OBSTBALL` | none | obstacle, unidentified |
| 18 | `CRFADE1` | `CRFADE` | the crystal |
| 19 | `MYSTBALL` | none | unidentified |

### Type 8 has no sprite of its own - measured

Type 8's *ball* pointer resolved to `BLUEBALL`, the identical address type 3
points at. There is no `FLASHBALL` resource anywhere in `TUBES.RES`. So the
wildcard's slot **borrows a colour sprite**, and this sample caught it holding
blue. That is the flashing, seen from the pointer side, and it is the same
mechanism as the `1..7` cell cycling the playfield notes recorded.

One sample cannot distinguish "the pointer is rewritten as it flashes" from
"permanently aliased to blue". **The check is two reads a second apart**: if
type 8's pointer walks the colour sprites, it is confirmed outright. Cheap, and
not yet done.

### Flashium always clears with `FFADE` - and the match *sound* is the variable

An earlier note here hypothesised that a settled Flashium's cell holds the colour
it matched, so that a type-indexed fade lookup would yield that colour's
animation. **Retracted.** It was built on a misremembered detail. A Flashium
fades with the multicoloured `FFADE` animation *always*, whether it matched two
other Flashiums or two reds - which is simply what the measured table says, type 8
to `FFADE`, with no reconciliation needed. The straightforward reading was right.

What actually varies is the **sound**. There is one match sound per family, and
the `.SFX` inventory proves it: eleven of the twenty-four sounds carry names
*identical* to the eleven fade sprite families -

        RFADE  GFADE  BFADE  CFADE  PFADE  YFADE  PNKFADE
        FFADE  AFADE  GLDFADE  CRFADE

So animation and audio are named in lockstep. Reported from play: the sound
played is the one for **the colour the stack matched as**, and `FFADE.SFX` is
heard only when three Flashiums match each other.

Put together, the model is that the two are indexed differently:

- **Animation** is per **ball**, by its own type. A Flashium in a red match still
  shows `FFADE` while the reds show `RFADE` - a mixed match shows mixed
  animations.
- **Sound** is per **match**, by the colour the stack resolved to. One sound for
  the whole chain, `FFADE.SFX` only for an all-Flashium chain.

The animation half is measured. The sound half is reported and coherent but
**not proven** - and it is directly checkable, because the sounds are loaded
resources like the sprites, so a pointer table indexed the same way should exist
in DGROUP and can be located by matching `.SFX` payloads against guest RAM,
exactly as the sprite tables were.

### The rest of the sound inventory

The other thirteen, with likely events - **all guesses** except where a name is
unambiguous:

| sound | use | how known |
|---|---|---|
| `DROP` | **missing a ball**, i.e. letting one fall past the tube | from play |
| `HITGLASS` | an atom landing in the test tube | from play |
| `HITATOM` | atom landing on settled atoms | guess |
| `SLIDE` | the tube sliding along its rail | guess |
| `SWITCH` `SELECT` | menu movement and confirmation | guess |
| `BUBBLE` | unidentified | - |
| `CLAP` `NOOOO` `WHATTHE` | **cutscene** reactions, not gameplay | from play |
| `WOOSH` `LIGHTN` `ABSMAGIC` | the Absolute Magic splash | already attributed |

### `DROP` ties the drop limits to the HUD

Three separate findings turn out to be one thing. `DROP` is the sound for
**missing** a ball; the in-play HUD is labelled **`Drops`** at top right; and
`9 / 6 / 3` "drop limits" were measured in the binary long before either was
known. So a "drop" is a missed ball, and the limit is **how many misses a
difficulty allows** before the game ends - not a count of atoms dumped into the
beaker, which is how the name reads at first.

That makes difficulty a *miss allowance*, selected by `Tubes 101 / 201 / 301`.
The pairing is the obvious one - the easiest level granting nine and the hardest
three - but which name maps to which number is **not** confirmed.

It also reinforces retiring the 5/3/2 tube capacity in `src/game.cpp`: the tube
holds five at every difficulty, `FILLBALL` is what permanently reduces it, and
the thing that actually varies per difficulty is the drop allowance.

### Only types 1-10 and 18 can be cleared

Types 11-17 and 19 have **null** fade pointers in all six frames. A thing that is
never cleared by matching needs no clear animation, so the matchable set is the
seven colours, Flashium, AntiMatter, Bonus and the crystal. Everything else -
Xenon, the four letter balls, `OBSTBALL`, `MYSTBALL` - does something on landing
instead.

Note type 18's *static* pointer is `CRFADE1`, not `CRYSTAL.CSP`: the crystal's
first fade frame doubles as its resting appearance, which the rendered images
bear out (they are nearly identical).

### `MULTBALL` is the Multiplier, not the wildcard

Worth stating plainly because this went back and forth. `MULTBALL` is type 12
with **no fade family**, so it cannot be a thing that gets matched and cleared.
The wildcard is type 8. Both an earlier guess (`FFADE` = Flashium, right for the
wrong reason) and a subsequent correction (`FFADE` = `MULTBALL`, wrong) are
superseded by the table.

### The small-ball table - 7 entries

`DS:0x200a`, stride 4: `SRBALL` `SGBALL` `SBBALL` `SCBALL` `SPBALL` `SYBALL`
`SPNKBALL`. Exactly the seven colours, parallel to types 1-7. **`SWBALL` is not
in it**, and neither is anything for Flashium or the specials - so wherever the
half-size balls are drawn, only ordinary colours appear there. Where that is
remains unknown.

## Published descriptions of the game

External sources, useful because they name behaviours the binary has not yet been
read for. Treat as documentation, not measurement - but note the special list
below matches the **measured type order for 9..16 exactly**, which is strong
independent corroboration of both.

From RGB Classic Games, verbatim:

> "Flashium is a wildcard that can used to create a chain of any color, Xenon
> won't react with any color atom, AntiMatter destroys the surrounding atoms,
> Bonus turns into Flashium when caught and gives you a bonus drop, Multiplier
> will fill your test tube with atoms, Evil Multiplier fills the test tube with
> Xenons, Convertor changes all of the atoms it lands on into Xenons, Blocker
> will fill the beaker column it lands in with Xenons, and Filler permanently
> reduces the number of atoms the test tube can hold by one atom."

Read against the type table that is: 8 Flashium, 9 AntiMatter, 10 Bonus,
11 Xenon, 12 Multiplier, 13 Evil Multiplier, 14 Convertor, 15 Blocker,
16 Filler - the same order, from two unrelated sources.

**So `XENBALL` is Xenon**, the inert atom that will not react with any colour.

Three corrections to the play-derived notes:

- `ANTIBALL` destroys the **surrounding** atoms, not every ball in the beaker.
- `GOLDBALL` (Bonus) turns into **Flashium** when caught and grants a bonus drop -
  not a random ordinary ball.
- The test tube holds **five** atoms. Both published descriptions say five
  outright, which undercuts the 5/3/2-by-difficulty guess in `PLAN.md`; and
  `FILLBALL` *permanently reduces capacity by one*, which is a far better
  explanation for varying capacity than difficulty is.

Other facts worth having:

| | |
|---|---|
| developer | Absolute Magic (also styled Exaggerated Software) |
| publishers | v1.0 Software Creations, v1.1 Impulse Software; Gold Medallion Software also credited |
| versions | v1.0 June 1994, v1.1 November 1994 |
| year | title screen and RGB say **1994**; MobyGames and the Internet Archive item say 1993 |
| modes | Endurance (play until you lose) and Waves (objective-based) |
| registered adds | 50 further waves, five backgrounds, and the **AntiMatter and Bonus** atoms |
| lineage | described as KLAX/Columns-inspired |
| the crystal | "mischief crystals and other bad elements" - an obstacle |

The registered-only AntiMatter and Bonus atoms explain why they were only ever
seen in the shareware's preview modes.

## Provenance of the gameplay descriptions

Worth recording, because it sets how much weight each claim carries. The
gameplay accounts above come from the user playing the **shareware** release,
not the full version, so coverage is uneven:

- **Reliable:** `GOLDBALL` and `ANTIBALL`. The shareware had preview modes that
  demonstrated both in action.
- **Partial:** `GOLDBALL` becoming a random ordinary ball once captured is
  believed but not certain.
- **Unknown:** `CRYSTAL`, `MARKER`, the six letter balls, and whatever `XENBALL`
  is properly called - it behaves as an inert grey sphere.

None of this is a substitute for reading the code; it is a set of hypotheses to
aim the debugger at, which is much cheaper than finding them blind.


## The in-game Instructions - the game documenting itself (authoritative)

Captured under the debugger from the main menu's **Instructions** entry, which is
a slide show ("The Detailed Instructions", paged with Up/Down). This is the
game's own documentation, so it outranks both play recollection and third-party
descriptions. `grab_instructions.py`; captures in the tooling `capture/instr/`.

### Controls - and the atom-speed mechanism, stated outright

> "Use Left and Right to move the Test Tube you control.
> Press Button A to drop an atom into the beaker below.
> **Press Button B or Down to increase the speed of any atoms in the tube
> directly above the test tube.**"

That is the answer to the speed question a whole session was spent on. Three
things fall out:

1. **Both Button B and Down do it.** Which retro-explains the `.SCR` demo's long
   runs of bit `0x02`: the recorded player was holding **Down to speed atoms**,
   not "holding down to drop faster" as the behavioural reading guessed. The bit
   assignment was right; the interpretation of what the player was doing was not.
2. **It is positional** - only atoms *in the tube directly above the test tube*
   are accelerated. So speed is a per-atom property gated on the atom's column
   matching the tube's column, not a global rate.
3. Combined with `GOLDBALL` travelling fast inherently, there are at least two
   speed inputs: the ball's type, and the boost.

### Chains and scoring (solved)

> "Molecule chains are formed by dropping 3 or more atoms of the same element or
> in combination with Flashium atoms in one of 4 chains"

| chain | points |
|---|---|
| Vertical | **250** |
| Horizontal | **500** |
| Diagonal (either direction) | **1000** |

Four "chains" = vertical, horizontal and the two diagonals. Chain counting:

> "3 atom molecules count as 1 chain. 4 atom molecules count as 2 chains.
> 5 atom molecules count as 3 chains. Forming multiple chains all at once will
> create a chain bonus point multiplier!"

So chains = atoms - 2, and simultaneous chains apply a multiplier. This replaces
the invented `kScorePerAtom` / `kChainBonus` in `src/game.cpp`.

Also: "in combination with Flashium atoms" is the game confirming the wildcard.

### The test tube

> "The test tube you control to collect and release atoms can hold up to 5 atoms
> at a time."
>
> "You are allowed to drop some atoms depending on your difficulty setting."

Capacity **5**, flat - which retires the 5/3/2-by-difficulty guess for good. And
the drop allowance *is* the difficulty setting, exactly as the `DROP` sound, the
`Drops` HUD counter and the measured 9/6/3 limits together implied.

> "Tubes has two different types of game play. However, both games end if you
> drop more atoms than allowed."

So exceeding the drop allowance is the lose condition, in both modes.

### Special Atoms

The game groups them, and the grouping is itself information.

| atom | sprite | the game's words |
|---|---|---|
| **Xenon** | `XENBALL`, plain grey, no letter | "will not bond with any atom including other Xenons. They just take up space." |
| **AntiMatter** | `ANTIBALL` | "unstable and causes surrounding atoms to explode. Useful for removing Xenons." |
| **Bonus** | `GOLDBALL` | "turns into Flashium when caught and awards you an extra drop. Also, the Bonus Jackpot is increased by 1000 points and awarded to you." |

`XENBALL` is Xenon and carries **no letter**. The `X` belongs to the Evil
Multiplier - which is why "X stands for Xenon" is a natural thing to remember:
the X ball is the Xenon *dispenser*.

Bonus awards an **extra drop**, i.e. it raises the miss allowance - another
mechanic tied to the drop counter.

### Penalty Atoms - the five letter balls

| atom | sprite | the game's words |
|---|---|---|
| **Multiplier** | `MULTBALL` **M** | "will fill the test tube with normal atoms. Drop some atoms fast before another atom arrives." |
| **Evil Multiplier** | `EVILBALL` **X** | "will fill the test tube with Xenons." |
| **Convertor** | `CONVBALL` **C** | "will change **all occurrences of the atom it lands on** into Xenons. Drop this on the least popular atom in the beaker." |
| **Blocker** | `BLOCBALL` **B** | "will fill the beaker column it lands in with Xenons. Similar to Evil Multiplier but occurs in the beaker." |
| **Filler** | `FILLBALL` **F** | "permanently **adds an atom to the bottom of the test tube** reducing the amount of atoms you can hold." |

Two corrections to the third-party description:

- **Convertor is far more destructive than "the atoms it lands on".** It converts
  *every* atom of that colour in the whole beaker - hence the advice to aim it at
  the least popular colour.
- **Filler is implemented as an occupying atom**, not a decremented counter: it
  adds a permanent atom to the tube's bottom. That matters for the port, and it
  identifies a sprite.

### `OBSTBALL` is very probably Filler's stuck atom (type 17)

Filler "adds an atom to the bottom of the test tube" that cannot be removed, and
play reports "a sticky black one that cannot be dumped from the tube".
`OBSTBALL` is a near-black sphere, has **no fade family** in the measured type
table - correct for something that lives in the tube and is never matched in the
beaker - and its name reads as *obstruction*. Strongly supported, not proven; the
proof is to catch a Filler and read the tube contents.

### Slide order corroborates the type numbering again

The Penalty Atoms are presented as Multiplier, Evil Multiplier, Convertor,
Blocker, Filler - which is **exactly** measured types 12, 13, 14, 15, 16, in
order. Third-party prose already matched types 9..16; the game's own slides match
12..16. Three independent orderings agreeing.

Not documented in the slides: `MYSTBALL` (type 19) and the crystal (type 18).

`MYSTBALL` is the **`?`** ball. Reported from play, unconfirmed: it **becomes a
random letter ball when caught** - i.e. it resolves into one of the five Penalty
atoms. That would fit the `?` glyph and explain why it has no fade family: it
never settles as itself. Testable by catching one and reading the tube contents
before and after.

### The copy being reversed is the registered version

Confirmed by the user: the copy in `..` is the **full registered release**, not
shareware. So everything in these notes describes the complete game, including
AntiMatter and Bonus, which published sources list as registered-only additions -
consistent with the slides documenting them.

This matters for coverage: the shareware's preview modes are why AntiMatter and
Bonus behaviour was known from play, while the crystal was not - it is
registered-only content that had never been seen.


## The dispenser path - observed (Experiment 2)

`PLAN.md` called this "the single genuine unknown left", and static analysis
never found the code that moves atoms. It did not need to be found: the atom
array holds live x/y, so **sampling it over time is the trajectory**.
`exp2_atom_paths.py` reads the 12 records 400 times without halting and prints
each record's route.

### The route

    spawn (303,186)
      -> bottom of an outer vertical tube, y = 187
      -> ascends, y decreasing, x constant
      -> reaches the top, y ~ 0..7
      -> traverses horizontally across the top, y ~ 0..3
      -> descends into a play column

Two real traces, abridged:

    rec A: (303,186) (34,187) (34,183) (34,179) ... (34,39) (34,35)
    rec B: (58,95) ... (58,11) (58,7) (60,7) (60,3) (67,3) (67,0)
           (69,0) (73,0) ... (97,0) (97,1) (101,1) (101,3) (105,3) (105,9)

`rec B` is the whole shape in one line: straight up the x=58 tube, over the top
along y=0, then turning down again at x=105 - which is the left-hand play column
(107) less the 2px the sprites are drawn at.

So the description from play - "enter bottom right, travel up, arc over the top,
come back down" - is confirmed, with coordinates.

### The tube columns

x values where atoms dwell, by sample count:

    294  246  34  270  58  197  107  179  161

Compare the static furniture already read from literal draw coordinates:

    y=13:  58, 107, 197, 246
    y=26:  34, 58, 107, 125, 179, 197, 246, 270

The observed vertical runs land on those same x positions, plus **294** which is
outermost and was not in the furniture list. The six *play* columns are
107, 125, 143, 161, 179, 197 - overlapping the tube set at 107, 179 and 197, so a
tube column and a play column can share an x.

Most-visited y values are 26, 13, 0, 19, 15 - the top band, where atoms spend
their time crossing. That matches the arcs drawn at y=13 and y=26.

### What this does *not* establish

**Per-frame step size is not measured.** The sampler runs at a fixed 50 ms, which
is coarser than the game's frame rate, so positions are aliased. Vertical runs
show a clean `-4` in y between consecutive samples over dozens of samples, which
is suggestive - but the arc shows mixed deltas of 2, 4 and 7 px, which a single
uniform step cannot produce. Either the motion tick is ~20 Hz and 4 px, or the
sampling is beating against a faster tick. **Do not take 4 px/frame as measured.**
Settling it needs a sampler synchronised to the game rather than to wall-clock.

### `1000:3a67` is not the frame update

The stage table in these notes lists `1000:3a67` as "frame update". **It is not.**
A breakpoint at `3a73` re-fired **zero** times in four seconds of active play: the
procedure is entered *once* and loops internally. That single fact explains why
Experiment 1's frame-relative offset stayed open - the static link can only be
read at that one entry, and at that moment the difficulty menu is up and no atoms
exist yet.

It also means any plan of the form "break in the frame loop each frame" needs a
different address - somewhere *inside* `3a67`'s loop body, not its prologue.

### Array address is stable, and a caveat about finding it

The array base is `0x2419e` in this configuration, the same across runs - the
DOS memory layout is deterministic given a fixed conf. Note the locator in
`exp2_atom_paths.py` reported `0x241ba`, exactly one record high, because it
returns the lowest record still parked at the spawn marker and record 0 happened
to be in flight. **The lowest parked record is not necessarily record 0.** Anchor
on the lowest hit whose spacing to the others is a multiple of 28 *and* extend
downwards while the stride holds.


## `TUBES.SAV` - partly decoded, and confirmed against the running game

Previously an open question. Loading the save under the debugger and comparing
what the game displays against the file settles several fields, because the
displayed values can be read straight off the screen.

The file is 960 bytes and extremely sparse - the sample here has **24 non-zero
bytes**, which is why it was briefly mistaken for an empty file.

| offset | bytes | meaning | how confirmed |
|---|---|---|---|
| `0x1e0` | `07` + `"Stephen"` | Pascal ShortString, player name | slot list shows `Stephen` |
| `0x1ff` | `b4 5f 00 00` = 24500 | score (u32 LE) | in-play HUD shows **24500** |
| `0x206` | `06` | wave number | slot list shows **Wave 6** |
| `0x207` | `0b` = 11 | drops allowed | briefing says "You are allowed **11** drops" |
| `0x20d` | `1e` = 30 | atoms to survive | briefing says "live through **30** atoms" |

The first three are confirmed - the game renders those exact values. The last two
are a strong correspondence from a **single** save; confirming them needs a
second save at a different wave and a diff.

Other non-zero bytes not yet accounted for: `0x1bb` = `eb`, `0x204` = `18`,
`0x20b` = `41`, `0x39b` = `f3`, and singles at `0x203`, `0x208`, `0x20a`, `0x20c`,
`0x20e`, `0x210`, `0x211`.

## Menu structure, and Wave mode

    Main menu
      Start Game / Continue Saved Game / Game Options / High Scores /
      Instructions / View Demo / Credits / Exit Tubes
    -> Game Mode
      Endurance Mode / Wave Mode / Exit
    -> Saved Games Available
      five slots, each "<name>  Wave <n>", or "(UNAVAILABLE)"

**The slot list is filtered by mode.** Choosing Endurance showed five
`(UNAVAILABLE)` entries for a save that Wave Mode lists immediately - so saves
belong to a mode, and a missing save is not necessarily absent.

### Wave briefings define the wave

Each wave opens on a briefing slide. Wave 6 reads:

> **Wave 6.** Form as many chains as you possibly can to live through 30 atoms.
> **Yellowium is disabled for the duration of this wave and will not disappear.**
> You are allowed 11 drops.

So a wave definition carries at least: an **objective** (survive N atoms), a
**disabled element** - which still spawns but cannot be cleared - and a **drop
allowance**.

That last point refines the drop limits. The measured `9 / 6 / 3` triple is not
the whole story: **waves carry their own allowance** (11 here), so 9/6/3 is
presumably the Endurance difficulty setting while Wave mode overrides it
per-wave.

"Disabled ... and will not disappear" is a mechanic the port has no concept of:
an element that participates but is unmatchable for the duration.

## The half-size balls are HUD counters (solved)

The seven 8x7 sprites at `DS:0x200a` had no known draw site. They are **Wave mode
HUD elements**, drawn top-left beneath `Chains`, with a count overlaid on the
ball in the HUD font.

Measured across samples in Wave 6: the count runs **24 -> 22**, decreasing, which
matches the briefing's "live through 30 atoms" counting down.

**The counter cycles through all seven element colours.** A first pass sampling
every 3.5 s caught only cyan and magenta and read it as a two-colour blink - that
was an aliasing artefact. Re-sampled at 0.6 s intervals, twelve consecutive
captures render in yellow, purple, cyan, green, red, magenta and blue: the
complete ordinary-colour set, rotating.

That is the **same** behaviour as Flashium, which cycles `1..7` because it has no
sprite of its own. Two HUD-and-playfield elements cycling the same seven colours
strongly suggests one shared rendering path - a frame-driven index into the
`DS:0x1da6` type table - rather than two coincidental effects. It also offers an
explanation for the type-8 measurement: the ball pointer caught holding
`BLUEBALL` would be that index mid-rotation, which is exactly what "rewritten as
it flashes" predicted.

Worth testing directly, and now cheap: read the type-8 ball pointer repeatedly
and see whether it walks the same seven sprites in the same order.

Wave-mode HUD layout:

    Chains <n>          <score>            <n> Drops
    <small ball + count>

Endurance mode showed `Chains` and `Drops` only, with no ball counter - which is
why this never appeared in earlier captures, and matches the report that these
counters exist "in certain modes".


## Flashium's sprite pointer rotates - measured, with a control

Type 8's ball pointer had been caught once holding `BLUEBALL`, which left two
readings open: permanently aliased to blue, or rewritten as the wildcard flashes.
Polling settles it.

`DS:0x1da6 + 4*8` sampled 500 times, alongside **type 3 as a control**:

| | distinct values in 500 samples |
|---|---|
| type 3 (`BLUEBALL`, an ordinary colour) | **1** - static |
| type 8 (Flashium) | **7** - every colour sprite |

The control never moved, so the polling itself is sound. Type 8 cycles:

    REDBALL -> GRENBALL -> BLUEBALL -> CYANBALL -> PURPBALL -> YELWBALL
      -> PINKBALL -> (wrap)

which is types **1 -> 7 in table order, wrapping 7 back to 1**, with near-uniform
dwell (74, 73, 72, 71, 71, 70, 69 samples each). Observed at ~12 samples/s with
runs of consistently 3, so roughly **4 colours a second**, full cycle ~1.7 s -
though treat the *rate* as approximate and the *order* as certain, since a fixed
sample interval has aliased twice already in this work.

### What this settles: the cell holds 8, only the pointer moves

The playfield notes recorded a loop that "increments a cell and wraps 8 back to
1" and called it a colour-cycling effect. That reading implied a settled
Flashium's **cell value** walks 1..7. The measurement says otherwise, and a third
fact decides between them:

- Fades are indexed by **type** - measured from the table layout.
- A Flashium **always** clears with `FFADE`, whatever it matched with - reported
  from play, and the reason an earlier "cell holds the matched colour"
  hypothesis was withdrawn.
- If the cell cycled 1..7, a type-indexed fade lookup could never yield `FFADE`.

So a settled Flashium's cell holds **8**, permanently, and the flashing lives
entirely in the *sprite pointer* being rewritten. That is cleaner, and it makes
all three facts consistent at once. The "increments a cell and wraps 8 to 1" loop
is then most likely the code driving this pointer rotation rather than a cell
mutation - worth re-reading with that in mind.

It also explains the Wave-mode HUD counter cycling the same seven colours: one
shared rotation, not two coincidences.


## Per-frame atom motion: 4 pixels (measured)

Experiment 2 recovered the path's *shape* but explicitly refused to claim a step
size, because the sampler was wall-clock paced and slower than a frame. This
measures it properly.

**The problem is a ratio.** An RSP read costs 40-80 ms; a frame is ~15 ms. Any
wall-clock sampler is slower than the thing it measures, so motion aliases - the
failure behind three separate wrong or unclaimable results in this work.

### Two ways to invert the ratio, and why the obvious one failed

**Make the guest wait (breakpoint per frame).** This is the textbook answer and it
did not work here:

- The clean Mode X page flip at image `0x1335f` - `mov bx,[0x2376]; xor bx,1;
  mov [0x2376],bx` - is **never executed**. It belongs to code this game does not
  use. Verified by breakpoint, zero hits.
- The only site found that *does* execute during play and touches the page index,
  image `0x11aa2` (a per-sprite dirty-rect save), fires **thousands of times per
  frame**. Servicing it over RSP never completed a single frame in five minutes.

So there was no cheap once-per-frame anchor to break on.

**Make the guest slower (what worked).** Cycles affect only how fast the guest
computes, never what it computes, so slowing the emulator changes wall-clock per
frame and nothing else. DOSBox-X drops cycles on **Ctrl+F11**, which QMP can
send - 42 presses from `cycles=fixed 20000`. Frames then take far longer than a
read, and frame boundaries become observable directly from the page index at
`ds:0x2376` changing.

Result: 3044 reads over 150 s caught **577 page flips at 5.3 reads per frame** -
comfortably oversampled, so boundaries are rarely missed.

### The measurement

Deltas between consecutive *frames*, across all records:

| dy | count | | dx | count |
|---|---|---|---|---|
| **-4** | **503** | | **-4** | **126** |
| -8 | 87 | | +4 | 70 |
| +4 | 95 | | -8 | 33 |
| +18 | 62 | | +8 | 32 |

**Atoms move 4 pixels per frame**, in both axes. `-4` dominates because most
observed travel is the ascent up an outer tube.

The `8`s are exactly `2 x 4` and occur at roughly the rate a boundary would be
missed at 5.3 reads/frame - they are sampling artefacts, not a second step size.
The scattered large values (`+/-49`, `+/-91`, `+/-133`) are records being
reassigned to a different tube column, i.e. teleports rather than motion, and the
handful of absurd ones (`35927`, `-15156`) are torn reads of a record mid-update.
`dy = +18` at 62 occurrences is real but unexplained - not a multiple of 4, and
not the 13 px row pitch either.

This **confirms** the tentative `-4` Experiment 2 saw and refused to claim: the
50 ms wall-clock interval had happened to sit near one frame, so the value was
right by luck. It is now right by measurement.

Note the contrast with the **test tube**, which static analysis showed moving
`x -= 6` / `x += 6` per frame. The tube travels at 6 px/frame and atoms at 4 -
different rates, which is worth carrying into the port.


## Atoms have two speeds: 4 px/frame in the tubes, 18 px/frame falling

`dy = +18` appeared 62 times in the per-frame measurement and was recorded as
real but unexplained - not a multiple of the 4 px step, not the 13 px row pitch.
It is not an anomaly. It is the **descent**.

A histogram of deltas cannot answer a question like this because it discards
*where* each transition happened. Re-running with full context - record index,
from-position, to-position for every frame-to-frame change - settles it at once.

Every `+18` and `+36` event has **`dx = 0`** and occurs at a **play column**
(107, 125, 143, 179, 197). One record's descent, sampled per frame:

    x=125:  46 -> 68 -> 68 -> 104 -> 122 -> 140 -> 140 -> 158 -> 176 -> 176 -> 176
    deltas:    +22   0    +36   +18   +18    0    +18   +18     0      0

So:

| phase | step |
|---|---|
| travelling the tube network (up the outer tubes, across the top) | **4 px/frame** |
| descending a play column | **18 px/frame** |
| the test tube sliding on its rail (static analysis) | **6 px/frame** |

`+36` is two descent steps across a missed sampling boundary; `+29` and `+11`
are compound and partial steps seen around y = 48-49, where an atom is
transitioning into the descent.

`68` is the top lane already recorded for the tube struct ("y snaps between two
lanes: 187 at the bottom where they enter, and 68 at the top"), and the descent
runs 68, 86, 104, 122, 140, 158, 176 from there - each atom stepping 18 px from
whatever y it entered the column at, so the lattice is per-atom rather than
global. The trace above ends with the atom holding at 176 for several frames,
which is presumably where it settles before becoming a grid cell.

Note the descent lattice is **not** the beaker's 13 px row pitch. Falling is free
motion at 18 px/frame; the conversion to a settled grid cell happens separately.

**Not established:** the frames where position does not change (`0` deltas in the
trace above) could be a genuine movement cadence - 18 px every other frame,
averaging 9 - or sampling artefacts. The sampler detects frames from the page
index and was running at ~5 reads/frame, so a spurious repeat is possible.
Distinguishing them needs either a higher oversampling ratio or a real per-frame
breakpoint.

Three speeds in one game, all different, is worth carrying into the port: the
current `fallSpeed` constant in `src/game.cpp` cannot be right for both phases.
