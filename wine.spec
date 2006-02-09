%define __global_cflags -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fno-stack-protector --param=ssp-buffer-size=4 -m32 -march=i386 -mtune=pentium4 -fasynchronous-unwind-tables

Name:		wine
Version:	0.9.7
Release:	3%{?dist}
Summary:	A Windows 16/32/64 bit emulator

Group:		Applications/Emulators
License:	LGPL
URL:		http://www.winehq.org/
# special fedora tarball without winemp3 stuff
Source0:        wine-0.9.7-fe.tar.bz2
Source1:	wine.init
Source2:	wine-fonts-20050524.tar.gz
Source3:        wine-README-Fedora
Source4:        wine-32.conf
Source100:      wine-notepad.desktop
Source101:      wine-regedit.desktop
Source102:      wine-uninstaller.desktop
Source103:      wine-winecfg.desktop
Source104:      wine-winefile.desktop
Source105:      wine-winemine.desktop
Source106:      wine-winhelp.desktop
Patch2:		wine-20050524-generated.patch

Buildroot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

ExclusiveArch: %{ix86}

BuildRequires:  autoconf
BuildRequires:	bison
BuildRequires:	flex
BuildRequires:  alsa-lib-devel
BuildRequires:  arts-devel
BuildRequires:  audiofile-devel
BuildRequires:  esound-devel
BuildRequires:  freeglut-devel
BuildRequires:  lcms-devel
BuildRequires:  libieee1284-devel
BuildRequires:  libjpeg-devel
BuildRequires:  libstdc++-devel
BuildRequires:  libusb-devel
BuildRequires:  libxml2-devel
BuildRequires:  libxslt-devel
BuildRequires:  ncurses-devel
BuildRequires:  openldap-devel
BuildRequires:  sane-backends-devel
BuildRequires:  zlib-devel
BuildRequires:  desktop-file-utils
BuildRequires:  fontforge
# modular x
BuildRequires:  libX11-devel
BuildRequires:  mesa-libGL-devel mesa-libGLU-devel
BuildRequires:  libXxf86dga-devel libXxf86vm-devel
BuildRequires:  libXrandr-devel libXrender-devel libXext-devel

BuildRequires:  fontconfig-devel
BuildRequires:  giflib-devel
BuildRequires:  cups-devel
BuildRequires:  libXmu-devel
BuildRequires:  libXi-devel

Requires(post): /sbin/ldconfig, /sbin/chkconfig, /sbin/service,
Requires(post): /usr/bin/update-desktop-database
Requires(preun): /sbin/chkconfig, /sbin/service
Requires(postun): /sbin/ldconfig, /usr/bin/update-desktop-database

%description
While Wine is usually thought of as a Windows(TM) emulator, the Wine
developers would prefer that users thought of Wine as a Windows
compatibility layer for UNIX. This package includes a program loader,
which allows unmodified Windows 3.x/9x/NT binaries to run on x86 and x86_64
Unixes. Wine does not require MS Windows, but it can use native system
.dll files if they are available.

%package tools
Summary:        Additional wine tools
Group:		Applications/Emulators
Requires:       %{name} = %{version}-%{release}

%description tools
Additional wine tools

%package arts
Summary: Arts sound support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description arts
Arts sound support for wine

%package esd
Summary: ESD sound support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description esd
ESD sound support for wine

%package jack
Summary: JACK sound support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description jack
JACK sound support for wine

%package nas
Summary: NAS sound support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description nas
JACK sound support for wine

%package ldap
Summary: LDAP support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description ldap
LDAP support for wine

%package cms
Summary: Color Managment for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description cms
Color Management for wine

%package twain
Summary: Twain support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description twain
Twain support for wine

%package capi
Summary: ISDN support for wine
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description capi
ISDN support for wine

%package devel
Summary: Wine development environment
Group: System Environment/Libraries
Requires: wine = %{version}-%{release}

%description devel
Header, include files and library definition files for developing applications
with the Wine Windows(TM) emulation libraries.

%prep
%setup -q
%patch2 -p1 -b .generated
autoconf

%build
export CFLAGS="$RPM_OPT_FLAGS"
%configure \
	--sysconfdir=%{_sysconfdir}/wine --disable-static \
	--x-includes=%{_includedir} --x-libraries=%{_libdir}

%{__make} depend
%{__make}

%install
rm -rf %{buildroot}

%makeinstall \
	includedir=%{buildroot}%{_includedir}/wine \
	sysconfdir=%{buildroot}%{_sysconfdir}/wine \
	dlldir=%{buildroot}%{_libdir}/wine \
	LDCONFIG=/bin/true \
	UPDATE_DESKTOP_DATABASE=/bin/true
%makeinstall \
	-C dlls/wineps \
	includedir=%{buildroot}%{_includedir}/wine \
	sysconfdir=%{buildroot}%{_sysconfdir}/wine \
	dlldir=%{buildroot}%{_libdir}/wine \
	LDCONFIG=/bin/true \
	UPDATE_DESKTOP_DATABASE=/bin/true

mkdir -p %{buildroot}%{_sysconfdir}/wine

# Allow users to launch Windows programs by just clicking on the .exe file...
mkdir -p %{buildroot}%{_initrddir}
install -p -c -m 755 %SOURCE1 %{buildroot}%{_initrddir}/wine

# Install Wine's fonts
mkdir -p %{buildroot}%{_datadir}/fonts/wine
tar xzf %SOURCE2 -C %{buildroot}%{_datadir}/fonts/wine

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE100}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE101}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE102}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE103}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE104}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE105}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  %{SOURCE106}

desktop-file-install \
  --vendor=fedora \
  --dir=$RPM_BUILD_ROOT%{_datadir}/applications \
  --add-category=X-Fedora \
  --delete-original \
  $RPM_BUILD_ROOT%{_datadir}/applications/wine.desktop

cp %{SOURCE3} README-Fedora

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/ld.so.conf.d/
install -p -m644 %{SOURCE4} $RPM_BUILD_ROOT%{_sysconfdir}/ld.so.conf.d/

%clean
rm -rf %{buildroot}

%post
/sbin/ldconfig
update-desktop-database &>/dev/null || :
if [ $1 = 1 ]; then
/sbin/chkconfig --add wine
/sbin/chkconfig --level 2345 wine on
/sbin/service wine start &>/dev/null || :
fi

%preun
if [ $1 = 0 ]; then
	/sbin/service wine stop >/dev/null 2>&1
	/sbin/chkconfig --del wine
fi

%postun
/sbin/ldconfig
update-desktop-database &>/dev/null || :

%post arts -p /sbin/ldconfig
%postun arts -p /sbin/ldconfig

%post esd -p /sbin/ldconfig
%postun esd -p /sbin/ldconfig

%post jack -p /sbin/ldconfig
%postun jack -p /sbin/ldconfig

%post nas -p /sbin/ldconfig
%postun nas -p /sbin/ldconfig

%post ldap -p /sbin/ldconfig
%postun ldap -p /sbin/ldconfig

%post cms -p /sbin/ldconfig
%postun cms -p /sbin/ldconfig

%post twain -p /sbin/ldconfig
%postun twain -p /sbin/ldconfig

%post capi -p /sbin/ldconfig
%postun capi -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc ANNOUNCE COPYING.LIB ChangeLog DEVELOPERS-HINTS LICENSE LICENSE.OLD
%doc AUTHORS README-Fedora README VERSION
%doc documentation/*
%{_bindir}/msiexec
%{_bindir}/regedit
%{_bindir}/regsvr32
%{_bindir}/wine
%{_bindir}/wineboot
%{_bindir}/winebrowser
%{_bindir}/wineconsole
%{_bindir}/wineprefixcreate
%{_bindir}/wineshelllink
%{_bindir}/winecfg
%{_bindir}/uninstaller
%{_initrddir}/wine
%{_libdir}/wine/expand.exe.so
%{_libdir}/wine/msiexec.exe.so
%{_libdir}/wine/regedit.exe.so
%{_libdir}/wine/regsvr32.exe.so
%{_libdir}/wine/rpcss.exe.so
%{_libdir}/wine/rundll32.exe.so
%{_libdir}/wine/start.exe.so
%{_libdir}/wine/wineboot.exe.so
%{_libdir}/wine/winebrowser.exe.so
%{_libdir}/wine/wineconsole.exe.so
%{_libdir}/wine/winemenubuilder.exe.so
%{_libdir}/wine/winevdm.exe.so
%{_libdir}/wine/winecfg.exe.so
%{_libdir}/wine/uninstaller.exe.so
%{_datadir}/applications/fedora-wine.desktop
%{_datadir}/applications/fedora-wine-regedit.desktop
%{_datadir}/applications/fedora-wine-uninstaller.desktop
%{_datadir}/applications/fedora-wine-winecfg.desktop
%{_mandir}/man1/wine.1.gz
%{_datadir}/wine/generic.ppd
%{_datadir}/wine/wine.inf
%{_bindir}/wine-kthread
%{_bindir}/wine-preloader
%{_bindir}/wine-pthread
%{_bindir}/winelauncher
%{_bindir}/wineserver
%{_mandir}/man1/wineserver.1*
%{_libdir}/libwine.so.1
%{_libdir}/libwine_unicode.so.1
%dir %{_libdir}/wine
%{_libdir}/wine/activeds.dll.so
%{_libdir}/wine/advapi32.dll.so
%{_libdir}/wine/advpack.dll.so
%{_libdir}/wine/amstream.dll.so
%{_libdir}/wine/atl.dll.so
%{_libdir}/wine/avicap32.dll.so
%{_libdir}/wine/avifil32.dll.so
%{_libdir}/wine/avifile.dll.so
%{_libdir}/wine/aviinfo.exe.so
%{_libdir}/wine/aviplay.exe.so
%{_libdir}/wine/cabinet.dll.so
%{_libdir}/wine/cards.dll.so
%{_libdir}/wine/cfgmgr32.dll.so
%{_libdir}/wine/clock.exe.so
%{_libdir}/wine/comcat.dll.so
%{_libdir}/wine/comctl32.dll.so
%{_libdir}/wine/comdlg32.dll.so
%{_libdir}/wine/comm.drv.so
%{_libdir}/wine/commdlg.dll.so
%{_libdir}/wine/compobj.dll.so
%{_libdir}/wine/crtdll.dll.so
%{_libdir}/wine/crypt32.dll.so
%{_libdir}/wine/cryptdll.dll.so
%{_libdir}/wine/ctl3d.dll.so
%{_libdir}/wine/ctl3d32.dll.so
%{_libdir}/wine/ctl3dv2.dll.so
%{_libdir}/wine/d3dim.dll.so
%{_libdir}/wine/d3drm.dll.so
%{_libdir}/wine/d3dxof.dll.so
%{_libdir}/wine/dbghelp.dll.so
%{_libdir}/wine/dciman32.dll.so
%{_libdir}/wine/ddeml.dll.so
%{_libdir}/wine/ddraw.dll.so
%{_libdir}/wine/devenum.dll.so
%{_libdir}/wine/dinput.dll.so
%{_libdir}/wine/dinput8.dll.so
%{_libdir}/wine/dispdib.dll.so
%{_libdir}/wine/display.drv.so
%{_libdir}/wine/dmband.dll.so
%{_libdir}/wine/dmcompos.dll.so
%{_libdir}/wine/dmime.dll.so
%{_libdir}/wine/dmloader.dll.so
%{_libdir}/wine/dmscript.dll.so
%{_libdir}/wine/dmstyle.dll.so
%{_libdir}/wine/dmsynth.dll.so
%{_libdir}/wine/dmusic.dll.so
%{_libdir}/wine/dmusic32.dll.so
%{_libdir}/wine/dplay.dll.so
%{_libdir}/wine/dplayx.dll.so
%{_libdir}/wine/dpnet.dll.so
%{_libdir}/wine/dpnhpast.dll.so
%{_libdir}/wine/dsound.dll.so
%{_libdir}/wine/dswave.dll.so
%{_libdir}/wine/dxdiagn.dll.so
%{_libdir}/wine/eject.exe.so
%{_libdir}/wine/gdi.exe.so
%{_libdir}/wine/gdi32.dll.so
%{_libdir}/wine/hh.exe.so
%{_libdir}/wine/hhctrl.ocx.so
%{_libdir}/wine/iccvid.dll.so
%{_libdir}/wine/icinfo.exe.so
%{_libdir}/wine/icmp.dll.so
%{_libdir}/wine/ifsmgr.vxd.so
%{_libdir}/wine/imaadp32.acm.so
%{_libdir}/wine/imagehlp.dll.so
%{_libdir}/wine/imm.dll.so
%{_libdir}/wine/imm32.dll.so
%{_libdir}/wine/iphlpapi.dll.so
%{_libdir}/wine/itss.dll.so
%{_libdir}/wine/joystick.drv.so
%{_libdir}/wine/kernel32.dll.so
%{_libdir}/wine/keyboard.drv.so
%{_libdir}/wine/krnl386.exe.so
%{_libdir}/wine/lz32.dll.so
%{_libdir}/wine/lzexpand.dll.so
%{_libdir}/wine/mapi32.dll.so
%{_libdir}/wine/mciavi32.dll.so
%{_libdir}/wine/mcicda.dll.so
%{_libdir}/wine/mciseq.dll.so
%{_libdir}/wine/mciwave.drv.so
%{_libdir}/wine/midimap.dll.so
%{_libdir}/wine/mlang.dll.so
%{_libdir}/wine/mmdevldr.vxd.so
%{_libdir}/wine/mmsystem.dll.so
%{_libdir}/wine/monodebg.vxd.so
%{_libdir}/wine/mouse.drv.so
%{_libdir}/wine/mpr.dll.so
%{_libdir}/wine/msacm.dll.so
%{_libdir}/wine/msacm.drv.so
%{_libdir}/wine/msacm32.dll.so
%{_libdir}/wine/msadp32.acm.so
%{_libdir}/wine/msdmo.dll.so
%{_libdir}/wine/msg711.acm.so
%{_libdir}/wine/mshtml.dll.so
%{_libdir}/wine/msi.dll.so
%{_libdir}/wine/msimg32.dll.so
%{_libdir}/wine/msisys.ocx.so
%{_libdir}/wine/msnet32.dll.so
%{_libdir}/wine/msrle32.dll.so
%{_libdir}/wine/msvcrt.dll.so
%{_libdir}/wine/msvcrt20.dll.so
%{_libdir}/wine/msvcrt40.dll.so
%{_libdir}/wine/msvcrtd.dll.so
%{_libdir}/wine/msvfw32.dll.so
%{_libdir}/wine/msvidc32.dll.so
%{_libdir}/wine/msvideo.dll.so
%{_libdir}/wine/mswsock.dll.so
%{_libdir}/wine/msxml3.dll.so
%{_libdir}/wine/netapi32.dll.so
%{_libdir}/wine/newdev.dll.so
%{_libdir}/wine/ntdll.dll.so
%{_libdir}/wine/objsel.dll.so
%{_libdir}/wine/odbc32.dll.so
%{_libdir}/wine/odbccp32.dll.so
%{_libdir}/wine/ole2.dll.so
%{_libdir}/wine/ole2conv.dll.so
%{_libdir}/wine/ole2disp.dll.so
%{_libdir}/wine/ole2nls.dll.so
%{_libdir}/wine/ole2prox.dll.so
%{_libdir}/wine/ole2thk.dll.so
%{_libdir}/wine/ole32.dll.so
%{_libdir}/wine/oleacc.dll.so
%{_libdir}/wine/oleaut32.dll.so
%{_libdir}/wine/olecli.dll.so
%{_libdir}/wine/olecli32.dll.so
%{_libdir}/wine/oledlg.dll.so
%{_libdir}/wine/olepro32.dll.so
%{_libdir}/wine/olesvr.dll.so
%{_libdir}/wine/olesvr32.dll.so
%{_libdir}/wine/powrprof.dll.so
%{_libdir}/wine/psapi.dll.so
%{_libdir}/wine/qcap.dll.so
%{_libdir}/wine/quartz.dll.so
%{_libdir}/wine/rasapi16.dll.so
%{_libdir}/wine/rasapi32.dll.so
%{_libdir}/wine/riched20.dll.so
%{_libdir}/wine/riched32.dll.so
%{_libdir}/wine/rpcrt4.dll.so
%{_libdir}/wine/rsabase.dll.so
%{_libdir}/wine/rsaenh.dll.so
%{_libdir}/wine/secur32.dll.so
%{_libdir}/wine/sensapi.dll.so
%{_libdir}/wine/serialui.dll.so
%{_libdir}/wine/setupapi.dll.so
%{_libdir}/wine/setupx.dll.so
%{_libdir}/wine/shdocvw.dll.so
%{_libdir}/wine/shell.dll.so
%{_libdir}/wine/shell32.dll.so
%{_libdir}/wine/shfolder.dll.so
%{_libdir}/wine/shlwapi.dll.so
%{_libdir}/wine/snmpapi.dll.so
%{_libdir}/wine/sound.drv.so
%{_libdir}/wine/stdole2.tlb.so
%{_libdir}/wine/stdole32.tlb.so
%{_libdir}/wine/sti.dll.so
%{_libdir}/wine/storage.dll.so
%{_libdir}/wine/stress.dll.so
%{_libdir}/wine/system.drv.so
%{_libdir}/wine/tapi32.dll.so
%{_libdir}/wine/toolhelp.dll.so
%{_libdir}/wine/typelib.dll.so
%{_libdir}/wine/unicows.dll.so
%{_libdir}/wine/url.dll.so
%{_libdir}/wine/urlmon.dll.so
%{_libdir}/wine/user.exe.so
%{_libdir}/wine/user32.dll.so
%{_libdir}/wine/usp10.dll.so
%{_libdir}/wine/uxtheme.dll.so
%{_libdir}/wine/vdhcp.vxd.so
%{_libdir}/wine/vdmdbg.dll.so
%{_libdir}/wine/ver.dll.so
%{_libdir}/wine/version.dll.so
%{_libdir}/wine/vmm.vxd.so
%{_libdir}/wine/vnbt.vxd.so
%{_libdir}/wine/vnetbios.vxd.so
%{_libdir}/wine/vtdapi.vxd.so
%{_libdir}/wine/vwin32.vxd.so
%{_libdir}/wine/w32skrnl.dll.so
%{_libdir}/wine/w32sys.dll.so
%{_libdir}/wine/win32s16.dll.so
%{_libdir}/wine/win87em.dll.so
%{_libdir}/wine/winaspi.dll.so
%{_libdir}/wine/windebug.dll.so
%{_libdir}/wine/wineaudioio.drv.so
%{_libdir}/wine/winedos.dll.so
%{_libdir}/wine/wineoss.drv.so
%{_libdir}/wine/winex11.drv.so
%{_libdir}/wine/wing.dll.so
%{_libdir}/wine/wininet.dll.so
%{_libdir}/wine/winmm.dll.so
%{_libdir}/wine/winnls.dll.so
%{_libdir}/wine/winnls32.dll.so
%{_libdir}/wine/winsock.dll.so
%{_libdir}/wine/winspool.drv.so
%{_libdir}/wine/wintab.dll.so
%{_libdir}/wine/wintab32.dll.so
%{_libdir}/wine/wintrust.dll.so
%{_libdir}/wine/wnaspi32.dll.so
%{_libdir}/wine/wow32.dll.so
%{_libdir}/wine/wprocs.dll.so
%{_libdir}/wine/ws2_32.dll.so
%{_libdir}/wine/wsock32.dll.so
%{_libdir}/wine/wtsapi32.dll.so
%{_libdir}/wine/security.dll.so
%dir %{_datadir}/fonts/wine
%{_datadir}/fonts/wine/wine_coure.fon
%{_datadir}/fonts/wine/wine_couree.fon
%{_datadir}/fonts/wine/wine_courer.fon
%{_datadir}/fonts/wine/wine_marlett.ttf
%{_datadir}/fonts/wine/wine_sserife.fon
%{_datadir}/fonts/wine/wine_sserifee.fon
%{_datadir}/fonts/wine/wine_sserifer.fon
%{_datadir}/fonts/wine/wine_vgasys.fon
%{_datadir}/fonts/wine/wine_vgasyse.fon
%{_datadir}/fonts/wine/wine_vgasysr.fon
%{_datadir}/fonts/wine/coue1255.fon
%{_datadir}/fonts/wine/coue1256.fon
%{_datadir}/fonts/wine/coue1257.fon
%{_datadir}/fonts/wine/coure.fon
%{_datadir}/fonts/wine/couree.fon
%{_datadir}/fonts/wine/coureg.fon
%{_datadir}/fonts/wine/courer.fon
%{_datadir}/fonts/wine/couret.fon
%{_datadir}/fonts/wine/cvgasys.fon
%{_datadir}/fonts/wine/hvgasys.fon
%{_datadir}/fonts/wine/jvgasys.fon
%{_datadir}/fonts/wine/marlett.ttf
%{_datadir}/fonts/wine/ssee1255.fon
%{_datadir}/fonts/wine/ssee1256.fon
%{_datadir}/fonts/wine/ssee1257.fon
%{_datadir}/fonts/wine/ssee874.fon
%{_datadir}/fonts/wine/sserife.fon
%{_datadir}/fonts/wine/sserifee.fon
%{_datadir}/fonts/wine/sserifeg.fon
%{_datadir}/fonts/wine/sserifer.fon
%{_datadir}/fonts/wine/sserifet.fon
%{_datadir}/fonts/wine/svgasys.fon
%{_datadir}/fonts/wine/vgas1255.fon
%{_datadir}/fonts/wine/vgas1256.fon
%{_datadir}/fonts/wine/vgas1257.fon
%{_datadir}/fonts/wine/vgas874.fon
%{_datadir}/fonts/wine/vgasys.fon
%{_datadir}/fonts/wine/vgasyse.fon
%{_datadir}/fonts/wine/vgasysg.fon
%{_datadir}/fonts/wine/vgasysr.fon
%{_datadir}/fonts/wine/vgasyst.fon
%{_libdir}/wine/winealsa.drv.so
%{_libdir}/wine/wineps.drv.so
%{_libdir}/wine/wineps16.drv.so
%{_libdir}/wine/d3d8.dll.so
%{_libdir}/wine/d3d9.dll.so
%{_libdir}/wine/d3dx8.dll.so
%{_libdir}/wine/glu32.dll.so
%{_libdir}/wine/glut32.dll.so
%{_libdir}/wine/opengl32.dll.so
%{_libdir}/wine/wined3d.dll.so
%{_sysconfdir}/ld.so.conf.d/wine-32.conf

%files tools
%defattr(-,root,root,-)
%{_bindir}/notepad
%{_bindir}/progman
%{_bindir}/wcmd
%{_bindir}/winedbg
%{_bindir}/winedump
%{_bindir}/winefile
%{_bindir}/winemaker
%{_bindir}/winemine
%{_bindir}/winepath
%{_bindir}/winhelp
%{_libdir}/wine/explorer.exe.so
%{_libdir}/wine/control.exe.so
%{_libdir}/wine/notepad.exe.so
%{_libdir}/wine/progman.exe.so
%{_libdir}/wine/taskmgr.exe.so
%{_libdir}/wine/wcmd.exe.so
%{_libdir}/wine/winedbg.exe.so
%{_libdir}/wine/winefile.exe.so
%{_libdir}/wine/winemine.exe.so
%{_libdir}/wine/winepath.exe.so
%{_libdir}/wine/winhelp.exe.so
%{_libdir}/wine/winver.exe.so
%{_libdir}/wine/winetest.exe.so
%{_datadir}/applications/fedora-wine-notepad.desktop
%{_datadir}/applications/fedora-wine-winefile.desktop
%{_datadir}/applications/fedora-wine-winemine.desktop
%{_datadir}/applications/fedora-wine-winhelp.desktop

%files arts
%defattr(-,root,root,-)
%{_libdir}/wine/winearts.drv.so

%files esd
%defattr(-,root,root,-)
%{_libdir}/wine/wineesd.drv.so

%files jack
%defattr(-,root,root,-)
%{_libdir}/wine/winejack.drv.so

%files nas
%defattr(-,root,root,-)
%{_libdir}/wine/winenas.drv.so

%files ldap
%defattr(-,root,root,-)
%{_libdir}/wine/wldap32.dll.so

%files cms
%defattr(-,root,root,-)
%{_libdir}/wine/mscms.dll.so

%files twain
%defattr(-,root,root,-)
%{_libdir}/wine/twain.dll.so
%{_libdir}/wine/twain_32.dll.so

%files capi
%defattr(-,root,root,-)
%{_libdir}/wine/capi2032.dll.so

%files devel
%defattr(-,root,root,-)
%{_bindir}/function_grep.pl
%{_bindir}/widl
%{_bindir}/winebuild
%{_bindir}/winecpp
%{_bindir}/winedump
%{_bindir}/wineg++
%{_bindir}/winegcc
%{_bindir}/winemaker
%{_bindir}/wmc
%{_bindir}/wrc
%{_libdir}/*.so
%{_mandir}/man1/widl.1*
%{_mandir}/man1/winebuild.1*
%{_mandir}/man1/winedump.1*
%{_mandir}/man1/winegcc.1*
%{_mandir}/man1/winemaker.1*
%{_mandir}/man1/wmc.1*
%{_mandir}/man1/wrc.1*
%{_mandir}/man1/winedbg.1*
%{_mandir}/man1/wineg++.1*
%{_datadir}/aclocal/wine.m4
%attr(0755, root, root) %dir %{_includedir}/wine
%{_includedir}/wine/*
%{_libdir}/wine/*.a
%{_libdir}/wine/*.def

%changelog
* Thu Feb 09 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.7-3
- fix up tarball

* Wed Feb 08 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.7-2
- fix up post/preun scriplets (#178954)

* Thu Feb 02 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.7-1
- version upgrade

* Thu Jan 19 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.6-1
- version upgrade
- drop wmf exploit patch (part of current version)

* Sun Jan 08 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.5-2
- fix for CVE-2005-4560

* Fri Jan 06 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.5-1
- version upgrade
- fix #177089 (winemine desktop entry should be in Game not in System)
- fix cflags for compile
- test new BR

* Wed Jan 04 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>     
0.9.4-5                                                                 
- fix #176834 

* Mon Jan 02 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.4-4
- add dist

* Sun Jan 01 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.4-3
- use ExclusiveArch instead of ExcludeArch

* Sun Jan 01 2006 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.4-2
- own font directory
- fix devel summary
- add ExcludeArch x86_64 for now

* Sat Dec 31 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.4-1
- version upgrade
- changed wine.init perissions to 0644
- added autoconf BR

* Mon Dec 12 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.3-1
- version upgrade

* Thu Nov 24 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.2-1
- version upgrade

* Thu Nov 17 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de
0.9.1-3
- fix typo in winefile desktop file
- drop in ld config instead of editing ld.so.conf

* Sun Nov 13 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.1-2
- add fontforge BR and include generated fonts...

* Sat Nov 12 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9.1-1
- version upgrade
- move uninstaller and winecfg into wine main package...
- drop wine suite

* Sat Oct 29 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9-3
- s/libwine/wine/

* Thu Oct 27 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9-2
- remerge some subpackages which should be defaults

* Tue Oct 25 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.9-1
- upgrade to new version
- start splitting

* Mon Oct 24 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
0.0-1.20050930
- add fedora readme
- switch to new (old) versioning sheme

* Sat Oct 22 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
20050930-3
- add desktop files
- revisit summary and description
- consistant use of %{buildroot}

* Sat Oct 22 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
20050930-2
- some more spec tuneups...

* Sat Oct 01 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
20050930-1
- version upgrade

* Sun Sep 25 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
20050925-1
- upgrade to current cvs

* Mon Sep 19 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
20050830-1
- version upgrade

* Mon Sep 19 2005 Andreas Bierfert <andreas.bierfert[AT]lowlatency.de>
20050524-2
- fedorarized version

* Mon May 30 2005 Vincent Béron <vberon@mecano.gme.usherb.ca> 20050524-1fc3
- Update to 20050524
- Remove pdf documentation build as it's no more included in the main archive
- Workaround for generic.ppd installation

* Tue Apr 19 2005 Vincent Béron <vberon@mecano.gme.usherb.ca> 20050419-1fc3
- Update to 20050419

* Thu Mar 10 2005 Vincent Béron <vberon@mecano.gme.usherb.ca> 20050310-1fc3
- Update to 20050310

* Sat Feb 12 2005 Vincent Béron <vberon@mecano.gme.usherb.ca> 20050211-1fc3
- Update to 20050211

* Tue Jan 11 2005 Vincent Béron <vberon@mecano.gme.usherb.ca> 20050111-1fc3
- Update to 20050111

* Wed Dec 1 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20041201-1fc3
- Recompile for FC3
- Update to 20041201
- Small reorganization:
    - use the generic ICU static libs name;
    - no more wine group;
    - use Wine's generated stdole32.tlb file;
    - use Wine's generated fonts.

* Wed Oct 20 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20041019-1fc2
- Update to 20041019

* Wed Sep 15 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040914-1fc2
- Update to 20040914

* Sat Aug 14 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040813-1fc2
- Update to 20040813

* Sat Jul 17 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040716-1fc2
- Update to 20040716

* Fri Jun 25 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040615-1fc2
- Recompile for FC2
- Backport from current CVS some fixes to the preloader to prevent
  a segfault on startup
- Include a currently uncommitted patch from Alexandre Julliard regarding
  further issues with the preloader

* Sun Jun 20 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040615-1fc1
- Update to 20040615
- Use of wineprefixcreate instead of old RedHat patches

* Wed May 5 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040505-1fc1
- Update to 20040505

* Fri Apr 9 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040408-1fc1
- Update to 20040408
- Change the handling of paths to DOS drives in the installation process

* Wed Mar 17 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040309-1fc1
- Update to 20040309
- Replaced winedefault.reg by wine.inf

* Wed Feb 18 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040213-1fc1
- Update to 20040213
- Moved Wine dlls back to %{_libdir}/wine rather than %{_libdir}/wine/wine

* Sun Jan 25 2004 Vincent Béron <vberon@mecano.gme.usherb.ca> 20040121-fc1
- Update to 20040121

* Sat Dec 13 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20031212-fc1
- Update to 20031212

* Wed Nov 18 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20031118-fc1
- Update to 20031118

* Thu Oct 16 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20031016-1rh9
- Update to 20031016

* Tue Sep 11 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030911-1rh9
- Fix of include location
- Better separation of run-time and development files
- Update to 20030911

* Wed Aug 13 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030813-1rh9
- Update to 20030813

* Wed Jul 09 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030709-1rh9
- Update to 20030709

* Wed Jun 18 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030618-1rh9
- Change the default C drive to ~/.wine/c, copied from /usr/share/wine
  if non-existant (Thanks to Rudolf Kastl)
- Updated to 20030618

* Tue May 20 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030508-1rh9
- Adapted for RH9

* Thu May 08 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030508-1
- Add libraries definition files to devel package
- Update to 20030508

* Tue Apr 08 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030408-1
- Update to 20030408

* Tue Mar 18 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030318-1
- Update to 20030318

* Thu Mar 11 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030219-2
- Fix the symlinks in wine-c.

* Wed Feb 19 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030219-1
- Update to 20030129
- Various fixes in RPM build process

* Fri Jan 17 2003 Vincent Béron <vberon@mecano.gme.usherb.ca> 20030115-1
- Update to 20030115
- fix to build problem

* Thu Nov  7 2002 Vincent Béron <vberon@mecano.gme.usherb.ca> 20021031-1
- Update to 20021031
- Tweaks here and there

* Wed Sep  4 2002 Bill Nottingham <notting@redhat.com> 20020605-2
- fix docs (#72923)

* Wed Jul 10 2002 Karsten Hopp <karsten@redhat.de> 20020605-1
- update
- remove obsolete part of redhat patch
- redo destdir patch
- redo kde patch
- redo defaultversion patch
- fix 'my_perl unknown' error
- work around name conflict with textutils 'expand'

* Fri Jun 21 2002 Tim Powers <timp@redhat.com>
- automated rebuild

* Thu May 23 2002 Tim Powers <timp@redhat.com>
- automated rebuild

* Wed Mar 27 2002 Bernhard Rosenkraenzer <bero@redhat.com> 20020327-1
- Fix wineshelllink (#61761)
- Fix up initscript (#53625)
- Clean up spec file
- Default to emulating Windoze ME rather than 3.1, nobody uses 3.1
  applications anymore
- Auto-generate default config if none exists (#61920)

* Mon Mar 04 2002 Bernhard Rosenkraenzer <bero@redhat.com> 20020304-1
- Assign gid 66 (closest to 666 [Microsoft number] we can get for a
  system account ;) )
- Don't use glibc private functions (__libc_fork)
- Update

* Tue Feb 26 2002 Bernhard Rosenkraenzer <bero@redhat.com> 20020226-1
- Fix bug #60250
- Update

* Thu Feb 21 2002 Bernhard Rosenkraenzer <bero@redhat.com> 20020221-1
- Update
- Don't try to launch winesetup in winelauncher, we aren't shipping it
  (#59621)

* Sun Jan 27 2002 Bernhard Rosenkraenzer <bero@redhat.com> 20020127-1
- Update
- Fix build in current environment

* Wed Aug 22 2001 Bernhard Rosenkraenzer <bero@redhat.com> 20010822-1
- Make sure the package can be cleanly uninstalled (#52007)
- Add build dependencies

* Thu Jul 26 2001 Bernhard Rosenkraenzer <bero@redhat.com> 20010726-1
- Fix -devel package group (#49989)
- remove internal CVS files
- chkconfig deletion should be in %preun, not %postun
- rename initscript ("Starting windows:" at startup does look off)

* Thu May 03 2001 Bernhard Rosenkraenzer <bero@redhat.com> 20010503-1
- Update
- generate HTML documentation rather than shipping plain docbook text
  (#38453)

* Sat Apr 14 2001 Bernhard Rosenkraenzer <bero@redhat.com>
- Update
- Update registry to mount "/" as drive "Z:", fixes winedbg (needs to be
  accessible from 'doze drives)
- Don't create KDE 1.x style desktop entries in wineshelllink
- Be more tolerant on failing stuff in %%post

* Thu Mar  1 2001 Bernhard Rosenkraenzer <bero@redhat.com>
- Update

* Thu Feb 15 2001 Tim Powers <timp@redhat.com>
- fixed time.h build problems

* Wed Jan 31 2001 Bernhard Rosenkraenzer <bero@redhat.com>
- Add a patch to handle .exe and .com file permissions the way we want them

* Thu Jan 18 2001 Bernhard Rosenkraenzer <bero@redhat.com>
- Update
- Restore wine's ability to use a global config file, it was removed
  in CVS for whatever reason
- Move libraries to %{_libdir}/wine to prevent conflicts with libuser
  (Bug #24202)
- Move include files to /usr/include/wine to prevent it from messing with
  some autoconf scripts (some broken scripts assume they're running on windoze
  if /usr/include/windows.h exists...)

* Tue Dec 19 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- Fix %%pre and %%postun scripts
- --enable-opengl, glibc 2.2 should be safe
- Update CVS

* Mon Nov 20 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- Update CVS
- Add a new (user) group wine that can write to the "C: drive"
  %{_datadir}/wine-c
- Fix up winedbg installation (registry entries)
- Add "Program Files/Common Files" subdirectory to the "C: drive", it's
  referenced in the registry

* Wed Oct 11 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- update

* Mon Aug 7 2000 Tim Powers <timp@redhat.com>
- rebuilt with new DGA

* Tue Jul 25 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- new snapshot
- fix compilation with gcc 2.96

* Fri Jul 21 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- Move init script back
- new version
- move man pages to FHS locations

* Thu Jul 13 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- move initscript
- new snapshot

* Fri Jun 23 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- Start the initscript on startup

* Mon May  9 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- New version
- new feature: You can now launch wine by just running a windows .exe file
  (./some.exe or just click on it in kfm, gmc and the likes)
- some spec file modifications

* Sun Feb 13 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- newer version
- Improve the system.ini file - all multimedia stuff should work now.

* Wed Feb  2 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- enable and fix up the urlmon/wininet patch
- add: autoexec.bat, config.sys, windows/win.ini windows/system.ini
  windows/Profiles/Administrator
- allow i[456]86 arches
- add some system.ini configuration

* Wed Feb  2 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- update current
- add urlmon and wininet patches from Corel (don't apply them for now though)
- create empty shell*dll and winsock*dll files (as mentioned in the HOWTO)

* Mon Jan 17 2000 Bernhard Rosenkraenzer <bero@redhat.com>
- update to current (lots of important fixes)
- Fix up the default wine.conf file (We really don't want it to look
  for CD-ROMs in /cdrom!)
- create a "root filesystem" with everything required to run wine without
  windows in %{_datadir}/wine-c (drive c:)
- add RedHat file in /usr/doc/wine-%{version} explaining the new directory
  layout
- wine-devel requires wine

* Tue Dec 14 1999 Preston Brown <pbrown@redhat.com>
- updated source for Powertools 6.2
- better files list

* Fri Jul 23 1999 Tim Powers <timp@redhat.com>
- updated source
- built for 6.1

* Tue Apr 13 1999 Michael Maher <mike@redhat.com>
- built package for 6.0
- updated package and spec file

* Mon Oct 26 1998 Preston Brown <pbrown@redhat.com>
- updated to 10/25/98 version.  There is really no point in keeping the
- older one, it is full of bugs and the newer one has fewer.
- commented out building of texinfo manual, it is horrendously broken.

* Mon Oct 12 1998 Michael Maher <mike@redhat.com>
- built package for 5.2
- pressured by QA, not updating.

* Fri May 22 1998 Cristian Gafton <gafton@redhat.com>
- repackaged for PowerTools
