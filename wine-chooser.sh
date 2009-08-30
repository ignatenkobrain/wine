#!/bin/sh
HAVE_32=false
HAVE_64=false

if [ -x "/usr/bin/wine32" ]; then
HAVE_32=true
fi

if [ -x "/usr/bin/wine64" ]; then
HAVE_64=true
fi

if [ -f "${1}" ]; then
    archtype=`file "${1}" | cut -d':' -f2 | cut -d' ' -f2`

    if [ ${archtype} = 'PE32+' ]; then # 64bit
	if $HAVE_64; then
	    exec "/usr/bin/wine64" "${@}";
	elif [ `uname -m` = 'x86_64' ]; then
	    echo "Your are trying to run a 64bit application. Please install the 64bit version of wine."
	    echo "You can achieve this by running 'su -c \"yum install \\\"wine(x86-64)\\\"\"'"
	else
	    echo "Your are trying to run a 64bit application on a 32bit installation of Fedora. You need a 64bit version of Fedora to run this application."
	fi
    else
	if $HAVE_32; then
	    exec "/usr/bin/wine32" "${@}"
	else
	    echo "Your are trying to run a 32bit application. Please install the 32bit version of wine."
	    echo "You can achieve this by running 'su -c \"yum install \\\"wine(x86-32)\\\"\"'"
	fi
    fi
else
    if $HAVE_64; then
	exec "/usr/bin/wine64" "${@}";
    else
	exec "/usr/bin/wine32" "${@}";
    fi
fi
