# LMS-to-Raop
LMS to AirPlay bridge

Allows AirPlay players to be used by Logitech Media Server, as a normal Logitech 
Hardware. It provides synchronisation, replaygain, gapless, fade in/out/cross and 
all other LMS goodies. AirPlay remotes can be used as well

Pre-packaged versions for Windows Linux (x86, x64 and ARM) and OSX can be found here https://sourceforge.net/projects/lms-to-raop and here https://sourceforge.net/projects/lms-plugins-philippe44

Support thread is here: http://forums.slimdevices.com/showthread.php?105198-ANNOUNCE-AirPlay-Bridge-integrate-AirPlay-devices-with-LMS-(squeeze2raop)&p=846204&viewfull=1#post846204

============================================================================

To re-compile, use makefile (none available for Windows, I use Embarcadero IDE) and add
 - pthread for Windows: https://www.sourceware.org/pthreads-win32
 - libupnp: https://sourceforge.net/projects/pupnp
 - nanopb: https://github.com/nanopb/nanopb
 - HTTP download: https://github.com/philippe44/HTTP-Fetcher
 - RAOP client library: https://github.com/philippe44/RAOP-Player
 - jansson: https://github.com/akheron/jansson
 - mDNS-SD (my fork) https://github.com/philippe44/mDNS-SD (use fork v2)
 - mDNS announce: https://github.com/philippe44/TinySVCmDNS
 - Curve25519 crypto: https://github.com/msotoodeh/curve25519
 - ALAC codec: https://github.com/macosforge/alac
 - faad2: http://www.audiocoding.com/
 - libmad: https://www.underbit.com/products/mad/
 - libflac: https://xiph.org/flac/
 - libsoxr: https://sourceforge.net/p/soxr/wiki/Home/
 - libogg & libvorbis: https://xiph.org/vorbis/
