# Modern DOS 8x16 - a CP437 text-mode font

Vendored **unmodified**. `fontlist.js` is 256 glyphs of 16 bytes each, one
scanline per byte, MSB leftmost - the format a VGA character generator holds.

| | |
|---|---|
| Typeface | Modern DOS 8x16, by Jayvee Enaguas (HarvettFox96) |
| Bitmap extraction | [`susam/pcface`](https://github.com/susam/pcface), by Susam Pal |
| File taken | `out/moderndos-8x16/fontlist.js` |
| Licence | **MIT or CC0 1.0** - see `LICENSE-MIT.md` and `LICENSE-CC0.txt` |

`tools/gen_cp437_font.py` turns it into `src/cp437_font.cpp`. Nothing here is
edited; regenerate rather than patching, the same rule the other generated
sources follow.

## Why this font and not the obvious one

The best-known CP437 recreations are VileR's *Ultimate Oldschool PC Font Pack*,
and `pcface` ships those too - as `out/oldschool-vga-8x16/`. **They are not
used here**, because they are GPL-3 or CC BY-SA 4.0. ShareAlike on a data file
compiled into an MIT binary is an ambiguity this project does not need, and
Creative Commons themselves advise against CC licences for software. Modern DOS
is dual MIT/CC0 and reconciles with nothing.

## On the copyright of the font it resembles

The original IBM VGA 8x16 ROM font is, under US law, almost certainly not a
copyrightable work at all:

* [37 CFR 202.1(e)](https://www.law.cornell.edu/cfr/text/37/202.1) excludes
  typeface designs outright;
* the Copyright Office's 1992 *Policy Decision on Copyrightability of Digitized
  Typefaces* holds that data "which merely represents an electronic depiction of
  a particular typeface" creates no work of authorship;
* the line is at font *programs* - a scalable format with hinting is protectable
  as a computer program (*Adobe v. Southern Software*, 1998). A 4 KB bitmap is
  not one.

None of that is relied on. A font with an explicit permissive licence costs
nothing and removes the question, which is the same reason this project prefers
an oracle to an opinion everywhere else.

## On fidelity

Modern DOS is a recreation, not the IBM ROM bytes, and for this screen that is
not a compromise. **Tubes never rendered these glyphs.** `TUBESEND.BIN` is
character codes written to `0xB800`; the *video card's* font drew them, so a
player with a different VGA BIOS, a clone chipset or a font-loading TSR saw
different pixels from the same file. There is no single correct bitmap to be
faithful to - "faithful" here means a period-correct VGA text font.

The glyphs where exactness does matter are the nine the screen draws its boxes
and drop shadows with, because the art only reads if they tile seamlessly:

    0xB3 |    0xBF ,    0xC0 `    0xC4 -    0xD9 '    0xDA .
    0xDB block   0xDC lower half   0xDF upper half

Checked in this font rather than assumed: the vertical stem sits at columns
3-4, the horizontal at row 7, all four corners join those exactly, and 0xDB is
solid across all sixteen rows. They tile. An earlier plan to generate these
nine procedurally was dropped once that was verified - the font's own are
correct, and one fewer bespoke part is worth more than the guarantee.

One deliberate difference from the IBM original is recorded so it is not later
mistaken for a bug: Modern DOS splits the half blocks at row 7 rather than row
8, so `0xDC` is nine rows tall where IBM's is eight. It costs one pixel on a
drop shadow and breaks no join.
