# These xxx markers are to be replaced by git_build_rpm
Name:           xxx
Version:        xxx

Release:        1%{?dist}
Summary:        horizon renderer

License:        LGPL
URL:            https://www.github.com/dkogan/horizonator/
Source0:        https://www.github.com/dkogan/horizonator/archive/%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  libepoxy-devel
BuildRequires:  freeglut-devel
BuildRequires:  freeimage-devel
BuildRequires:  tinyxml-devel
BuildRequires:  libpng-devel
BuildRequires:  libcurl-devel
BuildRequires:  fltk-devel >= 1.3.4

BuildRequires:  wget

%description
SRTM terrain renderer

%prep
%setup -q

%build
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
%make_install

%files
%{_bindir}/*
