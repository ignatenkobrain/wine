#!/bin/sh
if [ -x /usr/lib*/alsa-lib/libasound_module_pcm_pulse.so ] && [ -x "/usr/bin/padsp" ] ; then
    echo "Running padsp as pulseaudio wrapper for wine"
    exec padsp -n Wine -- /usr/bin/wine_bin "$@"
else
    exec /usr/bin/wine_bin "$@"
fi
