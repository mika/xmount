Name: xmount
Summary: Tool to crossmount between multiple input and output harddisk images
Version: 0.4.2
Release: 1
License: GPL
Group: Applications/System
URL: https://www.pinguin.lu/
Source0: %{name}-%{version}.tar.gz
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires: fuse openssl zlib
BuildPrereq: fuse-devel

%description
xmount allows you to convert on-the-fly between multiple input and output
harddisk image types. xmount creates a virtual file system using FUSE
(Filesystem in Userspace) that contains a virtual representation of the input
harddisk image. The virtual representation can be in raw DD, VirtualBox's
virtual disk file format or in VMware's VMDK format. Input harddisk images can
be raw DD or EWF (Expert Witness Compression Format) files. In addition, xmount
also supports virtual write access to the output files that is redirected to a
cache file. This makes it for example possible to boot acquired harddisk images
using QEMU, KVM, VirtualBox, VMware or alike. 
%prep
%setup -q

%build
%configure
make

%install
rm -fr %{buildroot}

%makeinstall

%clean
rm -fr %{buildroot}

%post

%preun

%postun

%files
%defattr(-,root,root) 
%{_bindir}/*
%{_mandir}/*
%doc AUTHORS COPYING INSTALL NEWS README ROADMAP

%changelog
* Wed Aug 19 2009 Gillen Daniel <gillen.dan@pinguin.lu>
— build package

