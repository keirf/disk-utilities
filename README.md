# Disk Utilities 

A collection of utilities for ripping, dumping, analysing, and modifying
disk images. All code is public domain (see the [COPYING](COPYING) file).

Targets Linux, Mac OS X, and Windows (using Cygwin or MinGW), and
should be very POSIX portable. [amiga-native/](amiga-native/) targets
classic Amiga m68k, tested with SAS/C 6.50.

Prerequisites: You need at least Make and a C compiler (GCC; for Clang
you'll have to edit CC in [Rules.mk](Rules.mk)). On Mac OS X, the
Xcode Command Line Tools include this. On Windows, you need
Cygwin/MinGW, Make, and a C compiler. Alternatively, on Windows 10 and 11
you can instead use [WSL](WSL).


## Building & Installing:

Run these commands in the *root directory* of the source tree:
```
  # make clean
  # make
  # sudo make install
```

To install to a local path (substitute `path/to/install`):
```
  # make PREFIX=path/to/install
  # make install PREFIX=path/to/install
```

On systems other than macOS, you may need to update your library
search path for a local install. See guidance below.

[Tomse](http://retro-commodore.eu) has made a
[beginners' guide](docs/LinuxSubsysOnWindows.md) for building
Disk-Utilities on Windows 10 using the Linux Subsystem.

## IPF & CT Raw support: The CAPS/SPS IPF support library

If you wish to read IPF and CT Raw images with disk-analyse then you must
explicitly configure support by specifying caps=y in the build process.
```
  # make clean
  # caps=y make
  # sudo caps=y make install
```

Your build may now fail with an error similar to
"caps.c:12:28: fatal error: caps/capsimage.h: No such file or directory".
This error occurs if you do not have the CAPS library header file in
/usr/include/caps. In this case you must download and install as follows:
```
  # wget -O ipflib42_linux-x86_64.tar.gz http://www.softpres.org/_media/files:ipflib42_linux-x86_64.tar.gz
  # tar xf ipflib42_linux-x86_64.tar.gz
  # cd x86_64-linux-gnu-capsimage
  # sudo cp -a include/caps /usr/include
```

You must also have v4 or v5 of the CAPS library installed (v5 is
required for CT Raw image support). Failure to install the library
will result in an informative error message when you attempt to read a
CTR or IPF image.  You can download, build and install v5 of the
support library as follows:
```
  # wget http://www.kryoflux.com/download/spsdeclib_5.1_source.zip
  # unzip spsdeclib_5.1_source.zip
  # unzip capsimg_source_linux_macosx.zip
  # cd capsimg_source_linux_macosx/CAPSImg
  # chmod u+x configure
  # ./configure
  # make
  # sudo make install
  # cd /usr/local/lib
  # sudo ln -s libcapsimage.so.5.1 libcapsimage.so.5
```


## Library search path ("error while loading shared libraries: libdisk.so.0"):

Note that libdisk.so will need to be on the run-time linker's search
path for many of these tools to run. There are a few ways to ensure this:
 1. Use LD_LIBRARY_PATH on Linux, DYLD_LIBRARY_PATH on Mac OS X, or PATH on
 Windows. This environment variable specifies a set of paths for the linker
 to search. So, to run disk-analyse without running 'make install', you could
 run it as follows:
```
  # LD_LIBRARY_PATH=libdisk disk-analyse/disk-analyse # Linux
  # DYLD_LIBRARY_PATH=libdisk disk-analyse/disk-analyse # Mac OS X
  # PATH=$PATH:`pwd`/libdisk disk-analyse/disk-analyse # Cygwin
```
 2. Install to a location on the system-wide search path. If the default
 install location for libdisk (/usr/local/lib) is not searched by default
 then you will need to add the path to /etc/ld.so.conf (or to a file in
 /etc/ld.so.conf.d/ in some Linux distros). For example:
```
  # echo "/usr/local/lib" >>/etc/ld.so.conf
  # ldconfig
```
 You should now be able to run disk-analyse and other tools just fine.

 3. Build with the -rpath linker option.  This will embed the run-time library
 path in the executables.  For example:
```
  # make clean
  # make install PREFIX=/opt/disk-utilities 'LDRUNPATH=$(LIBDIR)'
```


## Brief Descriptions:

[**disk-analyse/**](disk-analyse/)
   Disk image conversion tool.
   - Read-only support:
    * Kryoflux STREAM
    * DiscFerret (.DFI)
    * Amiga diskread (.DAT)
    * SPS/CTRaw
   - Read/write support:
    * SPS/IPF
    * ADF, Extended ADF
    * LibDisk (.DSK)
    * Supercard Pro (.SCP)
    * ImageDisk (.IMD)
    * Sector Image (.IMG)
    * HxC Floppy Emulator (.HFE) (orig,v3)

[**libdisk/**](libdisk/)
    A library for converting and manipulating disk images. It can create
    disk images in a range of formats from Kryoflux STREAM and SPS/IPF images
    (among others), and then allow these to be accessed and modified.

[**adfbb/**](adfbb/)
    Read/modify/write ADF boot blocks. Mainly I use for stuffing bootblock
    sectors and recomputing the checksum.

[**adfread/**](adfread/)
    Read file contents of an ADF and optionally dump into local host filesystem

[**adfwrite/**](adfwrite/)
    Stuff data into selected sectors of an ADF image

[**amiga-native/diskread**](amiga-native/)
    Dumps a disk to a (large, 20MB!) file. Really you need an
    Amiga equipped with PCMCIA-CF for this to be useful.

[**m68k/**](m68k/)
  m68k/
    M68k disassembler/emulator library.
  amiga/
    Amiga emulator (very limited, sufficient to run track loaders and
    protection routines).
  disassemble
    Example utility for disassembling raw binary files
  copylock
    Run a given RNC Copylock routine in emulated environment, copying
    decrypted code to a shadow buffer for subsequent disassembly and dump

[**ipfinfo/**](ipfinfo/)
    Dump information about an SPS/IPF image file.

[**imdinfo/**](imdinfo/)
    Dump information about an IMD image file.

[**scp/**](scp/)
    Dump floppy flux data from Supercard Pro to a .SCP image file.

[WSL]: https://docs.microsoft.com/en-us/windows/wsl/
