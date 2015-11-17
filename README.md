# Portaudio-Pulseaudio -driver
This is HostAPI Pulseaudio-driver for Portaudio. Pulseaudio in Common audio API for Linux today.
  * Pulseaudio (http://www.freedesktop.org/wiki/Software/PulseAudio/)
  * Official Portaudio (http://www.portaudio.com/)

## Little warning
This is just for Linux and if you don't know what Pulseaudio or Portaudio is you don't need this.<br>
**THIS IS NOT OFFICIAL PORTAUDIO REPOSITORY! IT IS: https://www.assembla.com/spaces/portaudio/subversion/source/HEAD/portaudio/trunk**<br>
*I don't want any Pull Requests or bug reports other than Pulseaudio driver related.*

## Compiling
You need
  * aclocal
  * autoconf
  * automake
  * libtools
  * make

This should get this compile:

1. aclocal
2. libtoolize -f
3. autoconf -f
4. automake -f
5. ./configure
6. make

There is also CMake version. But haven't got time to investigate it yet.

## Using
This is _Beta software_ and it will probably eat your dog's food but I'm using it like this:
<pre>
LD_CONFIG_PATH=/location/of/portaudio-pulseaudio/lib/.libs ./your-share-application
</pre>
*Again There is strong possibility that this driver won't work as you like. _You have been warned_*

# Bugs
I know many of them but I welcome bug reports or Pull Requests to fix them (**but only Portaudio-Pulseaudio-driver related**).<br>
If you have Linux ALSA/Jack related Portaudio problem please refer: http://www.portaudio.com/contacts.html.


