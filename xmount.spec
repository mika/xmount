%define debug_package %{nil}

Name:			xmount
Summary:		Tool to crossmount between multiple input and output harddisk images
Version:		0.5.0
Release:		1%{?dist}
License:		GPL
Group:			Applications/System
URL:			https://www.pinguin.lu/
Source0:		%{name}-%{version}.tar.gz
Buildroot:		%{_tmppath}/%{name}-%{version}-%{release}-root
Requires:		fuse openssl zlib libewf afflib
BuildRequires:		fuse-devel libewf-devel >= 20110903 afflib-devel

%description
xmount allows you to convert on-the-fly between multiple input and output
harddisk image types. xmount creates a virtual file system using FUSE
(Filesystem in Userspace) that contains a virtual representation of the input
harddisk image. The virtual representation can be in raw DD, VirtualBox's
virtual disk file format, Microsoft's Virtual Hard Disk Image format or in
VMware's VMDK format. Input harddisk images can be raw DD or EWF (Expert
Witness Compression Format) files. In addition, xmount also supports virtual
write access to the output files that is redirected to a cache file. This
makes it for example possible to boot acquired harddisk images using QEMU,
KVM, VirtualBox, VMware or alike. 
%prep
%setup -q
autoreconf

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
* Sun May 6 2012 Daniel Gillen <gillen.dan@pinguin.lu> 0.5.0-1
* Release 0.5.0-1
  Added support for virtual VHD image file emulation

