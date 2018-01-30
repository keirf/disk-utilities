## Installing disk-analyze on Windows 10 (1607+) using Linux subsystem
**by [Tomse](http://retro-commodore.eu) @ retro-commodore.eu**

This document is written as a beginners' step-by-step guide so even
those who are new to Linux should be able to use it.

Word of caution: The Linux distribution is a large download. Doing
this on a metered line is not recommended. Use on your own accord.


## Prepare for the linux subsystem.

Hit the ”Start/Windows” button, and type in ”settings” and hit enter.

In the left side menu, click on ”For developers” and choose ”Developer
mode”.

Hit the ”Start/Windows” button again, this time type in ”control
panel” and hit enter.

Choose ”Programs” and choose ”Turn Windows features on or off”

Scroll all the way down to the bottom of the list, and put a checkmark
in front of ”Windows Subsystem for Linux”

Reboot your computer if asked.

Hit the ”Start/Windows” button again, and type in ”store” and hit
enter.  In the search field enter ”ubuntu” and hit enter. Click
”install” and wait for it to download and install. This package is
quite big, so you might want to take a break. Warning for those who
pay for traffic.

When it’s installed a notification appears and you can click ”Launch”,
otherwise you can find ”Ubuntu” in the start menu, or search for it.

The first time you start up, you need to enter a username and
password. For the sake of this documentation I’ll use ”ubuntu” as
username and ”p4$$w0rd” as password. Please don’t use this for your
setup for your own security.

Congratulations, you’ve successfully installed Linux on Windows.


## Getting Linux ready

Next we’ll make sure Ubuntu is updated.

Note: whenever a line starts with #, you can copy paste the following
text into the shell window (linux command prompt).

```
# sudo apt update
```

TIP: Since this is the first time you type in ”sudo” you’ll be
prompted for a password here. Type the one you gave your user, for
this document it’s ”p4$$w0rd” without the quotes. If you don’t let the
session cache timeout you won’t be needing to use the password again
anytime soon. With too long wait of inactivity, you’d be required to
use it again.

```
# sudo apt upgrade
```

You’ll be asked if you want to continue [Y/n], hit the enter key here.
TIP: Whenever Linux asks you a question you’ll see an uppercase letter
and one or more lowercase. The uppercase letter is the default choice
if hitting the enter key.


## Install some missing tools
```
# sudo apt install make gcc g++ unzip
```
Hit enter when prompted to continue.


## Time to download the sources and compile

## Create a working directory.
```
# mkdir projects
```

And lets go into this newly created directory.
```
# cd projects
```


## Building IPF Support

If you don’t want/need support for kryoflux IPF & CT you can skip the
next steps.

Your build may now fail with an error similar to "caps.c:12:28: fatal
error: caps/capsimage.h: No such file or directory".  This error
occurs if you do not have the CAPS library header file in
/usr/include/caps. In this case you must download and install as
follows:
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

## Back on track: Get the disk utilities

Make sure you are in the right directory
```
# cd ~/projects
```

Get the latest disk utilities (disk-analyze).
```
# git clone https://github.com/keirf/Disk-Utilities.git
```

If you wish to read IPF and CT Raw images with disk-analyse then you must
explicitly configure support by specifying caps=y in the build process.
```
  # cd Disk-Utilities
  # make clean
  # caps=y make
  # sudo caps=y make install
```

Otherwise you just need to do the following.
```
  # make clean
  # make
  # sudo make install
```

as a last step before starting to work with disk-analyze, run 
```
  # sudo ldconfig
```

## Accessing your local windows drives
All the non network mapped drives are found in /mnt/ with their
respective driveletters.  So if you have a kryoflux dump of a diskette
in i.e D:\dumps\GianaSisters, you can navigate to this folder using
the "cd" command i.e.
Example:
```
cd /mnt/d/GianaSisters
``` 

Let's say all the streamfiles start with "track"  i.e. "track00.0.raw"
you can now convert the streamfiles to ipf, adf or hfe etc.

Example:
```
disk-analyze track ../GianaSisters.hfe
``` 
"../" will save the output file in the parent directory from where you
are standing, in this example D:\dumps in your Windows explorer

Another example:
```
cd /mnt/d/dumps
disk-analyze GianaSisters/track GianaSisters.hfe
``` 
Will do the same as the prior example.
