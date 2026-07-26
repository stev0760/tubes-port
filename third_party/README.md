# Third-party code

Vendored dependencies, unmodified. Everything else in this repository is our
own work.

## nuked-opl3

[Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) v1.8 by Nuke.YKT, at
commit `cfedb09efc03f1d7b5fc1f04dd449d77d8c49d50` (2024-07-01).
Licensed **LGPL 2.1 or later** — see `nuked-opl3/LICENSE`.

Files are copied verbatim; do not edit them, so the upstream commit stays
meaningful. Only `opl3.c` and `opl3.h` are needed.

### Why it is here

Tubes targets an AdLib/OPL2 card. Emulating that chip is a solved problem and
not what this project reverse engineers — the part recovered from the original
binary is the **sequencer** in `src/mus.cpp`, transcribed from
`DRIVERS.RES:FMMUSIC.DRV`, which produces the register writes fed to this
emulator.

Keeping the two separate is deliberate. `tubes-port --dump-regs` prints the
sequencer's register stream so it can be diffed against
`tools/mus_decode.py`, which means the port is verifiable without depending on
the synthesis being right. All 10 songs currently match to the byte.

An OPL3 with its "new" bit clear behaves as an OPL2, and the driver never
touches the OPL3 register set, so no compatibility shim is required.
