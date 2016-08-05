# LMS-to-Raop
LMS to AirPlay bridge

Allow AirPlay players to be used by Logitech Media Server, as a normal Logitech 
Hardware. It provides synchronisation, replaygain, gapless,fade in/out/cross and 
all other LMS goodies. AirPLay remotes can be used as well

Pre-packaged versions for Windows Linux (x86, x64 and ARM) and OSX can be found 
here https://sourceforge.net/projects/lms-to-raop/ and 
here https://sourceforge.net/projects/lms-plugins-philippe44/

=============================================
To re-compile, use makefile (none available for Windows, I use Embarcadero IDE) and add
https://github.com/philippe44/mDNS-SD
https://github.com/philippe44/TinySVCmDNS
https://github.com/philippe44/HTTP-Fetcher
https://sourceforge.net/projects/pupnp (you just need the libixml part of libupnp)
https://www.sourceware.org/pthreads-win32/
Use also faad, limad, mpg123 and libflac, libsoxr (use headers only if you do shared libs, 
full library if you want static link)
