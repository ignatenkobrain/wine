%define DATE 20030911
%define with_valgrind %{?_with_valgrind:1}%{!?_with_valgrind:0}
%define with_alsa     %{?_with_alsa:1}%{!?_with_alsa:0}

Summary: A Windows 16/32 bit emulator.
Name: wine
Version: 0.0
Release: 0.fdr.2.%{DATE}.rh90
Epoch: 0
Group: Applications/Emulators
License: LGPL
URL: http://www.winehq.com/
Source0: ftp://metalab.unc.edu/pub/Linux/ALPHA/wine/development/Wine-%{DATE}.tar.gz
Source1: wine.init
Patch0: wine-20030408-initial.patch
Patch1: wine-20030408-kde2.patch
Patch2: wine-20030408-winelauncher.patch
Patch3: wine-20030408-defaultcfg.patch
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
ExclusiveArch: %{ix86}
Prereq: shadow-utils
Conflicts: kdebase < 0:2.0
Requires: cups-libs >= 0:1.1.12, openssl
# require NPTL-capable glibc
Requires: /lib/tls
Requires(pre,postun): shadow-utils
BuildRequires: docbook-utils, cups-devel >= 0:1.1.12, perl
BuildRequires: XFree86-devel, libjpeg-devel, libungif-devel, libstdc++-devel
BuildRequires: bison, flex, autoconf >= 0:2.53, arts-devel, ncurses-devel
BuildRequires: openssl-devel, sane-backends-devel
%if %{with_valgrind}
BuildRequires: valgrind-devel
%else
BuildConflicts: valgrind-devel
%endif
%if %{with_alsa}
BuildRequires: alsa-libs-devel
%else
BuildConflicts: alsa-libs-devel
%endif
Provides: libntdll.dll.so

%description
While Wine is usually thought of as a Windows(TM) emulator, the Wine
developers would prefer that users thought of Wine as a Windows
compatibility layer for UNIX. This package includes a program loader,
which allows unmodified Windows 3.1/95/NT binaries to run under Intel
Unixes. Wine does not require MS Windows, but it can use native system
.dll files if they are available.

%package devel
Summary: Wine development environment.
Group: Development/Libraries
Requires: %{name} = %{epoch}:%{version}-%{release}

%description devel
Header and include files for developing applications with the Wine
Windows(TM) emulation libraries.


%prep
echo "
--------------------------------------------------------------
Unmaintained since RHL9. Maintainer needed.
--------------------------------------------------------------" > /dev/null
exit 1

%setup -q -n wine-%{DATE}
find . -type d -name CVS |xargs rm -rf
%patch -p1 -b .initial
%patch1 -p1 -b .kde2
%patch2 -p1 -b .wl
%patch3 -p1 -b .defcfg


%build
export CFLAGS="$RPM_OPT_FLAGS"
export CPPFLAGS="$(pkg-config openssl --cflags)" # fix ssl detection, need krb5
autoconf || autoconf-2.53
%configure \
	--with-nptl \
	--with-x \
	--libdir=%{_libdir}/wine \
	--includedir=%{_includedir}/wine \
	--sysconfdir=%{_sysconfdir}/wine


make depend
make %{?_smp_mflags}
#%{__make} -C documentation doc


%install
rm -rf $RPM_BUILD_ROOT

%makeinstall \
	includedir=%{?buildroot:%{buildroot}}%{_includedir}/wine \
	libdir=%{?buildroot:%{buildroot}}%{_libdir}/wine \
	sysconfdir=%{?buildroot:%{buildroot}}%{_sysconfdir}/wine \
	dlldir=%{?buildroot:%{buildroot}}%{_libdir}/wine/wine \
	LDCONFIG=/bin/true

for i in system command "Start Menu/Programs/Startup" Profiles/Administrator Fonts \
         Desktop Favorites NetHood Recent SendTo ShellNew; do
	mkdir -p "$RPM_BUILD_ROOT%{_datadir}/wine-c/windows/$i"
done
mkdir -p "$RPM_BUILD_ROOT%{_datadir}/wine-c/My Documents"
mkdir -p "$RPM_BUILD_ROOT%{_datadir}/wine-c/Program Files/Common Files"

# Take care of wine and windows configuration files...
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/wine
mv documentation/samples/config documentation/samples/config.orig
sed "s/\"GraphicsDriver\" = .*/\"GraphicsDriver\" = \"ttydrv\"/" documentation/samples/config.orig |\
sed "s|\"Path\" = \"/c\"\$|\"Path\" = \"$RPM_BUILD_ROOT%{_datadir}/wine-c\"|" |\
sed "s|\"Path\" = \"\${HOME}\"$|\"Path\" = \"%{_builddir}/%{buildsubdir}\"|" -> documentation/samples/config
WINEPREFIX=%{_builddir}/%{buildsubdir}/documentation/samples programs/regedit/regedit winedefault.reg #> /dev/null
# Wait until wineserver finishes and closes those files
sleep 5
install -c -m 0644 documentation/samples/system.reg $RPM_BUILD_ROOT%{_sysconfdir}/wine/system.reg
install -c -m 0644 documentation/samples/user.reg $RPM_BUILD_ROOT%{_sysconfdir}/wine/user.reg
install -c -m 0644 documentation/samples/userdef.reg $RPM_BUILD_ROOT%{_sysconfdir}/wine/userdef.reg
rm -f documentation/samples/system.reg
rm -f documentation/samples/user.reg
rm -f documentation/samples/userdef.reg

sed "s|\"Path\" = \"/c\"\$|\"Path\" = \"%{_datadir}/wine-c\"|" documentation/samples/config.orig > documentation/samples/config.rh
mv documentation/samples/config.rh documentation/samples/config.orig
sed "s|\"Path\" = \"/cdrom\"\$|\"Path\" = \"/mnt/cdrom\"|" documentation/samples/config.orig > documentation/samples/config.rh
mv documentation/samples/config.rh documentation/samples/config.orig
sed "s|\"Path\" = \"/mnt/fd0\"\$|\"Path\" = \"/mnt/floppy\"|" documentation/samples/config.orig > documentation/samples/config.rh

install -c -m 0644 documentation/samples/config.rh $RPM_BUILD_ROOT%{_sysconfdir}/wine/wine.conf
rm -f documentation/samples/config
rm -f documentation/samples/config.rh
mv documentation/samples/config.orig documentation/samples/config

# Install link to windows applications replacements
ln -sf %{_libdir}/wine/wine/start.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/command/start.exe
ln -sf %{_libdir}/wine/wine/notepad.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/notepad.exe
ln -sf %{_libdir}/wine/wine/regedit.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/regedit.exe
ln -sf %{_libdir}/wine/wine/rundll32.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/rundll32.exe
ln -sf %{_libdir}/wine/wine/wcmd.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/cmd.exe
ln -sf %{_libdir}/wine/wine/control.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/control.exe
ln -sf %{_libdir}/wine/wine/winhelp.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/help.exe
ln -sf %{_libdir}/wine/wine/notepad.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/notepad.exe
ln -sf %{_libdir}/wine/wine/progman.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/progman.exe
ln -sf %{_libdir}/wine/wine/regsvr32.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/regsvr32.exe
ln -sf %{_libdir}/wine/wine/winemine.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/winmine.exe
ln -sf %{_libdir}/wine/wine/winver.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/winver.exe
ln -sf %{_libdir}/wine/wine/uninstaller.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/uninstaller.exe
ln -sf %{_libdir}/wine/wine/winhelp.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/winhelp.exe
ln -sf %{_libdir}/wine/wine/winhelp.exe.so $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/winhlp32.exe

for i in shell.dll shell32.dll winsock.dll wnsock32.dll; do
	touch $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system/$i
done
touch $RPM_BUILD_ROOT%{_datadir}/wine-c/autoexec.bat
touch $RPM_BUILD_ROOT%{_datadir}/wine-c/config.sys
touch $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/win.ini
install -c -m 0644 documentation/samples/system.ini $RPM_BUILD_ROOT%{_datadir}/wine-c/windows/system.ini

cat >RedHat <<EOF
Wine directory structure used in Red Hat Linux:
===============================================

%{_datadir}/wine-c is the root directory (aka C: drive) wine looks for
by default. It contains (empty) C:\windows and C:\windows\system
directories, needed to operate Wine without an existing Windows installation.

If you want to use Wine with an existing Windows installation that is mounted,
for example, in /mnt/windows-c, edit /etc/wine.conf to say

[Drive C]
Path=/mnt/windows-c
Type=hd
Label=Whatever
Filesystem=win95

instead of the defaults set by installation.

If you do this, you can safely remove %{_datadir}/wine-c.
(Alternatively, just mount your Windows partition to %{_datadir}/wine-c.)
EOF

# Allow users to launch Windows programs by just clicking on the .exe file...
mkdir -p $RPM_BUILD_ROOT%{_initrddir}
install -c -m 755 %SOURCE1 $RPM_BUILD_ROOT%{_initrddir}/wine


%clean
rm -rf $RPM_BUILD_ROOT


%pre
/usr/sbin/groupadd -g 66 -r wine &>/dev/null || :

%post
if ! grep -q "^/usr/lib/wine$" /etc/ld.so.conf; then
	echo "/usr/lib/wine" >>/etc/ld.so.conf
fi
/sbin/chkconfig --add wine
/sbin/chkconfig --level 2345 wine on
/sbin/service wine start &>/dev/null || :
/sbin/ldconfig

%preun
if test "$1" = "0"; then
	/sbin/service wine stop &> /dev/null || :
	/sbin/chkconfig --del wine
fi

%postun
if test "$1" = "0"; then
	perl -pi -e "s,^/usr/lib/wine\n$,,g" /etc/ld.so.conf
	/usr/sbin/groupdel wine &>/dev/null || :
fi
/sbin/ldconfig


%files
%defattr(0775,root,wine)
%dir %{_datadir}/wine-c
%dir %{_datadir}/wine-c/windows
%dir %{_datadir}/wine-c/windows/command
%dir %{_datadir}/wine-c/windows/system
%dir "%{_datadir}/wine-c/windows/Start Menu"
%dir "%{_datadir}/wine-c/windows/Start Menu/Programs"
%dir "%{_datadir}/wine-c/windows/Start Menu/Programs/Startup"
%dir %{_datadir}/wine-c/windows/Profiles
%dir %{_datadir}/wine-c/windows/Profiles/Administrator
%dir %{_datadir}/wine-c/windows/Fonts
%dir %{_datadir}/wine-c/windows/Desktop
%dir %{_datadir}/wine-c/windows/Favorites
%dir %{_datadir}/wine-c/windows/NetHood
%dir %{_datadir}/wine-c/windows/Recent
%dir %{_datadir}/wine-c/windows/SendTo
%dir %{_datadir}/wine-c/windows/ShellNew
%dir "%{_datadir}/wine-c/My Documents"
%dir "%{_datadir}/wine-c/Program Files"
%dir "%{_datadir}/wine-c/Program Files/Common Files"
%defattr(-,root,wine,-)
%{_libdir}/wine
%{_bindir}/*
%{_mandir}/man?/*
%{_datadir}/wine-c/windows/command/*.exe
%{_datadir}/wine-c/windows/system/*.dll
%{_datadir}/wine-c/windows/system/*.exe
%{_datadir}/wine-c/windows/*.exe
%{_datadir}/aclocal/wine.m4
%config %{_datadir}/wine-c/autoexec.bat
%config %{_datadir}/wine-c/config.sys
%attr(0664, root, wine) %config %{_datadir}/wine-c/windows/win.ini
%attr(0664, root, wine) %config %{_datadir}/wine-c/windows/system.ini
%config %{_sysconfdir}/wine/*
%config %{_initrddir}/*
%doc ANNOUNCE BUGS COPYING.LIB ChangeLog DEVELOPERS-HINTS LICENSE LICENSE.OLD README VERSION
%doc AUTHORS RedHat

%files devel
%defattr(-,root,root,-)
%doc documentation/winelib-* documentation/wine-devel* documentation/debug*
%doc documentation/patches* documentation/porting.sgml documentation/implementation.sgml
%doc documentation/HOWTO-winelib
%{_includedir}/*


%changelog
* Sun Sep 28 2003 Ville Skyttä <ville.skytta at iki.fi> 0:0.0-0.fdr.2.20030911
- Fix SSL detection (need krb5), require openssl.
- Disable ALSA by default, use "--with alsa" to enable.
- Disable valgrind by default, use "--with valgrind" to enable.
- Cosmetic improvements.

* Tue Sep 16 2003 Panu Matilainen <pmatilai@welho.com> 0.0-0.fdr.1.20030911
- update to 20030911

* Tue Aug 19 2003 Panu Matilainen <pmatilai@welho.com> 0.0-0.fdr.1.20030813
- update to 20030813

* Thu Jul 10 2003 Panu Matilainen <pmatilai@welho.com> 0.0-0.fdr.1.20030709
- update to 20030709
- add /lib/tls requirement -> only works with NTPL capable systems

* Wed Jul 02 2003 Panu Matilainen <pmatilai@welho.com> 0.0-0.fdr.1.20030618
- update to 20030618

* Thu May 22 2003 Panu Matilainen <pmatilai@welho.com> 0.0-0.fdr.1.20030508
- update to 20030508
- add buildreq's & other QA issues from #255

* Sat May 03 2003 Panu Matilainen <pmatilai@welho.com> 0.0-0.fdr.1.20030408
- package for fedora
- based on modified RH wine from 8.0 found off the net

