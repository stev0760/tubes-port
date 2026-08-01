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

**The prefix does NOT mark chunky storage.** That was the reading for most of
this project, it is wrong, and the player caught it: the "Absolute Magic" text
in the second splash was corrupted. `AMWRITE.GFX` carries the prefix and its
pixels are **planar**.

What the prefix actually is: a header byte, and nothing more. Both draw
routines skip exactly one byte before reading the width -

    23df:0022   INC SI; w := [SI]; h := [SI+2]; ... copies w bytes a row
                { mode 13h, CHUNKY }
    2321:0948   INC SI; w := [SI] shr 2; h := [SI+2]; ... four plane passes
                { Mode X, PLANAR }

so **the LAYOUT is decided by which routine draws the file, not by the file**.
Two of the three prefixed resources happen to go through the chunky path,
which is why the wrong rule survived:

| resource | drawn by | layout |
|---|---|---|
| `SOFT.GFX` | `23df:0022`, the mode 13h blit | chunky |
| `CLOUD.GFX` | copied byte for byte by `2178:0000` | chunky |
| `AMWRITE.GFX` | `2321:0948`, the Mode X blit | **planar** |

The other 70 `.GFX` have no prefix, a 4-byte header, and are planar.

This is the exact failure `CLAUDE.md` warns about under "a size check is not a
correctness check": `176 * 25 + 5` fits the file whichever way the payload is
read, so every validation passed and only the picture was wrong. It also cost
nothing to find once looked at - the planar reading renders "Absolute Magic"
in clean lettering, the chunky one renders a smear.

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

### The screen fade - FOUND, after three failed searches

`23e7:0097` fades **in** and `23e7:00ce` fades **out**. They are the oldest
open item in `PLAN.md`, and the player's lead is what found them: *it behaves
like a mandatory effect, so look in the graphics unit rather than the game*.
It is not merely mandatory, it is universal - `MapProgram`'s caller lists are

    23e7:0097  1000:3a67  1000:96db  1b2e:0510  1b2e:0656  1b2e:1651
               1b2e:52bf  1b2e:61b6  2178:00eb  21d5:007b
    23e7:00ce  1000:9e53  1000:3a67  1000:60d8  1000:86b8  1000:9499
               1000:96db  1b2e:1651  1b2e:2d63  1b2e:411b  1b2e:52bf
               1b2e:61b6  2178:00eb  21d5:007b

- every screen, both splashes included. Scanning the GAME segments for a fade
was looking in the wrong unit three times over.

Both routines are one loop over a step counter, rewriting all 768 DAC
components each pass:

    fade in:   for n := 0 to [DS:0x0ce6] do  ramp(n)
    fade out:  for n := [DS:0x0ce6] downto 0 do  ramp(n)

    ramp(n):   for i := 0 to 767 do
                 scratch[i] := target[i] * n div [DS:0x0ce6]   { MUL BX / DIV }
               SetDAC(scratch)                                 { 23e7:003d }

with `target` at `DS:0x2400`, `scratch` at `DS:0x2702`, and
**`[DS:0x0ce6] = 40`** read out of DGROUP. The division is `DIV r/m8`, so the
arithmetic is on the raw 6-bit values and truncates.

**`23e7:003d` waits for one vertical retrace before it uploads**, and it is
called once per step. So a fade is **41 uploads at 70 Hz = 0.586 s** - which
is exactly the "half a second or so between screens" the player reported, and
is a second, independent confirmation that these are the right two routines.

`23e7:006c` is the companion `SetPalette`: it copies the 768 bytes to
`DS:0x2400` **and blacks the DAC as it stores**. So the sequence at every
screen is set the palette (display goes black), draw, fade in, run, fade out -
a screen is never flashed before its fade.

**The music fades with it.** `CALLF [DS:0x22de]` appears exactly once in each
of `1b2e:52bf`, `1b2e:2d63`, `1b2e:411b`, `1b2e:61b6`, `1b2e:1651` and
`1000:3a67`, always in the instruction immediately before the palette
fade-out, and nowhere else. It takes no arguments and it is **not** StopMusic,
which is `[DS:0x22da]` and is called from other places. The driver entry is
unread, so "the music half of a screen transition" is an inference from the
call sites, not a decompiled fact.

The port has the palette half in `fadePalette` (`src/gfx.h`), 40 steps by
default and `--fade-steps N` to shorten or disable it - the player has
sanctioned speeding it up, so the knob exists and its default is faithful.

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

    u16   frame count (bytes following this field, seed included)
    u32   RNG seed          PROVEN - see below
    u8[]  one input bitmask per frame

| Bit | Control | Evidence in the shipped demo |
|---|---|---|
| `0x01` | up | 20 frames, never held |
| `0x02` | down | 1357 frames, runs to 30 - held to drop faster |
| `0x04` | left | 280 frames, 146 runs, mean 1.92 |
| `0x08` | right | 258 frames, 125 runs, mean 2.06 |
| `0x10` | button A | 134 frames, never held |
| `0x20` | button B | unused |

### The seed is proven, and `DS:0xd24` is RandSeed

`1000:5fd9` reads four bytes out of the demo and `1000:6008` stores them into
`DS:0xd24`. That location was previously written up as "the demo pointer"; it
is Turbo Pascal's **`RandSeed`** - the RTL's own generator reads it at
`2000:75bb`. So attract mode is deterministic from a cold boot because the
recording carries the exact generator state it was made against.

The backdrop roll at `1000:5f54` happens **before** the seeding, so it does not
perturb the demo's sequence.

### The bits are read out of the input handler now

`1000:4511` onward tests the byte `2000:2356` returns: `$10` tips, `$04` is
left, `$08` is right, and `$02` or `$20` is the speed boost. That confirms the
inference below and settles `$01` as up by elimination.

The original reasoning, kept because it was right: The reasoning: `0x04` and `0x08` have near-identical run
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

### Attract mode is a scriptable play session - use it as the rig's player

The demo replays through **the normal game loop**, so everything the engine does
during real play it also does during attract mode: atoms spawn and fall, the
tube moves and catches, chains clear, the score and drop counter move. Watching
it was in fact how the drop counter's session-start value got sampled, before
anyone realised the "new game" was the demo.

That makes it the answer to the problem that has shaped this whole phase of the
work - **human latency**. Every live-play measurement so far has been paced by a
person typing, pausing and reporting, which is why transitions kept being missed
and why a pause key became load-bearing. The demo removes the human entirely:

- **deterministic.** Same recording, same seed, same run - so a measurement can
  be repeated and a suspicious result re-checked under identical conditions,
  which live play can never offer.
- **unattended and long.** 11,970 frames of real play with no one waiting, so the
  guest can be slowed as far as sampling needs without anyone minding.
- **already decoded.** `tools/scr_decode.py` gives the input stream, so the
  *expected* action on every frame is known in advance and can be compared
  against what the game state actually did.

Two things it unblocks directly. The **type-field question** - which record
offset really holds an atom's type, after the `+0x0b` reading was found
unsupported - is a correlation over thousands of catches, which is exactly what
an unattended deterministic run provides and what hand-play cannot. And the
constants still marked guessed in `CLAUDE.md` - **spawn rate, fall speeds,
scoring** - are all rate measurements that need a long, repeatable trace.

It is also the shape of the port's regression test: feed the same `DEMO.SCR` to
this engine, replay it frame by frame, and diff the resulting state against a
trace captured from the original. The notes already call `DEMO.SCR` safe to use
as the correctness oracle; this is how that gets cashed in.

Caveat to establish first: the header's u32 is *inferred* to be the RNG seed and
has never been confirmed against the playback code. Determinism is the whole
premise here, so that inference stops being a footnote - if the replay does not
reproduce the same atom sequence, every comparison built on it is meaningless.
Cheapest check is to run the demo twice from a cold boot and diff the state
traces.

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

### `SBSOUND.DRV` reads the same header - two corrections

The format above came off the files. The **Sound Blaster driver** is a second
consumer of the same byte stream, which is this project's most reliable
technique, and its play entry at offset `0x344` walks the header directly:

    if [SI] <> $F1 then exit;             { the marker }
    SI := SI + $20;  CX := [SI];          { the rate - a WORD }
    SI := SI + 2;
    DSP($40, 256 - (1000000 div CX));     { the time constant }
    if [SI] <> 0 then <another path>      { the flag, at $22 }
    else begin SI := SI + 3;  BX := [SI];  SI := SI + 2 end;
    <DMA BX bytes from DS:SI>

Two things change:

* the rate is a **word at `0x20`**, not a longword. Every shipped sound is
  8000 Hz so the high half is zero and the file side could not tell.
* the unknown byte is at **`0x22`**, not `0x24` - the driver reads it
  immediately after the rate and takes a different path when it is set. Every
  shipped sound has it clear, so what that path expects is still unknown.

The count at `0x25` and the PCM at `0x27` are confirmed unchanged: `SI` is
`0x22`, `+3` puts it at `0x25`, the word there is the DMA length and `+2` is
the data.

### There is ONE voice

`0x344` opens by calling the driver's own stop routine at `0x422`:

    if playing then begin <halt the DMA>; <reset the DSP> end

and the whole driver holds a single position/length pair in the sixteen bytes
at `cs:0x20`. In 1,158 bytes there is no mixing anywhere. **A new sound cuts
off whatever was playing** - so two events in one frame is not two sounds, it
is the second one.

### Which sound plays when

The session holds one handle per **atom type**, `sound[t]` at `F9 - 0x72 + 4t`,
loaded by name from `1000:a2e0`. Index 0 of the same array is `DROP`, so a lost
atom is the sound of type nothing. Three more sit just below it.

| slot | file | played by |
|---|---|---|
| `sound[0]` | `DROP` | `1000:15a0` an atom missed; `1000:172a` tipped into a full column |
| `sound[1..7]` | `R/G/B/C/P/Y/PNK FADE` | the four matchers, by the type the run matched AS |
| `sound[8]` | `FFADE` | a Flashium run |
| `sound[9]` | `AFADE` | `1000:0f6f`, the AntiMatter blast |
| `sound[10]` | `GLDFADE` | `1000:083b`, a Bonus caught |
| `sound[18]` | `CRFADE` | `1000:0690`, the Crystal |
| `F9-0x76` | `HITGLASS` | `1000:1776` landing on the beaker floor; `1000:18c9` reaching the tube's bottom slot |
| `F9-0x7a` | `HITATOM` | `1000:1796` landing on a stack; `1000:18de` landing on an atom in the tube; `1000:278c` the beaker settling |
| `F9-0x7e` | `SELECT` | `1000:4be4`, `1000:4ce9` - wave-mode element cycling, not ported |

Types 11..17 and 19 are silent, which is the same set that has no fade family.

### The F2 save screen's editor, and what says it is open

Reported from play: selecting a slot on the F2 screen looked like it had done
nothing - no prompt, no cursor, no way to tell the game was waiting for a name.

`1000:3448` onward is the answer, and it is three steps:

    Move(bank[slot], scratch, $50)        { 1000:3448 - the whole record }
    Move(scratch, desc, $1e)              { 1000:3485 - its description }
    y := slot * $11 + $32
    CopyRect(page 0 -> page 2, 30, y, 160, 14)   { 1000:34a4, 2321:024d }
    Write(30, y, $0f, 0, desc)                   { 1000:34ba }

so the description is copied into the edit buffer - re-saving over a slot
starts from what was there - the cell is **erased back to the paused game
behind it**, and the line is redrawn in **colour $0f**, where the list rows
are drawn in `kSaveRowColour` = $1e. The keystroke loop redraws it the same
way at `1000:35d1`, also $0f.

**That colour change is the entire indication.** There is no cursor: the high
score screen pulses a 4x4 block at `1000:9757` and this loop has nothing of
the kind. The port drew the edited row in the row colour, so nothing on screen
changed at all when the editor opened.

**And the MODE matters as much as the colour.** Both calls pass mode 0 - flat,
no colour walk - where the list rows walk. Taking the new colour and keeping
the rows' mode turned the edited line into a scrambled ramp rather than white,
which is what a player saw: `2000:35ec` steps the palette index once per
scanline, and index $0f is the top of its grey ramp, so the walk went straight
out of the greys into whatever follows them. Flat and $0f renders as pure
white; walked and $0f renders as a mess.

### The abort banner's F2 is a real offer

`1000:5ed7` draws "F2 to Save Game, ESC for Main Menu!" and then `1000:5eec`
calls `1000:2dd0` - the whole in-game key dispatch - a **second time**. That is
what makes the offer work: F2 at the banner opens the save screen, exactly as
F2 during play does, and it is the last chance to save a session the player has
just abandoned.

So the game has two ways in to the same screen, and the second one is the one a
player finds first:

    F2 during play         save and carry on
    ESC, then F2           save and then quit to the main menu

The port dismissed the banner on any key at all, so the hint pointed at
nothing and the only way to save was to remember F2 *before* aborting.
Reported from play, and the reporter had gone to the ORIGINAL to check - which
is how the two-way structure came to light at all.

The erase is a page-to-page copy from the page holding the paused game, which
is why a captured empty row shows the tube artwork through it rather than a
flat fill. A port that recomposes the screen gets that for free.

**And the editor accepts twenty characters, not thirty.** `1000:3594` refuses
a character once the length reaches $14, while the record's field is
`string[30]`. Two different numbers for two different things, and the port had
been using the field's.

### `GLDFADE`: the sound is used, the ANIMATION cannot be

Noticed by the player: the Bonus atom never plays a gold fade. It cannot, and
the reason is one byte in the catch path.

`1000:07f7` rewrites the caught atom's type to 8 the instant the tube takes it,
so a Bonus is a Flashium before it can be tipped. Every other route into the
beaker is closed too, and all of them are transliterated:

* the **pre-fill** (`1000:035e`) places `n mod 7 + 1`, so types 1..7 only;
* the **morph** (`1000:4bf6`) skips anything `>= 8`, so it rotates 1..7 and
  cannot produce a 10;
* the **spawn** can roll a 10, but that atom is either caught - and converted -
  or missed, and a missed atom is lost rather than deposited;
* and `1000:2790` turns a settled Bonus into **Xenon**, which is the original
  itself closing the case a fourth time.

So no cell of type 10 can ever be matched and cleared, and `GLDFADE1..6` - six
compiled sprites that are present, decode, and render - are **never drawn**.
The pointer sits in the fade table at `DS:0x1df6` beside the ten that are used.

**The SOUND is a different thing and it is used.** The sound table is indexed
by atom type, and `1000:083b` fires slot 10 on the CATCH - so `GLDFADE.SFX` is
exactly what a player hears when a Bonus turns into a Flashium. It is named
after an animation that never plays, and it plays at a moment that is not a
fade.

One more thing points the same way. Every fade sound carries an internal name -
"Red Fade", "AntiMatter Fade", "Mischief Crystal Fade" - and **`GLDFADE.SFX`'s
name field is empty**, alone among the eleven:

    AFADE   "AntiMatter Fade"      GLDFADE  ""
    CRFADE  "Mischief Crystal Fade"  FFADE  "Flash Fade"

A sprite family that nothing can reach, and a sound file named after it whose
name field was never filled in. The reading that fits both is the player's:
the gold fade was designed as a beaker effect, the Bonus was changed to convert
on catch instead, and what survived was the sound - repurposed - and six
sprites nobody removed.

**Do not "fix" this in the port.** The unreachability is the original's
behaviour and is closed four ways in code; reaching it would mean changing a
rule. It is written up as a candidate ENHANCEMENT in `PLAN.md` section 5,
behind a switch and after the port is faithful - which is where a thing the
original never shows anybody belongs.

`1000:278c` is worth a line: the settle sound is fired **once** at the tail of
the gravity pass if anything moved, not once per atom. A whole beaker
collapsing is a single knock, which is why the flag behind it is a boolean.

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
| `.SPR` sprite strips | 2 | **solved**, all 11 sub-images render |
| `.SCR` demo recording | 1 | solved |
| `.ANM` animation | 1 | **solved**, all 23 frames interpret |
| `.BIN` raw data | 1 | not examined |

**Every format the game loads is now decoded.** `.BIN` is the one remaining
resource and nothing decompiled reads it.

## .SPR - sprite strips (solved)

Two resources, `AMLOGO.SPR` and `LIGHTN.SPR`, both loaded by the Absolute
Magic splash at `2178:00eb` through `21ea:04c8` - a loader of its own, which
is what says this is a distinct format rather than a renamed `.GFX`.

    u16   0x00f5              a marker word; both files carry it
    u16   count               6 in AMLOGO, 5 in LIGHTN
    u16   offset[count]       from the start of the file
    ...   count .GFX images: u16 width, u16 height, then PLANAR pixels

The reading is settled by an oracle rather than by inspection: for all eleven
sub-images across the two files, `offset[i+1] - offset[i]` is exactly
`width * height + 4`, and `offset[0]` is exactly the header length. Nothing
else fits.

The pixels are planar, like an ordinary `.GFX` with no `0xE5` prefix - which
only rendering settles, since a planar/chunky mistake passes every size check
and this project has already made it once.

    AMLOGO   20x20  44x31  60x59  64x86  128x116  172x127
    LIGHTN   36x22  60x22  20x73  56x108  76x167

`AMLOGO`'s six frames are one logo at six sizes - the zoom that arrives with
`WOOSH.SFX` - and `LIGHTN`'s five are lightning bolts.

**The .SPR handle is a record, not a pointer.** `23e7:0105` and `2321:09d0`
both take `@handle` and read past it, so the caller's variable is

    TSprite = record data: Pointer; frame, x, y: Word end   { 10 bytes }

with `frame` **1-based**. `23e7:0105(var s, var w, var h, var count)` fetches
the current frame's dimensions and the strip's length; `2321:09d0(var s)`
draws it at the record's own x, y. Its inner loop is `LODSB; OR AL,AL; JZ` -
**index 0 is transparent**, read from the code rather than inferred from the
way the sprites look. The marker word's HIGH byte is a draw mode: 0 is this
plain path, 2 far-calls into the data as a compiled sprite, and both .SPR
files are mode 0.

## `2178:00eb`, the Absolute Magic splash

The second of the two, and much the larger. Nine resources: `INTRO.PAL`,
`CLOUD.GFX`, `AMWRITE.GFX`, `AMLOGO.SPR`, `LIGHTN.SPR`, `AMTHEME.MUS`,
`WOOSH.SFX`, `LIGHTN.SFX`, `ABSMAGIC.SFX`.

**The backdrop is built, not loaded.** `2178:0000` clears a 64,000-byte
buffer, copies `CLOUD.GFX` (320x83 chunky) in at offset 0, and then writes the
same bytes again **descending from offset 63,999**. So the bottom of the
screen is the cloud band rotated 180 degrees and rows 83..116 stay black - the
band the logo sits in. `21d0:0000` then de-chunks the buffer into Mode X
planes (`w div 4` bytes per plane row, four passes at stride 4) and
`2321:0792` blits it; both temporaries are freed straight afterwards, and page
0 is copied to pages 1 and 3, so page 3 is the pristine backdrop every
dirty-rect erase restores from.

Then `PlayMusic(AMTHEME.MUS)`, `FadeIn`, `Delay(30)`, and three phases:

| phase | rate | what |
|---|---|---|
| the arrival | `SetFrameRate(9)` | `WOOSH.SFX`, then `AMLOGO`'s six frames, each centred by `(320 - w) div 2`, `(200 - h) div 2` |
| the storm | `SetFrameRate(4)` | five strikes, `LIGHTN.SFX` each, one `LIGHTN.SPR` frame each |
| the writing | - | `ABSMAGIC.SFX`, the page cleared, the logo and `AMWRITE.GFX` at (72, 88) |

Each phase erases the previous rect from page 3 and redraws, and each polls
the input driver once a frame - 1 or 2 leaves at once.

**The lightning positions are five literals**, one per arm of a `case`:

    strike 1  (63, 7)     strike 2  (9, 164)    strike 3  (284, 71)
    strike 4  (46, 50)    strike 5  (182, 15)

**The white flash is real and it is a palette trick.** The splash allocates a
768-byte buffer of its own and `FillChar`s it with **63** - full-intensity
white - then each strike does

    DrawSpr(bolt); DrawSpr(logo);
    SetDAC(white);          { 23e7:003d, which waits a retrace first }
    FlipPage;               { 2321:014f }
    SetDAC(INTRO.PAL);

so the screen is white for the retrace either side of the page flip and then
snaps back. That is the flash, and it is the only palette effect in the game
that is not the standard fade.

The tail is `Delay(15)`, the sound, the final picture, then six holds of
`Delay(10)` polling for a key, and the usual `[DS:0x22de]` + `FadeOut` +
`ClearKeyBuffer` exit.

## .ANM - delta animation (solved)

One resource, `SOFT.ANM`, played by the Software Creations splash. Like a
`.CSP` it is **executable**: `21d5:0000` far-calls each frame with `ES:DI` on
the mode 13h framebuffer and `DS:SI` on the frame itself.

    u16   count                       23
    u32   size[count]                 bytes per frame; only the low word is read
    ...   count code blobs

The player, in full, is `21d5:0000`:

    N     := ANM^[0]
    table := @ANM^[2]
    frame := table + N * 4
    for i := 0 to N - 1 do begin
      size := table[i]
      WaitRetrace; WaitRetrace; WaitRetrace     { 23e7:0016, three times }
      ES := $A000; DI := 0
      CALL FAR frame                            { the blob paints itself }
      frame := frame + size
    end

Two things follow, and both shape the port:

* **three retraces per frame**, so the animation runs at 70/3 = 23.3 Hz and
  its 23 frames take almost exactly one second;
* **frames are deltas**. Each blob paints only what changed, so they have to
  be replayed in order over the still image beneath - `SOFT.GFX`, drawn first
  by the splash.

A blob starts by adding its own code length to SI, which lands SI on the
literal pixels stored *after* its `RETF`; the `movs` runs then copy from
there. So the bytes past the RETF are data, not code, and a decoder that
insists a resource ends at its RETF - as the `.CSP` decoder rightly does -
rejects every frame.

The whole instruction vocabulary, verified against all 23 frames with no
unknown opcode remaining:

    33 c9        xor cx,cx        f3 ab   rep stosw     ab   stosw
    b0 ii        mov al,imm8      f3 aa   rep stosb     aa   stosb
    b1 ii        mov cl,imm8      f3 a5   rep movsw     a5   movsw
    b9 iiii      mov cx,imm16     f3 a4   rep movsb     a4   movsb
    b8 iiii      mov ax,imm16     8b df   mov bx,di     cb   retf
    81 c6/ee     add/sub si,imm16 8b fb   mov di,bx
    81 c7/ef     add/sub di,imm16

`mov bx,di` appears exactly once per frame, at the head with DI still 0, and
`mov di,bx` never appears at all - the compiler emits the save and never needs
the restore.

`tools/anm_decode.py` interprets it (`INFO`, `OPS`, `RUNS`, `RENDER`), and
`tubes-port --dump-anm SOFT.ANM` prints what `RUNS` prints: per frame, the
number of coalesced runs, the bytes they carry, and an FNV-1a over
`(offset, bytes)`. **All 23 frames agree exactly** between the two
interpreters - the same standard the `.MUS` register stream is held to, and
for the same reason: a wrong SI would still paint something plausible.

Rendered over `SOFT.GFX`, the animation is four small characters flying in
from off screen and gathering above the "Software Creations" lettering.


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
correspondence is the confirmation: the twelve records are the atoms.

**But `(303, 186)` is an initial value, not a parking space.** See the lifecycle
section below - records never return to it.

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
| 19 | `MYSTBALL` | none | **a rendering state, not a ball** - substituted at the six network draw sites while `-0x189` is set |

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
| `BUBBLE` | the **intro cutscene** - the beaker foams and bubbles as the elements go unstable | from play |
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

So chains = atoms - 2, and simultaneous chains apply a multiplier. **Now
implemented** in `src/game.cpp`, replacing the invented `kScorePerAtom` /
`kChainBonus`.

Note the award is by **orientation only** - there is no length scaling - and
that reproduces both live measurements exactly: a vertical run of 3 paid 250,
and a diagonal run of 4 paid 1000 rather than a multiple of it. An attempt to
fit a curve `250*(len-2)^2` through those two points matched them by
coincidence and was thrown away once this table was re-read. The answer was in
the Instructions the whole time, which is the same lesson as `.MUS`: read the
second consumer of the data rather than modelling the first.

The simultaneous-chain multiplier is **not** implemented, because a 5-cell
clear was observed paying exactly 1250 = 250 + 1000 - two chains summed with no
multiplier at all. Nothing measured so far distinguishes the two readings.

Also: "in combination with Flashium atoms" is the game confirming the wildcard.

### Flashium matches three-of-a-kind on its own - reported from play

**Three Flashium with no element of their own DO form a chain.** This was
briefly written up here as an open question; it is not one, and the answer had
been reported in play before that - it was simply never written down, and was
lost when the session's context was compacted. Recording it properly is the
fix, and the lesson is that anything learned from play belongs in this file the
moment it is said, not in the conversation.

- A three-Flashium match has **its own sound**, which no other match makes.
- Its clear animation is a **multicoloured checkerboard**. The checkerboard is
  used by *any* Flashium that fades, so the animation is not unique to the
  three-Flashium case - but the **sound** is.

That the game bothers to distinguish the case with a dedicated sound is itself
the argument that the case arises. A wildcard that could never form a chain
unaided would have nothing to play it for.

**Flashium matches everything adjacent, simultaneously** - confirmed in play:

    2 8 2 2      the whole line clears (one Greenium run of four)
    2 2 8 1 1    the WHOLE line clears - the 8 completes 2-2-8 and 8-1-1 at once

So there is no tie-break and no ownership: a wildcard belongs to every run it
can complete. That was briefly written up here as an "unresolved ambiguity in
the original", which was wrong twice over - it was a bug in the port's greedy
line-walk, and the game's behaviour is the simplest possible option.

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

`MYSTBALL` is the **`?`** ball. **SOLVED, and it is not a ball** - see "The
hidden-atom modifier" at the end of these notes: it is the sprite substituted
for a real atom while a hidden-atom wave is running, so it has no behaviour of
its own. The play report below is superseded and kept only as a record of the
wrong turn. Reported from play, unconfirmed: it **becomes a
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

The file is 960 bytes and extremely sparse - the first sample had **24 non-zero
bytes**, which is why it was briefly mistaken for an empty file.

### The structure: two banks of five slots

A second save, written by play under a different name, exposed the layout. It
landed at `0x230` - `0x50` after the first - and **every field repeats at that
stride**:

| field | `+off` | slot at `0x1e0` | slot at `0x230` |
|---|---|---|---|
| player name | `+0x00` | `07 "Stephen"` | `0b "Stephen Hax"` |
| score, u32 LE | `+0x1f` | 24500 | 81250 |
| wave number | `+0x26` | 50 | 54 |
| drops remaining | `+0x27` | 11 | 11 |
| atoms to survive | `+0x2d` | 30 | 30 |

plus matching singles at `+0x23`, `+0x28`, `+0x2a`, `+0x2b`, `+0x2c`, `+0x2e`,
`+0x30`, `+0x31`. A slot is therefore **`0x50` bytes**, and `960 = 0x3c0` divides
into **two banks of `0x1e0`**:

    bank 0   0x000 .. 0x1df    slots at 0x000 0x050 0x0a0 0x0f0 0x140, trailer 0x190
    bank 1   0x1e0 .. 0x3bf    slots at 0x1e0 0x230 0x280 0x2d0 0x320, trailer 0x370

**Five slots and a trailer per bank, one bank per game mode.** That is not a
guess fitted to the arithmetic - it explains the two loose ends the old notes
listed as unaccounted for. `0x1bb` and `0x39b` are at `0x190 + 0x2b` and
`0x370 + 0x2b`: *the same offset within each bank's trailer*. A stray byte in
each half of the file becomes one field appearing twice.

It also matches the menu exactly. The saved-game list shows **five** slots, and
choosing Endurance listed five `(UNAVAILABLE)` entries for a save that Wave Mode
displayed immediately - bank 0 is Endurance and empty, bank 1 is Wave mode and
holds both saves, in its first two slots.

The trailer's `+0x2b` byte differs between samples (`0xeb` -> `0xe2`,
`0xf3` -> `0xbc`) including in the bank that stayed empty, so it is **not** a
checksum over slot contents. Purpose unknown.

**Consequence for every earlier experiment:** the wave byte at `0x206` that the
level-warp sweep edited is **bank 1, slot 0's** wave field - not a global. A
save in a different slot is warped by a different address, `0x230 + 0x26` for
slot 1. This never bit the sweep because only one slot was occupied at the time.

The name, score and wave fields are confirmed - the game renders those exact
values. Drops is confirmed separately and more strongly below. The atom target
at `+0x2d` is a strong correspondence across two saves that happen to share the
value 30, so it remains the weakest of the five.

### FULLY DECODED, from the three routines rather than from samples

The three consumers, found with `FindScalarRefs` on the bank addresses - the
method this file ranks first:

| | | |
|---|---|---|
| `1b2e:000a` | reader | `FillChar`s both banks with zero, then `BlockRead`s |
| `1b2e:00ac` | writer | two `BlockWrite`s of `$1e0`, nothing else |
| `1000:2dd0` | the F2 save screen | fills the record field by field |
| `1b2e:4d80` | the menu | reads a slot when the player picks one |

`1b2e:00ac` is the whole file:

    DS:0x1ae3 := Random(254) + 1;  DS:0x1cc3 := Random(254) + 1
    Assign(f, 'TUBES.SAV');  Rewrite(f, 1)
    BlockWrite(f, DGROUP:0x1928, $1e0)      { bank 0, Endurance }
    BlockWrite(f, DGROUP:0x1b08, $1e0)      { bank 1, Wave }
    Close(f)

**Six records per bank, five of them slots** - `$1e0 = 6 * $50` - which is the
same shape as `TUBES.HSC`'s eleven-for-ten. And the reader zero-fills before
reading, so a missing file leaves every length byte at zero and "empty" needs
no separate flag: `1b2e:5427` is literally `CMP byte ptr [0x1928],0`.

**The record is sixteen stores at `1000:3660`**, filling a scratch record at
`DGROUP:0x1ce8` from the session frame before `Move`ing all `$50` bytes into
the slot:

| `+off` | width | session | what |
|---|---|---|---|
| `0x00` | 31 | - | the typed description, `string[30]` |
| `0x1f` | u32 | `-0x153`/`-0x151` | score, two stores |
| `0x23` | byte | `-0x14f` | continues left |
| `0x24` | u16 | `-0x14e` | total chains |
| `0x26` | byte | `-0x170` | wave |
| `0x27` | byte | `-0x17e` | drops remaining |
| `0x28` | byte | `-0x17c` | chains this wave |
| `0x29` | u16 | `-0x180` | velocity |
| `0x2b` | byte | `-0x181` | dispense interval |
| `0x2c` | byte | `-0x183` | chain target |
| `0x2d` | byte | `-0x182` | atom target |
| `0x2e` | byte | `-0x184` | colour target |
| `0x2f` | byte | `-0x185` | crystals |
| `0x30` | byte | `-0x186` | marked |
| `0x31` | byte | `-0x17b` | pre-fill |

The last six are `WaveProgress` - the counters `1000:a4cd` seeds and
`1000:a616` steps - so **a save restores the progression, not just the wave
number.** That is what makes a loaded wave 40 harder than a warped one, and it
explains a loose end from the level-warp work: the sweep saw wave-6 counters on
a wave-75 save because it edited `+0x26` and nothing else.

**The mystery byte at `+0x2b` of the trailer is a nonce.** `1b2e:00ac` writes
`Random(254) + 1` into both banks at `bank + 0x1bb` on every save, which is
record 5 field `+0x2b` - the sixth record's interval byte. The reader loads it
into a local and never looks at it again. So the earlier reading, that it
"differs between samples including in the bank that stayed empty, so it is not
a checksum", was right, and this is why: it has no consumer at all.

Verified against three real files with `--dump-save`, which decodes, prints and
re-encodes: **0 of 960 bytes differ** on each, including the player's live save.
`tools/sav_decode.py` prints the same fields and the two agree line for line.

Two arithmetic checks fall out of that and are worth keeping, because they
confirm two fields at once from a file nobody wrote for the purpose:

* the warped save reads **wave 75** at `+0x26`, the byte the level-warp sweep
  edited at file offset `0x206` - which is bank 1, slot 0, `+0x26`;
* an unwarped slot reads **wave 4, interval 67**, and `70 - (4 - 1) = 67` is
  exactly `1000:a616`'s `Dec(interval)` once per wave cleared.

## Menu structure, and Wave mode

    Main menu                                    (8 items, selection WRAPS)
      Start Game / Continue Saved Game / Game Options / High Scores /
      Instructions / View Demo / Credits / Exit Tubes
    -> Game Mode
      Endurance Mode / Wave Mode / Exit
    -> Saved Games Available
      five slots, each "<name>  Wave <n>", or "(UNAVAILABLE)"

    Game Options                                 (captured, no difficulty here)
      Toggle Music <yes/no> / Toggle Sound FX <yes/no> /
      Redefine Input Device / Exit

The main menu **wraps**, which is measurable rather than assumed: from Game
Options (index 2), six `Up` presses landed on Instructions, and
`(2 - 6) mod 8 = 4` is exactly Instructions' index.

Difficulty is **not** in Game Options - it is chosen on the Start Game path.
Worth stating because the name "Game Options" is where one would look for it,
and the 9/6/3 allowance is per-difficulty.

### Between waves: the stats blackboard

Clearing a wave shows a blackboard slide before the next briefing:

    Wave <n> Stats
      Molecule Chains        <chains this wave>
      Total Molecule Chains  <chains across the run>
      Score                  <score>
      High Score!            (shown only when the run beats the table)

Two chain counters, one per-wave and one cumulative, which the port tracks
neither of. The `Chains` figure in the HUD is the per-wave one.

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


## The speed table, from a controlled A/B

The earlier "atoms have two speeds" note had a flaw in the *experiment*, not the
arithmetic: **no input was ever sent**. The test tube never moved, so it never
caught anything, so every descent sampled was an atom that had already been
missed. Calling 18 px/frame "the descent" was therefore an over-labelled result.

Re-measured with input as the controlled variable. Both conditions interleaved in
short blocks within one session - the first attempt ran 95 s of each back to
back and the second phase captured 15 frames with **zero motion**, because with
no input every atom is missed and wave 6 allows only 11 drops, so the wave had
ended and phase B was sampling a game-over screen. When the control condition
itself ends the game, long phases cannot work.

### Result

Upward steps inside the tube network (`y <= 68`), 292 vs 295 frames:

| dy | no input | **Down held** |
|---|---|---|
| -4 | **188** | 66 |
| -8 (= 2 x 4) | 11 | 32 |
| **-18** | 10 | **29** |
| -36 (= 2 x 18) | 0 | **10** |

and horizontally, `dx = -18` appears **only** with Down held.

So the accelerated step is **18 px/frame**, applied in whatever direction the
atom is travelling, against a normal **4 px/frame**.

**The `-4` count does not fall to zero under Down - it drops to 66.** That is the
load-bearing detail. A global speed-up would eliminate it; its persistence is
exactly what the Instructions predict: "increase the speed of any atoms in the
tube **directly above the test tube**". Some atoms accelerate, the rest carry on
at 4. The measurement independently confirms the wording.

### Summary

| what | step |
|---|---|
| atom travelling the tube network | **4 px/frame** |
| atom with Down / Button B held, and above the test tube | **18 px/frame** |
| atom below the tube mouth (`y > 68`) | **18 px/frame**, in *both* conditions |
| the test tube sliding on its rail (static analysis) | 6 px/frame |

Below the mouth, `18` dominates whether or not Down is held (18: 14 vs 15;
36: 4 vs 8) - consistent with the boost applying only to atoms still in the tube,
not to ones already past it.

### What this does *not* settle

Play reports that a missed atom's fall *looks* different from a boosted atom in
the tube. The measured per-frame step is the same magnitude - 18 - for both, so
the difference is not step size. It may be direction and path (a straight
vertical plunge versus following the tube route), or a distinction this
experiment cannot resolve: with no left/right input the test tube stays in one
column, so the `y > 68` band mixes atoms descending *into* the tube with atoms
falling *past* it. Separating those needs the tube deliberately positioned under
a known column, and is not yet done.


## Caught vs missed, separated

The speed A/B could not tell a caught atom from a missed one, because with no
left/right input the test tube never moved and the `y > 68` band mixed both. So
the tube was driven to a known column - hold **Left** to park it at the leftmost -
and every atom classified by whether its x matched.

Parking confirmed two ways: a screen capture shows the tube at the far left, and
the behaviour below is unambiguous on its own.

Frames observed per play column, over 379 frames:

| column | near the mouth (y 40..75) | deep below it (y >= 120) |
|---|---|---|
| **107 - the tube's column** | **226** | **0** |
| 125 | 4 | 303 |
| 143 | 8 | 197 |
| 161 | 8 | 205 |
| 179 | 8 | 159 |
| 197 | 4 | 276 |

The contrast is total. In the tube's column atoms **arrive at the mouth and stop
there**, accumulating for hundreds of frames, and **never once** appear below
y=120. In every other column they cross the mouth in a handful of frames and
fall away to y = 177..186.

**They are not landing in the beaker.** An earlier draft of this section said
they "plunge to the beaker floor", which was an interpretation laid on top of the
data. Atoms that miss the test tube are **lost** - that is a *drop*, and it is
what the drop allowance counts. The beaker is filled only by catching atoms in
the tube and tipping them in with **Button A**. Nothing reaches the beaker
without passing through the tube first.

### So the difference is not speed, it is what happens at the mouth

Both cases descend at the same 18 px/frame. What distinguishes them:

- **Caught** - the atom decelerates to a halt in the `y 40..75` band and stays
  there. Visually, an atom that stops.
- **Missed** - the atom continues through the mouth without interruption at a
  constant 18 px/frame until it reaches the beaker. Visually, an unbroken plunge.

That accounts for the reported impression that a missed atom's fall "looks
different" from a boosted atom in the tube, without needing a fourth speed: the
distinction is continuity, not rate.

### An open question this raised

Caught atoms never appear below **y = 120**, yet the test tube hangs from y=69 to
y=134 and holds five atoms of 13 px each - which would fill it to y=134. If
caught atoms were still tracked in the 12-record array at their drawn positions,
some would have to appear below 120. They do not.

The likely reading is that an atom **leaves the 12-record array once caught** and
the tube's contents are held in a separate structure - which would also fit the
tube being a LIFO stack rather than twelve free-moving sprites. Not proven, and
worth chasing: it means the array is "atoms in transit" only, exactly as its
initialisation to the spawn marker suggests.

### The test tube struct was not located

Snapshotting a +/-1 KiB window around the atom array, pressing Right, and
diffing found **no** word behaving like the tube - nothing holding a waypoint
target (104, 122, 140, 158, 176, 194) and nothing stepping by 6. `PLAN.md` places
the struct at `parent - 0x16a`, which would put it 7 bytes below the array base,
inside the window searched.

So either the tube's position is not stored as one of those values, or the struct
is somewhere else entirely. The static reading remains unconfirmed against a
running game, and the window should be widened before trusting it.


## The record lifecycle: slots are overwritten, never recycled through the marker

Predicted, then measured, then wrong - which is the useful kind.

Since a missed atom is *lost* rather than deposited, its record must become
reusable somehow, and the obvious candidate was a reset to the spawn marker
`(303, 186)` that all twelve initialise to. Every earlier analysis had **filtered
spawn-marker transitions out** as uninteresting, so precisely the evidence needed
was being discarded.

Recording them instead, over 382 frames of live play:

| | count |
|---|---|
| records transitioning **into** `(303, 186)` | **0** |
| records transitioning **out of** it | **0** |

So the hypothesis is dead. Combined with the large positional jumps already seen
in the per-frame data (`dx` of `+/-49`, `+/-91`, `+/-133`, and `dy` in the
hundreds), the model is:

- The twelve records are a **pool of slots**.
- `(303, 186)` is the **initial value of a slot that has never been used**. It is
  not a parked state and not a recycle target - nothing ever returns to it.
- A slot is reused by **overwriting x and y directly** with the atom's new entry
  position. That is what the large jumps are: not motion, allocation.
- A lost atom's record simply **retains its final position** (y = 177..186) until
  that slot is allocated again.

This is why the spawn-marker signature reliably locates the array: early in a
wave most slots are still untouched. It also means the signature gets **less**
reliable the longer a wave runs, as fewer slots retain the initial value - worth
knowing before relying on it late in a session.


## The beaker grid, located and proven

The last core data structure. Found by shape rather than by diffing - a
before/after diff drowns in per-frame churn (atom positions, animation counters,
the rotating Flashium pointer), and its candidate list filled with atom-record
bytes.

**The shape is the signature.** With the test tube parked under the leftmost
column, every atom tipped lands in column 1, so in a row-major array of stride 6
every filled cell must sit at an offset that is a multiple of 6. Scanning
conventional RAM for 30-byte windows that are zero except for a few bytes at
offsets `% 6 == 0` holding 1..19 is demanding enough to cut through the noise.

### Layout, proven by tipping into two columns

Consistency is not proof: one column of values cannot distinguish row-major from
column-major. So tip into column 1, move one column right, tip again:

    column 1 filled  0x2432c
    column 2 filled  0x2432d      <- exactly +1

`+1` for adjacent columns is row-major with 6 cells per row; column-major would
have put it 5 away. And two atoms stacked in one column landed at `0x24326` and
`0x2432c` - exactly 6 apart, one row.

| | |
|---|---|
| base | **`0x24314`** |
| size | 30 bytes, 5 rows x 6 columns |
| order | **row-major**, 6 cells per row |
| rows | increase *downward* with address - row 1 at `+0`, **row 5 (bottom) at `+24`** |
| cell | `0` = empty, otherwise a **ball type 1..19** from the sprite type table |

A live grid with three atoms settled:

    row 1:  0  0  0  0  0  0
    row 2:  0  0  0  0  0  0
    row 3:  0  0  0  0  0  0
    row 4:  0  2  0  0  0  0
    row 5:  2  2  0  0  0  0        type 2 = Greenium

### A settled Flashium's cell really does hold 8

Recorded earlier as the model that made three facts consistent - fades are
type-indexed, a Flashium always clears with `FFADE`, and a cell cycling 1..7
could never produce a type-8 lookup - but flagged as unproven.

An earlier pass caught a settled cell holding **8** at `0x2432c`. Since type 8 is
Flashium in the measured sprite table, that is the value read directly out of the
grid. The cell holds 8 permanently and the flashing lives entirely in the sprite
pointer, exactly as the rotation measurement implied. **Confirmed.**

It also confirms cell values are *not* limited to 1..7: the grid stores the full
type range, so specials settle as themselves.

### The "three parallel arrays" claim is not supported here

`PLAN.md` describes the beaker as three parallel arrays of stride 6. The memory
does not obviously bear that out:

- The 30 bytes **after** the grid (`0x24332`) are not a grid at all - they hold a
  Pascal string, `0b "CRFADE6.CSP"`, i.e. a resource name.
- The 60 bytes **before** it (`0x242d8`, `0x242f6`) are entirely zero. Two more
  empty arrays would look exactly like that with only ordinary atoms settled, so
  they are plausible candidates, but zeroes are not evidence.

So the type array is confirmed and located; whether two companion arrays sit
immediately below it is **open**. The test is to settle a *special* - the flag
that distinguishes one, if it exists, should appear in one of those blocks while
the type array holds the special's type.


## The test tube struct, located and confirmed

`PLAN.md` described this struct from static analysis and it had never been found
in a running game. An earlier hunt diffed +/-1 KiB around the atom array while
nudging the tube two columns, and found nothing.

**What worked was reversibility.** Park left, park right, park left again, and
keep only the bytes that changed *and returned*. Frame counters, RNG state and
score do not go back, so that one constraint removes almost all the churn that
made the earlier diff useless. Three bytes survived:

    0x245d0 : 104 -> 194 -> 104
    0x245ee :   1 ->   6 ->   1
    0x245ef : 104 -> 194 -> 104

`104` and `194` are the outermost waypoint targets, and `0x245ee`/`0x245ef` are
adjacent holding an index/target pair - exactly the `+0x1e` / `+0x1f` layout the
static reading gave. That puts the base at `0x245ee - 0x1e` = **`0x245d0`**, and
that byte holds x.

### Confirmed against the state machine, which was not used to find it

Fitting a base to three numbers proves nothing on its own, so it was tested
against a field it had not been fitted to - `+0x04`, the state:

| input | `+0x04` observed | x |
|---|---|---|
| idle | `0` | 158 |
| hold **Left** | `0`, **`1`** | 104, 110, 116, 122, ... 158 |
| hold **Right** | `0`, **`2`** | ... 164, 170, up to 194 |
| press **Button A** (Left Ctrl) | **`3`** | 194 |

Exactly the documented machine - 0 parked, 1 sliding left, 2 sliding right,
3 tipping. Three further things fall out of the same run:

- **x steps by 6**, taking 104, 110, 116, 122, 128, 134, 140, 146, 152, 158,
  164, 170 - the 6 px/frame rail speed, confirmed live.
- **The waypoint targets are exactly** 104, 122, 140, 158, 176, 194, and the
  index runs 1..6 in step with them.
- Button A driving state 3 **independently re-confirms A = Left Ctrl**.

| offset | field | width |
|---|---|---|
| `+0x00` | x | byte |
| `+0x04` | state: 0 parked, 1 left, 2 right, 3 tipping | byte |
| `+0x1e` | waypoint index, 1..6 | byte |
| `+0x1f` | target x | byte |

### Two corrections

**The tube is not adjacent to the atom array.** `PLAN.md` has the atoms at
`parent - 0x163` and the tube at `parent - 0x16a`, i.e. seven bytes apart with
the tube *below*. Measured, the tube is at `0x245d0` and the array at `0x2419e` -
**`0x432` bytes apart, with the tube above**. The two static offsets are right
about the field layout *within* each structure and wrong about the relationship
between them.

**It is probably not a self-contained record.** The 34 bytes read from
`0x245d0` include the **score** as a `u32` at `+0x17` (`0x245e7`), reading 24500
and matching both the HUD and `TUBES.SAV`. A tube record would not contain the
score. These are more likely adjacent locals of `1000:9e53` - which is what a
Pascal nested-procedure frame looks like - so "the tube struct" is a group of
neighbouring variables rather than a record.

Useful side effect: **the live score is at `0x245e7`**.


## `MYSTBALL` and the crystal: not determined

Recording a **negative result** and a **retraction**, because both are more
useful than a guess.

### What was established

**Type injection works.** Writing a type byte into an atom record's `+0x0b`
takes effect - the record carries the new type and keeps flying - so behaviour
does not have to be waited for. That is a usable technique for any future
question about a rare atom.

**A lost atom's type is zeroed.** An injected type-19 atom fell past the tube
and its type byte went `19 -> 0`. So `type == 0` marks a **free slot** in the
12-record pool, which fills in the lifecycle: a slot is never recycled through
the spawn marker, its position is left where the atom died, and the *type* is
what gets cleared.

### Retracted: `0x24546` is not the tube contents

An earlier run reported the value 19 appearing at `0x24546` and read it as a
caught `?` ball landing in the tube's storage. **That was wrong.** Watching the
same address over time shows it counting steadily downwards:

    0x24546:  20 -> 19 -> 18 -> 17 -> 16 -> 15
    0x245bc:   9 ->  8 ->  7 ->  6 ->  5 -> ... -> 1

Both are **decrementing counters**, and the detector - "any byte holding 12..19
that was not there before" - simply caught one passing through the type range.
A filter that cannot distinguish a value from a *trend* was the wrong instrument.

### Why it stalled, and what would fix it

The experiment needs an injected atom to be **caught**, which needs the atom and
the tube in the same column at the same moment. Four attempts failed on timing:

- an atom descends at 18 px/frame, so the window between "high in a column" and
  "past the mouth" is a couple of seconds even on a slowed guest;
- steering the tube costs ~1 s per column, which is longer than that window;
- parking first and waiting means the tube's column may simply receive no atom;
- and **the wave ends after 11 drops**, which with a parked tube arrives in
  under a minute - two runs ended with nothing moving and the grid reading
  garbage, because the locals had been repurposed.

The fix is available and not yet done: **freeze the drop counter**. The rig can
write memory, so finding the drops value - it is on the HUD, so a
diff-on-change hunt would locate it quickly - and holding it constant would
remove the time limit entirely. With unlimited wave time, catching an injected
atom becomes a matter of waiting rather than of luck.

Until then, `MYSTBALL` (type 19) and the crystal (type 18) behaviours remain
**unknown**. The play-derived hypothesis - that a `?` resolves into one of the
other five letter balls (12..16) on capture - is untested.


## Wave definitions, harvested by level-warping the save

`TUBES.SAV` holds the wave number as a single byte at **`0x206`**. Editing it and
loading the save warps to that wave - **there is no checksum**, the slot list
reads back "Wave 20" for `0x14`, and the briefing that follows is that wave's.
So the whole wave table can be read out of the game's own briefings without
playing, which is how the crystal was finally found: it is gated to late waves,
and no amount of playing wave 6 would ever have shown it.

Validated on consecutive waves - 10, 11 and 12 give different briefings - so the
byte genuinely selects the definition. (Waves 10 and 15 happen to share an
objective; that is a coincidence, not a warp failure.)

### Objective types seen

| wave | objective | modifier |
|---|---|---|
| 6 | survive 30 atoms, forming as many chains as possible | **Yellowium disabled**, will not disappear |
| 10, 15 | form **2 vertical chains using Cyanium** | - |
| 11 | form 2 chains using any atom | beaker atoms **morph into another atom every 45 s** |
| 20 | **remove marked atoms** from the beaker by forming chains | Marked Atoms: 3 |
| 25 | form 2 chains in the colour named by the **Task Display** | the required colour **changes after each task** |
| 30 | survive 30 atoms | atoms are **hidden until they leave a tube** |
| 40 | form **2 horizontal chains using Purplium** | - |
| 50 | **remove all Mischief Crystals using Anti-Matter** | Mischief Crystals: 1 |

Every wave sampled allowed **11 drops**, so the allowance may be constant in Wave
mode - eight samples, not proof.

A wave definition therefore carries: an objective *kind* (survive N atoms / form
N chains / remove N marked atoms / remove N crystals), optionally a **chain
orientation** and **colour**, a **count**, a **modifier** (disabled element,
morphing, hidden atoms, dynamic colour), and a drop allowance.

### `CRYSTAL` is the "Mischief Crystal" - solved

Wave 50: *"Using Anti-Matter remove all the Mischief Crystals that are
contaminating the beaker."* The slide shows exactly the red rounded block that
`CRYSTAL.CSP` renders.

So a crystal **contaminates the beaker and is destroyed with AntiMatter, not by
matching**. That resolves an apparent oddity in the type table: type 18 has a
full `CRFADE` family despite never forming a chain. A fade family means a thing
can be **cleared**, which is not the same as **matched** - and the earlier note
calling types 1-10 plus 18 "the matchable set" was too strong. Corrected: 1-10
are matchable; 18 is clearable by AntiMatter.

### `MARKER` is the marked-atom indicator - solved

Wave 20: *"Form chains to remove marked atoms from the beaker. Marked Atoms: 3"*,
illustrated with a red **X** - which is `MARKER.CSP`, 12 x 11, previously
recorded as "unidentified; a cursor or a target indicator". It overlays atoms in
the beaker that a wave objective requires clearing.

### The half-size balls are the **Task Display**

Wave 25 names the HUD element: "the colour specified in the **Task Display**".
So the seven 8x7 balls at `DS:0x200a` show the *required colour* for the current
task. That also explains the rotation observed in wave 6: that wave's objective
is "survive 30 atoms" with no required colour, so the display cycles rather than
naming one.


## The sound inventory is fully attributed

`BUBBLE` was the last unknown, and it is **not a gameplay sound**: it belongs to
the intro cutscene, where the beaker foams and bubbles as Lanny's elements go
unstable. Three things had already pointed away from gameplay without settling
it - it is **0.865 s**, far longer than the blips the game uses for actions; its
internal name is *"Bubbles"*, plural; and it was the only sound with no
counterpart anywhere else in the resources. The blackboard cutscene also already
owned `CLAP`, `NOOOO` and `WHATTHE`, and its resource list is `WRITE0..9.GFX`
plus **`EXPLOD1..4.GFX`** - an explosion sequence, which is exactly that scene.

All 24 `.SFX` now have an owner:

| group | count | sounds |
|---|---|---|
| match / clear, one per fade family | 11 | `RFADE` `GFADE` `BFADE` `CFADE` `PFADE` `YFADE` `PNKFADE` `FFADE` `AFADE` `GLDFADE` `CRFADE` |
| gameplay events | 6 | `DROP` (a miss) `HITATOM` `HITGLASS` (landing in the tube) `SLIDE` `SWITCH` `SELECT` |
| cutscene | 4 | `BUBBLE` `CLAP` `NOOOO` `WHATTHE` |
| splash screens | 3 | `WOOSH` `LIGHTN` `ABSMAGIC` |

Worth recording how it was resolved: not by analysis. The duration, the plural
name and the orphan status all *suggested* "not gameplay" but could not say what
it was. Someone recognising it settled it in one line - the same way the
`MULTBALL`, `DROP` and miss-mechanic corrections arrived. Play memory has been
unreliable as evidence and excellent at pointing, which is why these notes keep
the two on separate lines.


### The sound grouping, corroborated from the binary

`BUBBLE` being a cutscene sound was recognised from play. The binary agrees, and
the evidence is structural rather than circumstantial: **resource name strings
are emitted in contiguous blocks**, one per unit, and the blocks match the
grouping exactly.

| string offsets (image) | sounds |
|---|---|
| `0x9d53`..`0x9de8` | the 11 fade families, plus `DROP` `HITATOM` `HITGLASS` `SELECT` |
| `0xaa85`..`0xaa98` | `CLAP` `SLIDE` `SWITCH` |
| **`0xc5fc`..`0xc612`** | **`WHATTHE` `NOOOO` `BUBBLE`** |
| `0x11809`..`0x1181e` | `WOOSH` `LIGHTN` `ABSMAGIC` |

`BUBBLE` sits directly beside `NOOOO` and `WHATTHE` - two sounds already known
to be cutscene reactions - and scanning for code that references those three
resolves them to segments in the `0x1c5x` range, inside CODE_1, the unit holding
the blackboard cutscene at `1b2e:1651`. The splash trio is likewise contiguous
and separate.

**Caveat on the method.** A string's location shows the unit that *emitted* it,
not every unit that uses it. `SELECT` sits in the gameplay block, yet the program
map records the title/menu at `1b2e:52bf` referencing `SELECT.SFX` - a far
pointer crosses units freely. So block adjacency is good evidence of grouping and
poor evidence of exclusive ownership, and the table above should be read as "who
these belong to", not "who may play them".

For the port that distinction matters little: it says which stage needs which
sounds loaded, which is what `PLAN.md` §2 and §4 need.


## The wave objective vocabulary (23 templates)

Wave briefings are not stored per wave. They are **assembled at runtime** from a
fixed set of templates plus parameters - a count, a colour, a chain orientation -
which is why the text breaks oddly where a value is substituted. Extracting the
Pascal strings from image `0x5f00`..`0x8200` gives the game's complete objective
vocabulary, exactly, without transcribing a single screenshot.

**Objective kinds**

| kind | variants |
|---|---|
| endurance | form as many chains as possible; the game grows harder with each chain |
| survive N atoms | plain; **hidden until they leave a tube**; or with a named element **disabled for the wave and will not disappear** |
| form N chains of a colour | horizontal / vertical / diagonal, each with a named colour |
| form N chains, any atom | plain; **beaker already contains atoms**; or **beaker atoms morph every 45 s** |
| form N chains using **Flashium** | - |
| form N chains using a shown atom | "the following atom" |
| form N *Horizontal* or *Vertical* chains using any atoms | - |
| **Task Display** driven | required **colour**, required **chain**, or **colour & chain** - each in two flavours, changing **after each task** or **every 45 seconds** |
| remove N **marked atoms** | plain; or first remove the atoms covering them; or first use **Anti-Matter** to remove the Xenons surrounding them |
| remove N **Mischief Crystals** | using **Anti-Matter** |

That is a far richer rule set than the port models, and several kinds imply
machinery that does not exist yet: marked atoms, a Task Display that changes
mid-wave, atoms that morph on a timer, atoms hidden until they leave a tube, and
a pre-filled beaker.

**Where the parameters live is still open.** They are not a simple 50-entry
table: searching the whole image for a repeating `0x0b` at any stride from 2 to
16 found no drops column, and every wave sampled allows 11 drops, so that
allowance is a Wave-mode constant rather than per-wave data. Counts and colours
are most likely derived from the wave number, which would fit the difficulty
seeds and level bands already recorded above.

The full template list with offsets is in `wave-templates.md` in the tooling
directory, alongside per-wave screenshots in `capture/waves/`. Kept outside this
repository because it is the game's own text.


## The atom record layout is wrong - correction

The record layout in these notes came from a single draw site read statically:

        push word es:[di]        ; x      - record +0
        push word es:[di+2]      ; y      - record +2
        mov  al,  es:[di+0xb]    ; colour - record +0x0b
        ...
        mov  es:[di+0x14], dx    ; saved x, one slot per video page

Dumping whole records from a live game and tallying the values seen at every
offset contradicts most of it. Over 2242 in-transit samples:

| offset | values observed | verdict |
|---|---|---|
| `+0x00` | 125, 161, 143, 179, 107, 58, 197, 246 | **x** - confirmed |
| `+0x02` | 177, 186, 175, 176, 49, 185 | **y** - confirmed |
| `+0x04`, `+0x08` | y-like | further y fields, undocumented |
| `+0x06`, `+0x0a`, `+0x1a` | x-like | further x fields, undocumented |
| `+0x0b` | **only 0 and 1** | **not the colour** |
| `+0x14`..`+0x19` | **always 0** | **not the saved x/y** claimed above |
| `+0x0e` | 0,1,2,3,5,6,7 | candidate colour (1..7) |
| `+0x11` | 0,1,3,4,6,7,8,10,12 | candidate type, range includes specials |
| `+0x12` | 0,2,3,4,5,6 | candidate |

`+0x0b` never varies beyond 0/1 across thousands of samples while the beaker
grid plainly holds 2, 3, 4 and 8 in settled cells. A field that never takes a
colour value is not the colour. The x-like and y-like fields at `+0x04`..`+0x0a`
look like the two saved position pairs - stored as (y, x) rather than at
`+0x14`/`+0x18` - but that is a reading, not a measurement.

### What this invalidates

**The `MYSTBALL` injection experiments are void.** They wrote 19 to `+0x0b` and
then read the same byte back to confirm "the injection worked" - which proves the
write landed, not that the game reads that byte. Since `+0x0b` is not the type,
the atom was never made into a `?` ball, and **the negative wave-30 result means
nothing**: the type-19 count was zero because nothing was ever set to 19 in a
field the game uses. The hidden-atom hypothesis is untested, not disproved.

Also void: the "colours" columns in the dispenser-path and delta-context runs,
which read `+0x0b` and therefore reported 0/1 throughout.

### What survives

Everything that used only `+0x00`/`+0x02`, which are confirmed:

- the dispenser path and its coordinates;
- the speed table (4 travelling, 18 boosted and falling, 6 for the tube);
- caught vs missed;
- the record lifecycle - slots overwritten, never recycled through the marker.

And everything measured independently of the record: the beaker grid, the tube
struct, the 19-entry sprite tables, the sound grouping, the wave vocabulary.

### How to settle the type field

Correlate rather than guess: watch a single atom's whole record while it is
caught and tipped, then read the grid cell it produces. The cell holds the atom's
type, so the field whose value appears there is the type. `+0x0e` and `+0x11` are
the candidates - `+0x0e` spans exactly 1..7, `+0x11` reaches 12, which would
cover the specials.


## Grid decoding validated against a live screenshot

The beaker grid was located and its layout proven by construction, but never
checked against what the player actually sees. Read during a paused wave 31:

    row 4:  Yellowium  Purplium  Redium    .         .        .
    row 5:  Xenon      Yellowium Yellowium Purplium  Redium   Cyanium

which matches the screenshot cell for cell, including the grey ball at the
bottom-left corner reading **Xenon (11)**. Base `0x24314`, row-major, 6 per row,
bottom row at `+24` - confirmed visually, not just structurally.

That also settles a point previously only inferred: **specials settle in the grid
as themselves**, so cell values genuinely run past 1..7 into the full type range.

### The HUD `Drops` field counts down

Observed across a session: the field reads `8`, then `4`, then `0`, and the
label becomes **"No Drops"** when exhausted. So it is drops **remaining**,
starting from the wave's allowance, not drops used. Reaching "No Drops" is the
lose condition, matching the Instructions' "both games end if you drop more atoms
than allowed".

### The Convertor settles as a Xenon

Reported from play, with the observation boundary stated: a `C` ball
(`CONVBALL`, type 14) caught and tipped into an **empty** beaker settled as a
**Xenon** - the grid cell read `11`, and the grey ball was visible in the
screenshot.

The in-game Instructions say the Convertor "will change all occurrences of the
atom it lands on into Xenons". They do **not** say what becomes of the Convertor
itself, and this shows it becomes a Xenon too. The conversion behaviour was *not*
observed here, because the beaker was empty and there was nothing to convert.

This resolves something structural. `CONVBALL` is type 14 and has **no fade
family** - which looked odd for something that reaches the beaker. It never
settles as itself: it becomes Xenon (11), which also has no fade family because
Xenon cannot be matched. The type table and the behaviour agree.


## The game continues past wave 50

Wave 50 was cleared in play and the game advanced to **wave 51**, so 50 is not
the last wave. Published notes say the registered version "adds 50 waves", which
therefore means *+50 on top of* the shareware's set rather than a 50-wave total.

This corroborates a constant measured statically long beforehand. The difficulty
progression recorded from `1000:9e53` has **level bands at 30 / 60 / 75 / 90 /
95 / 101** - bands at 90, 95 and 101 are meaningless in a game that stops at 50,
and are exactly what a ~100-wave game would need.

The wave number is a single byte at `TUBES.SAV` `0x206`, so the format allows up
to 255. Where the real ceiling sits is **not** established: warping to
progressively higher waves and reading the briefing would settle it, since an
out-of-range wave should either refuse to load or produce a degenerate briefing.


## Correction to the correction: the record layout was right, the base was not

The previous section claimed the atom record layout in these notes was wrong
because `+0x0b` never varied. **That claim is withdrawn.** The layout is correct;
the *array base* was wrong by six bytes, which shifted every field I read.

Read at the corrected base `0x241a4`, records decode immediately:

| rec | x | y | `+0x0b` |
|---|---|---|---|
| 0 | 125 | 187 | 2 - Greenium |
| 1 | 58 | 15 | 4 - Cyanium |
| 3 | 179 | 187 | **14 - Convertor** |
| 4 | 161 | 47 | **16 - Filler** |

x values are tube and play columns, y values are real lanes, and the types
include specials. So the static read was right: **x `+0x00`, y `+0x02`, colour
`+0x0b`, stride 28** - base `0x241a4`.

### Why the base was six bytes early

The locator scanned for the spawn marker `(303, 186)` as two consecutive words.
But an **unused record holds `(0, 0)` as its current position and `(303, 186)` in
its saved-position fields** - the raw bytes read `2f 01 2f 01 ba 00 ba 00`, i.e.
x, saved-x, y, saved-y. The scan matched inside that saved pair and returned a
base six bytes early, and every experiment inherited the shift.

That also corrects a claim built on the bad base: "the 12 records initialise to
(303, 186)" describes the **saved** fields, not the live position.

### What the shift did, and did not, break

At a base six bytes early, my `+0x00`/`+0x02` were the *previous* record's saved
x and saved y. Saved positions are last frame's drawn position, kept for the
dirty-rect erase - so they trace **the same path, one frame behind**, and
frame-to-frame deltas of a lagged sequence are identical to the original's.

**Survives unchanged:** the dispenser path and its coordinates, the speed table
(4 travelling / 18 boosted and falling / 6 for the tube), caught vs missed, and
the slot lifecycle. All of those measured trajectory shape and per-frame deltas,
which a one-frame lag does not affect.

**Was broken:** every reading of the type. My `+0x0b` was really `+0x05`, which
is why it never left 0/1, and the `MYSTBALL` injection wrote to `+0x05` rather
than the type. Those experiments stay void - but for this reason, not because the
documented layout was wrong.

**The lesson is the locator, not the layout.** A signature that can match inside
a *different* field of the same record will silently return a shifted base, and
every downstream offset inherits it. The tell was there and I misread it: a field
that never varies is more likely a wrong offset than a wrong document.


## The Mischief Crystal, observed (wave 50)

First direct sighting of type 18, captured by a background logger sampling the
grid ~3x/second while the game was played normally.

**It starts in the beaker.** At wave start the grid already reads

        .   .   .   .   .   .
        .   .   .   .   .   .
        .   .   .   .   .   .
        .   .   .   .   .   .
        .   . CRYSTAL .   .  Pnk

so it is placed as initial contamination, not delivered through a tube.

**It teleports.** Over 99 seconds of play it moved once, r5c3 -> r5c2, seen in
memory as type 18 leaving one cell and appearing in another between consecutive
samples. The destination cell had held a Yellowium; a Yellowium appears one row
above 0.4 s later, which is consistent with the crystal displacing it upward,
though a newly-landed atom cannot be ruled out from one event.

### `CRFADE` is the teleport animation, not a clear animation

This corrects the framing used throughout these notes. Reported from play and
consistent with everything measured: the crystal **does not match**, so it has no
clear animation. `CRFADE` runs **forward as it vanishes and in reverse as it
reappears** - it is how the teleport is drawn.

That explains a measurement that never fitted: type 18's *static* sprite pointer
resolves to **`CRFADE1`**, not to `CRYSTAL.CSP`. Frame 1 is the solid crystal,
because the animation exists to take it away and bring it back.

And when AntiMatter destroys a crystal, it gets the **AntiMatter** animation -
as does everything else caught in a blast.

### So "a fade family means clearable" was wrong

Earlier notes reasoned that only types 1-10 and 18 have fade families, therefore
those are the things that can be cleared. That inference is retired. A fade
family is an **effect animation**, and what triggers it differs by type:

| family | what it actually is |
|---|---|
| the seven colour fades | match-clear, one per colour |
| `FFADE` | the wildcard's own clear, when wildcards match each other |
| `GLDFADE` | the Bonus atom |
| `AFADE` | the **AntiMatter blast**, applied to everything caught in it - not "ANTIBALL's clear" |
| `CRFADE` | the crystal's **teleport**, forward out and reverse back |

Which also dissolves the puzzle of why types 11-17 and 19 have none: they need no
effect of their own. Xenon just sits there, the penalty balls act on the tube or
the beaker and are gone, and anything destroyed by AntiMatter borrows `AFADE`.

## Live confirmations from the same session

**Vertical chain = 250 points, measured.** Cyanium sat at r4c5 and r5c5; a third
was tipped into column 5, both cells cleared, and the score went
**24500 -> 24750, exactly +250** - the value the Instructions state, now observed
in play.

**AntiMatter settles before it detonates.** Two AntiMatter atoms were visible in
the grid as type 9 at r3c6 and r4c6, and had vanished one sample later. So it
lands as an ordinary cell first, then explodes. Notably the Pinkium directly
below at r5c6 **survived**, which constrains the blast shape - it is not simply
"everything adjacent".

**Caught atoms freeze in the array - and that is the test tube's contents.**

Records sat unchanged at `(107,63)`, `(125,60)`, `(161,62)` and `(197,63)` -
four different columns at once, for nine seconds and more. My first reading was
that these were atoms *queued at their tube exits*, which is wrong: atoms at a
tube exit would fall.

Checking properly settles it. The game was **running**, not paused - 75 of 76
flight samples had at least one atom move. And the number of stationary records
in the `y 55..70` band **accumulates over time**:

        t= 70.8   0 held
        t= 76.6   1 held
        t= 82.6   2 held
        t= 87.8   3 held
        t= 93.0   4 held

That is the **test tube filling up**, against its capacity of five. The x values
differ because a caught atom's record is **frozen at the position where it was
caught** and is not updated as the tube slides along its rail - the tube draws
its contents itself.

This answers a question left open since the record-lifecycle work: **caught atoms
do not leave the array.** They persist, motionless, at their catch position.

It also re-explains the earlier caught-vs-missed measurement. That run saw 226
frames of dwell in the `y 40..75` band at the tube's column and none below y=120;
the dwell is caught atoms freezing there, which is why nothing appeared deeper.
The conclusion of that experiment - caught atoms halt at the mouth, missed ones
continue at 18 px/frame - stands. Only my later "queued at tube exits" gloss was
wrong.


## Drops are a persistent pool, not a per-wave allowance - correction

Earlier notes concluded the drop allowance was "a Wave-mode constant" because
every wave sampled announced **11 drops** and no per-wave drops table could be
found in the image. Both observations were real. The conclusion was wrong, and
the experiment that produced it could not have found otherwise.

**The blind spot:** the wave sweep warped by editing **only** the wave byte at
`TUBES.SAV 0x206`. The drops byte at `0x207` was left at 11 in every single run.
So all fifty briefings echoed the same number because they were all loading the
same drop count - the sweep had no way to show variation. A negative result is
only as good as the thing that varied, and here nothing did.

**What is actually happening**, from play:

- The save carries the drop count at `0x207` (11 in this save).
- A **miss decrements it** - observed 11 -> 10.
- A **Bonus atom increments it** - observed 10 -> 11 on catching one, which is
  exactly what the Instructions say: "awards you an extra drop".
- The briefing line "You are allowed *n* drops" is reporting the **current pool**,
  not a per-wave constant.
- The pool **carries across waves**, which is why a save made at wave 6 with 11
  drops still showed 11 after warping to wave 50.

This also explains, without any special pleading, why no per-wave drops table
exists in the binary: **there is nothing to store.** The number lives in the save
and in the session, and waves do not set it.

It makes the Bonus atom considerably more valuable than a scoring pickup - it is
the only known way to replenish a resource that otherwise only decreases, i.e.
an extra life.

**Confirming test - run, and passed.** The plan was to edit `0x207` to something
other than 11 and check the briefing echoed it. Live play delivered the same
test for free, and more convincingly, because nothing was tampered with: the
pool was standing at **8** when wave 52 was cleared, and the wave 53 briefing
announced **"8 drops allocated"**. The briefing reports the current pool. The
fifty screenshots all read 11 because the sweep always loaded a save holding 11
at `0x207` - the number was never the wave's to choose.

### The live drop counter is `0x245bc` - measured

Found by scanning DGROUP for the HUD's current value and keeping only bytes that
**decrement by one**, which is the drop counter's signature and not a generic
"a byte changed" filter.

Confirmed against the game's own display rather than against itself: with the
logger reading `0x245bc = 8`, a screenshot taken in the same script showed
`8 Drops` on the HUD. Reading back a value you predicted is evidence; reading
back a value you *wrote* is not, which is the trap the `MYSTBALL` injection fell
into.

Independently corroborated by a run that predates the identification. The
parked-tube experiment recorded, and dismissed as noise:

    0x245bc:   9 ->  8 ->  7 ->  6 ->  5 -> ... -> 1

With the tube parked in one column every atom missed, so a steady countdown to
zero is exactly what the drop counter must do. The observation was right; only
the label was missing.

It sits inside the session locals, just below the tube:

| linear | field |
|---|---|
| `0x245bc` | **drops remaining**, u8 |
| `0x245d0` | tube struct |
| `0x245e7` | score, u32 |

Note this address was **inside** the window the previous hunt scanned
(`0x23f00 + 0x900`) and was not in its exclusion list. That hunt failed for a
reason worth keeping: the game was frozen for the whole of its run, so no drop
was ever lost and the transition it was watching for could not occur. Another
search that could not have found what it was looking for.

### Clearing a wave does *not* refill the drops - measured

Play reported repeatedly returning to exactly 11 - "playing the game just gets
me back to 11" - and attributed it to **clearing the level**. That would have
made the pool per-wave after all. It is wrong, and the log says so cleanly:

    [  280.5] drops 6 -> 7  (GAINED, +1)   beaker=7 score=63250
    [  292.5] drops 7 -> 8  (GAINED, +1)   beaker=6 score=66500
    [  394.3] ---- WAVE BOUNDARY (beaker 5 -> 0), drops=8, score=67000 ----

The count is **8 on both sides of the boundary**, and the wave 53 briefing then
announced "8 drops allocated". Confirmed from the display, not just from memory.

The two `+1`s are what produced the "back to 11" impression: they land
*mid-wave*, twelve seconds apart, with a populated beaker - Bonus atoms, not a
wave clear. Attributing a change to the event you happened to notice is the same
error as reading fifty identical briefings as a constant; in both cases the real
cause was never the one being watched.

Settled, with each mechanism measured separately:

| event | effect | evidence |
|---|---|---|
| a miss | `drops -= 1` | `11 -> 10`, `8 -> 7 -> 6` |
| a Bonus atom | `drops += 1` | `10 -> 11`, `6 -> 7 -> 8` |
| clearing a wave | **no change** | `8` across the wave 52/53 boundary |

Marking the boundary *independently* - by the beaker emptying, rather than by
the drop count doing something interesting - is what made this readable. A
detector keyed to the change it hopes to see cannot report its absence.

### The 9 / 6 / 3 triple is the *starting* allowance - confirmed

The measured `9 / 6 / 3` constants and the 11 seen in every save looked like a
contradiction for most of this project. They are not: **9/6/3 is what a new game
seeds, and Bonus atoms raise it from there.**

The log caught a session start mid-capture:

    [  668.3] drops 11 -> 3   beaker=0 score=0
    [  725.7] drops  3 -> 2   beaker=14 score=1000

`score=0` and `beaker=0` mark a fresh session, not a miss - the logger's `MISS`
label there is wrong, and the score reset is the tell.

**That session was attract mode, not a new game** - the demo had kicked in while
the machine sat idle. So it measures what the *demo* runs at, which is a fact
about `DEMO.SCR` playback and not about starting a game. Recorded here because
3 is also the hardest difficulty's allowance and the coincidence would otherwise
invite the same wrong inference twice.

The difficulty mapping below rests on play instead - all three settings tried
directly:

| difficulty | starting drops |
|---|---|
| Tubes 101 | 9 |
| Tubes 201 | 6 |
| Tubes 301 | 3 |

So the full model, every part measured:

    new game        drops = 9 / 6 / 3   by difficulty
    a miss          drops -= 1
    a Bonus atom    drops += 1
    clearing a wave drops unchanged
    the briefing    reports the current value, it does not set it

**There is no cap at 11.** A cap was floated here after gains stopped at 11
twice; that was sampling, not a ceiling - "no further Bonus appeared" explains
it identically. Play reports starting a wave 6 save at **12**, and the field is
a whole byte in both the save and the session. The reading is withdrawn.

That also retires the last of the "drops are a wave property" family of errors.
The number was never stored per wave, never reset per wave, and never capped -
it is one counter, seeded once by difficulty and carried until the game ends.

## Measured from attract mode (demo-driven)

Attract mode replays `DEMO.SCR` through the normal game loop, so it is a real
play session with no human pacing it. Everything below came from traces taken
while the demo played itself.

### Replay is deterministic - verified

Two cold-boot runs, sampled independently:

| quantity | run A | run B |
|---|---|---|
| beaker-grid transitions, in order | 20 | 20, **all identical** |
| score sequence | `0, 1000, 2250` | `0, 1000, 2250` |
| drops sequence | identical | identical |

The atom-array snapshots overlap only 53%, which is *expected* - two
asynchronous samplers land on different frames of the same sequence. The
ordered grid and score sequences are the meaningful comparison and they match
exactly.

This was the premise that needed establishing before anything else: the header
u32 is only *inferred* to be an RNG seed, and without determinism no comparison
built on the demo means anything. It holds.

The demo runs at **3 drops** (visible on its HUD), which is what the earlier
`11 -> 3` log entry was actually measuring.

### `+0x0b` **is** the type field - the refutation was the error

Settled by predicting a *different* structure rather than by inspecting value
ranges: when an atom settles into a beaker cell, does that cell take the value
this offset held? Every one of the 28 record offsets was scored identically,
over 79 settle events pooled from three traces:

| offset | agrees with the settled cell |
|---|---|
| **`+0x0b`** | **76/79 = 96.2%** |
| `+0x0a`, `+0x0c`, `+0x0f` | 6/79 = 7.6% each |

So the original static reading off the draw site was right all along. The
observation that "appeared to refute" it - `+0x0b` only ever holding 0 or 1 -
was taken with array base `0x2419e`, **six bytes early**, landing inside the
saved-position fields. With the corrected base `0x241a4` the offset carries the
full range the beaker holds.

Consequence: writing a type to `+0x0b` *is* the way to inject an atom type, so
the technique the MYSTBALL experiments wanted is sound. Those particular runs
stay void, because they wrote at the wrong base (hitting `+0x05`), but the
approach can now be re-run correctly.

### The score ramps toward its target, it does not jump

Score changes captured during play do not land on round numbers. They pair up:

    +166 then +834   = 1000
    +41  then +209   =  250
    +166 then +1084  = 1250
    +166 then +1334  = 1500
    +332, +332, +170, +166 = 1000

`1000/6 = 166.7` and `250/6 = 41.7`, so the counter advances in roughly sixths
of the award. **The score variable itself animates** - this is not a display
effect layered over a settled value, because the trace reads the variable
directly.

Two consequences. For the port, an award should ramp rather than snap, or it
will look wrong next to the original. For measurement, **any single sample of
the score may be mid-ramp**, so chain values must be recovered by summing
consecutive deltas - reading one delta and calling it the award is a mistake
this trace would happily support.

Every recovered chain award is a multiple of **250**, and a vertical 3-chain
scored exactly 250 on two separate occasions, which matches the Instructions'
250-vertical / 1000-diagonal rule.

### Two techniques that did not work, recorded so they are not retried

**Ctrl+F11 slowdown via QMP does nothing here.** The adaptive harness measured
the state-change rate after each batch of twelve presses and it never trended
down (17.0, 17.3, 4.3, 13.3, 8.7, 16.7 ... - variance, not a trend). Two runs
were aliased before this was noticed. Slowing the guest must be done with
`cycles` in the config instead, at the cost of a proportionally slower boot.

**There is no frame counter at `0x24c2e` or `0x24dc0`.** Both came out of a scan
for strictly-increasing fields across 7 snapshots, over ~28,000 candidate
offsets - at that ratio, fields rise monotonically by chance. Checking the full
series killed both: `0x24dc0` reads 2568, 2319, 1553, 513 across one trace.

That is this project's recurring lesson pointed the other way. The standing rule
is "when a search comes back empty, suspect the search"; the same scepticism is
owed to a search that comes back **full**. The tell was available immediately -
a frame counter that disagrees with itself is not a finding, it is a filter
artefact.

### The full menu flow, captured

    Main menu  (8 items, selection wraps)
      Start Game / Continue Saved Game / Game Options / High Scores /
      Instructions / View Demo / Credits / Exit Tubes

    Start Game
      -> Game Mode      Endurance Mode / Wave Mode / Exit
      -> DIFFICULTY     Tubes 101 / Tubes 201 / Tubes 301 / Exit
      -> play

Difficulty is chosen **after** the mode, on the Start Game path - not in Game
Options, which holds only Toggle Music / Toggle Sound FX / Redefine Input
Device / Exit. Worth stating because "Game Options" is where one would look,
and the 9/6/3 drop allowance is the difficulty's main effect.

`Tubes 101 / 201 / 301` are the difficulty names in the game's own words,
confirming the reading of those strings in the binary.

### High Scores are per mode

The table is headed **"Endurance Mode High Scores"**, so each mode keeps its
own - which independently corroborates the two-bank `TUBES.SAV` layout, where
bank 0 is Endurance and bank 1 is Wave.

Ten entries, seeded with names and a clean 1000-down-to-100 ladder in steps of
100:

    Ken Heckbert 1000   Kelly Rogers 900   Glenda Moore 800   Jerry Herrin 700
    Rik Pierce    600   Doug Howell  500   Joe Siegler  400   Bob Mandel  300
    Larry Nelson  200   Adam Pedersen 100

The regular 100-point step marks these as **placeholder defaults**, not real
play. (Joe Siegler was Apogee's long-time webmaster, so the list is likely
staff.)

The screen does **not** dismiss on Enter - a probe that assumed it did carried
on pressing keys into a screen that never changed, and captured the same
blackboard three times while believing it was walking the Start Game path. Any
harness stepping through menus should verify the screen changed rather than
counting keystrokes.


## DGROUP measured live, and the DS offsets that unlock the game loop

`DS = 0x1fa9` read straight off the register while the demo was running, so
**DGROUP is linear `0x1fa90`**. This confirms the value the project had derived
statically (`L + 0x1785` with `L = 0x0824`) - it was previously asserted from
the file, and is now also observed.

That matters because it converts every address measured this session into the
form Ghidra needs. Ghidra creates no xrefs for DS-relative globals in 16-bit
segmented code, but `ghidra_scripts/FindScalarRefs.java` chases them by value:

| variable | linear | **DS offset to chase** |
|---|---|---|
| atom array | `0x241a4` | **`DS:0x4714`** |
| beaker grid | `0x24314` | **`DS:0x4884`** |
| drops remaining | `0x245bc` | **`DS:0x4b2c`** |
| tube struct | `0x245d0` | **`DS:0x4b40`** |
| score | `0x245e7` | **`DS:0x4b57`** |

This is the bridge back from observation to decompilation. Static analysis of
`1000:3a67` stalled because nothing indicated which code touched what; the code
that writes `DS:0x4884` **is** the match-and-clear routine, and the code that
writes `DS:0x4b57` **is** the scoring routine. They no longer have to be found
by reading 9,382 bytes of disassembly in order.

One sample caught `ds=0x4e93` with `cs=0x1b45`, which is the already-documented
case of a unit that sets `DS = CS` - a reminder to take DS from game code, not
from whatever happens to be executing.

### Why this is the next step, not more observation

The port's gameplay rules are currently **behavioural reconstructions**: watch
the original, write C++ that reproduces what was seen. `CLAUDE.md` rejects that
approach outright, and this session demonstrated why rather than merely
asserting it -

- a scoring rule was fitted to two data points and was simply wrong, while the
  real rule sat in the Instructions;
- the Flashium wildcard was wrong in two different ways until a player
  described the actual behaviour;
- an "ambiguity in the original" was written up that turned out to be a bug in
  the port's own loop.

Each was caught by a person, not by the method. Sampling yields points; the
binary yields the function. The traces keep their value - they are the oracle a
decompiled implementation must reproduce - but the *rules* should come from the
code.

## Gameplay state lives on the STACK, not in DGROUP

Chasing the game loop through the measured addresses failed at the first step,
and the failure is the finding. `FindScalarRefs.java` was run for the grid,
score and drops as DS offsets (`0x4884`, `0x4b57`, `0x4b2c`) and returned
**none** for all three. Per the standing rule, suspect the search - and this one
could not have succeeded.

Measured live, mid-session:

    DS = 0x1fa9   -> DGROUP linear 0x1fa90
    SS = 0x2294   -> stack   linear 0x22940

Every gameplay address measured this session is **above** `0x22940`, so it is
inside the **stack segment**, not DGROUP:

| variable | linear | as DS offset | as SS offset |
|---|---|---|---|
| atom array | `0x241a4` | `DS:0x4714` | `SS:0x1864` |
| beaker grid | `0x24314` | `DS:0x4884` | `SS:0x19d4` |
| drops | `0x245bc` | `DS:0x4b2c` | `SS:0x1c7c` |
| tube struct | `0x245d0` | `DS:0x4b40` | `SS:0x1c90` |
| score | `0x245e7` | `DS:0x4b57` | `SS:0x1ca7` |

`SP` sampled at `0x1622..0x1628` and `BP` up to `0x19f2`, so these sit inside
live frames. The grid is the clincher: it is 30 bytes at `SS:0x19d4`, and one
sampled `BP` is `0x19f2` - exactly `0x1e` higher. **The grid is `[BP-0x1e]`.**

Drops, tube and score sit *above* that `BP`, i.e. in the **enclosing** frame -
which is exactly what the notes already said about the structure: `1000:3a67`
is a nested Pascal procedure sharing `1000:9e53`'s locals, reached by static
link. The addresses now confirm it from the data side.

**Consequences for the logic transfer:**

- Searching DGROUP offsets for gameplay state is futile; the values to chase are
  **BP-relative displacements**, and they are negative. A past session already
  lost 38 stack mutations to a regex that only matched positive displacements.
- The measured *linear* addresses are only valid for one run's frame layout.
  They are fine for the live rig, and useless as identifiers in the binary.
- Sprite/table lookups are a different matter: `1000:3a67` references
  `0x1da8` (ball table), `0x1df2`/`0x1df4` (fade table) and `0x2376` (page
  index) as **absolute DS addresses**, matching the globals documented here.
  So the split is: presentation tables in DGROUP, gameplay state on the stack.

The decompilation of `1000:3a67` + `1000:9e53` is kept **outside this repo**, at
`~/Dev/tubes-tooling/decomp-3a67-9e53.txt`, for the same reason the Ghidra
project is: it is derived from copyrighted data.

## `1000:3a67` decompiled: the beaker grid and atom array, from the code

First gameplay structures recovered from the **code** rather than from watching
memory. Both were already known from observation; what is new is that their
shape is now read off the program, which is what settles details sampling
cannot reach.

### The beaker grid is a Turbo Pascal `array[1..5, 1..6] of byte`

    FUN_2685_0fbf(0, 0x1e, abStack_27 + 7, unaff_SS);      // FillChar(grid, 30, 0)

`0x1e` = 30 bytes, cleared at session start. The draw loop indexes it as

    abStack_27[row * 6 + col]        col unrolled 1..6

and the `+ 7` bias on the base is the giveaway: a 1-based two-dimensional Pascal
array is addressed as `base + (row*6 + col)` with `base` biased by
`-(1*6 + 1) = -7`. So the declaration is **`array[1..5, 1..6] of byte`**,
row-major, one byte per cell - confirming the 6 x 5 read off the loop bounds
earlier, and confirming which index is which.

Each cell value is used directly as a **ball-table index**:

    FUN_2321_0905(*(u16 *)(cell * 4 + 0x1da6),      // sprite ptr, low word
                  *(u16 *)(cell * 4 + 0x1da8),      // sprite ptr, high word
                  x, y);                            // Draw

which is the `DS:0x1da6` ball table with 4-byte entries, already documented -
and it independently confirms that a settled cell holds the atom's **type
number**, the fact the `+0x0b` settle correlation established by measurement.

### There are THREE parallel 30-byte arrays, not one

All three are cleared together at session start:

| array | role |
|---|---|
| `abStack_27 + 7` | the beaker contents - atom type per cell |
| `acStack_62 + 6` | per-cell flag, tested `== 1` in the draw loop |
| `local_3e` | third 30-byte array, purpose not yet established |

The port models the beaker as a single array of types. The original keeps at
least one parallel plane, tested per cell while drawing, which is what a
clearing/fading flag would look like. **This is exactly the kind of structure
black-box observation cannot see**: it never shows up in a state trace of the
grid, because it is a different array.

### Atom array: 12 records of 28 bytes, in this frame

    local_1b6[1] != 0xc                        // loop bound: 12 records
    FUN_2685_0f9b(0x1c, &local_1c8 + n * 7, ...)   // 28-byte record copy

`&local_1c8 + n * 7` on an `undefined4` base is `n * 28` bytes, so **the array
base is `local_1c8`**; Ghidra also exposes overlapping views of it
(`local_1c4`, `local_1b0`) at fixed field offsets. Initialisation writes `0xba`
= **186** to fields `+0` and `+2`, matching the spawn-marker pair and the
x/saved-x/y/saved-y record shape found when the array base was corrected.

Note `local_18e` is **not** the atom array - it is a 2-byte drawing coordinate.
It was briefly taken for the array because its number matched an offset
prediction; Ghidra's `local_N` naming does not map onto BP displacement that
way, and offset arithmetic is not an identification. Shapes are: a 28-byte
stride, a bound of 12, a 30-byte FillChar.

### Already visible: a colour-cycling rule the port does not have

    if (cell != 0 && cell < 8) {
        cell = cell + 1;
        if (cell == 8) cell = 1;
    }

Every ordinary atom in the beaker steps to the next colour, wrapping 7 -> 1.
This is very likely the wave modifier described in the briefings as beaker atoms
morphing on a timer. Nothing in any state trace suggested it, because no sampled
wave exercised it.

## The third plane, and what a beaker cell really holds

The three 30-byte planes are contiguous in the frame, so in Pascal they are
three consecutive `array[1..5, 1..6] of byte` declarations:

    acStack_62 + 6   -0x5c .. -0x3f     plane C   (purpose still unknown)
    local_3e         -0x3e .. -0x21     plane B   (animating flag)
    abStack_27 + 7   -0x20 .. -0x03     plane A   (the beaker cell values)

Live, plane A is at `0x24314`, so B is at `0x242f6` and C at `0x242d8`.

### A cell holds `type + 19 * fadeFrame`, not a type

Sampled fast enough to catch a clear in progress, plane B holds **1** at exactly
the cell where plane A holds a large value that climbs by **19 every frame**:

    A: ... 59 ... 78 ... 97 ... 116 ... 135      (+19 each)
    B: ...  1 ...  1 ...  1 ...   1 ...   1

19 is the number of entries in the ball table. So a cell being cleared does not
hold an atom type at all - it holds **`type + 19 * frame`**, and plane B marks
it as animating. `59 = 2 + 19*3`, `78 = 2 + 19*4`: Greenium, fade frames 3 and 4.

This closes a loop with something derived separately and long ago. The fade
table was recorded here as `DS:0x1df6 + 76*(frame-1) + 4*(type-1)`. Expand it:

    0x1df6 + 76*(frame-1) + 4*(type-1)  ==  0x1da6 + 4*(type + 19*frame)

which is the **ball table base** with the same `value * 4 + 0x1da6` indexing the
draw loop already uses. The fade frames are simply consecutive 19-entry blocks
following the ball table, so **one lookup draws both a settled atom and a fading
one** - the drawing code never branches on whether a cell is animating. Two
independent derivations, arrived at years apart in this project, agree exactly.

### Why this matters for the port

`src/board.h` models a cell as an atom type. That is **wrong at the
representation level**, not merely incomplete:

- a clearing cell holds an out-of-range value that is a table index, so any code
  treating a cell as a type must exclude cells flagged in plane B;
- the port has no second plane at all, so it has nowhere to record that a cell
  is mid-animation;
- and the clear animation is not a separate effects system - it *is* the cell
  value walking the table.

None of this is reachable by watching the grid: a state trace shows a cell going
`2 -> 0` and nothing else. The intermediate values only appear if sampled inside
the animation, and even then they look like corruption unless the table layout
is known.

**Plane C remains unidentified.** It is read in the draw loop as `== 1`, so it is
used, but it stayed zero across every sample taken so far - including a fast
poll. Both failures to observe it are sampling limits, not evidence of disuse:
the decompiled draw loop tests it, which settles that it does something.

## Plane C is an overlay marker, and the tube layout is selected by `DS:0x1d4e`

### Plane C: a per-cell overlay sprite

The draw loop reads plane C as:

    if (*(char *)0x1d4e == 6) {
        if (acStack_62[row*6 + col] == 1)
            Draw(<sprite ptr from the enclosing frame, -0x16e/-0x16c>,
                 col + 1, <row y>);
    }

So plane C flags cells that get an **extra sprite drawn over them**, and only
when the global at `DS:0x1d4e` holds 6. That matches the **red X marks** seen
sitting on top of beaker balls in a wave-50 screenshot: an overlay plane, not a
different atom type - which is why the type grid never showed anything unusual
for those cells.

It also explains why plane C read zero in every sample: those runs were not in
whatever mode `0x1d4e == 6` denotes.

### `DS:0x1d4e` selects the tube network layout

The same global gates large blocks of furniture drawing, taking at least the
values `0, 1, 4, 5, 6`:

    if (*(char *)0x1d4e == 4) { ... }
    if (*(char *)0x1d4e == 6) { ... }
    if (*(char *)0x1d4e == 5) { ... }
    if ((*(char *)0x1d4e != 1) && (*(char *)0x1d4e != 0)) { ... }

Inside those blocks the tube network is drawn from **literal coordinates**, one
call per segment, each with a different segment sprite taken from the enclosing
frame's locals:

    Draw(<seg sprite -0x90>, 0x1a, 0x22)     // y = 26, x = 34
    Draw(<seg sprite -0x90>, 0x1a, 0x10e)    // y = 26, x = 270
    Draw(<seg sprite -0x90>, 0x1a, 0x3a)     // y = 26, x = 58
    Draw(<seg sprite -0x90>, 0x1a, 0xf6)     // y = 26, x = 246
    Draw(<seg sprite -0x94>, 0x0d, 0x3a)     // y = 13, x = 58
    Draw(<seg sprite -0x94>, 0x0d, 0xf6)     // y = 13, x = 246

Those are exactly the furniture positions recorded earlier from literal draw
coordinates (`y=13: 58, 107, 197, 246` and `y=26: 34, 58, 107, 125, 179, 197,
246, 270`), and exactly the x values atoms were observed to dwell on.

**Two consequences.**

The tube network is **not one static backdrop**. It is assembled per layout from
individual segment sprites, and `0x1d4e` chooses the layout - so the arc an atom
travels differs between layouts, which is why a single hard-coded path cannot be
right. The port currently invents a three-leg route (rise, cross, descend) with
entry columns picked at random from observed dwell points; the original walks
whatever network the current layout drew.

And the port's tube rendering is wrong for the same reason: it does not compose
the network from segments at all. Getting either right means porting the layout
selection and the per-segment draw list, not tuning coordinates.

### The furniture segment list (positions solid, layout attribution NOT yet)

Extracted draw calls, sprite variable plus coordinates:

    pass 1   seg[-0x92]  y=26  x = 34, 270, 58, 246, 107, 197, 179
             seg[-0x9a]  y=26  x = 125
             seg[-0x96]  y=13  x = 58, 246, 107, 197
    pass 2   seg[-0xb2]  y=26  x = 34, 179
             seg[-0xaa]  y=26  x = 270, 125

Two things follow. The network is drawn in **multiple passes over the same x
positions with different segment sprites**, which is what a layered tube would
need - and the port draws none of it. And `x = 125` takes a *different* sprite
from its neighbours in the same pass, so the segments are not interchangeable.

Layout conditions on `DS:0x1d4e` sit at four places in `1000:3a67`, testing
`== 4`, `== 6`, `== 5` and `!= 1`.

**Which draws belong to which layout is not established.** The extraction above
groups calls by line range between successive conditions, which is wrong: a
block can close early and leave following draws unconditional, and the `== 5`
range swallowed 47 calls that are unlikely to all be inside it. Grouping needs
brace-aware parsing of the decompiler output, or reading each block directly.

Recording it this way on purpose. The coordinates are evidence; the grouping is
an artefact of how they were gathered, and a table of "layout 5 draws these
segments" would look equally authoritative while being unverified.

### RETRACTED: `DS:0x1d4e` does not select a tube layout

Grouping the draw calls by **brace depth** rather than by line range
(`~/Dev/tubes-tooling/group_layouts.py`) reverses the earlier reading:

| group | draws |
|---|---|
| guarded by `0x1d4e == 6` | **12**, all using sprite `-0x16e` |
| **no `0x1d4e` guard at all** | **114**, including every tube segment |

The tube network is drawn **unconditionally**. It is one fixed layout, built up
in roughly five layered passes over the same x positions with different segment
sprites:

    y=26  x = 34, 270, 58, 246, 107, 197, 179   seg[-0x92]   (125 -> seg[-0x9a])
    y=13  x = 58, 246, 107, 197                 seg[-0x96]
    y=26  x = 34, 179 / 270, 125                seg[-0xb2] / seg[-0xaa]
    y=13  x = 58, 246 / 107, 197                seg[-0x92] / seg[-0x9a]
    y=26  x = 34, 179 / 270, 125                seg[-0xae] / seg[-0xa6]
    ... and further passes with seg[-0xa2], seg[-0x9e]

`-0x16e` is the same sprite pointer the plane C overlay draw uses, so
**`0x1d4e == 6` gates the overlay, not the network**.

The earlier claim that `0x1d4e` selects the tube layout came from grouping by
line range between successive conditions, which attributes shared code to
whichever layout was tested last. It was flagged as unverified when written,
and it was wrong. Worth noting the direction of the error: it implied the port
needed per-layout networks and layout-dependent atom routing, which is
substantially more work than the truth - one fixed network, drawn in layers.

**Caveat on the coordinates.** The extractor takes the last two numeric tokens
of each call, which is only correct when the arguments are literals. The
furniture draws are literal and their `y`/`x` are trustworthy. The `== 6`
group's arguments are expressions (`local_1b6[0] + 1`, `*(int *)0x1e + 2`), so
its reported coordinates are fragments of those expressions and mean nothing -
the grouping is sound, the numbers in that row are not.

## The tube network draw list, with real sprite names

The furniture sprites are held in `1000:9e53`'s frame and referenced by
`1000:3a67` through the static link. `9e53` assigns each slot a **resource name
string** via `FUN_21ea_035b(dest, SS, off, 0x21ea)` - a Pascal string copy - so
the slots can be named.

### Recovering the names

The string addresses (`21ea:9c43` upwards) did not resolve through the usual
segment arithmetic, so the strings were located by **spacing fingerprint**
instead: the gaps between the 16 string addresses are known exactly
(`0, 13, 26, 39, 63, ...`), and only one position in the image holds printable
Pascal strings at every one of those spacings. That anchor is image offset
`0xbe43`, i.e. `file = 0x2200 + <ghidra offset>` for this segment.

    local_8c  BEAKER.CSP     local_a0  TUBEV.CSP      local_166 TESTUBE1.CSP
    local_90  BEAKERS.CSP    local_a4  TUBEVS.CSP     local_162 TESTUBE2.CSP
    local_94  TUBEH.CSP      local_a8  TUBEVR.CSP     local_15e TESTUBE3.CSP
    local_98  TUBEHS.CSP     local_ac  TUBEVRS.CSP    local_15a TESTUBES.CSP
    local_9c  TUBEHR.CSP     local_b0  TUBEVL.CSP     local_170 MARKER.CSP
                             local_b4  TUBEVLS.CSP

`3a67`'s `link - X` corresponds to `9e53`'s `local_(X + 2)`. That is **not**
asserted from arithmetic - Ghidra's frame bases differ by two between the
functions, so the offset alone proves nothing. It is confirmed by two
independent semantic checks:

- `-0x16e` maps to `MARKER.CSP`, and `-0x16e` is exactly the sprite the plane C
  overlay draws on flagged cells - which is what the **red X marks** on beaker
  balls are;
- `-0x8a` maps to `BEAKER.CSP`, drawn at `y=134, x=103`, and the port already
  draws the beaker at `(103, 134)` from geometry measured independently.

All twelve names are present in `TUBES.RES`, a third confirmation.

### The list (static furniture, in draw order)

    TUBEH     y= 26  x = 34, 270, 58, 246, 107, 197        TUBEHR y=26 x=125
    TUBEH     y= 26  x = 179
    TUBEHS    y= 13  x = 58, 246, 107, 197
    TUBEVLS   y= 26  x = 34, 179          TUBEVRS y= 26  x = 270, 125
    TUBEH     y= 13  x = 58, 246, 197     TUBEHR  y= 13  x = 107
    TUBEVL    y= 26  x = 34, 179          TUBEVR  y= 26  x = 270, 125
    TUBEVLS   y= 13  x = 58, 197          TUBEVRS y= 13  x = 246, 107
    TUBEVS    y= 26  x = 58, 246, 107, 197
    TUBEVL    y= 13  x = 58, 197          TUBEVR  y= 13  x = 246, 107
    TUBEV     y= 26  x = 58, 246, 107, 197
    BEAKERS   y=135  x = 186
    BEAKER    y=134  x = 103

Reading the suffixes: **`S` = shadow**, always drawn in an earlier pass than the
solid piece it sits under; **`L`/`R`** are left and right elbow variants;
`H`/`V` horizontal and vertical. The network is therefore built shadow-layer
first, then solids, over two lanes at `y=13` and `y=26` - which is why a single
composited backdrop cannot reproduce it.

Draws whose coordinates are expressions rather than literals are omitted here;
those are the moving atoms and the test tube, not furniture.

### The render order - why the tubes look hollow

The furniture is not a backdrop drawn once. Each frame `1000:3a67` interleaves
atom drawing **between** furniture passes, so later passes overpaint the atoms:

     1  TUBEH   y=26  x = 34, 270, 58, 246, 107, 197, 179   (TUBEHR at 125)
     2  TUBEHS  y=13  x = 58, 246, 107, 197
     3  TUBEVLS y=26 / TUBEVRS y=26
    ->  ATOMS
     4  TUBEH   y=13  (TUBEHR at 107)
     5  TUBEVL  y=26 / TUBEVR y=26
     6  TUBEVLS y=13 / TUBEVRS y=13
     7  TUBEVS  y=26
    ->  ATOMS
     8  TUBEVL  y=13 / TUBEVR y=13
     9  TUBEV   y=26
    10  TESTUBES                      (test tube shadow)
    ->  ATOM in the test tube
    11  BEAKERS                       (beaker shadow, y=135 x=186)
    ->  settled beaker atoms, 6 per row
    12  MARKER x6                     (the overlay on flagged cells)
    ->  ATOM
    13  BEAKER  y=134 x=103           (the glass front - drawn LAST)

So an atom travelling the network is drawn, and then the **solid** tube pieces
are drawn over it. The tubes are hollow and the atom is visibly *inside* them,
with tubes overlapping one another - a genuinely good effect for 1994, and
impossible to reproduce by compositing a single foreground image.

The same applies at the beaker: `BEAKER.CSP` is drawn **after** the settled
atoms, so the glass front overlaps the balls.

**The port currently has this inverted** - `src/main.cpp` draws the beaker
first and the atoms on top of it, and draws no tube furniture at all. Both are
now fully specified by the list above.

## MYSTBALL is a rendering state, not a ball - proven from code

Three experiments failed to settle this. It is settled now, and not by
observation: `1000:3a67` draws each travelling atom like this -

    if (hidden == 0)
        Draw(ball[type], x, y);          // DS:0x1da6 + 4*type
    else
        Draw(*(u16 *)0x1df2, ..., x, y); // the same x, y

and `DS:0x1df2` is **`MYSTBALL.CSP`**, established from the entry program's
resource table below. So a concealed atom keeps its real type in its record and
is merely *drawn* as a `?`. The flag lives in `9e53`'s frame at `-0x189`.

That is exactly the reading the wave 30 briefing implied - "atoms hidden until
they leave a tube" - and it explains every earlier failure to find it:

- **it is not a type**, so scanning the atom array for the value 19 could never
  find it, no matter how the sampler was written;
- injecting 19 into a record's type byte could not produce it either, because
  the branch is on a *separate flag*, not on the type;
- and it never appears in the beaker, which is why type 19 has no fade family -
  a concealment sprite resolves before landing and never needs a clear
  animation.

The lesson is the one the port-wide pivot was about. Three experiments were
built to detect a value that does not exist anywhere in the data, and each was
capable only of reporting its absence. The branch that decides it is two lines
of code.

## The entry program's resource table, decoded

`1000:aaba` assigns every graphics global its resource name through a repeated
`(name pointer, destination)` pair feeding `FUN_21ea_035b`. Decoding all 77
assignments gives the game's own name for each global.

**The ball table** at `DS:0x1da6 + 4 * type`:

| addr | type | resource | | addr | type | resource |
|---|---|---|---|---|---|---|
| `0x1daa` | 1 | REDBALL | | `0x1dd6` | 12 | MULTBALL |
| `0x1dae` | 2 | GRENBALL | | `0x1dda` | 13 | EVILBALL |
| `0x1db2` | 3 | BLUEBALL | | `0x1dde` | 14 | CONVBALL |
| `0x1db6` | 4 | CYANBALL | | `0x1de2` | 15 | BLOCBALL |
| `0x1dba` | 5 | PURPBALL | | `0x1de6` | 16 | FILLBALL |
| `0x1dbe` | 6 | YELWBALL | | `0x1dea` | 17 | OBSTBALL |
| `0x1dc2` | 7 | PINKBALL | | `0x1dee` | 18 | CRYSTAL |
| `0x1dc6` | 8 | **absent** | | `0x1df2` | 19 | MYSTBALL |
| `0x1dca` | 9 | ANTIBALL | | | | |
| `0x1dce` | 10 | GOLDBALL | | | | |
| `0x1dd2` | 11 | XENBALL | | | | |

This confirms the type numbering **from the code**, where it had previously
rested on a settle correlation. The gap at type 8 is Flashium, which genuinely
has no sprite of its own - previously an inference from watching it cycle
colours, now a hole in the game's own table.

Also recovered: the seven small HUD balls at `DS:0x200a` (`SRBALL`, `SGBALL`,
`SBBALL`, `SCBALL`, `SPBALL`, `SYBALL`, `SPNKBALL`), the blackboard and
cutscene art (`BLACKBRD`, `TALK1..5`, `CLAP1..3`, `JUMP1..3`, `BOOKS`,
`POINTER0..3`, `POINTERT`), the four fonts (`SCRIPT.816`, `TINY6X8.88`,
`FUTURE.816`, `STARTREK.816`), `TUBES.PAL`, `CLASS.MUS` and three `.SFX`.

## Rendering is dirty-rect over double-buffered pages

The per-atom draw is:

    FUN_2321_0874(0xd, 0x10, prevX[page], prevY[page]);   // 13 x 16 rect
    if (active) {
        Draw(ball or MYSTBALL, x, y);
        FUN_2321_0874(0xd, 0x10, x, y);
        prevY[page] = y;  prevX[page] = x;
    }

`FUN_2321_0874` takes **dimensions, not a sprite**, so it registers a 13 x 16
region rather than drawing - the classic dirty-rectangle pattern. The previous
position is stored **per page**, indexed by the page counter at `DS:0x2376`
already documented, because each of the two Mode X pages needs its own restore
list.

That is why the atom cell is 16 x 13 everywhere: it is the unit of redraw.

## Still not located: the movement code

The routing that walks an atom through the network has **not** been found. The
tube x positions (`34, 58, 107, 125, 179, 197, 246, 270`) appear in `1000:3a67`
**only inside draw calls** - never in a comparison - so the path is not a series
of hard-coded position tests in this function. Candidates not yet examined: a
waypoint table in DGROUP, or a called routine.

Record fields seen being used but not yet identified: `+0x07`, `+0x09`,
`+0x0f` (compared against 6, so plausibly a destination column), `+0x13`, and
`+0x21` used as a record index.

Until that is found, `src/game.cpp`'s three-leg route stays a placeholder, and
it is marked as one.

## The movement code: `1000:0f80` (solved)

`1000:3a67` calls `FUN_1000_0f80(record)` per atom. This is the router, and it
is a **state machine over a fixed-point position**, not a path.

### Record fields, from the code and confirmed live

| offset | meaning |
|---|---|
| `+0x00` u16 | **x** |
| `+0x02` u16 | **y** |
| `+0x04` u16 | **anchor x** - the column base the arc offsets are measured from |
| `+0x06` u16 | **target y** |
| `+0x08` u8 | **state** |
| `+0x09` u16 | **velocity**, in 1/128 px per frame (unaligned) |
| `+0x0b` u8 | type |
| `+0x0c` u8 | **destination column, 1..6** |
| `+0x10` u16 | x sub-pixel accumulator |
| `+0x12` u16 | y sub-pixel accumulator |

Live sample: `x=246 y=175 anchorX=246 targetY=0 state=3 col=4 vel=512 type=2`.
**`512 / 128 = 4`** - the 4 px/frame network speed measured by sampling is
literally this field divided by 128. The "speeds" recorded earlier are the
integer part of a fixed-point step.

### The topology is a table at `DS:0x18`

    DS:0x18 = 26
    DS:0x1a = 143   DS:0x1c = 125   DS:0x1e = 107      <- columns 1,2,3
    DS:0x20 = 197   DS:0x22 = 179   DS:0x24 = 161      <- columns 4,5,6
    DS:0x26 = 104   0x28 = 122  0x2a = 140  0x2c = 158  0x2e = 176

`*(u16 *)(0x18 + col*2)` gives an atom's destination x. This is why no tube x
value ever appears in a comparison in `1000:3a67` - the geometry is **data**,
and the earlier failed search for those constants was looking in the wrong kind
of place. The two descending triples `143,125,107 / 197,179,161` indexed
`3,2,1 / 6,5,4` are exactly what the notes recorded long ago from the sprite
table.

### The states

    3  RISE      acc_y += vel;  y -= acc_y/128;  acc_y &= 0x7f
               on y <= targetY:  y = targetY, acc = 0,
                                 state = (col < 4) ? 6 : 5
    6  GO RIGHT  acc_x += vel;  x += acc_x/128;  acc_x &= 0x7f
               anchorX = colTable[col];  on x >= anchorX: snap, state = 7
    5  GO LEFT   as 6 but x decreasing
    7  DESCEND   y += acc/128
    9              y += 9

`col < 4` chooses the turn direction: columns 1-3 sit left of their feed tube
and the atom travels **right**, columns 4-6 travel **left**.

### The arc is an offset table, not a curve

This is the part the port gets visibly wrong. While within 9 px of the corner,
the *other* axis is displaced by a small table, which rounds the turn:

    rising, near the corner (y - targetY):
        <= 1 -> x = anchorX +/- 9
        <= 2 -> x = anchorX +/- 6
        <= 4 -> x = anchorX +/- 4
        <= 6 -> x = anchorX +/- 2
        else -> x = anchorX +/- 1

    travelling horizontally, near the corner (anchorX - x):
        <= 1 -> y = targetY + 9
        <= 2 -> y = targetY + 6
        <= 4 -> y = targetY + 3
        <= 6 -> y = targetY + 2
        else -> y = targetY + 1

Sign follows the direction of travel. Note the two tables are **not** the same -
`{9,6,4,2,1}` rising versus `{9,6,3,2,1}` horizontally - so this is hand-tuned
pixel art, not a computed curve, and it must be transliterated rather than
approximated.

The port's three-leg route (rise, cross at y=0, descend) is wrong in every
respect: it has no fixed-point step, no column table, no turn direction rule,
and square corners.

### Two numbering schemes for the pipes - do not mix them

A running source of confusion, worth stating plainly.

**By mouth position** (what you see, left to right at the beaker):

    top:   1 2 3 | 4 5 6        x = 107 125 143 | 161 179 197
    base:  3 2 1 | 6 5 4        x =  10  34  58 | 246 270 294

**By the game's internal column index**, which is the `DS:0x18` table order:

    index:  1    2    3    4    5    6
    dest x: 143  125  107  197  179  161

So the game's index 3 is the *leftmost* mouth, and its index 1 is the third
from the left. Confirmed from play: "at the top it is 123|456 and at the base
321|654 - the 3,1 and 4,6 switch positions".

**The pipes cross.** The tube entering furthest out comes out furthest in:

| enters at | comes out at | lane |
|---|---|---|
| x=10 (outermost left) | x=143 (innermost left) | 26 |
| x=34 | x=125 | 13 |
| x=58 (innermost left) | x=107 (outermost left) | 0 |

and mirrored on the right. The **longer the horizontal run, the lower the
lane** - x=10 to x=143 travels 133 px on lane 26, x=58 to x=107 travels 49 px
on lane 0. That is why the arches nest as drawn: the widest arc is furthest
back, and the draw order follows.

This also retro-explains the "two descending triples `143,125,107,197,179,161`
indexed `3,2,1,6,5,4`" that was recorded from the sprite table long before the
routing was understood. It was the crossing, seen from the table end.

## WITHDRAWN: "the tube network IS a backdrop"

Earlier notes stated flatly that "the tube network is not a backdrop - it is
assembled from segment sprites in layered passes". That is **wrong**, and it
was wrong in a way that produced visibly bad output.

`GAMEFG.GFX` is the entire network, already composited with the correct depth:
every pipe's near and far wall interleaved with its neighbours'. That is what
makes the frontmost pipe's **back** line sit behind the other pipes while its
**front** line sits in front of them - a per-line depth relationship that no
sequence of whole-sprite draws can produce.

So the furniture passes decompiled out of `1000:3a67` are **dirty-rectangle
restoration**, not the primary drawing. The original never blits the whole
foreground; it repaints the pieces around moving atoms, which is why those
draws exist at all and why they are interleaved with the atom draws.

The error was reasonable and still wrong: the draw list is real, the ordering is
real, and atoms genuinely are drawn between the passes. What did not follow is
that the passes *build* the network.

**Consequence for a full-blit renderer.** Blitting `GAMEFG` already restores
everything, so replaying the passes draws every pipe a second time in a
flattened order and destroys the layering that `GAMEFG` encodes. The port did
exactly this and the network came out chunky with pipes overlapping wrongly.
Removing the passes and keeping only the per-atom overlay reproduces the
original's look.

The per-atom overlay is still required, and is the same mechanism: after drawing
an atom, repaint the pipe over it. In the original that is part of the restore;
in a full-blit renderer it is the only part that is still needed, because the
atom is the only thing that damaged the foreground.

`kFurniture` in `src/main.cpp` is retained as the record of the original's
restore order, not as the drawing path.

### ...and this section is itself withdrawn

Removing the passes left **the arcs without their vertical walls**, reported
immediately from play as missing tiles and confirmed against a capture: the
original has vertical tube walls running through the lane rows, `GAMEFG` alone
does not, and only the passes supply them.

So `GAMEFG` carries *much* of the network but not all of it, and both are
needed - which is the position the notes held before this section was written.

What went wrong is worth keeping. The "chunky, wrongly overlapped" reading that
motivated removing the passes came from comparing a with/without pair at a crop
and scale where the difference that mattered - the missing verticals - was not
visible, and a difference that did not matter looked decisive. A three-way
comparison against the original at a tight crop settled it in one image, and
should have been the first move rather than the third.

The genuine finding underneath survives: `GAMEFG` **does** encode per-line depth
that whole-sprite draws cannot reproduce, which is why the pipes' front and back
lines interleave correctly there. That is what the passes are drawing *over*,
and the open question - reported from play and still unresolved - is whether
replaying them in the transcribed order disturbs that interleaving.

---

## The frame render, read out of the disassembly rather than the decompiler

The C output of `1000:3a67` cannot be trusted for structure. It is a **nested
Pascal procedure** reached through a static link in `[BP+4]`, and Ghidra folds
its own frame and the parent's into one set of `local_XXX` names. Two different
bases print as the same expression, which is how a previous session concluded
that one 28-byte array was both the atom pool and the test tube's contents.

The listing has no such ambiguity, and `ghidra_scripts/DisasmRange.java` dumps
it:

    [BP - n]        1000:3a67's own frame
    SS:[DI - n]     1000:9e53's frame, DI loaded from [BP+4]

Every claim below is from the listing.

### The two graphics primitives that decide how everything layers

Three routines in the graphics unit, all writing to the video segment held at
`DS:0x235e`:

| routine | what it does |
|---|---|
| `2321:0792` | **opaque** full-screen blit - four `MOVSW.REP` of 0x1f40 words, one per Mode X plane |
| `2321:0711` | **transparent** full-screen blit, `OR AL,AL / JZ` skipping index 0 |
| `2321:0874` | **transparent w x h box** from the buffer whose far pointer is at `DS:0x238e` |
| `2321:0905` | `Draw(x, y, sprite)` - the compiled-sprite call, 126 sites in `3a67` |
| `2321:024d` | `Restore(mode, page, x, y, w, h)` - repaints the backdrop from a saved page |

The session setup blits the backdrop with `0792`, blits `GAMEFG.GFX` over it
with `0711`, and then stores **that same GAMEFG buffer pointer** at `DS:0x238e`.

So `2321:0874` re-stamps *GAMEFG* over a box. Not a snapshot of the composed
screen - GAMEFG alone. That one fact explains the whole look of the game and
was worth more than everything else found this session.

### Draw(x, y, sprite): the argument order

Pascal pushes left to right, and Ghidra prints the list reversed. The listing
settles it:

    PUSH 0x22            ; x = 34
    PUSH 0x1a            ; y = 26
    PUSH SS:[DI+0xff70]  ; sprite segment
    PUSH SS:[DI+0xff6e]  ; sprite offset
    CALLF 2321:0905

### .CSP sprites carry a placement offset, and it is NOT decorative

A `.CSP` is compiled code storing pixels at signed displacements from a base
pointer, so each sprite records where its pixels begin relative to that base.
Over all 108 sprites in `TUBES.RES` the minimum is `originX = 128`,
`originY = -2`, and 84 sit exactly there. **That is the shared base**, and the
excess is real placement data:

| sprite | offset | |
|---|---|---|
| `TESTUBES` | +6, +1 | the test tube's shadow |
| `TUBEVS` | +11, 0 | 1 px wide |
| `TUBEVLS` | +11, 0 | |
| `TUBEVRS` | +10, 0 | |
| `TUBEHS` | 0, +8 | 1 px tall |
| `GFADE1..6` | +0,+3 +0,+6 +2,+6 +4,+6 +7,+5 +7,+5 | |

The fade families are the proof it is not an artefact of the decoder's modulo
arithmetic: a contracting animation has to walk its origin inward to stay
centred, and `GFADE1..6` does exactly that. No other reading produces a
monotone walk.

The port had this documented as "useful provenance but meaningless as a
placement offset" and dropped it. Restoring it is worth 6 to 11 pixels on the
test tube's shadow and on all three thin vertical pieces - which is what the
plan had been calling "the missing vertical pieces in the arcs".

### The atom array is `array[1..12]`, indexed BY COLUMN, in 3a67's own frame

Base `[BP-0x1c6]`, stride 28, so record *i* is at `[BP - 0x1c6 + i*0x1c]`.

    +0x00 x          +0x02 y        +0x04 anchor x   +0x06 target y
    +0x08 state      +0x09 velocity +0x0b type       +0x0c column
    +0x10 / +0x12    the fixed-point accumulators
    +0x14 / +0x16    saved x, one per video page
    +0x18 / +0x1a    saved y, one per video page

Records **1..6 are the six atoms travelling the network, one per column**, and
records **7..12 are the atoms tipped out of the test tube**, falling into the
beaker. The split is not inferred - the spawn scans 1..6 and the tip scans
7..12, and the two are drawn in different places in the frame.

The spawn, at `1000:4918`, is decisive about the indexing:

    PUSH 0x6 / CALLF Random / INC AX      ; col := Random(6) + 1
    IMUL DI,AX,0x1c
    CMP byte ptr [BP+DI+0xfe42],0x0       ; atom[col].state = 0 ?

`[BP-0x1be + col*0x1c]` is record `col`'s `+0x08`. **The array index IS the
column.**

### The six draw slots, and why they pair off 1/6, 2/5, 3/4

The frame interleaves the atoms with the furniture at six hard-coded sites,
each bound to one slot:

    atom 1, atom 6      pass 1  (16 draws)
    atom 2, atom 5      pass 2  (16 draws)
    atom 3, atom 4      pass 3  ( 8 draws)

That is the network's mirror symmetry. Columns 1 and 6 are fed by the
outermost tubes at x = 10 and 294, which take the lowest lane and cross
furthest, so they are the deepest layer; 2/5 and 3/4 nest inside them.

Each site is the same three steps:

    Stamp(GAMEFG, savedX, savedY, 16, 13)     ; erase the old box
    if state > 2 then
        Draw(x, y, ball[type])                 ; or MYSTBALL if hidden
        Stamp(GAMEFG, x, y, 16, 13)            ; clip to the pipe

`state > 2` is the draw test at all six sites and at the 7..12 loop.

**The second stamp is what makes an atom look like it is inside glass**, and it
is selective in a way no guessed overlay reproduces. GAMEFG is solid along the
long horizontal runs between the arcs, so an atom crossing lane 13 near x=217
is clipped by them; GAMEFG is *transparent* inside the feed tubes, so the same
atom rising at x=246 is not clipped by the horizontals at all - the walls there
come from the furniture sprites, drawn before the atom. Both behaviours were
measured against the original and both are reproduced.

### The full per-frame draw order

    restore backdrop over each moving object's saved box
    atom 1, atom 6
    furniture pass 1                (16 draws)
    atom 2, atom 5
    furniture pass 2                (16 draws)
    atom 3, atom 4
    furniture pass 3                ( 8 draws)
    test tube, sprite[4], if phase = 1        at (tube.x, tube.y)
    the atoms held in the tube, records 1..count of the TUBE's own array
    the HUD
    test tube, sprite[phase]                  at (tube.x, tube.y)
    BEAKERS.CSP                               at (186, 135)
    the beaker grid, 5 rows x 6 columns
    MARKER.CSP over flagged cells             if DS:0x1d4e = 6
    atoms 7..12, the ones falling into the beaker
    BEAKER.CSP                                at (103, 134)

The test tube is drawn **twice**, from two entries of a four-entry sprite table
at `tube + 2 + phase*4`, with its contents between them - so the atoms it holds
sit between two layers of glass.

### The test tube's record and its state machine

Base `parent - 0x16a`; its contents array is a field of it at `parent - 0x163`,
which is why the two are seven bytes apart.

    +0x00 x     +0x02 y (0x44 = 68)     +0x04 state     +0x05 tip phase
    +0x06 .. +0x15   four sprite pointers, indexed by phase 1..4
    +0x1e stop index 1..6    +0x1f target x
    +0x21 how many atoms it holds

The whole input block sits inside `if state = 0`, so a direction is accepted
only when parked:

    if (btn and 4) and (index > 1) then  state := 1; Dec(index); target := stop[index]
    if (btn and 8) and (index < 6) then  state := 2; Inc(index); target := stop[index]
    case state of
      1: x := x - 6;  if x <= target then begin x := target; state := 0 end
      2: x := x + 6;  if x >= target then begin x := target; state := 0 end
      3: the four-phase tipping animation

### The four geometry tables, read out of DGROUP

At `2785:0000`, four consecutive six-word tables, all indexed by column 1..6:

    DS:0x00  feed x     10  34  58 246 270 294
    DS:0x0c  lane y     26  13   0   0  13  26
    DS:0x18  column x  143 125 107 197 179 161
    DS:0x24  tube x    104 122 140 158 176 194

Column 1's feed x was carried as "inferred from the mirror symmetry" because no
atom used that column while sampling. It is measured now, and it was right.

### Difficulty seeds three numbers, not one

`1000:a483`, on `DS:0x1d4f`:

| Tubes | drops `0x1d51` | velocity `0x1d54` | spawn period `0x1d56` |
|---|---|---|---|
| 101 | 9 | 0x100 = 2 px/frame | 70 frames |
| 201 | 6 | 0x180 = 3 px/frame | 60 frames |
| 301 | 3 | 0x200 = 4 px/frame | 50 frames |

Per wave the period drops by one; every fifteenth wave the velocity gains
`0x20` and the period gains twelve. Every twentieth, four more counters step.

This retires `kSpawnIntervalFrames`, which was an outright invention, and
corrects the atom velocity - the 512 a live record once read was a late wave on
the easiest setting, not the base value.

### Velocity has exactly three writers

Searched over the whole procedure, so this is a closed list:

* the spawn, `= the difficulty's value`;
* the Down/B boost, `= 0x480` (nine pixels a frame);
* the tipping animation, `= 0x52` for the atoms in the tube.

**There is no assignment when an atom starts descending.** An atom comes down at
exactly the speed it crossed the top at. The port's "descending sets 18
px/frame" was inferred and is withdrawn.

The boost dispatches on the tube's stop index through a chain of comparisons -
1 to slot 3, 3 to slot 1, 4 to slot 6, 6 to slot 4, everything else to the
same-numbered slot. That permutation is just "the slot whose destination x is
where the tube is", and it confirms the Instructions' "atoms directly above the
test tube".

**Where the write sits matters, and the port had both halves wrong.** At
`1000:4534` it is:

* **inside** `if tube.state = 0`, so a tube that is sliding or tipping grants no
  boost at all - the port ran it unconditionally, every frame;
* **before** the Left/Right handler at `1000:4583` updates `tube.stop`, so on
  the frame a direction is pressed the boost still goes to the slot the tube is
  *leaving* - the port moved the tube first and boosted after.

The velocity field is reloaded by the router at the end of every frame, so the
boost lasts exactly one frame and has to be held. The recorded demo holds Down
for 150 of its first 250 inputs, which is what turns a 4 px/frame traverse into
a 9 px/frame one - and it is why the original frees an atom's record inside a
single 50-frame dispense period where the port was taking 95 frames.

### The dispensed type: one roll picks a class, three classes re-roll

From `1000:49fd`:

    type := Random(11) + 1
    if type = 10 then   { Bonus }
        if Random(100)+1 < DS:0x1d4a then 10 else Random(8)+1
    if type =  9 then   { AntiMatter }
        if Random(100)+1 < DS:0x1d49 then  9 else Random(8)+1
    if type = 11 then   { the specials share one eleventh between them }
        case Random(100)+1 of
             0..29: 11 Xenon        30..59: 12 Multiplier
            60..74: 16 Filler       75..89: 14 Convertor
            90..94: 13 EvilMult     95..100: 15 Blocker

`DS:0x1d49` and `DS:0x1d4a` are written to 50 and 25 by the session setup.

Two things here were invisible to observation. Slot 11 is not "Xenon" - it is
the whole special family sharing one eleventh of the roll. And the failed-roll
fallback is `Random(8)+1`, which **includes 8**: Flashium is dispensed as an
ordinary member of the pool and has no rate of its own.

### The rig's atom array base was one record late

`capture_frame.py` read the array at `0x241A4`. The emitted index then ran
exactly two behind each record's own column field, on every sample - and the
spawn indexes the array by the column, so the two have to agree. The first
element is at **`0x24188`**; `0x241A4` is `array[2]`.

Caught by the port keying its draw slot off the emitted index. Nothing in the
old harness could have noticed, because it never compared the two.

---

## The beaker: three planes, four matchers, and the fade - all from code

`1000:22a6` is the per-frame beaker update, called unconditionally from the
frame loop at `1000:47d0`. It is four match scans, then the fade, then gravity.
Everything below is from the listing, and the encoding was then checked against
the running game (see the end of this section).

### The three planes

Contiguous 30-byte arrays in `1000:3a67`'s frame, `array[1..5, 1..6] of byte`:

| base | live address | plane |
|---|---|---|
| `[BP-0x25]` | `0x24314` | **cells** - `type + 19 * fadeFrame` |
| `[BP-0x43]` | `0x242f6` | **marked** - 1 while the cell is clearing |
| `[BP-0x61]` | `0x242d8` | **objective** - 1 = a wave target, drawn `MARKER` |

Indexed `base + row*6 + col`, row 1..5 and col 1..6.

These addresses cross-check last session's correction to the atom array base.
`cells[1,1]` at `0x24314` puts 3a67's `BP` at `0x24332`, and atom record 1 is
`BP - 0x1aa` = **`0x24188`** - the corrected base, not the `0x241a4` that was
carried for two sessions. Two independent routes to the same number.

### The four matchers

`22a6` calls four nested procedures over four different ranges, and the ranges
alone identify them: they are exactly the seed positions where a run of three
fits, so no matcher ever tests an out-of-range cell.

| routine | rows | cols | direction | award | orientation code |
|---|---|---|---|---|---|
| `1000:1e90` | 1..3 | 1..4 | diagonal down-right | 1000 | 0 |
| `1000:209b` | 1..3 | 3..6 | diagonal down-left | 1000 | 0 |
| `1000:1c9f` | 1..5 | 1..4 | horizontal | 500 | 1 |
| `1000:1aae` | 1..3 | 1..6 | vertical | 250 | 2 |

The awards are `ADD [pending], 0xfa / 0x1f4 / 0x3e8` - which **confirms the
Detailed Instructions from code**, the first time that rule has had a source
better than the manual.

The body, from `1000:1aae`, with the other three differing only in `(dc, dr)`:

    matchType := cells[r, c]
    if (matchType < 1) or (matchType > 8) then exit
    if matchType = disabledElement then exit
    for i := 1 to 2 do
        other := cells[r + i*dr, c + i*dc]
        if other > 8 then exit
        if (matchType = 8) and (other <> 0) then matchType := other
        if matchType = disabledElement then exit
        if (matchType <> other) and (other <> 8) then exit
    for i := 0 to 2 do marked[r + i*dr, c + i*dc] := 1

Four things fall out of this that were previously guessed or wrong:

* **The wildcard ADOPTS.** A Flashium seed becomes whatever the next non-empty
  cell is, and the run is that colour from then on. That is why one Flashium
  can complete runs of two different colours in two different directions, which
  the port had reconstructed correctly but for the wrong reason.
* **A fading cell cannot re-match, and nothing tests for it.** A cell mid-fade
  holds `type + 19*frame`, which is above 8, so both the seed test and the
  `other > 8` bail-out exclude it automatically. The encoding does the work.
* **`disabledElement`** is `9e53`'s `[BP-0x18a]`, compared against every
  candidate type - the wave modifier "an element that still spawns but cannot
  be cleared", found rather than inferred.
* **The award is per SEED, not per run.** A run of four has two seed positions
  and pays twice; a run of five pays three times. "4 atom molecules count as 2
  chains" in the Instructions is a literal description of the scan.

Each matcher also increments one of three chain counters, but only when the
cell one step *back* does not continue the run - so a run of four counts as one
distinct run however many seeds it has. Both diagonals share a counter.

### The chain bonus multiplier, and the ramp

`1000:2410`, plus the flush at `1000:58c5`:

    if rampSteps > 0 then begin
        multiplier := multiplier + (chainsDiag + chainsHoriz + chainsVert);
        if increment = 0 then
            increment := (pending * multiplier) div rampSteps;
        total := total + increment;
    end

`rampSteps` is set to 6 by any matcher, and `1000:58c5` counts it down one a
frame; when it hits zero the remainder is flushed and `pending` and
`multiplier` are cleared. So the money is **`pending * multiplier`**, paid over
six frames, and the multiplier is the number of distinct runs formed at once.

That is the "chain bonus point multiplier" the Instructions mention without
quantifying, and it had been left unimplemented for want of a number.

**One conflict, recorded rather than resolved.** An earlier live measurement had
a diagonal run of four paying 1000; this model pays 2000 (two seeds x 1000,
multiplier 1). That measurement came from the black-box session whose
conclusions have already been overturned twice, and the code is the authority -
but it is worth a targeted check when the HUD exists to read the score off.

### The fade

`1000:2538`, for every marked cell:

    cell := cell + 19
    if cell > 152 then begin cell := 0; marked := 0 end

152 is 8 * 19, so a cell runs **eight** fade steps. Sprites exist for six - the
loader loop in `1000:9e53` ends on `CMP [BP-0x2], 0x6` - so frames 7 and 8 have
null table entries and draw nothing. That is not a bug: the cell is invisible
for two frames before it empties and the column falls.

The gravity pass moves **all three planes together**, so a cell that is
mid-fade keeps fading as it falls, and an objective marker travels with its
atom.

### Gravity is one row per frame

`1000:25b3` walks a destination cursor from row 5 and a source row from 4, both
decrementing every iteration, so the destination is always the row below the
source. Scanning bottom-up lets a whole column shift down by one in a single
pass - and only by one. That is why the beaker visibly settles rather than
snapping, and the port's previous full compaction could not have shown it.

### The fade families, and the sound

`1000:9e53` builds the sprite table at runtime, indexing it
`(frame*19 + type)*4 + 0x1da6` for types 1..10 and 18, frames 1..6. The eleven
name strings sit consecutively at `1000:9d07`:

    RFADE GFADE BFADE CFADE PFADE YFADE PNKFADE FFADE AFADE GLDFADE CRFADE

mapping to types 1..10 and 18 in order, and are followed immediately by the
same eleven with `.SFX` - one sound per family. The matcher plays
`sound[matchType]`, where `matchType` is what the run resolved to after any
wildcard adoption, which is exactly the reported behaviour: a mixed chain shows
mixed fade animations under a single sound.

### The objective plane

Only live when `DS:0x1d4e = 6`. `1000:24db`: a marked cell that also carries an
objective marker clears the marker and decrements a wave counter at
`9e53`'s `[BP-0x1f4]`. `MARKER.CSP` is drawn over flagged cells at
`(x + 2, y + 1)`.

`1000:192f`, called by every matcher with its orientation code, is the rest of
that system: in mode 2 it decrements the wave counter when the orientation
matches the wave's required one, and in mode 3 when the matched colour matches
the wave's required colour. So wave objectives are "make N vertical chains",
"make N chains of Greenium", and so on.

### Checked against the running game

`exp17_fade_encoding.py` writes three Redium into the bottom row of the
original's cells plane and samples it. Observed: **1, 39, 77, 96, 134, 0** -
every value exactly `1 + 19k`, and the run ends at 134 (`134 + 19 = 153 > 152`)
before emptying. Eight steps, as the code says.

The sampling is coarser than the game frame, so intermediate values were
skipped; and the marked plane read 0 alongside `cells = 134`, which is a torn
read rather than a finding - the two planes are fetched in separate GDB
requests while the game runs. The stride, the step count and the terminal
clear are what this confirms.

The objective plane read all zeros throughout, consistent with its being gated
on a wave mode that Endurance never enters.

### Still unread in the beaker update

`1000:2790` onward post-processes settled specials through a scan helper at
`1000:0c82` - "find a cell of type N" - converting types in place. That is the
specials' behaviour and it is the next block worth taking.

### The router reloads velocity every frame - and that is the whole speed model

The last thing `1000:0f80` does to every atom, at `1000:1906`:

    if record.type = 10 then record.velocity := 0x480
                       else record.velocity := session.baseVelocity

Two consequences, both reported from play before they were found in code:

* **The Down/B boost has to be held.** `1000:3a67` writes `0x480` into one
  atom's velocity in the tube-input block, which runs *before* the router loop.
  The router then reloads it at the end of the same frame, so the boost is
  worth exactly one frame of movement.
* **`GOLDBALL` is fast by type**, unconditionally, which is why the Bonus atom
  visibly outruns everything else. There is a one-frame lag: a newly spawned
  Bonus runs its first frame at the base speed because the reload happens at
  the end of the router.

**This overturned a claim made one session earlier**, that the velocity field
had "exactly three writers" and nothing reset it, so the boost was permanent.
The search behind that was over `1000:3a67` only; the fourth writer is here, in
a function that had never been disassembled. The search was sound and the
conclusion was still false, because a list is only closed over what was
searched. `CLAUDE.md` already warns "when a search comes back empty, suspect
the search" - this is the same failure with a search that came back *full*.

### There is no beaker-overflow loss

`1000:3a67` sets its game-over flag in exactly two places:

    1000:5d0a   the drop counter wraps past zero to 0xff
    1000:47f8   the wave's objective pattern is satisfied and the clear timer
                has run out

A full beaker is neither. Filling a column simply means nothing more can be
tipped into it. The port used to end the game the moment a column reached the
top, which froze it with no message - `update()` returns immediately once the
flag is set - and was reported as a crash.

---

## The specials, from `1000:2790`

The tail of the beaker update, running every frame after gravity. Everything
here acts on atoms that have **settled**, so the trigger is simply "a cell of
this type exists". Each routine handles at most one per frame.

    1000:0e08   AntiMatter (9)
    the four scan-and-convert loops: 10, 12, 13, 16  ->  11 Xenon
    1000:0ce7   Blocker (15)          gated on DS:0x1d48
    1000:0d56   Convertor (14)

### `1000:0c82` - find a cell of type N

Scans the 30-byte cells plane for a byte equal to `want` and turns the index
back into (row, col) by dividing by 6. Row-major, first match only, and the
comparison is on the **raw** cell - so a fading atom is never found.

### AntiMatter destroys a 3x3 block, and rewrites it to its own type

    Dec(col); Dec(row);
    colSpan := 3;  rowSpan := 3;
    if (col < 1) or (col >= 5) then Dec(colSpan);
    if (row < 1) or (row >= 4) then Dec(rowSpan);
    if col < 1 then col := 1;   if row < 1 then row := 1;
    for each cell in the block do
        if cell <> 0 then begin
            cells[row, col]  := 9;      { AntiMatter's OWN type }
            marked[row, col] := 1;
        end
    PlaySound(AFADE);  clearTimer := 10

Note it narrows the span rather than clamping both ends, so a corner blast is
2x2 rather than a 3x3 shifted inward. The AntiMatter is the centre of its own
block and therefore destroys itself.

**The rewrite to type 9 is the whole trick.** Because every caught cell becomes
AntiMatter's own type, the single `ball[cell]` lookup draws `AFADE` over all of
them and the blast animation needs no special case anywhere in the renderer.
That is also why `AFADE` is a fade family without being a "can be cleared"
marker - the notes had guessed exactly this; here is the code doing it.

The cells cannot re-trigger the blast on the next frame because the fade pass
runs first and moves them from 9 to 28.

### Blocker fills its column, upward

    if FindCell(row, col, 15) then begin
        cells[row, col] := 11;
        while row > 1 do begin Dec(row); cells[row, col] := 11 end
    end

"Fills the beaker column it lands in with Xenons", now from code rather than
from a published description. Only the cells at and *above* it.

### Convertor converts by TYPE, board-wide

    if FindCell(row, col, 14) then begin
        cells[row, col] := 11;
        if row = 5 then exit;
        victim := cells[row + 1, col];          { the ONE cell below }
        if (victim <= 0) or (victim >= 11) then exit;
        while FindCell(r2, c2, victim) do cells[r2, c2] := 11;
    end

**This is stronger than the published description.** "Turns the atoms it lands
on into Xenons" suggests a local effect; the code takes the type of the single
cell directly below and converts *every* atom of that type anywhere on the
board. The victim must be an ordinary type (1..10), so it will not chain off a
Xenon or another special.

### Bonus, Multiplier, EvilMultiplier and Filler go inert if they settle

Four identical loops convert types 10, 12, 13 and 16 to Xenon wherever they
appear. Their real effects happen when the test tube **catches** them - the
router dispatches at `1000:180c`:

| type | routine | gated on `DS:0x1d48` |
|---|---|---|
| 10 Bonus | `1000:07db` | no |
| 12 Multiplier | `1000:08d2` | yes |
| 13 EvilMultiplier | `1000:0a27` | yes |
| 16 Filler | `1000:0b55` | yes |

so anything of these types that reaches the glass is a leftover. The Crystal
(18) is deliberately not in the list - it lives in the beaker.

### `DS:0x1d48` gates the specials' effects

It gates the Blocker here and the Multiplier, EvilMultiplier and Filler at
catch time. The Bonus atom is **not** gated by it. Nothing in `1000:9e53`
writes it, so it is set further out - the wave or mode setup - and the port
defaults it on.

## The test tube's record, and the specials at catch time

### The tube carries its contents inline

The four catch-time routines all begin identically:

    DI := link^.link^;      { 1000:9e53's frame }
    DI := DI - $16A;        { @tube }

and every access after that is off that one base, which pins the whole record
down. `Move` calls give the slot stride and the slot's own start, and the byte
they index by gives the count:

| offset | field |
|---|---|
| `+0x00` | x - the drawn x, a column stop less 3 |
| `+0x02` | y - a constant `0x44` = 68 |
| `+0x05` | the tipping animation's phase; `1000:4701` acts at phase 4 |
| `+0x1e` | the stop index, 1..6 |
| `+0x21` | **count** - how many atoms are in it |
| `+0x07 + 28n` | `slot[n]`, an ordinary 28-byte AtomRec, n = 1..5 |

So `slot[n].type` is `tube + 28n + 0x12` and `slot[n].y` is `tube + 28n + 0x09`,
which is what the disassembly reads and writes throughout.

**Slot 1 is the bottom.** Each fill routine writes the slot's y offset from a
literal per index - 52, 39, 26, 13, 0 for slots 1..5, the 13 px row pitch - and
then `slot[n].y := offset + $44`. The router confirms the same relation from
the other side, comparing `tube.y + rec.dy` against the record's y as the
caught atom slides down at 9 px a frame.

### The tube is a STACK

`1000:4715`, inside the tipping animation, is unambiguous:

    if tube.count = 0 then exit;
    if tube.slot[tube.count].type = 17 then exit;      { FILLBALL }
    tube.slot[tube.count].state  := 9;
    tube.slot[tube.count].column := tube.stop;
    n := 1; while atom[n + 6] is taken do Inc(n);
    if n = 6 then RunError;
    Move(tube.slot[tube.count], atom[n + 6], 28);
    Dec(tube.count)

A catch lands in `slot[count]` and a tip takes `slot[count]`, so the tube
empties newest-first. The port had it as a queue, tipping slot 1.

This is also where the atoms of records **7..12** come from - the tipped atom
is `Move`d whole into the first free one of the six, with state 9, and falls
from there into the beaker. `n = 6` with none free is a fatal error, so the six
slots are exactly the six columns' worth of in-flight tipped atoms.

### The four catch-time routines

`1000:180c`, in the router, on the frame the caught atom reaches its slot:

    if rec.arrived <> 0 then begin
        if rec.type = 10 then Bonus;                { 1000:07db }
        if ds:[$1D48] <> 0 then begin
            if rec.type = 12 then Multiplier;       { 1000:08d2 }
            if rec.type = 13 then EvilMultiplier;   { 1000:0a27 }
            if rec.type = 16 then Filler;           { 1000:0b55 }
        end
    end

#### Bonus - `1000:07db`

    slot[count].type := 8;                          { Flashium }
    if drops = 0 then <redraw the counter>;
    Inc(drops);
    PlaySound(...);
    award := award + 1000;
    Inc(multiplier);
    pending := pending + award;
    <arm the score ramp, exactly as 1000:1c63 does>

Two things worth having. The extra drop is now from **code** - `DS`-frame
`-0x17e` is the same byte `1000:5d02` tests against `0xff` for game over -
rather than from the Instructions. And the award is a **word of its own** that
gains 1000 each time and is then paid whole. Its only other write is the zero
at `1000:3a8d`, in the session prologue, so the first Bonus of a session is
worth 1000, the second 2000, the third 3000. Nothing observable said so.

#### Multiplier - `1000:08d2`

    slot[count].type := Random(8) + 1;
    i := count;
    while i < 5 do begin
        Inc(i);
        Move(slot[count], slot[i], 28);
        slot[i].type := Random(8) + 1;
        slot[i].dy := yofs[i];  slot[i].y := slot[i].dy + $44
    end;
    count := 5

`Random(8) + 1` is 1..8, so **Flashium is in the fill distribution**. Five is a
literal, not a capacity variable.

#### Evil Multiplier - `1000:0a27`

The identical routine with the roll replaced by `11`. The `Move` copies the
caught record upward, so every slot it fills is Xenon.

#### Filler - `1000:0b55`

    for i := 5 downto 2 do begin
        Move(slot[i - 1], slot[i], 28);
        slot[i].dy := yofs[i];  slot[i].y := slot[i].dy + $44
    end;
    slot[1].type := 17                              { FILLBALL }

The count is **not** touched. The Filler is sitting at `slot[count]`, so the
shift pushes it off the top and discards it, and what remains is the same
number of atoms with an immovable one underneath them. Together with the type
17 gate in the tipping code above, that is the whole of "permanently reduces
your tube's capacity": **nothing anywhere writes a capacity variable**, and
type 17's identity as `FILLBALL` - previously "almost certainly" - is settled.

### The ramp's clock is not part of the beaker update

`1000:22a6` pays one sixth of the award (`1000:2410`); the decrement and the
flush are a **separate statement** in `1000:3a67`'s body, at `1000:58c5`. The
frame order is what makes that matter:

    1000:47d0   call 1000:22a6      { the beaker }
    1000:4819   call 1000:0f80      { the router - and so the specials }
    1000:58c5   Dec(rampSteps) ...  { the clock }

A Bonus arms the ramp at `1000:0866`, after the beaker has already run for the
frame, and `1000:58c5` then spends the first of the six steps anyway - so the
total paid is exactly `pending * multiplier`. The port had the add and the
decrement fused into one function that runs before the router, which paid a
Bonus **seven** sixths of its award. A match was unaffected, because its award
is raised inside the beaker update itself, which is why the fusion looked right
for as long as only matches paid.

`1000:1c63` also does **not** clear the increment; the only zero into it is at
`1000:58d2`, when the ramp runs out. An award landing mid-ramp therefore
extends the ramp at the rate already running.

## The tipping animation, `1000:463a`

`tube.state = 3` runs it, and A sets that state - `1000:4511`, with **no edge
detection anywhere**. The only gate is the input block sitting inside
`if tube.state = 0`, so holding A tips once every six frames, which is exactly
how long the animation takes to hand the state back.

    Inc(tube.divider);                        { +0x16 }
    if tube.divider <> 2 then exit;
    tube.divider := 0;
    Inc(tube.phase);                          { +0x05 }
    case tube.phase of
      2: begin for i := 1 to 5 do Dec(slot[i].x);
               slot[1].y := 99;  slot[2].y := 93;  slot[3].y := 87;
               slot[4].y := 81;  slot[5].y := 73 end;
      3: for i := 1 to 5 do slot[i].y := 82;
      4: for i := 1 to 5 do Inc(slot[i].x);
    end;
    if tube.phase = 4 then begin
        tube.state := 0;  tube.phase := 1;  <release>
    end

Every position is a literal per slot, not computed from an angle. Phase 2 is
the tube tilting and the contents bunching toward the middle; phase 3 is it
pouring, with all five stacked on the same pixel.

### The tube sprite is indexed by the phase

`1000:5922` draws it from a four-entry table of far pointers at `tube + 4*phase
+ 2`:

    Draw(tube.x, tube.y, tube.sprite[tube.phase])

and only three sprites exist - `TESTUBE1/2/3`, heights **65 / 42 / 27**. There
is no fourth because **phase 4 never survives to a draw**: the same body that
reaches it resets the phase to 1 before the frame is rendered. So the rendered
sequence over a tip is 1, 2, 2, 3, 3, 1.

**This retires the difficulty-capacity reading of those heights.** They were
taken for several sessions as the tube holding 5/3/2 by difficulty; the flat
capacity of 5 in the Instructions contradicted that without explaining it.
They are a tube going over.

### The slot loop is skipped while tipping

`1000:4823`, immediately after the twelve-record router loop:

    if tube.state <> 3 then
        for i := 1 to tube.count do Router(@tube.slot[i], BP);

That gate is what lets the animation own the slots' positions. Without it the
router's state 8 would put every slot back at `tube.x + 3` and slide its y
toward the resting offset on the very frame the animation moved it.

## `Random` is Turbo Pascal 7's, and the port now uses it

`2000:75bb` is the step, written with shifts and adds because the 8086 has no
32-bit multiply:

    AX := RandSeedLo;  BX := RandSeedHi;  CX := AX;
    DX:AX := AX * $8405;                  { CS:[0xda1], dumped }
    CX := CX shl 3;  CH := CH + CL;       { + lo * $0808 }
    DX := DX + CX;  DX := DX + BX;
    BX := BX shl 2;  DX := DX + BX;  DH := DH + BL;
    BX := BX shl 5;  DH := DH + BL;       { + hi * $8405 }
    AX := AX + 1;  DX := DX + carry;
    RandSeed := DX:AX

which is `RandSeed := RandSeed * $08088405 + 1`. And `Random(n)` at
`2000:755e` steps it, then multiplies the 32-bit seed by the range and keeps
the **top 32 bits of the 48-bit product** - `(RandSeed * n) shr 32`, a scaled
fraction rather than a modulus. A `mod n` from the same seed is a different
sequence, which is why the port's old xorshift could never have replayed a
recording however correct the rules were.

### The tube starts in a random column

`1000:43d6`, in the session setup:

    tube.count := 0;
    FillChar(@tube.slots, $8C, 0);
    tube.stop := Random(6) + 1;
    tube.x := stopX[tube.stop]

**This is the session's first call to the generator**, before anything is
dispensed. The port used to park the tube in the middle, which was wrong twice
over: the tube in the wrong place, and every later roll off by one call, so the
whole spawn sequence differed. Replaying `DEMO.SCR` is what exposed it.

### The demo runs at TUBES 301, and `DS:0x1d4f` is the difficulty index

`1000:a483` is the only writer of the three constants a session runs on, and it
switches on `DS:0x1d4f`:

    case DS:0x1d4f of
      0: drops := 9; velocity := $100; spawnInterval := $46   { 70 }   Tubes 101
      1: drops := 6; velocity := $180; spawnInterval := $3c   { 60 }   Tubes 201
      2: drops := 3; velocity := $200; spawnInterval := $32   { 50 }   Tubes 301
    end

and the menu's **View Demo** arm sets `[0x1d4f] := 2` at `1000:b272` before
calling `1000:9e53`. **The recording was made at Tubes 301.**

The port assumed 101, and the mistake was easy to make in a specific way worth
recording: `1000:b1ee` presets the whole difficulty block to the *101* values
once, before the menu loop, and the View Demo arm sets two neighbouring flags
(`[0x1d4e] := 0`, `[0x1d4c] := 1`) as well - so `[0x1d4f]` read as one more mode
flag, and the preset made 101 look like what the demo inherits. It is not:
`a483` rewrites the block on entry to every session.

**Confirmed live, twice over.** The running demo reads 3 at the drops counter
(`0x245bc`) before the session has made its first `Random` call, and the HUD on
a screenshot of the same moment says `3 Drops`. The difficulty block itself
reads `[0x1d51] = 3`, `[0x1d54] = 0x200`, `[0x1d56] = 0x32` twelve seconds in,
against `9 / 0x100 / 0x46` at the menu one keypress earlier.

This is not a cosmetic difference. The interval sets how often an atom is
dispensed and the velocity how fast it travels, so a replay at 101 was running a
70-frame dispense beat against a recording made on a 50-frame one.

### The first dispense is on frame ZERO

`1000:3be0`, the last thing the session setup does - immediately after the loop
that parks all twelve records at (303, 186):

    spawnTimer := 1

and the tick at `1000:490a` is `Dec(spawnTimer); if spawnTimer = 0 then
dispense`. A seed of **1** therefore fires on the very first frame, and only
then reloads the full period from `1000:4953`.

The port seeded the timer with the interval, which delayed the first atom by one
whole period and slid the entire recorded input stream out of step with the game
state for the rest of the session.

### A `.SCR` holds one byte per IDLE frame, not one per frame

The single most load-bearing thing about the replay, and it is a consequence of
where the input read sits rather than of anything in the demo format.
`1000:44f0`:

    if tube.state <> 0 then goto RunStateMachine;    { 1000:45e7 }
    if not InputAvailable then goto RunStateMachine; { CALLF [ds:$2352] }
    btn := ReadButtons;                              { CALLF [ds:$2356] }
    ...

The driver vectors are **not called at all** on a frame where the tube is
sliding or tipping. For live play that is invisible - not reading the keyboard
and reading it then ignoring it look identical. For a **replay** it is the whole
mechanism, because in demo playback those vectors are the demo reader and
calling one is what advances the recording.

So a recording is one byte per frame the tube was *idle*. A replay that steps
the stream unconditionally drifts the first time the player moves and never
recovers - the tube slides for three frames (6 px a frame over the 18 px stop
pitch, `1000:45f3`), so three frames of the recording are consumed that the
original would have held back.

**Measured, and it is exact.** The demo's first Left presses are at stream
indices 16, 17 and 20. Under the idle gate the original consumes them at three
separate idle frames and ends at stop 3; consumed one-per-frame the middle one
lands mid-slide and is lost, ending at stop 4. Modelling both against six
sampled `(atom y -> tube x)` pairs read off the running original - the atom's
own 4 px/frame rise serving as the frame clock - the idle-gated model matches
**6 of 6 exactly** and the unconditional one matches 1.

### What the four fixes were worth: the dispenser is exact

Measured on the spawn-by-spawn roll count, which is the sharpest form of the
oracle: at each dispense, how many times has `Random` been called? The original's
count is read by mapping a live `RandSeed` sample back through the LCG orbit
(see below), so it needs no frame alignment at all.

**Every one of the 35 spawns the port produces now matches the original's
exactly** - column, type and cumulative roll count. Before the fixes it diverged
at spawn 4 and lost every drop by frame 1,049.

That is a stronger statement than it sounds. The column is re-rolled up to ten
times looking for a free record, so a matching roll count at spawn N means the
network occupancy matched at every spawn up to N as well.

**A retracted claim.** This was first written up as "matches to spawn 24,
diverges at 25". That was an artifact of the measurement, not a property of the
port: the original's spawn list had been derived from the *plateaus* of the
`Random` call count, and a Multiplier caught by the tube also moves that count
(four `Random(8)` rolls at `1000:08d2`) without dispensing anything. Comparing
against spawns actually detected in the atom array instead, there is no
divergence. The lesson is the project's usual one from the other direction: a
positive result is only as good as the thing it counted.

### The endurance ramp: the game speeds up as you CLEAR

`1000:235c`, inside the beaker update and gated on the matchers having formed at
least one run this frame (`[BP-8] >= 1`, the same count the score multiplier
uses):

    while runs >= 1 do begin                          { 1000:2342 }
      if (waveMode = 1) or (waveMode = 0) then begin
        Inc(counter);                                 { [fe84], a BYTE }
        if counter = 0 then break;                    { wrap guard, 1000:2368 }
        if counter mod 5 = 0 then begin
            if not latch5 then begin
                spawnInterval := spawnInterval - 5;  latch5 := true end
        end else latch5 := false;
        if counter mod 10 = 0 then begin
            if not latch10 then begin
                velocity      := velocity + $20;
                spawnInterval := spawnInterval + 5;  latch10 := true end
        end else latch10 := false
      end else Inc(counter);                          { 1000:23fe, other modes }
      Dec(runs)                                       { 1000:240a }
    end
                                                      { 1000:240d JMP 2342 }

The counter is `[BP+0xfe84]`, seeded to 0 at `1000:a4e4`; the two latches are
`[fe46]` and `[fe47]` and exist so a crossing fires once. Every tenth run the
two adjustments cancel, so the net shape is **five frames off the dispense
interval per ten runs**, with the network velocity climbing 0x20 alongside. The
demo steps 50 -> 45 -> 40 over its first three thousand frames.

**It is driven by matches, not by time and not by atoms dispensed.** That is the
part no amount of watching would have given up, and it is why it stayed hidden:
for the first 29 atoms of the demo nothing about it is visible at all.

**And it is a LOOP, once per RUN.** `1000:240a` decrements the run count and
`1000:240d` jumps back to the `runs >= 1` test, so the body executes once for
every run formed this frame and falls through to the score multiplier at
`1000:2410` only when the count reaches zero. Runs are counted once per SEED, so
a line of four pays twice and a line of five three times, and simultaneous runs
in different orientations each count - which means one good clear can walk the
counter through a crossing by itself.

That distinction is worth a full paragraph because the first transliteration
missed it, hanging the body off `if runs >= 1` instead, and the cost is invisible
for thousands of frames: the counter reached 14 by frame 2,792 where the original
was past 25, so the interval never made its second step and every atom after that
ran progressively late. It is the same shape of error as the score award, which
is also **per seed** rather than per run - this binary counts seeds in more than
one place, and assuming "once per event" has now been wrong twice.

### How the demo replay was finally closed

The port had the tube exactly right and the dispenser exactly right, and still
lost a green ball at frame 1,627. The chain of measurements that found it is
worth keeping, because three of the four steps disproved a hypothesis rather
than confirming one.

**1. The `.SCR` reader, read rather than guessed.** Demo playback swaps the two
input vectors for its own reader (measured live - `DS:0x2352` and `DS:0x2356`
point into the keyboard driver at the menu and into `24c1:00a6` / `24c1:00bc`
inside the demo):

    avail   LES SI,CS:[0x1a] / ADD SI,CS:[0x1e]
            XOR AX,AX / CMP SI,CS:[0x20] / JA +1 / INC AX / RETF
    read    LES SI,CS:[0x1a] / ADD SI,CS:[0x1e]
            MOV AL,ES:[SI] / INC word ptr CS:[0x1e] / RETF

So **`read` advances the stream and `avail` only bounds-checks**, confirming one
byte per frame the game calls `read` - which `1000:44f0` gates on the tube being
idle. The port's model was right.

`CS:[0x1e]` is the stream index, at linear **`0x24c2e`**, and it starts at **6**
- it is a file offset, so the loader consumed the `u16 count` and the `u32 seed`
through the same buffer. Bytes consumed = `index - 6`.

That address is in `demo_trace.py`'s comments as one of two candidates a
monotonicity scan turned up and **dismissed as false positives**. It was the
real thing, thrown away by a filter that could not tell a counter from a
coincidence.

**2. The byte schedule was never wrong.** With the index readable, the port and
the original can be aligned on the byte being consumed rather than on time. Over
680 samples the two agree on *which frame each byte is consumed at* to within
±1.7 frames.

An earlier version of this comparison reported a smooth ±12-frame drift and sent
this session hunting a phantom. The cause was the fit: calibrating the guest's
frame rate through the ORIGIN forced the line through a wrong anchor and
manufactured exactly the kind of slow monotonic error that looks like a real
drift. Fitting slope *and* intercept collapsed the residuals to ±1.7. Another
entry for the list - a positive result is only as good as the model behind it.

**3. So it was the atoms, not the input.** Detecting the original's spawns and
converting to frames:

    spawn 29   frame 1400.1        spawn 32   frame 1535.5
    spawn 30   frame 1445.3   <-   spawn 33   frame 1580.6
    spawn 31   frame 1490.5        spawn 34   frame 1625.8

Exactly 50 frames apart for 29 atoms, then exactly 45. The port dispensed at 50
forever, so from spawn 30 its atoms ran later and later against a tube that was
in the right place all along. The catch window is eleven pixels against a nine
pixel step - one frame - so it took only a few atoms for one to arrive after the
tube had moved on.

**4. What it was worth.** With the ramp transliterated, the port and the
original agree on **which frame every byte of the recording is consumed at**,
over all 1,184 common byte counts, with residuals of -0.7 to +1.6 frames - and
that covers the port's entire life, frames 0 to 2,963. Every spawn in that range
agrees in column, type and frame. The port's first missed atom, frame 911, is
the one the original misses too.

### Attract mode does not play the whole recording, and there is nothing after

`DEMO.SCR` holds 11,970 input bytes and the original's demo session consumes
**2,395** of them before ending. Two separate things are going on, and both are
now measured.

**The file is mostly dead air.** Real play - bytes carrying `02` Down, `04`
Left, `08` Right, `10` A - runs from the start to about byte 3,950. After that
only a scatter of lone `0x05` bytes appears, at 4,216, 4,234, 4,269, 5,356,
7,585 and 8,930, and nothing at all beyond. So the recording is roughly four
thousand frames of play sitting in a twelve-kilobyte buffer, and the `count`
field describes the buffer rather than the performance. Nothing is "after" the
demo: the tail is slack.

**The session ends on drops, not on the stream.** Reading the drops counter
through a run:

    bytes   693   3 -> 2      a miss
    bytes  2188   2 -> 3      a BONUS caught - the one thing that gives one back
    bytes  2276   3 -> 2
    bytes  2320   2 -> 1
    bytes  2352   1 -> 0
    bytes  2367   0 -> 255    the byte underflows: game over

So the recorded player is beaten by the allowance about 1,600 bytes before their
own input runs out. That strongly suggests **the recording was made at an easier
setting than the one attract mode replays it at**: nothing in a `.SCR` carries a
difficulty, only the seed, and View Demo hardcodes Tubes 301 with its three
drops. A recording made at 101's nine would run much further than the replay
does.

It also explains why the reader's `avail` bounds check is never what stops
playback - the game is over long before the index reaches the limit.

**The replay is now exact end to end.** With the ramp counting per run, the port
runs the whole session and stops on the same byte the original does:

| event | original | port |
|---|---|---|
| a miss, score 1,000 | byte 693 | byte 693 |
| **a Bonus caught, +1 drop** | byte 2,188 | byte 2,184 |
| a miss | byte 2,276 | byte 2,274 |
| a miss | byte 2,320 | byte 2,319 |
| a miss, drops now 0 | byte 2,352 | byte 2,353 |
| the drops byte underflows: game over | byte 2,367 | byte 2,367 |

Final score 13,000 on both sides. Over 752 common byte counts the two agree on
which frame each byte is consumed at with a median difference of -0.2 frames, and
all 71 spawns land within 4.7 frames - which is the measurement's noise, not the
port's.

**How the last atom was found.** The tell was one atom, record 4, dispensed at
the same byte in both but reaching the catch window at byte 2,125 in the port
against 2,122 in the original - three frames in which the tube leaves stop 6. It
was tempting to call that a slow rise, and the first look did: the port's copy is
at y = 162 when the original's is at y = 119. Measuring the rate settled it -
4.25 px/frame in the port against 4.33 in the original, the same velocity within
the timing error. The atom was not slow, it was **dispensed later**, and that led
to the interval, and the interval led to the ramp counter.

**A measurement bug worth recording**, because it produced a confident wrong
answer for half an hour. The port's CSV first wrote `Board::typeAt()`, which
strips the fade, against the original's raw cells. Every clearing cell then
looked unmatched, and the diff reported - convincingly, with the right cells and
the right types - that the port's matcher was missing a Flashium-seeded
diagonal. It was not: those cells were at fade frame 4 and clearing normally.
Compare like with like, and prefer the rawest form of both.

### The catch tests the tube's ACTUAL x, not its stop

`rec.x = tube.x + 3`, so the two differ for the three frames of a slide: Left
and Right move the stop immediately and the tube then takes 6 px a frame to
catch up. A tube on its way to a column does not catch there yet, and one on its
way out still catches at the column it is leaving until it has physically left.

The port compared stop indices, which caught an atom up to three frames early.
That is not a wash, because the tube holds five: catching early can fill it and
`tube.count <> 5` then refuses a later atom the original had room for.

The boost at `1000:4534` genuinely does use the stop index, so the asymmetry
between the two tests is the original's rather than an oversight.

### Reading `RandSeed` gives an exact call count, and it needs no breakpoint

The plan was to break on `Random` and count hits. That does not work on this
rig, and the negative was checked before being believed: the entry address is
right (exactly **one** copy of the LCG's signature exists in the whole address
space, at runtime linear `0x1f7fb`), the stub itself works (a breakpoint at the
seed write `1000:6008` fires, with `DX:AX = 0x322d385e`), and the code runs
(`RandSeed` advances) - yet `Z0` on the RTL never traps, on `core=normal`,
whether or not it is the first breakpoint set. Unresolved.

It does not matter, because the generator hands over a better instrument for
free. `RandSeed := RandSeed * $08088405 + 1` is a **bijection**, so the orbit
from the demo's seed visits each value at most once and a single read of
`RandSeed` maps straight back to *how many times `Random` has been called*.
Build `{seed -> k}` for a few hundred thousand steps, sample the guest at
leisure, and look the answer up.

It also validates the port's generator on the way: over 737 samples taken across
three minutes of the running demo, **every one** landed on the orbit computed
from `DEMO.SCR`'s seed by the port's own algorithm.

## Router state 7 - the descent, the catch and the miss

### The descent accelerates below y = 50

`1000:13ed`, the first thing state 7 does:

    if rec.y >= 50 then rec.acc := rec.acc + $480
                   else rec.acc := rec.acc + rec.velocity;
    rec.y := rec.y + rec.acc div 128;  rec.acc := rec.acc and $7F

So the difficulty's 2 / 3 / 4 px a frame applies only to the **top fifty
pixels** of a play column, and below that every atom falls at a flat **nine** -
the same `0x480` the Down/B boost and the Bonus atom use. A missed ball drops
away much faster than it travelled the network.

Two consequences worth stating. **The Down/B boost cannot affect the last
stretch of a descent**: it writes `rec.velocity`, and below y = 50 the velocity
is not read at all. And a boost test conducted below y = 50 measures nothing -
the port had one at y = 70 that passed by coincidence, because the nine pixels
a frame it was checking for is what an unboosted atom does there anyway.

### The catch is a window, 60..70

    if (rec.y >= 60) and (rec.y <= 70) and (rec.x = tube.x + 3)
       and (tube.count <> 5) and (tube.state <> 3) then begin
        rec.acc := 0;  rec.state := 8;
        Inc(tube.count);  rec.dy := yofs[tube.count];
        Move(rec, tube.slot[tube.count], 28);
        rec.state := 1
    end

Eleven pixels against a nine-pixel step, so an atom lands inside the window on
one frame and is past it the next. **The tube will not catch while it is
tipping** - `tube.state = 3` disqualifies it outright.

The caught record is **copied** into the slot and the network record drops to
state 1, the two-frame teardown, rather than straight to free.

### The miss, and the Bonus exemption

    if rec.y > 187 then begin
        rec.y := 187;  rec.state := 1;
        if rec.type <> 10 then begin
            Dec(drops);
            if drops = 0 then <redraw the counter>
        end;
        PlaySound(...)
    end

A missed **Bonus costs no drop**. That is the same exemption state 9 makes when
a tipped atom finds its column full.

## Flashium's colour cycle, `1000:486b`

Type 8 has no sprite of its own, and the game does not special-case it in the
renderer - it **rewrites the ball table**:

    Inc(subTick);
    if subTick = 5 then begin
        subTick := 1;
        Inc(flash);  if flash = 8 then flash := 1;
        ball[8] := ball[flash]                 { DS:0x1dc6 := DS:0x1da6 + 4*flash }
    end

Both counters start at 1 in the session prologue, so it advances every **four**
frames - about 4.5 times a second at 18.2 Hz, which is the "~4x/sec" a play
session measured. The cycle was known from watching; this is the code doing it.

Only the bare slot 8 is rewritten. A fading Flashium cell holds `8 + 19*frame`,
whose table slot is `FFADE` and is left alone - which is why a Flashium always
clears with `FFADE` however it happens to be drawn at the time.

## Router state 8 - in the tube

    rec.x := tube.x + 3;
    if rec.type = 17 then begin rec.y := tube.y + rec.dy; rec.arrived := 1 end;
    <the catch-time specials dispatch here, if rec.arrived>
    if (tube.y + rec.dy = rec.y) and rec.arrived then exit;
    rec.y := rec.y + 9;
    if tube.y + rec.dy < rec.y then begin
        rec.y := tube.y + rec.dy;
        PlaySound(if rec.dy = 52 then <floor> else <stack>);
        rec.arrived := 1
    end

A catch lands at the **mouth** and slides down to its slot at a flat 9 px a
frame. The FILLBALL is the only type that arrives instantly, which makes sense:
the Filler inserts it *under* everything, where a slide would travel upward.

`rec.arrived` is never cleared while the atom is in the tube, so `1000:180c`
re-enters the specials dispatch every single frame. Each of the four rewrites
the type of the slot it acts on, and **that** is the only thing stopping them
firing repeatedly - the same trick as AntiMatter rewriting its cells to 9.

One consequence, left in because it is the original's: the routines act on
`slot[count]`, not on the record that just arrived. An atom caught while an
earlier one is still sliding arrives second, and the special then converts
whatever is at the mouth.

## Router state 9 - falling into the beaker

    rec.y := rec.y + 9;
    if      grid[5, rec.col] = 0 then rec.dest := 186
    else if grid[4, rec.col] = 0 then rec.dest := 173
    else if grid[3, rec.col] = 0 then rec.dest := 160
    else if grid[2, rec.col] = 0 then rec.dest := 147
    else if grid[1, rec.col] = 0 then rec.dest := 131
    else                              rec.dest := 187;        { column FULL }
    if rec.y < rec.dest then exit;
    rec.dest := <the row that dest meant>;                    { 1000:167c }
    rec.state := 1;
    if <the column was full> then begin
        if rec.type <> 10 then Dec(drops);                    { the atom is LOST }
        PlaySound(...)
    end else
        grid[rec.dest, rec.col] := rec.type;

The target is recomputed **every frame**, so an atom already on its way down
lands correctly if the column settles under it. Row 1's 131 is three pixels
above where the cell actually draws (134); every other row is exact, and no
reason for it has turned up.

`rec.dest` is the same `+0x0d` field the tube used for the slot offset, and
`1000:167c` rewrites it in place from a y to the row number that y meant. The
reuse is the original's.

A **full column destroys the atom and costs a drop** - it does not sit on top
and it does not bounce. A Bonus is exempt.

### The grid array is left to right; the network's columns are not

Worth stating because it was nearly read the other way. `1000:1761` writes
`grid[r, c]` at `BP - 0x25 + 6r + c`, and the draw loop at `1000:599b` pairs
grid column 1 with `DS:0x1e`. Dumping DGROUP settles it:

    DS:0x1a..0x24 = 143 125 107 197 179 161

so `DS:0x1e` is **107**, and the grid's columns run 107, 125, 143, 161, 179,
197 - left to right. The *network's* column index reads the same table in a
different order, which is why the port compares x values rather than indices.
The tube's stop index is left to right too, so `rec.col := tube.stop` at
`1000:475b` hands the fall a grid column directly.

## Records 7..12

`1000:47fe` runs the router over `atom[1..12]` in one loop; nothing
distinguishes the two halves except the states their records are in. The
tipping code allocates from 7..12 by the first record whose state is 0, and
**halts the game** if none is free. Six slots against six columns and a fall of
at most nine frames, so it cannot happen.

`1000:5c1f` draws them after the grid and the `MARKER` overlay and before
`BEAKER.CSP`, so a falling atom passes in front of the settled ones and behind
the glass.

States **1 and 2** are a two-frame teardown - `1000:18ec` steps 1 to 2 and 2 to
0 - and `drawn()` is `state > 2`, so neither renders. They exist so a slot is
not reallocated on the frame it was released.

## The HUD, and how text is drawn

### The text renderer, `2000:35ec` and `2000:36ab`

`OutText(x, y, colour, mode, s)` - `2000:36ab`, `RETF 0xc` - draws each
character with `2000:35ec` and steps x by the advance. Two things in it are
the whole look of the game's text.

**The colour walks down the cell.** `2000:35ec` keeps the index in `BH` and
adjusts it after every scanline, so a glyph is a vertical gradient off ONE
palette index rather than a flat colour:

    src := font + char * cellH;
    for row := cellH downto 1 do begin
        bits := src^;  Inc(src);
        if bits <> 0 then <plot the set bits in BH>;
        case mode of
          1: Dec(BH);
          2: Inc(BH);
          3: if row > peakRow then Dec(BH, 2) else Inc(BH, 2);
        end
    end

The loop counter runs DOWN, so mode 3's turning point is measured from the
bottom of the cell. Only set bits are written - a glyph is transparent and
never lays down a background.

**Bit 7 of the mode is a shadow.** With it set, `2000:36ab` draws the glyph a
second time first, at `(x + 1, y + 1)`, flat, in `DS:0x239c`. Every call in the
game session sets it. A space is skipped entirely rather than drawn, so it
lays down no shadow either.

`OutTextCentred(x0, x1, y, colour, mode, s)` - `2000:37ea` - measures the
string with `2000:3774` and starts it at `(x0 + x1 - width) div 2`.

### `SetFont`, `2000:3fab`

    DS:0x2392 := fontPtr;      DS:0x2396 := cellW;
    DS:0x2397 := cellH;        { also the per-character stride }
    DS:0x2398 := advance;      DS:0x2399 := peak + 1;

The game selects two fonts and swaps between them mid-frame:

| call site | args | font |
|---|---|---|
| `1000:42ae` | `(8, 8, 6, 4)` | `TINY6X8.88` |
| `1000:42e8` | `(8, 16, 8, 7)` | `FUTURE.816` |

The small one has to be `TINY6X8.88` - it is the only 8 x 8 font in the
archive. `FUTURE.816` was identified by pulling the digit `0` out of a captured
HUD frame and comparing it against all four `.816` fonts: it matches byte for
byte and the other three are not close.

### The HUD itself

Labels once at session setup, `1000:42ae`; numbers every frame, `1000:5707`.

    SetFont(small);
    OutText(  1, 1, 127, $81, 'Chains');
    OutText(288, 1, 127, $81, 'Drops');
    SetFont(big);
    OutTextCentred(0, 319, 0, 127, $81, Str(score));
    OutText(32, 0, 127, $81, Str(chains:3));
    if (drops > 0) and (drops < 255) then
        OutText(265, 0, 127, $81, Str(drops))
    else begin
        SetFont(small);  OutText(270, 1, 127, $81, 'No');  SetFont(big)
    end

`$81` is the shadow bit plus mode 1. **There is no second colour constant
anywhere** - the labels and the numbers are both index 127, and they look
different only because the ramp runs over eight scanlines in one font and
sixteen in the other. The palette holds a cyan ramp at 112..127 with 127 its
darkest end, so every glyph brightens toward its own bottom.

Confirmed against a captured frame: the pixels of `Chains` read 127, 126, 125,
124, 123, 122, 121 down its eight rows, exactly one per scanline, with index 0
one down and one right of every stem. `DS:0x239c`, the shadow index, is
therefore **0**.

The width-3 field on `chains` is why its digit sits at x = 48 and not 32 - the
two leading spaces advance without drawing.

The 255 arm is the drop counter having wrapped past zero, which is the
game-over condition, so `No` is on screen for the frame that ends the session
as well as the last one before it.

### The score pop-ups

`1000:582a`, while the ramp is running, in the small font at colour **168**
with mode **3** - a peak rather than a ramp, brightening to the middle of the
cell and dimming again:

    if pending > 0 then
        OutTextCentred(0, 319, 13, 168, $83, '+' + Str(pending));
    if multiplier > 1 then
        OutTextCentred(0, 319, 21, 168, $83, 'x' + Str(multiplier));

### Two pixels the port draws and the original does not

The port reproduces the HUD band exactly - every ink and shadow pixel of
`Chains`, `Drops`, the score and the counters - **except two**: the shadow at
`(36, 4)` and `(36, 7)`, both cast by the `s` of `Chains`. Every shadow pixel
at x <= 35 is present in both; x = 36 is present only in the port.

The cause is not established. It is not a per-character effect - `Drops` also
ends in `s` and matches completely - and the dirty-rect restore over the chains
value starts at x = 37, one column short of explaining it. Most likely an edge
case in the original's Mode X nibble writer, which is the one part of that
routine that is about the hardware rather than the game. Recorded rather than
chased, like the three-pixel GAMEFG floor.

### Still open on the specials

`1000:041c`, the Crystal's teleport, which `1000:0f3b` calls in wave mode 5
when the blast catches a Crystal.

Also unported: all three gated routines carry a tail guarded by `DS:0x1d4e = 4`
that keeps a running count of what is in the tube - the fills increment it per
ball, the Filler and the router's release path decrement it, and reaching zero
adds 2 to the clear timer. It is wave-mode machinery with nothing to hook into
yet.


## Wave mode, decompiled - the whole objective system

The largest remaining unread block, and it came apart in one pass once the
right question was asked: **who writes `DS:0x1d4e`?** `FindScalarRefs.java`
answers with a contiguous run of 22 functions between `1000:62f1` and
`1000:8320`, each setting the mode byte and nothing else in common. They are
the objective templates, one procedure per briefing.

Earlier notes recorded the wave definitions as "still open - most likely
derived from the wave number". That guess is now retired: they are **a literal
75-arm dispatch**, and the parameters are **six counters that step on a
schedule**.

### `1000:86b8` is the briefing screen, and its body is the wave table

`1000:9e53` calls it once per wave, immediately before `1000:3a67`:

    a5d2:  if waveMode <> 1 and waveMode <> 0 then Briefing        { 1000:86b8 }
    a5e4:  PlayWave                                               { 1000:3a67 }

`86b8` loads a background, prints `Wave <n>` and `You are allowed <n> drops.`,
then runs an `if wave = 1 ... else if wave = 2 ...` chain **75 arms long**,
calling one objective routine per arm. Endurance (`waveMode` 0 or 1) never
reaches it, which is why the port has never needed it.

**The table, wave 1..75.** Read out of the dispatch rather than transcribed:

| wave | template | wave | template | wave | template |
|---|---|---|---|---|---|
| 1 | any | 26 | survive | 51 | marked-xenon |
| 2 | survive | 27 | diag-colour | 52 | td-chain-45s |
| 3 | vert-colour | 28 | marked-covered | 53 | survive-disabled |
| 4 | any-morph | 29 | td-chain-task | 54 | horiz-any |
| 5 | vert-any | 30 | survive-hidden | 55 | mystery |
| 6 | survive-disabled | 31 | flashium | 56 | td-both-45s |
| 7 | shown-atom | 32 | td-colour-45s | 57 | diag-any |
| 8 | marked | 33 | any-prefill | 58 | marked-xenon |
| 9 | any | 34 | vert-any | 59 | vert-any |
| 10 | vert-colour | 35 | td-both-45s | 60 | td-chain-45s |
| 11 | any-morph | 36 | survive | 61 | flashium |
| 12 | shown-atom | 37 | horiz-colour | 62 | survive-hidden |
| 13 | horiz-any | 38 | marked-covered | 63 | td-both-45s |
| 14 | td-chain-task | 39 | td-chain-task | 64 | marked-covered |
| 15 | vert-colour | 40 | horiz-colour | 65 | td-both-task |
| 16 | marked | 41 | marked-xenon | 66 | td-colour-45s |
| 17 | shown-atom | 42 | diag-colour | 67 | any |
| 18 | diag-any | 43 | survive-disabled | 68 | td-colour-task |
| 19 | any-prefill | 44 | td-both-task | 69 | crystals |
| 20 | marked | 45 | diag-any | 70 | td-colour-45s |
| 21 | diag-colour | 46 | mystery | 71 | mystery |
| 22 | horiz-any | 47 | any-prefill | 72 | td-both-task |
| 23 | any-morph | 48 | flashium | 73 | crystals |
| 24 | survive | 49 | horiz-colour | 74 | td-chain-45s |
| 25 | td-colour-task | 50 | crystals | 75 | survive-hidden |

**This reproduces every briefing the level-warp sweep sampled, 9 for 9** -
waves 6, 10, 11, 15, 20, 25, 30, 40 and 50 - including that 10 and 15 share an
objective, which the sweep had to write off as a coincidence. The sweep was
right and now it is derived rather than observed.

### The 25 objective routines

| addr | name used here | mode | what it sets |
|---|---|---|---|
| `62f1` | marked | 6 | counter := markedCount |
| `643b` | marked-covered | 6 | + `-0x187` |
| `6592` | marked-xenon | 6 | + `-0x188` |
| `66cb` | crystals | 5 | **Inc(crystalCount)** first, then counter := it |
| `67d8` | flashium | 3 | reqColour := 8, anyOrientation |
| `68b7` | shown-atom | 3 | reqColour := Random 1..7, anyOrientation |
| `69e4` | horiz-colour | 3 | reqColour := Random 1..7, reqChain := 1 |
| `6b5d` | vert-colour | 3 | as above, reqChain := 2 |
| `6cd6` | diag-colour | 3 | as above, reqChain := 0 |
| `6ede` | survive | 4 | counter := atomTarget |
| `6fd5` | survive-hidden | 4 | + `-0x189` |
| `7100` | survive-disabled | 4 | + `-0x18a` := Random 1..7 |
| `72ad` | td-colour-task | 3 | anyOrientation, rotateColour |
| `744d` | td-chain-task | 3 | reqChain := Random(3), rotateChain |
| `764b` | td-both-task | 3 | rotateColour + rotateChain |
| `7802` | td-colour-45s | 3 | as `72ad` + **rotateOnTimer** |
| `798b` | td-chain-45s | 3 | as `744d` + rotateOnTimer |
| `7b72` | td-both-45s | 3 | as `764b` + rotateOnTimer |
| `7cce` | any | 3 | reqColour := 0, anyOrientation |
| `7de3` | any-prefill | 3 | + `-0x1f2`, the pre-filled beaker |
| `7f42` | any-morph | 3 | + `-0x1f0`, the 45-second morph |
| `8056` | horiz-any | 2 | reqChain := 1 |
| `81bb` | vert-any | 2 | reqChain := 2 |
| `8320` | diag-any | 2 | reqChain := 0 |
| `8581` | mystery | - | calls one of `68b7`/`81bb`/`8056`/`8320` at `Random(4)`, then sets `-0x1f1` so the Task Display stays blank until the first task lands |

**Chain orientation is `0 = diagonal, 1 = horizontal, 2 = vertical`** - fixed by
the three pairs above and by the rotation wrapping `2 -> 0`.

Every routine except `62f1`, `643b`, `6592`, `66cb`, `6ede`, `6fd5`, `7cce`,
`7de3` and `7f42` wraps its randomisation in `if -0x1ff = 0`, so a **Continue
replays the same wave with the same objective** rather than rolling a new one.

### The objective record - `1000:9e53`'s frame

Two groups. The **wave-independent counters** are seeded once per game and
stepped by the progression; the **per-wave objective** is rewritten by the
briefing every wave.

| offset | field |
|---|---|
| `-0x170` | wave number |
| `-0x17b` | pre-fill size - how many atoms `any-prefill` puts in the beaker |
| `-0x17e` | drops remaining (the briefing echoes this as "you are allowed N") |
| `-0x180` | atom velocity (word) |
| `-0x181` | dispense interval |
| `-0x182` | atom target, for the three `survive` templates |
| `-0x183` | chain target when the **orientation** is what is required |
| `-0x184` | chain target when the **colour** (or nothing) is required |
| `-0x185` | Mischief Crystal count |
| `-0x186` | marked-atom count |
| `-0x187` | modifier: the marked atoms are covered |
| `-0x188` | modifier: the marked atoms are ringed with Xenon |
| `-0x189` | modifier: atoms hidden until they leave a tube |
| `-0x18a` | modifier: this colour is disabled and will not clear |
| `-0x1ef` | the rotation runs on the 45-second timer, not per task |
| `-0x1f0` | modifier: beaker atoms morph every 45 seconds |
| `-0x1f1` | Mystery Wave: the Task Display is blank until the first task lands |
| `-0x1f2` | modifier: the beaker starts pre-filled |
| `-0x1f3` | the objective is a **count** the Task Display should print |
| `-0x1f4` | **the live objective counter** |
| `-0x1f5` | required colour, 0 = any, 8 = Flashium |
| `-0x1f6` | required chain orientation |
| `-0x1f7` | orientation does not matter |
| `-0x1f8` | rotate the required colour |
| `-0x1f9` | rotate the required orientation |
| `-0x1fd` | quit |
| `-0x1fe` | game over |
| `-0x1ff` | this wave is being replayed after a Continue |

### The seeds are literals, and every observed number falls out

`1000:a4c6` branches on `DS:0x1d4c`, the new-game flag. The new-game arm at
`a4cd` writes the six counters as **immediates**:

    atomTarget   := 30      { -0x182 }
    chainTargetO := 3       { -0x183, orientation-driven waves }
    chainTargetC := 2       { -0x184, colour-driven waves }
    crystals     := 0       { -0x185 }
    marked       := 3       { -0x186 }
    preFill      := 8       { -0x17b }
    drops        := [0x1d51]   { 9 / 6 / 3, the difficulty }
    velocity     := [0x1d54]
    interval     := [0x1d56]
    wave         := [0x1d50]

and the load arm at `a525` reads the same ten fields out of `DS:0x1d07..0x1d19`
instead. **`3, 30, 2, 0, 3, 8` - the six numbers `PLAN.md` had recorded as
"difficulty seeds, variables not yet named" - are these, in this order.**

Checked against the sweep's briefings, and they are exact:

| briefing | says | from |
|---|---|---|
| wave 6 | live through **30** atoms | atomTarget 30 |
| waves 10, 15, 40 | form **2** chains of a colour | chainTargetC 2 |
| wave 11 | form **2** chains, any atom | chainTargetC 2 |
| wave 20 | Marked Atoms: **3** | marked 3 |
| wave 50 | Mischief Crystals: **1** | crystals 0, `66cb` increments before use |

The crystal count is the one that is not stepped by the progression: `66cb`
increments it itself, so it is "how many crystal waves you have reached", and
wave 50 - the first - is 1.

### The wave progression, `1000:a616`

Runs only when the wave was **cleared** (`-0x1ff = 0`), after the stats
blackboard:

    Dec(interval)                          { a616 - one frame faster every wave }
    if wave mod 15 = 0 then begin
      velocity := velocity + $20           { a62b }
      interval := interval + 12            { a630 - a partial refund }
    end;
    if wave mod 20 = 0 then begin
      Inc(chainTargetC); Inc(chainTargetO);
      atomTarget := atomTarget + 10;
      Inc(marked)                          { a646 }
    end;
    if wave >= 75 then EndOfGame;          { a657, 1000:9499 }
    Inc(wave)

So the difficulty curve is: the dispense interval tightens by one frame a wave
and is partly refunded every fifteenth, while the objectives get one step
harder every twentieth. The "level bands at 30/60/75/90/95/101" recorded
earlier belong to something else and are not this loop.

### `1000:192f` - how a chain scores against the objective

Called by every matcher with the orientation code of the run it just found.
Transliterated:

    scored := false;
    if waveMode = 2 then begin
      if (reqChain = orientation) and (counter <> 0) then begin
        Dec(counter); scored := true
      end
    end
    else if waveMode = 3 then begin
      if ((reqColour = 0) or (reqColour = matchType) or (matchType = 8))
         and (anyOrientation or (reqChain = orientation))
         and (counter <> 0) then begin
        Dec(counter); scored := true
      end
    end;
    if scored then begin
      if rotateChain and not rotateOnTimer then begin
        if reqChain = 2 then reqChain := 0 else Inc(reqChain);
        taskDisplayChain := reqChain
      end;
      if rotateColour and not rotateOnTimer then begin
        if reqColour = 7 then reqColour := 1 else Inc(reqColour);
        taskDisplayColour := reqColour
      end;
      if mysteryHidden then begin
        mysteryHidden := false; PlaySound(...)          { the reveal }
      end
    end

`matchType = 8` - an all-Flashium run - satisfies **any** colour requirement.

### Modes 4, 5 and 6 do not go through `192f`

* **mode 4, live through N atoms.** `1000:4b31`, in the dispense path: every
  atom sent out decrements the counter, and once it is zero the new record's
  `+8` is cleared instead. So the objective counts atoms *dispensed*, not
  caught, and the Task Display shows the count (`-0x1f3`).
* **mode 5, the crystals.** `1000:2722` in the beaker update, and the count is
  seeded into the beaker by `1000:0236` at `1000:3b5e`.
* **mode 6, the marked atoms.** `1000:24db`, already recorded: clearing a
  marked cell decrements the counter. `1000:0000` places them at `1000:3b4a`.

### The 45-second timer

One counter, `[BP-0x1b0]` in `1000:3a67`, seeded **720** at `1000:3b00` and
reloaded at `1000:4b6d`. On expiry:

* if `rotateOnTimer`, step whichever of colour and chain the wave rotates, and
  play a sound;
* if `morphBeaker`, walk the beaker and morph its cells.

720 frames against the game's own frame beat is the "every 45 seconds" the
briefings promise.

### Ending a wave

At `1000:5cff`, the tail of the frame loop:

    if drops = $ff then gameOver := true;           { the underflow, already ported }
    if waveMode not in [0,1] then
      if (counter = 0) and (clearTimer = 0) and (inPlay = 0) then
        waveComplete := true;
    if clearTimer > 0 then Dec(clearTimer);
    if not waveComplete and not gameOver and not quit then <next frame>

`1000:8da5` then shows the stats screen and `1000:8c38` the Continue
screen, which is what sets `-0x1ff` - and that is why a continued wave keeps
its objective.

### A banner is a five-step sequence, not a key wait

`1000:5dbb` (Wave Complete) and `1000:5e2a` (Game Over) are the same five
steps, and only the middle one looks at input:

    PlayMusic(<the arm's song>)
    WriteCentred(<caption>);  WriteCentred(<rule>)
    Delay($28)                        { 1000:5dfb / 1000:5e68 }
    ClearKeyBuffer                    { [DS:$234e] then 2591:0552 }
    repeat key := ReadInput until ([DS:$22ce] = $ff) or (key <> 0)
    StopMusic                         { [DS:$22da] }

and `1000:5ef0` runs `Delay($28)` again on the way out, for all three arms.

**The opening Delay is why the banner cannot be missed.** 40 retraces at 70 Hz
is 0.571 s in which no input is read at all, and the key buffer is FLUSHED
afterwards - so the keypress that ended the wave cannot dismiss the banner it
caused. That matters most in exactly the case a player hit it: a mode-4 wave
ends when the last atom lands, which the player caused by holding the tip
button, and a port that goes straight to a key wait shows the banner for one
frame. The report was "the first wave ending did not show wave complete", and
it was not user error.

**`[DS:0x22ce]` settles the other half.** It returns `$ff` once the song has
been through once, so the wait ends on the music as well as on a key - which is
the player's earlier report that the banner ends when its music does,
now read off the code. The driver keeps playing either way: `$f0` REWINDS
rather than stopping, and the flag is the sequencer's `cs:0x32`, which this
port already models as `MusSequencer::looped()`. Nothing new had to be
decoded - the value was sitting in `mus.cpp` waiting for a caller.

The abort arm at `1000:5e97` has no wait: caption, rule, the F2 hint when the
mode is not attract and saving is enabled, one call into the key handler
`1000:2dd0` so the offer works, and then straight to the common tail. The port
leaves that arm waiting for a key, which is a deliberate deviation and is
marked as one at the call site.

**The decrement is AFTER the test, and one frame of clear animation depends on
it.** A match sets the timer to 10 at `1000:1c70`, which is early in the frame;
the tail then tests it at 10 and steps it to 9. The port ran the decrement at
the end of its beaker update instead, so the same frame tested 9 and every
clear animation was judged one frame short. Cheap to get wrong, and invisible
except as a wave that ends slightly too soon.

### `-0x1be` is the IN-PLAY count, and it is why a wave does not end at the dispenser

The third term above was carried for several sessions as a Task Display field
called `count`, on the strength of being seeded from the objective right beside
the colour and the chain. It is nothing of the sort - **it is never drawn**, and
the number the Task Display shows is the objective counter `-0x1f4` itself.

Two counters run in a mode-4 ("survive N atoms") wave, and they come down at
different moments:

| | seeded | decremented | by |
|---|---|---|---|
| `-0x1f4` objective | 30, by the briefing | when an atom is **dispensed** | `1000:4b3b` |
| `-0x1be` in play | 30, at `1000:3af0` | when an atom **leaves play** | `1000:158d`, `1000:17b4`, `1000:0c6b` |

`1000:3af0` seeds the second one from the first **only in mode 4** and zeroes it
in every other mode, which is how the extra term vanishes for the modes that do
not count atoms.

An atom leaves play three ways, and every site is the same three lines gated on
`DS:0x1d4e = 4`:

    if inPlay > 0 then begin
        Dec(inPlay);
        if inPlay = 0 then Inc(clearTimer, 2)
    end

* `1000:158d` - the end of the **descend** arm (state 7), inside the `y > $bb`
  branch: the atom fell past the tube and is lost. It sits *after* the Bonus
  exemption's join, so a missed Bonus costs no drop but still counts.
* `1000:17b4` - the end of the **tipped** arm (state 9), where both landing
  branches join: settled into the beaker, or destroyed against a full column.
* `1000:0c6b` - the **Filler**, which shifts the tube up and pushes the top
  slot out of the stack.

and it goes UP at `1000:0a13` and `1000:0b41`, **inside the fill loops** of the
Multiplier and the Evil Multiplier - one per ball, because those balls have to
leave play too. Without that a filled tube would strand the count above zero.

So the gap between the two counters is a whole atom lifetime: a descent, a stay
in the test tube for as long as the player leaves it there, and a fall into the
beaker. **A wave ends when the last atom has landed, not when the last atom has
been sent out** - and a tube left full holds the wave open indefinitely, which
is the original's behaviour and now the port's.

The dispenser's own arm is worth reading too, because it is not a bail-out:

    if mode = 4 then
      if counter > 0 then Dec(counter)
                     else record.state := 0        { 1000:4b4a }

`+0x08` is the state field, and this runs at the very END of the dispenser -
after the record is built and its colour rolled. So a wave past its quota still
spends a random number and then throws the atom away. Bailing out early would
give the same picture and a different random sequence.

The `Inc(clearTimer, 2)` is why the last atom of a wave still gets two frames of
screen after it settles - or ten, if landing formed a chain and rearmed the
timer, which is what usually happens.

## Closing the session loop: the three flags, the banners, the two screens

### The three flags, read off the listing and NOT the decompiler

Ghidra names `1000:9e53`'s own locals **two bytes low** - its `local_1ff` is
really `BP-0x1fd`. The listing at `1000:a5e8`..`1000:a6a9` is unambiguous, and
the three bytes are three different things that all look alike in C:

| offset | in `3a67` / `2dd0` | meaning |
|---|---|---|
| `BP-0x1fd` | `SS:[DI+0xfe03]` | **aborted** - leave the wave loop at once, no stats, no high-score entry |
| `BP-0x1fe` | `SS:[DI+0xfe02]` | **game over** - show the Continue screen |
| `BP-0x1ff` | `SS:[DI+0xfe01]` | **replay this wave** - skip the progression |

The loop is `repeat ... until aborted or gameOver`, so a Continue that clears
`gameOver` re-enters it, and `replay` is what stops `1000:a616` from stepping
the wave on the way round. Only `-0x1fe` is written inside `3a67`
(`1000:47f8` and `1000:5d0a`); **`-0x1fd` is written only by the key handler**
`1000:2dd0`, which is why searching `3a67` for it finds nothing.

### `1000:2dd0` is the in-game key handler

Called from `1000:5cf7`, once per frame and only when `KeyPressed`
(`2000:5e52`) is true, right after the flip. Extended keys arrive as
`0x80 + scancode`, so `0xbb`..`0xbf` are F1..F5:

    k := ReadKey;
    if mode = 0 then k := $1b;                  { attract: ANY key aborts }
    if [0x1d4b] <> 0 and k = $bc then k := 0;   { F2 disabled in this build }
    case k of
      $1b: parent.parent.aborted := 1           { two static links - 2dd0 is
                                                  nested in 3a67 in 9e53 }
      $bb: <Help, 'Press Any Key...'>
      $bc: <Save Game, with a slot picker>
      $bd: <music toggle, [0x215f]>
      $be: <sound toggle, [0x215e]>
      $bf: <pause>
    end

**Pause blocks the whole loop.** It draws `Game Paused` / `_________` centred
at `y` 92 and 95, colour 47, mode 3, stops music and sound, then

    repeat k := ReadKey until k = $bf

so **only F5 releases it** - every other key is swallowed. That is exactly why
the rig can freeze a frame with Pause and still answer a `screendump`, and it
means the port must not treat any-key as unpause.

The two toggles are symmetrical: flip `[0x215f]` (music) or `[0x215e]` (sound),
call the driver, then flash `Music On` / `Music Off` / `Sound On` / `Sound Off`
centred at `y` 92, colour 47, mode 3. The music arm restarts the *current* song
from `parent[-0xa]` when switching on, and calls the stop vector when off.

### The end-of-session banners, `1000:5d64`

Still inside `3a67`, after the loop falls out. All three are centred over
`0..319`, colour `0x2f` = 47, mode `0x83` = `kShadow | kPeak`:

| condition | line 1, `y` 92 | line 2, `y` 95 | music |
|---|---|---|---|
| wave complete | `Wave Complete` | `___________` | `VICTORY.MUS` |
| game over | `Game Over` | `_______` | `DEATH.MUS` |
| aborted | `Game Aborted` | `___________` | - |

The underline literal is a different length in each case and is **not** derived
from the word - `Game Over` is 9 characters over 7 underscores. They are three
separate string constants at `1000:3a12`, `1000:3a20`, `1000:39f8`, `1000:3a2c`
and `1000:3a36`.

The first two then wait on `repeat until KeyPressed or timeout`. The abort
banner does not wait; instead, when `mode <> 0` and `[0x1d4b] = 0` it adds

    F2 to Save Game, ESC for Main Menu!

centred at `y` 115, colour 47, mode `0x81`, and **calls `1000:2dd0` again** -
so the abort screen reuses the same key handler to offer the save.

A **Perfect Bonus of 2500** (`0x9c4`) is added to the score at `1000:5dae`,
before any of the banners, when `parent[-0x17d]` is set.

### `1000:8da5`, the stats screen - and it is NOT a blackboard

`PLAN.md` and `MapProgram` both called this "the stats blackboard". It loads no
blackboard art: it re-blits the **held `GAMEBG`** through `2321:068d` and puts
text over it, exactly as the briefing does. The blackboard is the *cutscene*,
`1b2e:1651`. Renamed here to stop the two being conflated again.

Layout, with the argument order the briefing settled -
`OutTextCentred(x0, x1, y, colour, mode, s)`:

| y | font | colour | mode | text |
|---|---|---|---|---|
| 38 | big | 159 | 3 | `Wave ` + N + ` Stats` |
| 41 | big | 159 | 3 | `___________` |
| 60 | label | 155 | 1 | `Molecule Chains` |
| 68 | small | 175 | 1 | chains **this wave**, `-0x17c` |
| 82 | label | 155 | 1 | `Total Molecule Chains` |
| 90 | small | 175 | 1 | running total, `-0x14e` |
| 104 | label | 155 | 1 | `Score` |
| 112 | small | 175 | 1 | the score longint at `-0x153` |
| 126 | label | 155 | 1 | `Perfect Bonus!` - only if `-0x17d` |
| 134 | small | 175 | 1 | `2500` - only if `-0x17d` |
| 135 **or 151** | label | 38 | 2 | `High Score!` - see below |

Two things are easy to miss and both change what is drawn:

- **the running total is accumulated here, in the draw code.** Between the two
  lines it does `-0x14e := -0x14e + -0x17c; -0x17c := 0`. The per-wave counter
  is zeroed by the screen that displays it, so the stats screen is not a pure
  view - re-rendering it would double-count.
- **`High Score!`'s `y` moves.** It is 135 normally and **151** when the
  Perfect Bonus lines are present, because those occupy the row it would use.

The high-score comparison reads `[0x1774]`/`[0x1776]` when `[0x1d4e] = 1` and
`[0x1900]`/`[0x1902]` otherwise - the two banks the title screen draws. **The
`mode = 1` arm is dead code**: `1000:a5f2` only calls `8da5` when the mode is
neither 0 nor 1, so the endurance bank can never be selected here. Recorded as
dead rather than ported, because porting it would invent a path the original
does not have.

Music is `STAT.MUS`, started after the drawing and before the flip. The tail is
the briefing's: `WaitKey(10)`, then `1b2e:0e37(0x1e)` unless the key was 1 or 2.

**Its background is the classroom, and that is now settled.** `2321:068d` is a
Mode X `REP MOVSW` blit whose arguments, taken off the listing rather than the
decompiler's reversed list, are `(x, y, srcPtr, w, h)` - `[BP+0xe]` is the `y`,
because it is multiplied by 80, the Mode X plane pitch, and `[BP+0x10]` is the
`x`. The stats screen passes **`x = 0`, `y = 12`**, and then calls
`1b2e:0a11`, the same scene routine the briefing uses. The held image is
`BLACKBRD.GFX`: 48644 bytes is exactly 320 x 152 plus a header, so it lands on
rows 12..163 and the slide at `y` 31..162 sits inside it. `GAMEBG` cannot be
the held image - it is 320x200 and does not fit at `y = 12`.

## The video pages: FOUR of them, and one is the eraser

`2321:0109` sets the mode and `2321:0000` lays out the pages, and between them
they answer every page question this project had open.

`2321:0109` is Mode X by the book: `INT 10h` mode `0x13`, sequencer `04 = 06`
to unchain, CRTC `11` write-protect cleared, `14 = 00` and `17 = e3` for byte
mode, `3c4/02 = 0f` for all four planes, then 64 KiB of `A000` zeroed. It ends
by storing the resolution as two variables rather than constants:

    DS:0x2382 := 320        DS:0x2384 := 200

`2321:0000` then derives everything else from them:

    pageBytes := (DS:0x2382 div 4) * DS:0x2384      { 80 * 200 = 16000 }
    DS:0x2364 := pageBytes
    DS:0x2362 := $10000 div pageBytes               { 65536 div 16000 = 4 }
    for i := 0 to DS:0x2362 - 1 do begin
      DS:0x2366[i] := $a000 + (pageBytes div 16) * i;    { the page's segment }
      DS:0x236e[i] := pageBytes * i                      { and its offset }
    end;
    DS:0x2376 := 0;  DS:0x2378 := 0;  DS:0x237a := 0

So there are **four pages** of 16000 bytes at offsets 0, 16000, 32000 and
48000 - 64000 bytes, which is why the mode's own 64 KiB window is the limit and
`DS:0x2362` comes out as 4 rather than being written down anywhere.

`DS:0x2376` is the page being DRAWN to, `DS:0x2378` the one being SHOWN, and
`DS:0x235e` / `DS:0x2360` are the current draw page's segment and offset,
cached so the blitters do not index the tables.

### The five entry points, with their argument order

Ghidra prints Pascal calls in the REVERSE of the push order, so every signature
below is read off the listing rather than the decompiler.

| address | signature | what it does |
|---|---|---|
| `2321:014f` | `Flip` | swap the draw page, point the CRTC at the one just drawn |
| `2321:019b` | `SetDrawPage(p)` | `DS:0x235e`/`0x2360` only - does NOT touch `DS:0x2376` |
| `2321:01b5` | `SetShownPage(p)` | programs CRTC `0x0c`/`0x0d` only |
| `2321:01e6` | `ClearPage(p)` | all four planes, `DS:0x2364` bytes of zero |
| `2321:020c` | `CopyPage(src, dst)` | whole page, latch mode |
| `2321:024d` | `CopyRect(src, dst, x, y, w, h)` | `y*80 + x div 4`, `w div 4 + 1` bytes a row |

`CopyRect`'s row width is `w div 4 + 1` - it rounds UP by a whole byte, so it
always copies up to three columns more than asked for. That is deliberate: the
callers pass a sprite's width and need the byte the sprite ends inside.

### Page 3 is the eraser, and that is the whole dirty-rect model

There is no per-sprite background save on the menu side. **Page 3 holds a clean
copy of the backdrop, and anything that moves is erased by copying its old
rectangle back from page 3.** Three routines do nothing else:

* `1b2e:0510`'s roll-down: `CopyRect(3, DS:0x2376, 57, 26, SLIDEBAR.w, 150)`
  before each new bar position;
* `1b2e:0b8f`'s jump: the same call around the professor's box;
* `1b2e:1188`, which is exactly `Flip; CopyRect(3, DS:0x2376, x, y, w, h);
  Flip` and nothing else - a four-argument "put the background back here".

`1b2e:0510` is where page 3 is built: it does `SetDrawPage(3); ClearPage(3);`
and draws `BLACKBRD.GFX` and the two navigation lines into it before switching
back to the live page for the professor and the bar.

**`DS:0x2058` is `BLACKBRD.GFX`**, loaded at `1000:ae0b` into the record
`{ptr @0x2058, w @0x205c, h @0x205e}`. It was carried as the stats screen's
"stand-in background" needing explanation; there is nothing to explain, it is
the blackboard, and the port already draws it at `(0, 12)`.

### Open: which page `1b2e:0656` snapshots FROM

`1b2e:0656` opens with `PUSH 0; PUSH 3; CALLF 2321:020c`, and by the argument
order above that is `CopyPage(src := 0, dst := 3)` - it overwrites the clean
stash with page 0. That reads backwards against everything else here, and the
honest position is that the direction is settled but **what page 0 contains at
that moment is not**. The two readings to separate on the rig are whether the
screens always compose their backdrop on page 0 (in which case this refreshes
the stash and is right), or whether `DS:0x2376` can be 1 by then (in which case
the reading of `2321:020c`'s parameters is wrong somewhere).

Do not port an erase model off this until it is settled. Nothing in the port
depends on it: the port composes whole frames.

### A scope correction on "the page flip is never executed"

The measurement section above records that `2321:014f` at image `0x1335f` got
**zero breakpoint hits** and concludes it "belongs to code this game does not
use". The zero was real and the conclusion was too broad. That probe ran during
PLAY, where `1000:3a67` flips inline; `2321:014f` is what the CLASSROOM screens
use, and `1b2e:0510`, `1b2e:084e`, `1b2e:0a11` and `1b2e:1188` all call it.
The claim holds for the game loop and not for the program.

## The classroom scene is `1b2e:0a11`, and three screens share it

The briefing `1000:86b8`, the stats screen `1000:8da5` and the Continue screen
`1000:8c38` all call it, which is why they look alike. It draws:

    FillRect(62, 26, 196, 145, 19)        { the frame behind the slide }
    FillRect(74, 31, 172, 132, 17)        { the slide }
    Draw(74,  31, ULCORNER)  Draw(242,  31, URCORNER)
    Draw(74, 159, LLCORNER)  Draw(242, 159, LRCORNER)

`2321:060b` is a filled rect and its argument order is
**`(x, y, w, h, colour)`**, settled by `1b2e:097a`: that procedure threads its
own two parameters into the same two slots the fixed call fills with `0x4a`
and `0x1f`. The four corner clips are `ULCORNER`/`URCORNER`/`LLCORNER`/
`LRCORNER.GFX`, each **20 bytes** - a header plus 4 x 4 - which is exactly the
`2321:0711(4, 4, ...)` the calls pass.

**`(74, 31, 172, 132, 17)` is the rectangle `main.cpp` carried for several
sessions as "MEASURED, NOT DECOMPILED".** The measurement was exactly right in
all five numbers. It is now derived, and the marking is gone.

### The slide DROPS, once per program run

`1b2e:0a11` gates a six-frame animation on `DS:0x210e`, which it then sets - so
this plays the first time any of the three screens is shown and never again.
`1b2e:097a(y, x)` redraws frame, slide and corners at a moving origin, each
frame held for **10 vertical retraces**:

    (62,30) (66,32) (72,30) (79,29) (75,37) (71,33)  ->  rest at (74,31)

It wobbles around the resting place rather than easing into it.

**This is the SLIDE, not the screen.** The note above used to gloss it as "the
projector screen being pulled down and bouncing", and `PLAN.md` went looking
for the missing roll-down on `DS:0x210e` because of that sentence. The roll is
a different animation in a different routine, and it is not gated on anything -
see the next section.

### The joke slide is `1b2e:084e`, and `FLASH.GFX` settles it on sight

The player reported this from play: "Lanny accidentally shows a WRONG slide -
he is flashing, wearing an Absolute Magic shirt under his lab coat." It was
filed as "not yet located", with the guess that it was an unattributed sprite
arm or a slide the extraction had read as text-only. It is neither. It is
`1b2e:084e`, which this file already quoted as "a one-in-twenty easter egg,
not ported" without knowing what the egg was.

**`FLASH.GFX` is 172 x 132** - the slide rectangle exactly - and what it draws
is the professor holding his lab coat open over an "AM" T-shirt, on a
vignetted photo of his own blackboard. `POINTERT.GFX` is the 28 x 21 that goes
with it: a startled face, stamped over the head of whichever pose is up. Both
were sitting in the resource table with no known consumer.

`1b2e:0a11` calls `1b2e:084e` on every scene redraw - so every slide change and
every screen entry - and it is gated twice:

    if (DS:0x210f = 0) and (Random(100) < 5) then begin
      Flip;  Delay($f);
      FillRect(74, 31, 172, 132, 17);  <the four 4x4 corners>
      Draw(74, 31, FLASH, 172, 132);             { 2000:389d, OPAQUE }
      Flip;  SetPage(DS:0x2376);
      Draw(267, 121, POINTERT, 28, 21);          { 2000:389d, over his face }
      Delay($1e);
      Draw(267, 121, POINTER[DS:0x20b0]);        { and his own head back }
      Delay($f);
      DS:0x210f := 1;
      PlaySound(DS:0x2124)                       { SLIDE.SFX }
    end

`DS:0x210f` is cleared once, by `1000:b1da` at start-up, and set here - so the
roll is one in twenty **per slide** but the gag happens **at most once per
program run**. That is why it is so rarely seen and why it took a player to
report it.

Two details a port gets wrong by default. The slide rect and the four corners
`1b2e:084e` lays down are the SAME ones `1b2e:0a11` draws immediately
afterwards, so the gag is wiped by its own caller and is on screen for its own
`$1e + $f` retraces - 0.64 s - and no longer. And `1b2e:084e` is a blocking
routine called *before* the key waits, so nothing else on the screen moves
while it runs: no mouth, no pointer.

Both stamps are OPAQUE (`2000:389d`), which matters for `FLASH.GFX` because its
vignette is black and masking it would let the blank slide through. Ported, and
both stamps diff pixel-exactly against the decoded resources. `--joke` forces
it, since waiting on a 5% roll is not a capture method.

### The projector SCREEN rolls down, in `1b2e:0510`

`1b2e:0510` is the routine that builds the classroom from nothing: it clears,
draws the blackboard, writes the two navigation lines, stands the professor up
at `POINTER0` - and then rolls the screen down. The loop is `i := 1 to 15` and
three calls wide:

    CopyRect(3, page, 57, 26, SLIDEBAR.w, 150)     { erase the bar's old row }
    FillRect(62, 26, 196, DS:0xb9c[i], 19)         { the screen, growing }
    Draw(57, DS:0xb9c[i] + 26, SLIDEBAR)           { the bar rides its edge }
    Flip;  Delay(3)

so it is exactly the rect and the bar `1b2e:0656` draws at rest, with the
height stepped. `DS:0xb9c` is `array[1..15] of word`:

    11  22  33  44  55  66  77  88  99  110  121  132  145  150  145

Twelve even steps of 11, the resting height, an **overshoot to 150**, and back -
a roller blind yanked down and bouncing once. Fifteen frames at `Delay(3)` is
`45/70` = **0.64 s**. The last entry is `DS:0xbba`, which is the word
`1b2e:0656` reads when it redraws the scene at rest, so `kFrameH` and the
table's tail are the same number by construction.

**Nothing gates it.** What varies is which screens call `1b2e:0510` at all:

| screen | builds with | rolls? |
|---|---|---|
| Instructions `1b2e:2d63` | `1b2e:0510` | yes, every time |
| Credits `1b2e:411b` | `1b2e:0510` | yes, every time |
| briefing `1000:86b8` | `if (wave = 1) and not replay then 0510 else 0656` | wave 1 only, and not after a Continue |
| stats `1000:8da5`, Continue `1000:8c38`, ending `1000:9499` | `1b2e:0656` | no |

Ported, with the roll-down and its bounce, and `--instructions` / `--credits`
open the screen the same way the menu does so a capture needs no other flag.

### `23e7:0024` is a VERTICAL RETRACE wait - which settles every timeout

    repeat
      repeat until (in(0x3da) and 8) = 0;
      repeat until (in(0x3da) and 8) <> 0;
      Dec(n)
    until n = 0;

So `n` is `n` VGA frames at Mode X's **70 Hz** - not milliseconds, and not the
game's 16.11 Hz simulation tick. That in turn settles `1b2e:0e37(param)`, which
runs `param * 7` iterations of `23e7:0024(10)`: `param * 70` retraces, so
**`param` seconds exactly**. The round number is the confirmation.

| call site | argument | wall clock |
|---|---|---|
| Continue screen, per tick | 2 | **2 s** (10 s for the whole five-count) |
| briefing, give up and continue | 0x1e | **30 s** |

### The professor is `1b2e:0656`, and he is TWO draws

**The note that annotates `1b2e:0656` as `{ music }` in the briefing's frame is
wrong** - it draws. It runs *before* `1b2e:0a11`, which is why the frame and
slide never paint over him: the frame spans `x` 62..257 and he stands at 267.

    if DS:0x20e3 = 0 then
      if DS:0x20c8 = 0 then begin
        DS:0x20b0 := 1;                      { reset the wave }
        Draw(267, 121, POINTER0)             { 2321:0711, masked }
      end else begin
        Draw(267, 165, BOOKS);  Draw(267, 100, CLAP[n])
      end
    else begin
      Draw(276, 165, BOOKS);  Draw(267, 94, JUMP[n])
    end;
    FillRect(62, 26, 196, DS:0xbba, 19);     { the frame, rolling }
    Draw(57, DS:0xbba + 26, SLIDEBAR)        { the bar rides its edge }

**The normal arm never draws `BOOKS.GFX`.** It is reached only from the clap
and jump arms, where he stands at a different height. Drawing it as well puts a
second stack 21 px too high, over his legs - which is what a row profile
against the capture showed.

The **sprite sizes settle the rest**, and they are the whole trick:

| resource | bytes | pixels | shape |
|---|---|---|---|
| `POINTER0.GFX` | 3480 | 3476 | **44 x 79** - the whole figure, legs and books |
| `POINTER1..3.GFX` | 1720 | 1716 | **44 x 39** - his upper body only |
| `POINTERT.GFX` | 592 | 588 | 28 x 21 |
| `BOOKS.GFX` | 1124 | 1120 | 40 x 28 |
| `SLIDEBAR.GFX` | 2292 | 2288 | 208 x 11 |

So the professor is **two draws stacked**: `1b2e:0656` lays down the full
`POINTER0` masked, and `1b2e:0e37` - the key wait - stamps a 44 x 39 wave frame
**opaquely** (`2321:068d`) over his top half. Drawing only the wave frame
erases him from the waist down; drawing it *masked* leaves the base pose's arm
showing through it. Both were tried and both are visible in a capture.

### The wave is a ping-pong, and it is why the key wait will not exit early

`1b2e:0e37` advances `DS:0x20b0` once per iteration - every ten retraces - so
**the professor waves while the game waits for a key**. The counter runs 1..5
and indexes a stride-8 bank at `DS:0x2078`, but entries 4 and 5 are not loaded
resources: `1000:af23` and `1000:af34` call `2000:7133`, a struct copy, to
alias them onto POINTER2 and POINTER1. The sequence is therefore

    1 -> POINTER1   2 -> POINTER2   3 -> POINTER3   4 -> POINTER2   5 -> POINTER1

a ping-pong, not a loop - he raises the pointer and lowers it. That also
explains the loop's exit condition, `until (answer <> 0) and (frame = 1)`: it
finishes the gesture before letting the screen change.

### The corners are UL / UR / DL / DR

`1000:aaba` loads `ULCORNER`, `URCORNER`, **`DLCORNER`**, **`DRCORNER`** into
`DS:0x20fe`, `0x2102`, `0x2106`, `0x210a`. `LLCORNER.GFX` and `LRCORNER.GFX`
also exist in the archive and are *not* these - a plausible guess that the load
table disproves. The loader is `2000:2264(name, ptrSlot, wSlot, hSlot)`; the
corners pass `DS:0x1d70` for both size slots, discarding them, because they are
known 4 x 4.

### Measured: the briefing is now pixel-exact

Against `capture/ref-briefing-wave1.png`, **whole screen**, not just the slide:

| stage | whole screen |
|---|---|
| blackboard at the origin, no scene | 29.64% |
| blackboard at (0, 12), frame + slide + corners | 3.73% |
| professor and roller bar added | 1.35% |
| the spurious `BOOKS` draw removed | 1.14% |
| `POINTER0` base + the wave frame over it | **0.00%** |

0.00% is at the animation phase the capture caught; sampling six phases gives
0.00%, 0.19% and 0.22%, and the residue is his arm moving. That is the same
band as the play field's 0.02..0.22%.

### A measurement that was measuring nothing

The briefing has been reported at "title band 0.00%, body 0.09%, whole slide
0.14%" for several sessions. Every one of those regions is **inside the white
slide**, where the port and the original agree by construction - a flat colour
17 rectangle with text on it. The scene *around* the slide was never in any
measured region.

Measured properly, the whole screen was **29.64%** differing, and the top 31
rows were **80%**, because the port blitted `BLACKBRD` at the origin instead of
at `y = 12`. Moving it to `(0, 12)` and adding the frame and corners takes the
whole screen to **3.73%**, rows 0..30 to **0.00%** and the left margin from
38.14% to **0.46%**. The residue is almost entirely rows 163..199 - the
professor and the roller bar.

The lesson is the one this file already states about searches, applied to a
metric: a number is only as good as the region it covers. Quote the region with
the percentage, and make sure the region includes the thing being claimed.

### He TALKS first, and that is `1b2e:0cd1` - the other key wait

`TALK1..5.GFX` were loaded by `1000:aaba` and drawn by no arm anyone had found,
which `PLAN.md` recorded as "a fourth behaviour somewhere". It is not a fourth
arm of `1b2e:0656` at all. It is a **second key wait**, and every screen that
holds for a key runs the two of them in order:

    k := 1b2e:0cd1(bursts);                  { he talks }
    if k = 3 then k := 1b2e:0e37(seconds);   { it timed out - now he waves }

| screen | talk | wave |
|---|---|---|
| Instructions `1b2e:2d63`, Credits `1b2e:411b` | `0cd1(35)` | `0e37($1e)` |
| briefing `1000:86b8` | `0cd1($17)` | `0e37($1e)` |
| stats `1000:8da5` | `0cd1(10)` | `0e37($1e)` |
| Continue `1000:8c38` | none | `0e37(2)` |

Both return the same codes, 3 being "ran out" - so the talk running out is what
starts the wave, and only the wave running out advances the screen.

The loop:

    repeat
      DS:0x20c6 := Random(12) + 1;         { where in the script to start }
      DS:0x20c7 := Random(4)  + 4;         { 4..7 mouths in this burst }
      repeat
        Delay(8);
        Draw(276, 133, TALK[script[DS:0x20c6]], 12, 8);      { 2000:3921 }
        DS:0x20c6 := DS:0x20c6 + 1;  if DS:0x20c6 > 12 then DS:0x20c6 := 1;
        <poll the keyboard into k>
        DS:0x20c7 := DS:0x20c7 - 1
      until (DS:0x20c7 = 0) or (k <> 0);
      bursts := bursts - 1;  if bursts = 0 then k := 3
    until k <> 0;
    Draw(276, 133, TALK3, 12, 8)           { closes on TALK3 on the way out }

**The parameter counts BURSTS, not mouths.** A burst is 4..7 mouths at 8
retraces each, so a slide's 35 is 16..28 seconds, not four. Reading it as
frames is the one mistake that makes the whole thing look wrong.

`DS:0xbbb` is `array[1..12] of byte` and dumps as

    1  2  3  3  3  5  2  3  1  2  3  3

so **`TALK4` is loaded and never scripted**, and `TALK3` is half the cycle -
which is what makes it read as speech rather than as a flicker.

Three things pin the drawing down independently:

* the size is a **literal in the call**, `12 x 8`, not the resource header -
  which is why the five records at `DS:0x20b2` hold a far pointer and nothing
  else, the loader throwing their width and height into the `DS:0x1d70`
  scratch. `TALK1..5.GFX` measure exactly 12 x 8, so literal and art agree;
* `(276, 133)` is inside the professor's own 44-wide box at `(267, 121)` - his
  mouth, nine right and twelve down from his origin;
* it draws through **`2000:3921`**, the masked thunk `1b2e:0510` uses for
  `POINTER0`, not the `2000:389d` the wave frames are stamped with. Loading the
  mouths opaque leaves three black columns beside his chin, because the 12 x 8
  the call passes is wider than the mouth in the art.

He does not gesture while he speaks: `1b2e:0cd1` never touches `DS:0x20b0`, and
`1b2e:0656` has parked it at the standing pose, so the mouth moves over
`POINTER0` and the pointer only comes up once the talk has run out.

### `1b2e:0e37` is the shared key wait, and its return codes matter

Reached as `1000:c117` from `8c38` - the same linear address, `0x1c117`, under
a different segment:offset, which is worth knowing before concluding there are
two routines. It waits out `param * 7` iterations of `23e7:0024(10)` while
cycling a five-frame animation, and exits only when it has an answer **and**
the animation is back on frame 1:

| returns | on |
|---|---|
| 1 | Enter or Space |
| 2 | ESC |
| 3 | the timer expiring |
| 4 / 5 | Down / Up |

The Continue screen acts on 1 and 2 only; 3, 4 and 5 fall through to its own
countdown. **The length of an iteration is now settled, and the port's
assumption was right.** `23e7:0024` is `Delay(n)` and its unit is the
VERTICAL RETRACE: the body is `23e7:0016` - wait for the current vblank to
end, then for the next to begin - with `LOOP` around it, at `0x3da` bit 3. So
`Delay(10)` is 10/70 s and `param * 7` iterations of it is `param` seconds
exactly, which is one count a second. This also fixes the unit for every other
hold in the game, the banners' `Delay($28)` included.

### `1000:8c38`, the Continue screen

    if parent.continuesLeft < 1 then exit;        { -0x14f, unsigned }
    PlayMusic('CONTINUE.MUS');                    { parent[-0x1e] }
    n := 5;
    repeat
      SetFont(big);
      OutTextCentred(0, 319, 70, 159, 3, 'Continue');
      OutTextCentred(0, 319, 73, 159, 3, '______');
      SetFont(small);
      OutTextCentred(0, 319, 90,  15, 2, Str(n));
      Flip;  [0x2376] := [0x2376] xor 1;  SetVisualPage([0x2376]);
      k := WaitKey(2);
      if k = 2 then exit;                         { declined - really over }
      if k = 1 then begin                         { accepted }
        Dec(parent.continuesLeft);
        parent.score := 0;                        { the longint at -0x153 }
        parent[-0x17e] := [0x1d51];               { drops back to the seed }
        parent.replay := 1;  parent.gameOver := 0;
        exit
      end;
      Dec(n)
    until n = 0;

So it is a **five-tick countdown**, the number on screen *is* the counter, and
letting it run out declines. This answers the open question above - it writes
drops, `-0x1fe` and `-0x1ff`, and zeroes the score but **not** the chain
totals.

### The session's seven songs are one array in `9e53`'s frame

`1000:a3cd`..`1000:a436` builds them with `2000:2451`, four bytes apart:

| slot | resource | used by |
|---|---|---|
| `-0x6` | `BRIEF.MUS` | the briefing |
| `-0xe` | `GAME.MUS` | play |
| `-0x12` | `FASTGAME.MUS` | play, the faster difficulties |
| `-0x16` | `VICTORY.MUS` | the Wave Complete banner |
| `-0x1a` | `DEATH.MUS` | the Game Over banner |
| `-0x1e` | `CONTINUE.MUS` | the Continue screen |
| `-0x22` | `STAT.MUS` | the stats screen |

`-0xa` is assigned elsewhere and is the *current* song, which is what the F3
toggle restarts. All seven names are present in `TUBES.RES`. This is the
"external standard in the decoded output" check: each screen's music matches
its purpose **by name**, which a wrong frame-offset reading could not produce.

### What this leaves open

* `1000:0000` (place N marked atoms), `1000:0236` (place N crystals) and
  `1000:035e` (pre-fill the beaker with N) are named but not read.
* `1000:9499`, reached on clearing wave 75.
* `1000:2dd0`'s F1 Help body and its F2 Save slot picker - the dispatch is
  read, the two screens are not.
* Where `-0x14f`, the number of Continues, is seeded.
* The beaker morph body at `1000:4bf6`.
* Whether the shareware really carries all 75 arms or the later ones are dead;
  the dispatch has them, and published notes claim the registered version
  "adds 50 waves".


## The Task Display is `1000:2a4a`, and it draws a full-size ball

The three fields the Task Display shows live in `1000:3a67`'s own frame, not
`9e53`'s, and are seeded from the objective at `1000:3ac7`:

    taskColour := reqColour;  if reqColour = 0 then taskColour := 8
    taskChain  := reqChain
    if waveMode = 4 then taskCount := counter else taskCount := 0

A required colour of **0 shows Flashium**, which has no sprite of its own and
whose table slot is rewritten every fourth frame - so a "form N chains using
any atom" wave shows a *cycling* ball. That is the same mechanism the wave 6
sampling caught and read as a rotating counter, and it needs no separate
explanation.

`1000:2a4a` itself:

    if (counter <> 0) and not mysteryHidden then begin
      case waveMode of
        4: Draw(ballTable[taskColour], 6, 10);
        5: Draw(ballTable[18], 6, 10);                  { the Crystal }
        6: Draw(ballTable[taskColour], 6, 10);
           Draw(marker, 8, 11)
        else Draw2894                                   { modes 2 and 3 }
      end;
      n := counter;
      if      n <= 9   then OutText(n, x 10, y 10, 127, mode 1)
      else if n <= 99  then OutText(n, x  6, y 10, 127, mode 1)
      else                  OutText(n, x  2, y 10, 127, mode 1)
    end

The three x values step by **4**, half the big font's advance, so the number is
**centred about x = 14** - which is the centre of a 16-wide ball drawn at x = 6.
Ball and number are the same height at the same y, so the number sits squarely
on the ball, exactly as the wave 6 capture showed ("a count overlaid on the
ball"). See "Sprite draw argument order" below: an earlier pass had these two
coordinates the wrong way round.

**Argument order, settled.** `OutText` pushes `x, y, colour, mode, text`, read
off the HUD's own drops draw at `1000:5782` - `PUSH 0x109` is x = 265, which
the port already renders correctly. Ghidra lists call arguments in *reverse*
push order, so a decompiled `049b(str, 1, 0x7f, 10, 10)` is `mode 1, colour
127, y 10, x 10`. Getting this backwards would have put the whole Task Display
on its side.

### `DS:0x200a` and `DS:0x1da6` are BOTH the Task Display, in different modes

An earlier note attributed the seven 8x7 sprites at `DS:0x200a` to the Task
Display on the strength of their size and count alone. Reading `1000:2a4a`
then showed it indexing `DS:0x1da6`, the ordinary 16x16 table, and the
attribution was withdrawn with `1000:2894` named as the candidate. Reading
`2894` settles it: **the candidate was right**, and both tables are the Task
Display's.

* modes **4, 5 and 6** - a count objective - draw ONE full-size ball from
  `DS:0x1da6`, in `2a4a` itself;
* modes **2 and 3** - a chain objective - call `1000:2894`, which draws
  **three small balls from `DS:0x200a`** in the shape of the required chain.

The loader at `1000:ad41` names them: `SRBALL`, `SGBALL`, `SBBALL`, `SCBALL`,
`SPBALL`, `SYBALL`, `SPNKBALL` - the seven colours again, in the same order,
half size. `1000:2894` is their only consumer in the game session; `1000:2dd0`
uses `[0x200a]` itself, type 1's, for something of its own.

The lesson stands even though the guess came out right. Shape and count made
the attribution *plausible*; only the draw site made it true, and in between it
was withdrawn for exactly the right reason.

### `1000:2894` - the chain illustration

    if taskChain = 0 then begin                    { diagonal }
      if not diagonalFlip then begin
        Draw(small[taskColour],  3,  9);
        Draw(small[taskColour], 17, 19)
      end else begin
        Draw(small[taskColour], 17,  9);
        Draw(small[taskColour],  3, 19)
      end;
      Draw(small[taskColour], 10, 14)
    end
    else if taskChain = 2 then begin               { vertical }
      Draw(small[taskColour], 10,  9);
      Draw(small[taskColour], 10, 14);
      Draw(small[taskColour], 10, 19)
    end
    else if taskChain = 1 then begin               { horizontal }
      Draw(small[taskColour],  3, 14);
      Draw(small[taskColour], 10, 14);
      Draw(small[taskColour], 17, 14)
    end

Three balls on a pitch of 7 across and 5 down, which is what an 8x7 sprite
wants. **This confirms the orientation numbering a fourth time, and visually
this time**: code 1 lays them out in a row and code 2 in a column, so 1 really
is horizontal and 2 vertical. And the **diagonal alternates direction** on a
flag at `[BP-0x1c1]`, so the illustration flips between the two diagonals
rather than committing to one - which is right, since both count.

### Sprite draw argument order, settled properly

`Draw` pushes `x, y, sprite`, and Ghidra lists call arguments in **reverse**
push order - so in a decompiled `Draw(spr_lo, spr_hi, A, B)` the **last**
argument is x and the second-to-last is y.

That is checked two ways. `2894` above only lays out as a row and a column with
this reading. And `2a4a` pushes `6, 10` for its ball and `8, 11` for the
`MARKER` over it - a difference of `(+2, +1)`, which is exactly the offset the
beaker draws `MARKER` at.

**This corrects the Task Display coordinates given earlier in these notes**:
the ball is at **(x 6, y 10)**, not (10, 6), and the marker at (8, 11). With
the ball spanning x 6..21 its centre is x = 14 - which is precisely the point
the count is centred about. Ball and number are the same 16 pixels tall at the
same y, so the number sits squarely on the ball, which is what the wave 6
capture showed.

### The Task Display cycles on the Flashium tick

`1000:486b`, the four-frame tick that rewrites Flashium's sprite slot, does
three more things - all of them presentation, all of them wave mode:

    Inc(flashTick);
    if flashTick = 5 then begin
      flashTick := 1;
      Inc(flashColour); if flashColour = 8 then flashColour := 1;
      ballTable[8] := ballTable[flashColour];

      if (reqColour = 0) or (reqColour = 8)
         or (waveMode = 2) or (waveMode = 4) or (waveMode = 6) then
        taskColour := flashColour;                   { 1000:48d0 }

      diagonalFlip := not diagonalFlip;              { 1000:48d8 }

      if (waveMode = 3) and anyOrientation then      { 1000:48e6 }
        if taskChain = 2 then taskChain := 0 else Inc(taskChain)
    end

So **when the wave requires no particular colour the Task Display ball cycles
the seven**, on the same clock and from the same variable as Flashium. That is
the rotating counter the wave 6 sampling measured and wrote up as an effect of
its own; it needs no separate mechanism, and mode 4 is in the list, which is
why wave 6 in particular showed it.

And **when the orientation is free the chain illustration cycles too**, through
diagonal, horizontal, vertical. The two rotations are the same idea applied to
the two halves of a task, and both are cosmetic: they move `taskColour` and
`taskChain` in `1000:3a67`'s frame, never `reqColour` or `reqChain`.

`2000:3b15` and `2321:0905` remain two different sprite entry points; nothing
here distinguishes them beyond the caller.


## The four wave-setup routines, and the Crystal's whole life

`1000:3a67`'s prologue calls three of them, at `3b51`, `3b65` and `3b7a`:

    if waveMode = 6 then PlaceMarked(marked)        { 1000:0000 }
    if waveMode = 5 then PlaceCrystals(crystals)    { 1000:0236 }
    if preFillBeaker then PreFill(preFillSize)      { 1000:035e }

and the fourth, the morph, is inline at `1000:4bf6` on the 720-frame clock.

All three share one idiom, and it is worth naming once because it appears five
times between them:

    repeat col := Random(6) + 1 until cells[1, col] = 0;   { a column with room }
    row := 1;
    while (row <> 5) and (cells[row + 1, col] = 0) do Inc(row);   { fall to rest }

Cells are `array[1..5, 1..6]` at `[BP-0x25]`, the marked plane at `[BP-0x43]`
and the objective plane at `[BP-0x61]`, so `cells[1, col]` is `[BP + col -
0x1f]` and a full column is one test.

### `1000:0000` - place N marked atoms, and cover them

    for n downto 1 do begin
      repeat col := Random(6)+1; row := Random(5)+1 until cells[row,col] = 0;
      while (row <> 5) and (cells[row+1,col] = 0) do Inc(row);
      cells[row, col]     := Random(8) + 1;      { 1..8 - Flashium included }
      objective[row, col] := 1                   { the MARKER overlay }
    end;

    k := 8;
    if markedCovered then                        { -0x187, wave 28 and friends }
      while k <> 0 do begin
        <pick a column with room, fall to rest>
        cells[row, col] := k mod 7 + 1;  Dec(k);
        Inc(col); if col > 6 then col := 1
      end;
    if markedXenon then                          { -0x188 }
      while k <> 0 do begin
        <the same>
        cells[row, col] := 11;  Dec(k);
        Inc(col); if col > 6 then col := 1
      end

Three things are not obvious. The marked atom's **type is rolled 1..8**, so a
marked cell can be a Flashium. The two modifier loops share **one** counter
`k`, seeded 8 once - so the second is dead if the first ran, which is harmless
only because `643b` and `6592` never set both flags. And both walk the columns
**round robin** from wherever the last placement left off rather than rolling
each time, which is what spreads the cover out instead of burying one column.

`k mod 7 + 1` for k = 8 down to 1 gives colours 2, 1, 7, 6, 5, 4, 3, 2.

### `1000:035e` - pre-fill the beaker

The same loop again, standing on its own:

    col := Random(6) + 1;
    while n <> 0 do begin
      <pick a column with room, fall to rest>
      cells[row, col] := n mod 7 + 1;  Dec(n);
      Inc(col); if col > 6 then col := 1
    end

with `n` = 8, the `-0x17b` seed. So "the beaker will already contain atoms"
means **eight**, spread one per column round robin, coloured 2, 1, 7, 6, 5, 4,
3, 2 - deterministic apart from the starting column.

### `1000:0236` - place N Crystals, and the record they get

    for i := 1 to n do begin
      crystal[i].arriving  := 1;
      crystal[i].departing := 0;
      crystal[i].step      := 0;
      repeat crystal[i].col := Random(6)+1; crystal[i].row := Random(5)+1
      until cells[crystal[i].row, crystal[i].col] = 0;
      while (crystal[i].row <> 5) and (cells[row+1, col] = 0) do Inc(row);
      cells[row, col]   := 18;
      crystal[i].active := 0;
      crystal[i].timer  := (dispenseInterval * 10 * i) div n
    end

**The Crystal has a record**, 10 bytes, `array[1..n]` based at `[BP-0x1f8]` of
`1000:9e53`'s frame. Element 0 would sit exactly on top of the objective block
- `-0x1f8` IS `rotateColour` - but only 1..n are ever touched, so the two
coexist. Anyone chasing these offsets should expect that overlap and not read
it as aliasing.

| offset | field |
|---|---|
| +0 | **active** - `1000:041c` tests this and clears it |
| +1 | **arriving** - the reverse fade is running |
| +2 | **departing** - the forward fade is running |
| +3 | fade step counter |
| +4 | col |
| +5 | row |
| +6, +7 | destination col, row |
| +8 | timer, a word |

`+0` and `+1` were the wrong way round on a first reading and are fixed here:
`1000:0236` writes `+0 := 1` first and `+1 := 0` last, and `1000:041c` tests
`+0` and clears it, so `+0` is the live flag.

The timers are **staggered**: `interval * 10 * i div n` spreads n crystals
evenly over one full period, so they never all jump at once.

### `1000:0560` - the Crystal teleports, and `CRFADE` is how

Called from `1000:47c4`, guarded by `waveMode = 5`, immediately **before** the
beaker update `1000:22a6`. Per crystal, per frame:

    if arriving then                        { record +1 }
      if cells[row,col] = 18 then arriving := false
      else Dec(cells[row,col], 19);          { walk the fade BACKWARDS }

    if departing then begin
      Dec(step);
      if step = 0 then begin
        col := destCol;  row := destRow;
        cells[row,col]  := 151;              { = 18 + 19*7, the LAST fade frame }
        marked[row,col] := 0;
        departing := false;  arriving := true;
        PlaySound(CRFADE)
      end
    end;

    if timer = 0 then begin
      timer := dispenseInterval * 10;
      tries := 10;
      repeat                                  { choose somewhere to go }
        destCol := Random(6) + 1;
        r := 1; while (r < 5) and (cells[r,destCol] = 0) do Inc(r);
        destRow := r + Random(5 - r);
        if (cells[destRow,destCol] <> 0) and (cells[destRow,destCol] < 11)
          then tries := 1;                    { an ordinary atom - take it }
        Dec(tries)
      until tries = 0;
      marked[row,col] := 1;                   { the fade pass animates it out }
      departing := true;  step := 7;
      PlaySound(CRFADE)
    end
    else Dec(timer)

So the animation the notes guessed at is exactly right, and now derived: the
crystal **marks its own cell** so the ordinary fade pass runs `CRFADE` forward
over it, then reappears at the destination on frame 6 of the same family and
walks it **backwards** to frame 0. One sprite family, played out and then in.
`CRFADE1` being its static sprite is not an oddity; frame 0 is where it rests.

Two consequences worth stating. The destination is chosen to be **an occupied
cell holding an ordinary atom**, and landing there **overwrites it** - so a
Crystal eats an atom every time it moves, which is what "contaminating the
beaker" means mechanically. And it moves every `interval * 10` frames, so it
speeds up with the wave progression exactly as the dispenser does.

### `1000:041c` - CORRECTION: this is the removal, not the teleport

`PLAN.md` and these notes both carried `1000:041c` as "the Crystal's teleport".
It is not. `1000:0e08`, the AntiMatter blast, calls it at `1000:0f47` with the
cell it is about to destroy:

    for i := 1 to crystalCount do
      if crystal[i].active and (crystal[i].col = col) and (crystal[i].row = row)
      then begin
        if counter <> 0 then Dec(counter);    { the wave objective }
        crystal[i].active := 0
      end

and the guard in front of it, at `1000:0f24`, is **`cell mod 18 = 0`** - not
the `cell mod 19 = 18` the gravity pass uses at `1000:272f`. The two tests
agree in practice only because types 11..17 have null fade pointers and so
never hold a value that is a multiple of 18 without being a Crystal. Both are
transliterated as written rather than unified.

That is the whole reason a crystal is "removed with Anti-Matter, never by
matching": nothing else calls it. The teleport is `1000:0560`, above.

### `1000:04ca` - and the record follows its cell down

The gravity pass calls it at `1000:2750` whenever a cell holding type 18 falls
a row, with the old and new positions; it finds the record at the old position
and rewrites its `col`/`row`. Without it a crystal that settled a row would
become invulnerable, because `041c` would look for it where it no longer is.

### `1000:4bf6` - the beaker morph, and it is a ROTATION

    for row := 1 to 5 do
      for col := 1 to 6 do
        if (cells[row,col] > 0) and (cells[row,col] < 8)
           and (marked[row,col] = 0) then begin
          Inc(cells[row,col]);
          if cells[row,col] = 8 then cells[row,col] := 1
        end;
    PlaySound(SELECT)

Every settled ordinary atom steps to the **next** colour, 7 wrapping to 1.
Specials, Flashium and anything mid-fade are skipped - the `< 8` test excludes
a fading cell for free, since a fading cell holds `type + 19*frame`.

The obvious guess would have been "each atom becomes a random other atom", and
it is wrong in a way that changes the mechanic completely. A uniform rotation
is a **permutation**, so every chain in the beaker survives it intact. What it
destroys is the player's *plan*: the atoms in the test tube and in the network
do not morph, so the two reds you were saving for the beaker's reds are now
looking at greens.


## The hidden-atom modifier, `-0x189` - and where it is NOT

"Form as many chains as you possibly can to live through N atoms **that are
hidden until they leave a tube**" is `1000:6fd5`, and the flag it sets has
exactly **six** readers in `1000:3a67`, all of the same shape:

    if hidden = 0 then Draw(ballTable[rec.type], rec.x, rec.y)
                  else Draw(ballTable[19],       rec.x, rec.y)

at `1000:4f7b`, `500e`, `5209`, `529c`, `5497` and `552a`. Type 19 is
`MYSTBALL`, at `DS:0x1df2`.

Chasing which record each site belongs to - by the type field it loads, at
`array base + 28*i + 0x0b` - gives records **1, 6, 2, 5, 3, 4**: the six
network slots, in the original's interleaved draw order, and nothing else.

**That is the whole modifier, and where it stops is the interesting half.**
`1000:3a67` makes 21 sprite-table draws. The six above are the only ones that
test the flag, so:

* the atoms travelling the tubes are concealed;
* the test tube's contents are **not**;
* records 7..12, falling out of the tube into the beaker, are **not**;
* the settled beaker is **not**.

So the concealment ends the instant an atom is caught, which is exactly what
the briefing says, and it is achieved without any per-atom state at all - one
global flag and a substituted sprite.

**`MYSTBALL` is therefore a rendering state, not a nineteenth ball**, and this
settles a question the type table left open. An atom keeps its real type
underneath the whole time: it still matches, still counts, still fires its
special. The earlier speculation that a `?` might *resolve* into one of the
letter balls on capture has nothing behind it and can be dropped.


## The briefing screen's presentation half, `1000:86b8`

The dispatch half is above, under "Wave mode, decompiled". This is everything
around it, and it is all literals.

### The frame

    if a background is already held then Free it;               { 21ea:065c }
    repeat n := Random(10) + 1 until n <> DS:0x2056;            { never twice }
    DS:0x2056 := n;
    Load('GAMEBG' + Str(n) + '.GFX');                           { 21ea:03c4 }
    if error then Abort('Game Background ResourceError');
    DS:0x2376 := 0;                                             { page 0 }
    ... blit the background ...                                 { 2321:068d }
    if (wave = 1) and not replay then 1b2e:0510 else 1b2e:0656; { music }
    1b2e:0a11;
    SetFont(big);                                               { 23e7:013b }
    OutTextCentred(0, 319,  45, 159, 3, 'Wave ' + Str(wave));
    OutTextCentred(0, 319,  48, 159, 3, '____________');
    SetFont(small);
    <zero the twelve flags, then dispatch to the objective routine>
    OutText(76, 150, 155, 1, 'You are allowed ' + Str(drops) + ' drops.');
    Flip;                                                       { 2321:014f }
    k := WaitKey(0x17);                                         { 1b2e:0cd1 }
    if (k <> 2) and (k <> 1) then k := 1b2e:0e37(0x1e);
    replay := false;                                            { -0x1ff := 0 }

The background is **re-rolled until it differs from the last one**, which is
what stops two briefings in a row sharing a backdrop. `DS:0x2056` is the only
thing that remembers it.

### Text argument order, confirmed at the source

`1000:630b` settles it beyond argument, because the pushes are literal:

    PUSH 0x4c        x = 76
    PUSH 0x55        y = 85
    PUSH -0x65       colour = 0x9b = 155
    PUSH 0x1         mode = 1
    PUSH CS / PUSH DI    the string
    CALLF 2000:36ab

and the same routine's illustration pushes `x = 150, y = 110` for the ball and
`x = 152, y = 111` for the `MARKER` over it - the `(+2, +1)` the beaker uses.
So `OutText(x, y, colour, mode, text)` and `Draw(x, y, sprite)`, first pushed
first, and Ghidra's argument lists are the reverse of that.

### The layout, every routine

Body text is **x = 76 (sometimes 75), colour 155, mode 1**, on a **10-pixel
line pitch**. The composed line - the one carrying the count - is
`OutTextCentred(0, 319, y, 155, 1, ...)`.

The one thing that is not uniform is worth having: **the modifier sentence is a
different colour**. Every template that carries one draws its objective in
**155** and its modifier in **169**, at x = 75:

| routine | objective lines (y) | modifier lines (y, colour 169) |
|---|---|---|
| `62f1` marked | 85, 95 | - |
| `643b` marked-covered | 75, 85, 95, 105 | - |
| `6592` marked-xenon | 70, 80, 90, 100, 110 | - |
| `66cb` crystals | 75, 85, 95 | - |
| `67d8` flashium | 95, 105 | - |
| `68b7` shown-atom | 85 | - |
| `6ede` survive | 85, 95 | - |
| `6fd5` survive-hidden | 80, 90, 110 | - |
| `7100` survive-disabled | 80, 90 | 125, 135 |
| `72ad` td-colour | 90, 100 | 115, 125, 135 |
| `744d` td-chain | 80, 90, 100 | 115, 125, 135 |
| `764b` td-both | 80, 90, 100 | 115, 125, 135 |
| `7802` td-colour-45s | 90, 100 | 115, 125 |
| `798b` td-chain-45s | 80, 90, 100 | 115, 125 |
| `7b72` td-both-45s | 80, 90, 100 | 115, 125 |
| `7cce` any | 95, 105 | - |
| `7de3` any-prefill | 90, 100 | 115, 125 |
| `7f42` any-morph | 90, 100 | 115, 125, 135 |
| `8056` horiz-any | 125, 135 | - |
| `81bb` vert-any | 125, 135 | - |
| `8320` diag-any | 125, 135 | - |
| `8581` mystery | 70, 80, 90, 100, 110, 120, 130 | - |

### The illustrations are FULL-size balls

Unlike the Task Display's, the briefing draws from `DS:0x1da6` at a pitch of
**20 across and 15 down**:

| routine | balls |
|---|---|
| `62f1` marked | one at (150, 110) with `MARKER` at (152, 111) |
| `66cb` crystals | one Crystal at (150, 110) |
| `68b7` shown-atom | one at (152, 110) - the atom the wave wants |
| `69e4` horiz-colour | (130, 110) (150, 110) (170, 110) |
| `6b5d` vert-colour | (150, 95) (150, 110) (150, 125) |
| `6cd6` diag-colour | **six** - (100,95) (115,110) (130,125) and (205,95) (190,110) (175,125) |
| `8056` horiz-any | (130, 95) (150, 95) (170, 95) |
| `81bb` vert-any | (150, 80) (150, 95) (150, 110) |
| `8320` diag-any | **six**, the same X one row higher |

The two diagonal templates draw **both** diagonals side by side rather than
one, which is the briefing saying "either direction" in pictures - the same
thing the scoring does by giving both diagonals one counter.

### What is left: the text itself

Everything above is geometry and can live in this repository. The briefing
**prose cannot**: it is the game's own text, which is why the template list is
kept in `wave-templates.md` outside this tree. The strings sit in the code
image at roughly `0x5f00`..`0x8700`, as Pascal ShortStrings, and the shipped
`TUBES.EXE` is LZEXE-packed, so reaching them at runtime means unpacking the
user's own executable the way `tools/unpack.sh` does offline.

### RESOLVED by capture: it is a blackboard, not the game backdrop

The open question - whether colour 155 was right, or whether each `GAMEBG`
carried its own palette - was the wrong question, and one cheap check killed it
before the capture: **the archive holds exactly three palettes**, `INTRO.PAL`,
`SOFT.PAL` and `TUBES.PAL`, against 73 `.GFX`. Images do not carry palettes
here, so the gameplay palette is the only candidate and 155 is what it says:
`#000071`, a dark blue.

Capturing the original's wave 1 briefing through the rig settles the rest, and
the answer was in the *background*. The briefing is drawn on **`BLACKBRD.GFX`**
- a 320x152 classroom scene - with a **projector slide** over it and the
professor beside it. Dark blue on a near-white slide, entirely legible. The
port was drawing it over the play backdrop, which is what made it look wrong.

**`1000:86b8` does not blit `GAMEBG` at all.** It loads it into `[BP-0x86]`,
and `1000:3a67` blits that at `1000:3c0b` - so the briefing is loading the
backdrop for the wave that is *about to start*, which is also why `DS:0x2056`
remembers the last one: no two consecutive waves share a backdrop.

Reference capture kept at `capture/ref-briefing-wave1.png` in the tooling
directory.

### Measured against that capture

Over the slide, `x 74..245`, `y 31..162`:

| band | differing |
|---|---|
| body text, `y 62..163` | **0.09%** |
| title band, `y 45..62` | **19.9%** |
| whole slide | 2.70% |

So the transliterated prose, its positions and its two colours are right to
within sixteen pixels, and the text is confirmed verbatim - wave 1 reads "Form
2 chains using any atom to advance to the next wave." over "You are allowed 9
drops.", with `chainTargetColour` 2 and Tubes 101's 9 drops.

**The title band: SOLVED, at 0.00%.** It was the font after all, and it took
two wrong turns to get there. Both are recorded because each was a bad *method*,
not just a bad answer.

The band read as glyphs "two rows taller" with a thick bright rule. Two causes,
compounding:

1. **The wrong font file.** It is `STARTREK.816`, not `FUTURE.816` - see "the
   big font is a SLOT" below. The port was drawing `Wave 1` in the HUD's font.
2. **The wrong peak row.** `2000:3fab`'s last argument is **8**, and
   `decodeFont` adds one, so `peakRow` is 9 and the port was passing 7.

Fixed, the ramp matches row for row - 65, 89, 113, 138, 166, 190, 215, 239,
then back down - and the band goes **19.9% -> 0.00%**, the whole slide
**2.70% -> 0.14%**.

**Wrong turn one: "changing the mode from 3 to 1 makes no difference, so it is
not the mode byte."** True, and useless: the mode was never wrong. Ruling out
one suspect is not evidence about the others.

**Wrong turn two, and the bad one.** A session "refuted" the font theory by
scoring each `.816` font's glyph bitmap against the capture - the same method
that had just worked on the title screen - and reported that `FUTURE.816` fit
best and nothing was decisive. **The test was broken.** Its "is this pixel lit"
predicate was `r+g+b > 200`, written for the title screen, which is *bright text
on black*. The briefing slide is **dark blue text on light grey**, so the
predicate selected the background. Every font scored badly because every font
was being compared against the wrong pixels.

Run with `b > r + 60`, the same test returns `STARTREK.816`, advance 8, `y 45` -
**187 of 187** lit pixels accounted for, at exactly the `y` the port already
used. Decisive, and it had been decisive all along.

The tell was there in the numbers: *no* font fitting well is not a finding when
the text is plainly rendered from one of them. That is this file's own rule -
when a search comes back empty, suspect the search - applied to a search that
came back uniformly mediocre.

### Still not drawn

The projector slide is a **measured** rectangle - palette index 17 over
`x 74..245, y 31..162`, read off the capture rather than decompiled, and marked
as such in `main.cpp`. `2000:389d` at `1000:8774` and `1000:bcf1` are the two
routines that would settle it; `SLIDEBAR.GFX` is a 208x11 roller bar, so
`bcf1` is almost certainly the screen rolling down.

Also absent: the professor - `1000:86b8` calls `1000:b7f0` on wave one and
`1000:b936` otherwise - and `POINTER0..3`, `POINTERT`, `BOOKS.GFX`, `TALK1..5`
are his sprites. And the background is not yet re-rolled `Random(10)+1` against
`DS:0x2056`, nor is the key wait `1b2e:0cd1`'s two-key protocol.

---

## The title screen is `1b2e:52bf`, and the menu is nested inside it

`MapProgram.java` labelled `1b2e:52bf` "title / main menu" from the strings it
references. That is half right, and the half it gets wrong matters: `52bf` is
the **title/attract screen**, and the menu is a *nested Pascal procedure*
inside it at `1b2e:4d80`, sharing its frame through the static link at `[BP+4]`
- the same arrangement as `1000:3a67` inside `1000:9e53`.

So they decompile as a pair, and neither reads correctly alone. `4d80` takes no
arguments and addresses everything through `in_stack_00000002`, which is the
parent's `BP`.

### What `52bf` actually does

Before the loop it draws the credits block: **eleven** records, tested for a
non-empty first byte and drawn only if present.

    0x1928  0x1978  0x19c8  0x1a18  0x1a68        (bank 1, stride 0x50)
    0x1b08  0x1b58  0x1ba8  0x1bf8  0x1c48        (bank 2, stride 0x50)

Each is a name at `+0` (padded to 20 with `2591:008a`) and a number at `+0x28`.
That is the same `bank + slot*0x50` shape the save slots use, so the two banks
here are the two high-score tables - one per game mode.

### The title screen is a tube network spelling TUBES

The four filenames it loads are at `1b2e:5238`:

    TUBESBG.GFX   TUBESFG.GFX   TUBES.MUS   SELECT.SFX

A **background/foreground pair**, which is the `GAMEBG`/`GAMEFG` arrangement
from the play field, and in the frame loop the foreground is re-stamped over
the atom's box after the atom is drawn. So the title screen is not a logo with
a sprite bouncing over it: it is a **tube network in the shape of the word
TUBES**, in blocky connected letters, with an atom travelling *inside* the
pipes and clipped by the foreground exactly the way a falling atom is clipped
in `1000:3a67`. This was first described by the player and the file pair
confirms it - the earlier "bouncing along a scripted path" reading here was
wrong, and wrong in a way that would have produced a plausible, entirely
inauthentic title screen.

The path is a scripted walk of that network:

    dirV := [0x95 + step]        'F' | 'B'      { forward / back }
    dirH := [0xaf + step]        'L' | 'R' | 'U' | 'D'
    limX := [0x30 + step*2]
    limY := [0x62 + step*2] + 0x20

`step` runs 1..0x19 and wraps to 1, so the walk is **25 legs** - the strokes of
the five letters. Each leg moves 4 px a frame along `dirH` until it passes its
limit, and within 10 px of the limit the cross-axis is displaced by 7, 6, 3, 2,
1, which is what rounds the **corners of the letterforms** rather than turning
square. Leg 10 additionally re-stamps foreground at `x` 0xb0 and 0xc8, which is
a crossing where one stroke passes over another; the exact rect wants the
listing, since the argument order of `2321:0874` is not settled here.

The four tables are **one contiguous block, `0x30`..`0xc9`**, and the menu page
table begins immediately after at `0xca`. `0x95`, `0xaf` and `0xc9` are all
`0x00` - the unused element 0 of each byte table, and the terminator - which
brackets the block on both sides and confirms the strides.

    leg  dH dV  limX limY      from        to
      1   D  B    61  127    ( 61, 53) -> ( 61,127)     U  left stroke
      2   R  B    94  127    ( 61,127) -> ( 94,127)        bottom
      3   U  F    94   34    ( 94,127) -> ( 94, 34)        right stroke
      4   R  F   120   34    ( 94, 34) -> (120, 34)     -> B
      5   D  B   120  127    (120, 34) -> (120,127)     B  spine
      6   R  B   166  127    (120,127) -> (166,127)
      7   U  B   166   73    (166,127) -> (166, 73)        lower bowl
      8   L  B   139   73    (166, 73) -> (139, 73)
      9   D  B   139  100    (139, 73) -> (139,100)
     10   R  B   224  100    (139,100) -> (224,100)     -> E, crossings
     11   U  B   224   73    (224,100) -> (224, 73)
     12   L  B   190   73    (224, 73) -> (190, 73)
     13   D  B   190  127    (190, 73) -> (190,127)
     14   R  B   280  127    (190,127) -> (280,127)     -> S
     15   U  B   280  100    (280,127) -> (280,100)
     16   L  F   245  100    (280,100) -> (245,100)
     17   U  F   245   73    (245,100) -> (245, 73)
     18   R  F   303   73    (245, 73) -> (303, 73)
     19   D  F   303  147    (303, 73) -> (303,147)     down to the return run
     20   L  F     1  147    (303,147) -> (  1,147)     the return run, y 147
     21   U  F     1   33    (  1,147) -> (  1, 33)
     22   R  F    31   33    (  1, 33) -> ( 31, 33)     T  left half of the bar
     23   D  ?    31  127    ( 31, 33) -> ( 31,127)        stem, down
     24   U  F    31   33    ( 31,127) -> ( 31, 33)        stem, back up
     25   R  F    61   33    ( 31, 33) -> ( 61, 33)        right half of the bar

Two things fall out of the trace and both are checks on the reading:

- **the loop closes exactly.** Leg 25 ends at `x = 61` and leg 1 immediately
  descends from `x = 61`. The initial `(61, 53)` is simply a point part-way down
  leg 1, so the walk is a closed circuit with no seam.
- **leg 23's `dirV` is `0x3f`, not `'F'` or `'B'`.** The code tests only for
  those two, so *neither* corner-rounding branch fires on that leg. It is not
  corrupt data: legs 23 and 24 are the T's stem travelled down and then straight
  back up, which is a reversal rather than a turn, and rounding it would bulge
  the stem sideways. The one leg in the table that must not curve is the one
  leg whose `dirV` byte is neither value.

Legs 20 and 21 run along `y = 147` and `x = 1`, well outside the letters, so the
network includes a **return run** below and around the word that carries the
atom back to the T.

### The menu highlight is two turning stars

In the draw half, guarded on the menu-is-up flag `[0x1d42]`:

    [0x1d78] := [0x1d78] + 1;                 { 1..3, the divider }
    if [0x1d78] = 4 then begin
      [0x1d78] := 1;
      [0x1d79] := [0x1d79] + 1;               { 1..4, the frame }
      if [0x1d79] = 5 then [0x1d79] := 1
    end

so the star turns through four frames every three frames, a 12-frame cycle. It
is blitted **twice** per frame at one shared `y` (`0x1da2`) and two `x`
(`0x1d9a`, `0x1d9e`) - one star either side of the selected item. All three are
recomputed from `0x1d72`/`0x1d74`/`0x1d76` by `func_0x0002f8e7`, which is
therefore "place the stars against item N" and is called both at entry and on
every selection change inside `4d80`.

**Not settled:** Ghidra renders the sprite lookup as `[0x1d79*8 + 0x1d76]`,
which collides with `0x1d76` used as the y. One of the two readings is a
decompiler artifact; the stride wants the listing, not the decompiler.

The loop ends one of two ways, and returns the reason in `AL`:

- **timeout.** `local_1c6` seeds at `0x2d0` = **720 frames**, is reset to 720 on
  every keypress, and on reaching zero returns **9**. Nine is not a menu item;
  it is the attract-mode arm, which is how `DEMO.SCR` gets played without a
  keypress.
- **a menu choice**, returned as the item number that `4d80` stored.

### The two-key protocol, `DS:0x1d42`

`52bf` reads both the joystick and the keyboard, and both go through the same
gate:

    if [0x1d42] = 0 then begin                  { menu not up yet }
      if key in [ESC, SPACE, RETURN] then begin  { or joy button 1 or 2 }
        [0x1d42] := 1;                           { raise the menu }
        key := 0                                 { and SWALLOW the key }
      end
    end
    else Menu(...)                               { 1b2e:4d80 }

So the first press only raises the menu, and is deliberately discarded so it
cannot also select an item. `[0x1d42]` is the menu-is-up flag, and everything
in the draw half is guarded on it. Every accepted press also sets a 4-frame
repeat lockout in `local_1c9`.

### The menu is one table of seven pages

`4d80` switches on the current page in `[BP-3]` and the highlighted item in
`DS:0x1d44`, and changes page by calling `func_0x0002f95a(count, ptr)`. The
seven pointers it passes are

    0x00ca  0x0256  0x03e2  0x056e  0x06fa  0x0886  0x0a12

which are **evenly spaced by 0x18c**, so this is not seven tables but one:

    MenuPages: array[1..7] of array[0..10] of string[35];   { at DGROUP 0x00ca }

with stride 0x24 per entry and 0x18c per page, entry 0 the page **title** and
1..10 the items. The page number satisfies `(ptr - 0xca) div 0x18c + 1` at every
call site - but see *SetMenuPage* below: it is **passed as a parameter**, not
computed, so that formula is a check on this reading rather than the mechanism.
Read out:

| # | Title | Items |
|---|---|---|
| 1 | *(none)* | Start Game / Continue Saved Game / Game Options / High Scores / Instructions / View Demo / Credits / Exit Tubes |
| 2 | `Game Mode` | Endurace Mode / Wave Mode / Exit |
| 3 | `Difficulty` | Tubes 101 / Tubes 201 / Tubes 301 / Exit |
| 4 | `Saved Games Available` | five slots / Exit |
| 5 | `Saved Games Available` | five slots / Exit |
| 6 | `Game Options` | Toggle Music / Toggle Sound FX / Redefine Input Device / Exit |
| 7 | `Exit Tubes?` | Yes / No |

`Endurace Mode` is the game's own spelling and is reproduced as such. Pages 4
and 5 hold `(Unavailable)` in the image; the live text is copied over each slot
from the save records at `0x18d8` (endurance) and `0x1ab8` (wave), `slot*0x50`,
by the `2685:08e3` block move of 0x50 bytes into `0x1ce8`.

### The transitions, and the three globals they set

    page 1  Start Game            -> [0x1d4c] := 1; page 2
            Continue Saved Game   -> [0x1d4c] := 0; page 2
            Game Options          -> page 6
            Exit Tubes            -> page 7
            anything else         -> leave, returning the item number

    page 2  Endurace Mode         -> [0x1d4e] := 1; page 3 if [0x1d4c]=1
                                                    else page 4
            Wave Mode             -> [0x1d4e] := 2; page 3 if [0x1d4c]=1
                                                    else page 5
            Exit                  -> page 1

    page 3  Tubes 101/201/301     -> [0x1d4f] := 0/1/2; leave, returning 1
            Exit                  -> page 2

    page 4  slot 1..5             -> [0x1d4d] := slot; copy the record;
    page 5                           leave returning 2 if the record is live
            anything else         -> page 2

    page 6  Toggle Music          -> stop or restart the song
            Toggle Sound FX       -> [0x215e] toggled
            Redefine Input Device -> a sub-procedure
            Exit                  -> page 1, and 227b:007a

So the three globals the game session already reads are all set here, and
nothing else sets them:

| Global | Meaning | Set by |
|---|---|---|
| `DS:0x1d4c` | new game (1) vs load (0) | page 1 |
| `DS:0x1d4d` | save slot 1..5 | pages 4, 5 |
| `DS:0x1d4e` | game mode: 1 endurance, 2 wave | page 2 |
| `DS:0x1d4f` | difficulty 0..2 = Tubes 101/201/301 | page 3 |

That closes the loop on `DS:0x1d4f`, which was already known to seed the 9/6/3
drops and to be forced to 2 by the View Demo arm at `1000:b272`.

### `1b2e:0cd1` is a press-any-key prompt, not the menu's key wait

It is 358 bytes and does its own thing: it flips the draw page (`[0x2376] xor
1`), seeds `[0x20c6]` with `Random(12)+1` and `[0x20c7]` with `Random(4)+4`,
then spins an atom through frames 1..12 out of the table at `DS:0xbbb` at
`(0x85, 0x114)` until a key arrives. It returns 5, 4, 1 or 2 for four different
keys rather than a single flag, so callers can distinguish them - which is why
the briefing needs it rather than a bare `ReadKey`.

### The blit family, and its argument order - settled

Four routines in `2321` do all the drawing, and they share a calling shape.
Pascal pushes left to right, so with `RETF n` the **first** source argument sits
at the **highest** `BP` offset. Ghidra prints call arguments in reverse push
order, so its rendering of these calls reads backwards.

| Routine | `RETF` | Source args | Inner loop | Source shape |
|---|---|---|---|---|
| `2321:07e4` | 8 | `(x, y, w, h)` | `REP MOVSW/MOVSB` | screen-shaped |
| `2321:0874` | 8 | `(x, y, w, h)` | `LODSB; OR AL,AL; JZ` | screen-shaped |
| `2321:0711` | 0xc | `(x, y, src, w, h)` | `LODSB; OR AL,AL; JZ` | **packed** |
| `2321:0905` | 8 | `(x, y, ?, proc)` | `CALLF [BP+6]` | indirect |

In all of them `[BP+0xc]`-or-`[BP+0x10]` is `x` (`SHR DI,2` - Mode X plane
column), the next word down is `y` (`MUL 0x50`), then `w` (`SHR 2; INC`) and
`h` (the outer `DEC BX; JNZ` counter).

Two distinctions that matter:

- **`07e4` is opaque, `0874` and `0711` are masked** - the latter two skip
  palette index 0, which is what makes a sprite transparent.
- **`07e4` and `0874` add the row stride to *both* `DI` and `SI`**, so their
  source is a full 320-wide screen-shaped buffer and they blit a *rect out of
  it*. `0711` advances only `DI`, so its source is a **packed** sprite, `w`
  bytes per row, passed as an explicit far pointer rather than taken from the
  global at `DS:0x238e`.

So `TUBESBG`/`TUBESFG` are screen-shaped and go through `07e4`/`0874`, and the
stars are packed sprites and go through `0711`.

Re-reading the title loop with the order fixed:

    Blit(x, y, 16, 13)          { the atom's foreground stamp }

**16 wide by 13 tall - the game's own cell size**, which is a check on the
reading rather than a coincidence. And leg 10's two extra calls are

    Blit(0xb0, y, 1, 0xd)       { 1 px wide, 13 tall, at x = 176 }
    Blit(0xc8, y, 1, 0xd)       {                        x = 200 }

with `DS:0x238e` pointed at **TUBESBG** for the duration and restored to
TUBESFG afterwards, so those two columns get *background* where every other
column gets foreground. Reading that as "the atom shows through at the two
crossings" is inference; the exact visual wants a render.

### The star sprite, and `0x1d76`'s double duty - settled

From the listing at `1b2e:60cf`, which is authoritative here because the
decompiler's rendering looked self-contradictory:

    PUSH [0x1d72]           { x - the LEFT star }
    PUSH [0x1d76]           { y }
    DI := [0x1d79] * 8
    PUSH [DI + 0x1d78]      { source segment }
    PUSH [DI + 0x1d76]      { source offset  }
    PUSH 0xc                { w = 12 }
    PUSH 0xa                { h = 10 }
    CALLF 2321:0711
    ... and again identically with [0x1d74], the RIGHT star

So the frame-`f` sprite pointer lives at `DGROUP:0x1d76 + f*8`, `f` in 1..4,
and the star is **12 x 10**, packed and masked.

`0x1d76` really is used two ways - as a bare scalar it is the stars' `y`, and
as `[DI + 0x1d76]` it is the base of a stride-8 pointer array whose element 0
would land on that same `y`. Ghidra was not confused; the code is genuinely
written that way, and since `f` is never 0 the overlap is harmless. This is the
third table in this screen with an unused element 0 - the menu pages and the
path tables are the others.

The rest of the block, read off the same listing:

| Address | Holds |
|---|---|
| `0x1d72` | live x of the left star |
| `0x1d74` | live x of the right star |
| `0x1d76` | live y of both |
| `0x1d78` | frame divider, 1..3 |
| `0x1d79` | frame number, 1..4 |
| `0x1d7e + (f-1)*8` | far pointer to star frame `f` |
| `0x1d9a`, `0x1d9c` | left-star x **as last drawn on page 0 / page 1** |
| `0x1d9e`, `0x1da0` | right-star x, per page |
| `0x1da2`, `0x1da4` | y, per page |

The per-page copies exist because the screen is double buffered (`[0x2376]` is
the draw page and is flipped with `xor 1`): each page has to restore background
over wherever *it* last drew, not wherever the other page did.

### `1b2e:4607` - PlaceStars

    procedure PlaceStars(var y, xRight, xLeft: word);   { RET 0xe, near }

Called at entry and after every selection change. With `i = [0x1d44]`, the
highlighted item, and `L` the **length byte** of menu entry `i` - read at
`parent[-0x190] + i*36`, the 36-byte stride confirming the page-table layout
from the other side:

    xLeft  := 140 - 4*L
    xRight := 163 + 4*L
    y      := yBase + i*16 + 2      { or i*26 + 2 on page 6, Game Options }

So the stars bracket the centred item text and move outward as it lengthens,
four pixels a character - half an 8-wide glyph on each side.

**Measured against the original** - `capture_title.py`, five captures across
three pages and three different selections - the `y` is exact in every one, and
the `x` is exact once you account for the star's own frame: the blit is 12 wide
but the lit artwork inside it starts one to three pixels in depending on which
of the four frames is up, and the *same* inset appears on both stars in every
capture, which is what confirms it is the sprite content rather than the
placement.

| capture | measured xL, xR, y | formula |
|---|---|---|
| main menu, `Start Game` | 102, 205, 36 | 100, 203, **36** |
| Game Mode, `Endurace Mode` | 91, 218, 76 | 88, 215, **76** |
| Difficulty, `Tubes 101` | 106, 201, 68 | 104, 199, **68** |
| Difficulty, `Tubes 201` | 105, 200, 84 | 104, 199, **84** |
| Difficulty, `Tubes 301` | 107, 202, 100 | 104, 199, **100** |

### `1b2e:467a` - SetMenuPage

    procedure SetMenuPage(pageId: byte; page: pointer; count: byte);
    { RET 0xa, near - 5 words including the static link }

Call site, `1b2e:5332`:

    PUSH 0x1 / PUSH DS / PUSH 0xca / PUSH 0x8 / PUSH BP / CALL

What it does:

- `REP MOVSB` **0x18c bytes** - one whole page, 11 entries of 36 - from the
  argument into the parent's `[BP-0x190]`, via `2000:7133`;
- `parent[-4] := parent[-3]` then `parent[-3] := pageId`, so `[-4]` is the
  **page to go back to** and `[-3]` the current one;
- `[0x1d44] := 1`, *except* that arriving at page 1 restores the remembered
  main-menu item from `[0x1d43]`;
- `parent[-0x191] := count`;
- then **`INC byte ptr [BP+6]`** - the count is bumped *before* the next step,
  which is easy to miss and changes the answer by six pixels;
- `parent[-0x194] := (180 - 16*(count+1)) div 2`, the y origin - the block is
  **vertically centred in 180 rows at 16 px a row**, or 26 px a row on page 6.
  The `+1` is the page **title**, entry 0, which occupies a row of its own, so
  a page of `n` items is laid out as `n+1` rows.

**The page id is an explicit parameter, not derived from the pointer.** The
earlier note here inferred `pageId = (ptr - 0xca) div 0x18c + 1`; that formula
does hold at **all 21 call sites**, which is good evidence the seven pages are
one array, but it is a check on the reading rather than what the code computes.

### Ghidra gotcha: `func_0x000XXXXX` names carry a 0x10000 bias

Ghidra did not resolve the nested procedures of `1b2e:52bf` into functions. It
named them from near-call targets as `func_0x0002f8e7`, `func_0x0002f95a`,
`func_0x0002fa23` - and those linear addresses are **0x10000 too high**. The
true addresses come from the `CALL rel16` bytes:

    1b2e:533c  e8 3b f3   ->  0x533f - 0xcc5 = 1b2e:467a
    1b2e:53f6  e8 0e f2   ->  0x53f9 - 0xdf2 = 1b2e:4607
    1b2e:57c9  e8 77 ef   ->  0x57cc - 0x1089 = 1b2e:4743

so for this segment `offset = XXXXX - 0x2b2e0`. The pairwise gaps match
exactly (0x73 and 0xc9 in both), which is what confirms the constant.

Worse, Ghidra left those bytes **undefined**, so `DisasmRange.java` came back
with an empty listing for an address that plainly holds code - `ENTER 0x18c` is
the first instruction at `467a`. That is the failure mode this file warns about
twice already, so `DisasmRange.java` now takes **`+disasm`**, which converts
undefined bytes in the range to instructions before listing them and prints how
many runs it created.

| Ghidra's name | Real address | What it is |
|---|---|---|
| `func_0x0002f8e7` | `1b2e:4607` | PlaceStars |
| `func_0x0002f95a` | `1b2e:467a` | SetMenuPage |
| `func_0x0002fa23` | `1b2e:4743` | page flip / present |
| `func_0x0002fc16` | `1b2e:4936` | (options, unread) |
| `func_0x0002fd55` | `1b2e:4a75` | (redefine input, unread) |

### The "big font" is a SLOT, and the title screen's is STARTREK.816

`DS:0x2110` is not "the big font", it is the *current* big font, and each stage
loads what it wants into it through `2000:3fab`. The HUD's was identified as
`FUTURE.816` by pulling a digit out of a captured frame and matching it byte
for byte; that is still right, and it is right **for the HUD**.

The title screen's is not the same file. Rendering `Start Game` from each of
the four `.816` fonts and scoring lit pixels against the menu capture:

| Font | advance | y | hit | miss |
|---|---|---|---|---|
| **`STARTREK.816`** | **8** | **34** | **344** | **7** |
| `STARTREK.816` | 8 | 33 | 302 | 49 |
| `FUTURE.816` | - | - | *no competitive fit* | |

and `y = 34` is exactly `yBase + 1*16` = `18 + 16`, computed independently. So
the match pins the font, the advance and the row at once.

The **briefing title band uses the same font**, and that is what had been wrong
with it: `STARTREK.816`, advance 8, `y 45`, 187 of 187 lit pixels accounted for.
Together with `peakRow` 9 that takes the band from 19.9% to **0.00%**.

An earlier run of this same test on the briefing reported the opposite, because
its lit-pixel predicate was written for bright-text-on-black and the briefing is
dark-text-on-light. See the title band section above; the correction matters
more than the result.

The general lesson: **do not assume one font per size**, and when reusing a
pixel-matching test on a new screen, check the predicate against that screen
first.

### The title screen draws the foreground over the WHOLE screen

`1b2e:5754` is

    Blit2(0, 0, TUBESFG, 320, 200)          { 2321:0711, masked }

a full-screen masked blit, run once before the loop, with `2321:0792` having
pointed the restore source at `TUBESBG` first. So the visible screen is the
background with the foreground stencilled over all of it - and the per-frame
`2321:0874` stamp on the atom's 16 x 13 box is only the *repair* after the atom
is drawn, not the only place the foreground appears.

Porting only the box put the atom inside the pipes correctly and left the rest
of the network as a flat silhouette: every pipe wall away from the atom was
missing. The tell was a 14.8% diff over artwork that looked right at a glance,
and sampling two pixels settled it - `(200, 160)` is grey 56 in the original
and black in the port.

With both blits, and with `STARTREK.816`:

| Region | differing |
|---|---|
| the eight menu text rows | **0.00%** |
| a page title, `Difficulty`, plus its items | **0.00%** |
| lower artwork, `y 150..199` | 0.43% |
| whole screen | **0.25%** |

(excluding the atom's box and the two star columns, which animate).

### Escape, `1b2e:50cf`

The menu-up key handler tests `0x1b` (ESC) or joystick button `0x20` and then
runs a chain of comparisons on the current page. It is a fixed parent table,
**not** the page you came from:

    1 -> 7      2 -> 1      3 -> 2      4 -> 2
    5 -> 2      6 -> 1      7 -> 1

ESC on the *main* menu therefore opens the `Exit Tubes?` confirm rather than
doing nothing, and since `SetMenuPage` resets the item to 1 it arrives with
**Yes** selected. That is exactly the behaviour `docs/debug-rig.md` records
from driving the original ("ESC out of a submenu lands on Exit Tubes? with YES
highlighted") - observation and code agreeing after the fact.

`SetMenuPage` does still save the outgoing page at `[BP-4]`, and the escape arm
does not read it. What that copy is for is not yet known.

### Every stage runs on the 18.2 Hz tick, not on the render rate

The title screen's atom walks 4 px a leg *per frame*, its star turns every
three *frames*, and attract mode is 720 *frames*. Those are game frames - the
DOS timer tick at 18.2 Hz - not presented frames. The first port of the title
loop stepped it once per `SDL_RenderPresent`, so on a 60 Hz host the whole
screen ran about **3.3x too fast** and the attract countdown expired in twelve
seconds instead of forty.

The play session never had this bug because `Game::update` converts elapsed
real time into a whole number of 18.2 Hz steps. `tubes::kFrameHz` is now a
named constant in `game.h` and the title screen accumulates against it the same
way, capped at 8 steps so a stall cannot teleport the atom.

Worth stating generally, since the splashes and the cutscenes are still to
come: **anything counted in frames is counted in game frames.**

### The corner curve's sign is NOT the same on horizontal and vertical legs

Extracted from the four `dirH` arms of `1b2e:52bf`'s loop:

| `dirH` | `dirV` = 'F' | `dirV` = 'B' |
|---|---|---|
| `L` | `y := limY - n` | `y := limY + n` |
| `R` | `y := limY + n` | `y := limY - n` |
| `U` | `x := limX + n` | `x := limX - n` |
| `D` | `x := limX - n` | `x := limX + n` |

`U` and `D` are the opposite way round to `L` and `R`. The first port read
`'F'` as one sign for all four, which made the atom curve **away** from the
next leg at every corner entered on a vertical - it visibly clipped outside the
pipe rounding each bend, which is how the player spotted it.

The invariant that catches this without any pixel measurement: the curve exists
to *lead into the next leg*, so at the instant a leg hands over, the cross-axis
must be displaced toward the way the next leg travels. That is now a test, and
it fails on the old signs and passes on the new. Leg 23 is excluded, being the
one leg that hands over to another vertical.

**A proxy that did NOT work, recorded so it is not tried again.** The obvious
check is "is the atom's centre over a transparent pixel of `TUBESFG`", the
foreground being 23.8% opaque and the transparent part being the pipe hollow.
It improves with the fix (8.7% of frames to 6.9%) but never reaches zero, and
the residue is not error: it concentrates on leg 10 - the crossings leg, where
the atom legitimately passes *behind* a wall - and on the `D` legs, which cross
the letterforms' horizontal strokes. The network is full of junctions, so
"centre over an opaque pixel" and "outside the pipe" are simply different
things. The listing plus the hand-over invariant settle this; the pixel proxy
cannot.

### Stages do NOT all run at the play session's frame rate

The game installs its own timer: `226c:00c6` reprograms the PIT and hooks
`INT 8`, and `226c:00b0` returns the counter that ISR maintains. `21ea:06ba`
then waits out a per-frame period held in `DS:0x0d40`, and the title screen
additionally page-flips through `2321:014f`, which polls `0x3da` for vertical
retrace. **The play session calls neither** - it is paced some other way, which
is why the two stages can differ at all.

Read live through the debugger (`probe_frameperiod.py`, in the rig):

| Stage | `DS:0x0d40` |
|---|---|
| title screen, menu down | 6 |
| title screen, menu up | 6 |
| menu page 2 | 6 |
| briefing | 6 |
| **play session** | **9** |

So the title screen, the menu and the briefing run **exactly 1.5x faster** than
the session. The first port ran the title at the session's rate and the player
spotted it immediately.

**The absolute rate is NOT settled.** The divisor the game writes is `16384`,
i.e. `1193182/16384` = 72.83 Hz, exactly four times the BIOS tick - the usual
reprogram-and-chain arrangement. But `72.83/9` is 8.09 Hz, nowhere near the
session's established 18.2 Hz, so `[0x0d40]` is not simply ticks-per-frame and
`21ea:06ba`'s `145 div elapsed` estimate is not simply fps. The **ratio** is
what was measured and it does not depend on resolving that; the port carries
`kTitleHz = kFrameHz * 1.5` and says so.

Worth noting for later: 18.2 Hz for the session has never actually been
*measured* either. It is the BIOS tick, assumed early, and the `DEMO.SCR`
oracle cannot confirm it - the replay consumes one input byte per game frame,
so it pins the frame **sequence** and says nothing about the frame **rate**.

### The title screen's atom colour is `Random(7) + 1`

`1b2e:5312` rolls one of the seven ordinary colours on entry and indexes the
ball table at `0x1da6 + (r+1)*4`. The port seeded its generator with a fixed
constant, so the atom was always the same blue. It now seeds from the clock for
interactive play - the original's `Randomize` - while every harness entry point
keeps the fixed seed so captures and traces stay reproducible. `--seed N`
forces a specific one.

### The frame rate is 16.11 Hz, not the 18.2 Hz assumed since the start

`kOriginalFps` was carried at **18.2 Hz** - the PC BIOS tick - for the whole
project, on the strength of "attract mode produced ~17 state changes a second,
and 18.2 is the obvious candidate". `src/game.cpp` listed it in its own header
as one of only two things in the file that were *not* from code. It was wrong
by 12.5%.

Two independent routes now agree on the real figure.

**From the listing.** `21ea:06ba` waits out a per-frame period held in
`DS:0x0d40` against a dividend of `0x91` = 145 (`21ea:0706`). Read live, that
word is **9** in the play session and **6** on the title screen, the menu and
the briefing:

    145 / 9 = 16.11 Hz        145 / 6 = 24.17 Hz

**From the demo.** `DEMO.SCR`'s byte index lives at linear `0x24c2e` and
`demoidx.jsonl` holds 979 timed readings of it over 130 s of the original. The
index is not a frame counter - it only advances on frames the tube is idle -
but the port reproduces that gating exactly, so `--demo-csv`'s `frame,idx`
columns convert it. Least squares on (wall clock, game frame), fitting slope
**and** intercept:

    16.180 Hz        intercept -2.7 frames, rms 4.0 over a 2106-frame span

**0.4% apart.** The constant is now derived from the formula and corroborated
by the fit.

**And now the 9 itself is read rather than measured.** `21ea:0690` is
`SetFrameRate(fps)`, and it is six instructions:

    [DS:0x0d40] := 145 div fps        { IDIV, so it truncates }
    SetTimer([DS:0x0d40])
    [DS:0x0d38] := $ff;  [DS:0x0d39] := 0

so the argument is the frame rate the game ASKS for, and `[0x0d40]` is the
period it gets. `1000:44d8` calls it with **16** at the top of the play
session: `145 div 16 = 9`, and `145 / 9 = 16.11`. The 16.11 Hz is therefore an
artefact of the truncation - the game wants a flat 16 and misses by 0.7%. The
title's 6 is the same story from `SetFrameRate(24)`.

The Absolute Magic splash sets it twice, to **9** and to **4**, which is what
paces its two animation phases at 145/16 = 9.06 Hz and 145/36 = 4.03 Hz.

Two things this does and does not change:

- it does **not** change the simulation. Every speed in the game is a whole
  number of pixels per *frame*, so the frame sequence is identical - which is
  why `--demo-trace` still ends on score 13000 and every pixel diff is
  unmoved. It changes only how fast the port runs in real time.
- it retires the reasoning that produced 18.2. "~17 state changes a second" was
  the better evidence and 16.18 sits closer to it than 18.2 ever did; the
  number was fitted to the nearest famous constant instead of to the data.

**Still not resolved:** how 145 relates to the PIT divisor the game actually
writes, which is `16384` = 72.83 Hz, exactly four times the BIOS tick.
`72.83/9` is 8.09 Hz and the measurement contradicts it flatly, so the tick
that `[0x0d40]` counts is not simply that interrupt. The rate is settled; the
mechanism behind the 145 is not.

**Method note.** The previous session read `145/9 = 16.11` off this very
formula, compared it against the assumed 18.2, and concluded the *unit reading*
must be wrong. It was the assumption that was wrong. A derived number
disagreeing with an undocumented guess is evidence against the guess, not
against the derivation - and this file said "ASSUMED, not measured" in the same
breath, which should have settled which one to doubt.


## `TUBES.HSC` - the high score table, fully decoded

A format nobody had seen, because **the game does not ship one**. It is created
the first time a score qualifies, which is why the game directory has only
`TUBES.SAV`. Recovered in one pass by warping a save to wave 75, clearing it,
and entering a known string - the player typed `Reverse Engineering!` so the
record would be unmistakable in the bytes.

**792 bytes = 2 banks x 11 records x 36 bytes**, and every one of those three
numbers is confirmed by the file rather than assumed:

    record = 36 bytes
      [0]        name length          (Pascal ShortString)
      [1..31]    name, NUL padded     31 chars
      [32..35]   score                u32 little endian

    bank 0 at 0x000     bank 1 at 0x18c

The two banks are the two the title screen draws under the headings ` Chains`
and ` Wave`. **Bank 1 is Wave mode**: the run that produced this file loaded a
saved *Wave* game, and the new entry landed at bank 1 slot 1. Bank 0 was left
untouched, still holding ten defaults and an empty eleventh.

The table is **sorted descending and insert-and-shift**. Before the run both
banks held ten names scoring 1000, 900 ... 100 with an empty eleventh; the new
34000 went in at the top of bank 1 and pushed everything down one, filling that
bank's spare slot. So eleven is the real capacity, not ten.

A name field is 31 bytes and the entry screen accepted at least 20 characters.

### The defaults are in the EXE, packed, and there are exactly twenty

At file offset `0x00d621`, immediately after the `TUBES.HSC` filename literal
at `0x00d617`, sit **twenty Pascal ShortStrings back to back with no padding** -
ten for each bank, in the same order the file writes them:

    bank 0   Ken Heckbert, Kelly Rogers, Glenda Moore, Terry Herrin,
             Rik Pierce, Doug Howell, Joe Siegler, Bob Mandel,
             Larry Nelson, Adam Pedersen
    bank 1   Ronald Davis, Jason Blochowiak, Matt Long, Dan Linton,
             Scott Miller, Evan Heckbert, Grant Heckbert, Casey Rogers,
             Micheal Moore, Mike Bartelt

The block ends cleanly at `0x00d723`, where code resumes with `ENTER 0x80,0` -
which brackets it and confirms the walk did not run past the end.

Note the **storage differs from the file**: packed and unpadded in the binary,
fixed 36-byte records on disk. The scores are not stored at all - both banks
default to the same 1000..100 ladder, so they are generated.

### The VIEWER is `1b2e:61b6`, and a STRING search is what found it

The menu's High Scores item was written up one session as "unfound, not
absent": `FindScalarRefs` on the two bank addresses `0x1610` and `0x179c`
returned only the loader `1b2e:0243` and the entry screen `1000:96db`, and the
conclusion drawn was that the viewer must take the bank as a parameter, where a
scalar scan cannot see it.

**The reasoning was sound and the conclusion was wrong.** `1b2e:61b6` names
both banks with plain literals - it is exactly the kind of function that scan
finds. What it does *not* do is matter, because the search that finds it is a
different one:

    awk '/^@FUNC/{f=$0} /@STR/{print f" || "$0}' map.txt | grep -i "high scores"

One line, against a `MapProgram` dump that was already on disk, and it lands on
the function immediately - by the strings it prints, `Endurance Mode High
Scores` and `Wave Mode High Scores`. This is `CLAUDE.md`'s standing rule in its
mildest form: **when a search comes back empty, suspect the search** - and here
"suspect" means try a *different kind* of search before writing up an absence.
The tell was available too: a screen the player describes in detail, with two
titles already quoted in these notes, cannot be code that does not exist.

**One table at a time, on the two video pages.** The function draws Endurance
onto page 0 and Wave onto page 1 before showing anything, then flips between
them with `2321:01b5`. So no redraw happens when the player presses a key, and
a port that redraws produces the same picture.

    SetVisualPage(0); SetActivePage(0)
    { draw the Endurance table }
    SetActivePage(1)
    { draw the Wave table }
    PlayMusic(CLASS.MUS)                 { [DS:0x22ca] with DS:0x212c }
    PlaySound(CLAP.SFX)                  { [DS:0x230e] with DS:0x2120 }
    wait                                 { showing page 0 }
    if DS:0x1d45 <> 2 then begin         { 2 is ESC - it leaves at once }
      SetVisualPage(1)
      wait                               { showing page 1 }
    end

The wait is the usual one, inline rather than through `1b2e:0e37`:

    n := $1a4
    repeat
      if not SoundBusy then PlaySound(CLAP.SFX)
      Delay(5)
      DS:0x1d45 := ReadInput
      Dec(n); if n = 0 then DS:0x1d45 := 3
    until DS:0x1d45 <> 0

`0x1a4 * 5 = 2100` retraces at Mode X's 70 Hz is **thirty seconds**, the same
give-up the briefing uses, and a timeout does exactly what a key does. The
`SoundBusy` poll means **the applause loops** for as long as the screen is up
rather than playing once.

Each page, in draw order:

    Draw(0, 12, DS:0x2058)               { BLACKBRD.GFX, the classroom's board }
    FillRect(10, 37, 299, 118, 111)      { the panel }
    SetFont(SCRIPT.816, 8, 16, 8)
    for i := 1 to 10 do begin
      WriteAt(16, i * 12 + 21, 30, 3, name[i])
      Str(score[i]:10, s)
      WriteAt(220, i * 12 + 21, 30, 3, s)
    end
    DrawMasked(57, 26, DS:0x2060)        { SLIDEBAR.GFX, the roller bar }
    SetFont(STARTREK.816, 8, 16, 8)
    WriteCentred(0, 319, 14, 159, 3, title)

Three things fall out of that:

- **The chalkboard carries no writing.** `BLACKBRD.GFX` has equations chalked
  across it and the `2321:060b(10, 37, 299, 118, 111)` panel covers all but its
  frame. The port drew the board bare for one revision and the player caught
  it. It is the entry screen's own panel, so the two high-score screens are the
  same picture under different headings.
- **Every row constant is the entry screen's**, including the rows starting at
  `y` 33, four pixels proud of the panel at 37. Two independent functions
  reaching the same slightly-odd geometry retires that as "not reconciled": it
  is what the game does.
- **The score is `Str(score:10)`** - right-justified in ten characters, so
  `x` 220 is where the *field* starts. With the 8-pixel advance a 25-character
  name reaches `x` 216 and the field ends at 300, inside the panel's 309: the
  layout is exactly tight enough for the longest name the typing loop allows,
  which is a good sign the reading is right.

`DS:0x2120`, `0x2124`, `0x2128` and `0x212c` are set at `1000:b14c` onward and
hold `CLAP.SFX`, `SLIDE.SFX`, `SWITCH.SFX` and `CLASS.MUS`. That also names
`[DS:0x230e]` as **`PlaySound`** and `[DS:0x230a]` as **`SoundBusy`**, which
retires a lead `PLAN.md` was carrying: `[0x230e]` was written up as taking
"two palette pointers" and being the obvious candidate for the missing screen
fade. It takes ONE far pointer, printed as two words. `1b2e:0a11` calls it
with `SLIDE.SFX` as the projector screen rolls down, and with `SWITCH.SFX`
when `DS:0x1d6c` is set - **neither of which the port plays yet.**

**Corroborated against the original: both pages diff at ZERO pixels.**
`grab_hiscores.py` captures them and `diff_hiscores.py` compares all 64,000
pixels with no mask - this screen has no random backdrop, no animated stars and
no atoms in flight, so the whole frame is comparable, which the gameplay diff
never is. Every constant above is therefore confirmed from both ends: read off
the disassembly, and rendered pixel for pixel.

The key model was tested rather than assumed, by running the screen twice:

| pass | keys | result |
|---|---|---|
| 1 | RET, RET, RET | Endurance, then Wave, then the menu |
| 2 | RET, ESC | Endurance, then the menu **at once** |

The ESC frame is 0.2% different from the other menu capture - the title atom
and stars have moved - and 77.6% different from the Wave page, so it is
unambiguous. The two page-0 captures are byte-identical, so the screen is
deterministic.

That also explains an old note in this file: a probe once "captured the same
blackboard three times while believing it was walking the Start Game path". It
was looking at these two pages plus the menu. The pages differ only in the
heading and the ten rows, so paging through them does look like one screen that
will not dismiss.

### The F2 save screen, `1000:2dd0`

Same routine as the in-game key handler and the F1 help, which is why it never
showed up as a function of its own. It draws STRAIGHT OVER THE PLAY FIELD -
captured to be sure, because the page calls at `1000:3062` looked like they
might be clearing one - and the result is as hard to read on the original as it
is in the port.

    SetFont(STARTREK.816)
    WriteCentred(0, 319, 22, $2f, 3, 'Save Game')
    WriteCentred(0, 319, 25, $2f, 3, '_________')
    WriteAt(30,  50, $1e, 3, 'Description')
    WriteAt(30,  53, $1e, 3, '____________________')
    if mode = 1 then begin
      WriteAt(242, 50, $1e, 3, 'Chains');  WriteAt(242, 53, $1e, 3, '______')
    end else begin
      WriteAt(258, 50, $1e, 3, 'Wave');    WriteAt(258, 53, $1e, 3, '____')
    end
    SetFont(SCRIPT.816)
    for i := 1 to 5 do begin
      WriteAt(30, i * 17 + 50, $1e, 3,
              if rec.len <> 0 then rec.desc else '( Available )')
      Str(rec[+$28] : 6, s)                  { or rec[+$26] : 4 in Wave mode }
      WriteAt(242, i * 17 + 50, $1e, 3, s)   { 258 in Wave mode }
    end

**The same mode split as the menu's slot list**, down to the heading x: 242 plus
six characters and 258 plus four both end at 290. And the number is drawn for
EVERY row, because `1000:31a8` sits after the two arms join - an empty slot
shows a right-justified `0`. The port guarded that on "the record exists" and
the capture said otherwise.

The selected row is flanked by `SRBALL.CSP` at x 15 and 299, four pixels below
the row - the small red ball from the Task Display's own set, loaded into
`DS:0x200a` at `1000:ada2`. Down and Up move the selection and it WRAPS at five.

RETURN copies the record out to the scratch, takes its description as the line
to edit, clears the cell and lets the player type. **There is no cursor.** The
high score screen pulses a 4 x 4 block at `1000:9757`; this loop draws
characters and erases an 8-wide cell on backspace, and that is all. It was
given one by analogy for a revision, which is the invention the prime directive
exists to prevent.

`1000:3620` ends the typing on ESC or RETURN and they are NOT the same:

* **RETURN** commits - an empty description becomes `Undescribed`
  (`CS:0x2d92`), the fourteen session fields are filled in, the whole 0x50-byte
  record is `Move`d into the bank, `1b2e:00ac` writes the file, and
  `1000:3722` holds for 20 retraces before the game resumes;
* **ESC** at `1000:3635` jumps straight past all of it to the exit. It abandons
  the save rather than committing what has been typed.

## The boot sequence, read out of `1000:aaba`

The entry program's own order, and the two gates are the same test:

    1000:ac21   if 2685:08aa = 0 then Splashes           { 1b2e:11b0 }
    ...         { resources, high scores, the save file }
    1000:b1ee   { seed a new game's defaults }
    1000:b224   if 2685:08aa = 0 then Cutscene           { 1b2e:1651 }
    1000:b236   Title                                    { 1b2e:52bf }
    1000:b23e   { the menu dispatch }
    1000:b2c1   JMP 1000:b236

**The cutscene runs once, at boot, immediately before the title screen is
first shown.** The loop's own jump goes back to `b236`, the title call, not to
`b1ee`, so neither the cutscene nor the defaults block is part of the cycle.
The second call at `1000:b28b` is the attract arm and is a different thing.

`2685:08aa` is three instructions in the RTL and gates both intros; the port's
`--no-splash` is its analogue.

**Music, per screen.** Each screen starts its own song and nothing is playing
before the first one that does:

| screen | song |
|---|---|
| Software Creations `21d5:007b` | **none** - it loads three resources and not one is a song |
| Absolute Magic `2178:00eb` | `AMTHEME.MUS` |
| the cutscene `1b2e:1651` | `[DS:0x212c]`, which is `CLASS.MUS` |
| the title `1b2e:52bf` | `TUBES.MUS`, one of the four resources `1b2e:5238` loads for it |

The port had `TUBES.MUS` starting at device-open time, which put the title
theme over the first splash. It now starts where the title stage does.

## The opening cutscene, `1b2e:1651` - and the professor has a name

**Dr. Lanny B. Brilliant.** The cutscene names him, and it names the eight
elements too - which is where the port's type names come from and, checked
against it, `kPurplium` is spelled the way the game spells it.

The story, extracted the same way the Instructions were, in TINY6X8 at colour
`0x9a` mode 1:

    (7, 30)   In a lab far, far away in the Great White North...
    (7, 9)    Dr. Lanny B. Brilliant was completing his  work  on
    (7, 19)   the creation of eight new elements not yet included
    (7, 29)   on the periodic table of elements:
              Redium    Greenium   Bluium    Cyanium      { y 43 }
              Purplium  Yellowium  Pinkium   Flashium     { y 63 }
    (7, 76)   Already  going  over  his  Nobel  Prize  acceptance
    (7, 86)   speech in his head...
    (7, 30)   Suddenly, his future didn't seem so bright...
    (7, 10)   Lanny hadn't researched his new elements enough  to
    (7, 20)   have discovered they were highly  unstable.  Before
    (7, 30)   he could  react,  they  were  everywhere.   He  had
    (7, 40)   nothing for the Nobel Prize committee to assess.
    (7, 10)   But there is still hope!  Lanny knows that  he  can
    (7, 20)   stabilize the atoms if he bonds three or more atoms
    (7, 30)   of the same element  together  forming  a  molecule
    (7, 40)   chain.
    (7, 55)   Maybe you can help...

The eight element names sit in a 4 x 2 grid at x 34, 108, 188, 258, and **each
has its own atom drawn beside it** - eight `2321:0905` calls at y 40 and 60,
one per type, so the story introduces the balls by name and by sprite at once.

It owns three sounds nothing else uses - `WHATTHE.SFX`, `NOOOO.SFX`,
`BUBBLE.SFX` - and twenty-six sprites: `WRITE0..9.GFX`, the writing animation,
and `EXPLOD1..16.GFX`, the explosion. Sixteen explosion frames, not the four
`PLAN.md` carried.

### The two helpers, read

**`1b2e:1188` is 40 bytes and is a rectangle blank.** Four word parameters and
`RETF 8`:

    2321:014f                        { select the other page }
    FillRect(x, y, w, h, 3, page)    { 2000:345d, colour 3 }
    2321:014f                        { and back }

So the story pages are cleared by blanking a rect on BOTH pages, in colour 3 -
which is why the text can be replaced without redrawing the blackboard.

**`1b2e:0f46` is the sprite-sequence player**, 578 bytes, and it takes TWO
Turbo Pascal open arrays - the reason the decompiler produces nonsense for it
is the conformant-array copy loops at its head, which is exactly the case
`CLAUDE.md` says to read the listing for.

Its one timing literal is the whole cadence: **`Delay(10)` per frame** -
`1b2e:0fe9`, ten retraces at 70 Hz, so **7 frames a second**. That is the same
beat as the projector slide drop and the professor's wave, which is a good sign
it is the house animation rate rather than a number picked for this scene.

It draws through `2321:0905` and `2321:068d` and flips with `2321:014f` and
`2321:019b`, so it is double-buffered like everything else.

### `1b2e:0f46`'s parameters, read

It is **not** a sprite-sequence player. It is a **two-track** one: it runs two
animations at once, each with its own frame list, position, frame count and
sound cue, and either track may be idle. That is why the argument list is 23
words long and why the decompiler's output looked like noise.

It is entered with `PUSH CS; CALL near` - a far call synthesised inside the
segment, not a nested-procedure static link - so the arguments start at
`[BP+6]` and the first one pushed is at `[BP+0x32]`.

| offset | track | meaning |
|---|---|---|
| `[BP+0x32]` | - | byte, `* 7` into `[BP-4]`; the screen's own tick budget |
| `[BP+0x30]`, `[BP+0x2e]` | A | x, y |
| `[BP+0x2c/0x2a]` | A | the frame list: `array[0..n] of Pointer` |
| `[BP+0x28]` | A | its high index |
| `[BP+0x26]`, `[BP+0x24]` | A | w, h |
| `[BP+0x22]` | A | frame count; 0 leaves the track idle |
| `[BP+0x20/0x1e]` | A | a sound, played once |
| `[BP+0x1c]` | A | the frame it is played on |
| `[BP+0x1a]`, `[BP+0x18]` | B | x, y |
| `[BP+0x16/0x14]`, `[BP+0x12]` | B | its frame list and high index |
| `[BP+0x10]`, `[BP+0xe]` | B | w, h |
| `[BP+0xc]` | B | frame count |
| `[BP+0xa/0x08]` | B | a sound |
| `[BP+0x06]` | B | the frame it is played on, or `$ff` |

The two frame counters are **globals**, `[DS:0x1d6e]` for A and `[DS:0x1d6f]`
for B, both 1-based and both wrapping back to 1. Two counts are special: a
track whose count is **25** (A) or **16** (B) stops when it wraps instead of
looping - so those two numbers mean "play once", and they are exactly the
lengths of the two one-shot sequences.

`$ff` in B's sound-frame slot means something else again: play the sound
whenever the effects voice reports itself idle (`[DS:0x230a]`, `SoundBusy`),
which is the same "keep it going" idiom the high score viewer uses for the
applause.

Both tracks draw through `2321:068d`, whose signature is now pinned as
`Blit(x, y, img, w, h)`, except that B draws through `2321:0905` instead when
its count is exactly 7. The frame is `Delay(10)`, ten retraces, 7 fps.

**The first call site, worked** (`1b2e:1b05`):

    A: (86, 122) 28 x 41, frames 1..5 of the 26-entry list, no sound
    B: idle

and 28 x 41 is exactly the size of `WRITE1..WRITE5.GFX`, which is what
confirms the w/h reading rather than an x/y one. `WRITE0` is 28x50 and
`WRITE6..9` are 28x66, so the list is not uniform and the caller passing the
size per call is the reason it can hold all of them.

### All five call sites

`[BP+0x32]` is the page's duration **in seconds**: `IMUL AX,7` and a
`Delay(10)` per iteration is `param * 70` retraces, the same idiom the
Continue screen uses at `1000:c117`.

| at | secs | track A | track B |
|---|---|---|---|
| `1b2e:1b05` | 2 | (86,122) 28x41, frames 1..5 | idle |
| `1b2e:1cdf` | 18 | (86,122) 28x41, frames 1..8 | (238,60) 16x13, frames 1..7, from `DS:0x1da6` |
| `1b2e:1d53` | 4 | (86,122) 28x41, frames 1..8 | (258,119) 60x46, frames 1..4, sound on `$ff` |
| `1b2e:1e12` | 12 | (86,122) 28x66, frames 10..25 ONCE, sound on frame 25 | (258,119) 60x46, frames 4..16 ONCE |
| `1b2e:1efe` | 10 | idle | idle |

Four readings drop out of that table and each is corroborated by a size:

* **the second page's track B is an ATOM, not a sprite of its own.** Its list
  is `DS:0x1da6` - the ball table - it is 16x13, which is the atom box the
  whole port already stamps with, and it cycles frames 1..7, the seven
  ordinary colours. The page that names the eight elements shows a ball
  changing colour beside them.
* **60x46 is `EXPLOD*.GFX`'s size**, so track B on pages 3 and 4 is the
  explosion - looping four frames while the text talks about instability, then
  running all sixteen once.
* **28x41 is `WRITE1..5.GFX` and 28x66 is `WRITE6..9.GFX`**, which is why the
  caller passes the size at all: one list holds frames of two different sizes
  and only the caller knows which stretch it is playing.
* **the last call has both tracks idle.** It is a plain ten-second hold that
  still polls for a key, so `1b2e:0f46` doubles as the screen's wait.

`1b2e:1e08` pre-seeds both counters before the fourth call - `[DS:0x1d6e] :=
10`, `[DS:0x1d6f] := 4` - which is how a track starts part way in. That is
also why the counters are globals rather than locals.

### The slot map, extracted

Ten `WRITE*.GFX` fill 26 slots and sixteen `EXPLOD*.GFX` fill 17, because the
caller **copies pointers**: `1b2e:16aa` and fifteen more like it duplicate a
slot rather than loading a second time. That is how a pose is held for seven
ticks when the player has no delay parameter at all.

    writing  [BP-0x78], 26 slots
      0  WRITE1   1  WRITE2   2  WRITE2   3  WRITE3   4  WRITE3
      5  WRITE4   6  WRITE4   7  WRITE3   8  WRITE3   9  WRITE5
     10..16  WRITE6 (seven)  17,18  WRITE7  19..23  WRITE8  24,25  WRITE9

    explosion  [BP-0xbc], 17 slots
      0..15  EXPLOD1..16      16  EXPLOD16 again

**The sizes prove it.** Slots 0..9 are `WRITE1..5`, every one 28x41, and the
three pages that play frames 1..5 or 1..8 pass 28x41. Slots 10..25 are
`WRITE6..9`, every one 28x66, and the one page that plays frames 10..25 passes
28x66. Nothing else fits, so this is an oracle rather than a reading - which
matters, because "guessing at 26 slots" is how a page animates the wrong thing
in a way nobody notices.

`WRITE0.GFX` is loaded but is **not in either list** - it goes to `[BP-8]` on
its own. The frame counter starts at 0 and the wrap goes to 1, so element 0 is
drawn once at the start of a track and never again.

The extraction is `tools/gen_cutscene.py`, which reads the loads, the pointer
copies, the text calls, the atom draws and the two tracks' arguments, and
emits `src/cutscene.cpp`. It is the third screen through that method after the
Instructions and the Credits.

### The panel, and the rest of the page furniture

`2321:0ac0(x, y, w, h)` is the bevelled grey panel the story text sits in -
nine `FillRect`s, a body in colour 7 with highlights in 15 and shadows in 8:

    (x, y, w, h, 7)              (x, y, w, 1, 15)      (x, y, 1, h, 15)
    (x+w-3, y+2, 1, h-4, 15)     (x+3, y+h-3, w-5, 1, 15)
    (x+2, y+2, w-4, 1, 8)        (x+w-1, y+1, 1, h-1, 8)
    (x+2, y+2, 1, h-4, 8)        (x+1, y+h-1, w-1, 1, 8)

Read off the listing by walking its pushes, for the same reason as everything
else here: "a grey box with a border" is exactly the kind of thing that looks
right and is three pixels wrong everywhere.

The scene under it is `2321:068d(0, 12, [DS:0x2058])` - the same held
blackboard, at the same y, that the briefing and the stats screen use - with
`WRITE1` blitted at (86, 122) and `EXPLOD1` at (258, 119) before the fade-in.
`EXPLOD1` is not an explosion frame at all: it is the beaker, sitting on the
board's ledge, and the sixteen frames are it bubbling and then bursting.

### The whole screen, as a program

    ClearPage(0); SetVisual(0); SetActive(0)
    Blit(0, 12, blackboard);  Blit(86, 122, WRITE1);  Blit(258, 119, EXPLOD1)
    PlayMusic(CLASS.MUS);  FadeIn;  Delay(45)
    for each of the five pages:
      Panel(...);  { the text and, on page 2, the eight atoms }
      { seed the counters this page drives }
      k := Animate(...)         { see the call-site table above }
      { the between-pages sound, if any }
      BlankRect(...)            { clears the text on BOTH pages }
    FlipPage;  FadeMusic;  FadeOut

A port that recomposes the whole screen each frame does not need `BlankRect`
or the `FillBar` half of the panel call - both exist to clear the *other*
page in a double-buffered scheme - but it does have to hold a stopped track's
LAST frame rather than wrapping it, since the original simply stops drawing
and what is on the page stays there.

### Captured off the original, and the two things it caught

`grab_cutscene.py` sweeps the original with a screendump every ~1.4 s from
boot; `diff_cutscene.py` then finds, for each of the port's five pages, the
capture that matches best. The sweep is deliberate - the cutscene cannot be
paused into, because it reads the input driver once a frame and acts only on
1 and 2, so the game's own Pause key is simply ignored.

The captures also **confirm the durations independently**. The attract run
shows page 1 at t=63.5 and page 2 by t=65.0, and the first run has page 3
spanning 4.5..16.6 and page 4 spanning 18.2..27.2 - 2, ~13 and ~10 seconds
against the 2, 12 and 10 the `[BP+0x32] * 7` reading predicts.

Two real differences fell out of the diff, and neither was visible any other
way:

* **`WRITE0.GFX` is Lanny's base pose.** `1b2e:1a80` blits it once at
  (87, 150) through `2321:0711`, outside both frame lists, and the animated
  frames are drawn OVER his top half at (86, 122). 28x41 reaches y 163 and
  28x66 reaches 188, so his legs below 188 are only ever this draw. The port
  drew the frames alone and he had no legs - 256 pixels, on every page.
* **The fourth page's exit takes the top of him with it.** `1b2e:1e6b`
  restores both animation rectangles from the other page, so on page 5 what
  survives is the base pose *minus* the box - his feet and nothing else.

With those two fixed the diff is **0 pixels of 59,184 on all five pages**,
with only the two animation rectangles and the one cycling atom masked.

`sweep_cutscene_ticks.py` then closes the mask: it renders the port at every
tick of a page and reports the best match with NOTHING masked - the whole
screen, animation included. That is what puts a number on the frame lists
themselves, and it found two more differences that the masked diff could not:

* **the beaker stays on the page.** It is drawn once before the fade and never
  erased, so on page 2 - where track B is driving the eighth element's atom
  instead - it is still there. The port recomposes every frame and dropped it,
  losing the atoms inside it: 77 pixels.
* **page 5's figure is base MINUS the erased box PLUS the frozen frame.**
  Drawing the base pose and the last frame together doubles him, because the
  two draws sit a pixel apart. The order that matches is base, then both
  boxes back to the bare scene, then each track's frozen frame on top.

**And one apparent difference that was the RIG's fault, not the port's.** The
first capture run began after the game had already started, so its early pages
were cut off - and comparing against it left a stubborn 24-pixel residue in
the beaker's bubbles that looked exactly like a one-tick phase error between
the two tracks. Capturing a run from its start removed it entirely. A number
that will not go away is worth suspecting the measurement over the code, which
is this project's own rule about negative results pointed the other way.

### The last 144 pixels were an OPAQUE blit, not the page bookkeeping

This was open for two sessions and filed under the video pages, on the reading
that the original "never erases" and the port lost the union by rebuilding.
Both halves of that were wrong, and the pixels' own colour says so:

    x 90..97, y 164..187      original: colour 0, all 144 of them
                              port:     the base pose's greys

**The original is BLACK there and the port let something show through** - the
opposite way round from "the original holds more". So nothing was accumulating.

`1b2e:0f46` draws every animation frame through `2000:389d`, which is
`2321:068d` - the OPAQUE member of the blit family, the same one `1b2e:0e37`
stamps the professor's wave frames with. An opaque blit writes the whole
`w x h` box including the pixels the art leaves at index 0, and those land as
colour 0. So a frame REPLACES its box; it does not merge into it.

The strip is where that shows. `WRITE1..5.GFX` are 28 x 41 and `WRITE6..9.GFX`
are 28 x 66, and page 4 is the first to use the tall ones. Their bottom rows
are index 0, so the original blacks out the base pose underneath - and the
port, which kept the figures in a layer stamped with index 0 meaning "not
painted", drew the base pose through them.

Two things had to change and neither is a page: the frame lists load OPAQUE
(they were loading with index 0 transparent, on a note that named `2321:068d`
as the masked one - it is not), and the figures are drawn straight onto the
composed page instead of through an overlay, because an overlay cannot tell
"wrote black" from "wrote nothing". The base pose stays masked, since
`1b2e:1a91` draws it through `2000:3921`.

**The result is 0 pixels on all five pages with nothing masked at all** - 42
captures, whole screen, `sweep_cutscene_ticks.py` per page - and the masked
`diff_cutscene.py` stays at 0 too.

### What the page bookkeeping turned out to be, and why it was not the answer

Read anyway, since it was the standing lead. It is all real and none of it was
needed:

* `1b2e:0f46` calls `Flip` once per tick, and then - only when track A has
  stopped and B has not - toggles `DS:0x2376` a SECOND time and re-selects the
  draw page, so a lone track animates on the shown page instead of being
  double-buffered;
* `1b2e:1188` is `Flip; CopyRect(3, DS:0x2376, x, y, w, h); Flip`, and every
  page ends with one. The rectangle differs per page, read off the four call
  sites: `(0, 25, 320, 20)`, `(0, 4, 320, 95)`, `(0, 25, 320, 20)`,
  `(0, 4, 320, 50)`. The 95-tall one is page 2's, because its band has to
  reach the eight elements' atoms at y 60..72;
* `1b2e:1e58`..`1e84` copy both animation boxes from the other page, and only
  the FOURTH page does it.

Building all three on top of a two-page model was tried and measured before
the real cause was found: it moved page 4 from 144 pixels to 148 and put 176
back on the others. The number that mattered was the one that got worse, and
the colour of the pixels is what finally pointed the right way.

**A method note, because this cost two sessions.** The residue had been
described as "the original still holds the difference" without anyone reading
the pixels' VALUE. One `Counter` over the differing pixels said `(0,0,0) x
144` and the whole thing fell out in minutes. Diff the colours, not just the
count.

This also matters beyond the cutscene: `1000:b287` runs it BEFORE the attract
demo and skips the demo if it returns 2, so the port's attract mode is missing
its first half until this lands.

## Wave 75 is the last wave, and it is the hidden-atom objective

Confirmed by playing it, after warping `TUBES.SAV`'s wave byte at `0x206`:

* **75 really is the end.** Clearing it runs an **end-of-game cutscene** and
  then the high-score entry screen, `1000:96db` - which is what `1000:a6c1`
  calls once the wave loop falls out, gated on `not aborted`, `mode <> 0` and
  `DS:0x1d4b = 0`.
* **wave 75 and wave 30 share an objective.** The player reported both as
  "mystery balls", and the decompiled table agrees independently: both are
  `kSurviveHidden`. That is the **hidden-atom** objective, whose concealed
  cells render as `MYSTBALL` (type 19, the `?`) - *not* the Mystery Wave,
  which is the random-objective picker at waves 46, 55 and 71. The two are
  easy to conflate and are different mechanisms.

This also retires a stale claim in the tooling: `watch_play.py`'s header says
type 19 "has never been observed". It has now, and the port already models it.

**`1000:9499` itself is still unread.** The breakpoint set for it never fired -
see below - so what the cutscene actually does is known only from watching it.

### Why the breakpoint missed, and the lesson

Three execution breakpoints were armed at linear addresses computed as
`0x8240 + offset`, from the rig's anchor `1000:6008 = linear 0xE248`. None
fired, even though the player demonstrably reached all three screens.

The anchor was measured under `tubes.conf`, where the game is launched **by
hand after attaching**. This run used `tubes-play.conf`, which launches
`TUBES.EXE` from `AUTOEXEC` - a different environment block, therefore a
different PSP, therefore almost certainly a different load segment. The
computed addresses were very likely not the intended code at all.

`CLAUDE.md` already says a negative result is only as good as the filter that
produced it. This is that failure exactly: "no breakpoint fired" was reported
by an instrument that had never been checked against the run it was measuring.
**Read `CS` at an entry breakpoint for the run in hand; never carry a load
address across configurations.**
