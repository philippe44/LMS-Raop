# LMS-to-Raop
LMS to Air Playbridge

Allow AirPlay players to be used by Logitech Media Server, as a normal Logitech Hardware
Pre-packaged versions for Windows (XP and above), Linux (x86, x64 and ARM) can be found here
https://sourceforge.net/projects/lms-to-raop/ and here https://sourceforge.net/projects/lms-plugins-philippe44/

=============================================
To re-compile, use makefile (Linux only, need some mods for OSX and Windows) using:
https://github.com/philippe44/mDNS-SD
https://sourceforge.net/projects/pupnp (you just need the libixml part of libupnp)

You also need to find libFLACthe header files of libmad, libmpg123, libfaad2 and libosxr (only if you want resample)
