# amiga-native/diskread


## Usage

`diskread <target_filename> <drive #>`

Dump 64kB of MFM data per track, in sequence, to `<target_filename>`.
A byte of timing information is dumped before each MFM byte, indicating
how many CIA timer ticks occurred while the relevant MFM byte was streamed
from disk. Bit 7 of this byte is used to indicate if DSKINDEX was asserted
during reading of this MFM bytes. Hence a total of 128kB data is dumped
to the target file per track.


## Compiling

Just type `smake`. `smake clean` to tidy up generated files.
Compiled with SAS/C v6.50.
