Name:       a2ui-dali
Summary:    DALi-based renderer for the A2UI protocol
Version:    0.14.0
Release:    1
Group:      Graphics & UI Framework/Libraries
License:    Apache-2.0
URL:        https://github.com/dalihub/a2ui-dali
Source0:    %{name}-%{version}.tar.gz

# a2ui-dali ships as a static library; keep .a files in the buildroot
# (Tizen's rpm macros otherwise strip every *.a unless keepstatic is set).
%define keepstatic 1

BuildRequires:  cmake
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(dali2-core)
BuildRequires:  pkgconfig(dali2-adaptor)
BuildRequires:  dali2-integration-devel
BuildRequires:  dali2-adaptor-integration-devel
BuildRequires:  pkgconfig(dali2-ui-foundation)
BuildRequires:  dali2-ui-foundation-integration-devel
BuildRequires:  pkgconfig(dali2-ui-components)
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(egl)

Requires:   dali2-ui-foundation
Requires:   dali2-ui-components

%description
A2UI-DALi renders Google A2UI protocol streams (v0.9) natively with the DALi
engine, on top of the thin dali-ui foundation/components layer. This package
contains the runnable example renderers (basic-renderer, gallery-demo,
custom-component) together with their A2UI sample streams and image assets.

##############################
# devel
##############################
%package devel
Summary:    Development files for a2ui-dali
Group:      Development/Building
Requires:   %{name} = %{version}-%{release}
Requires:   dali2-ui-foundation-devel
Requires:   dali2-ui-components-devel

%description devel
Static library, public headers and pkg-config for building applications
against the A2UI-DALi renderer.

##############################
# Preparation
##############################
%prep
%setup -q

##############################
# Build
##############################
%build
CXXFLAGS+=" -Wall -g -Os -DNDEBUG -fPIC -fvisibility-inlines-hidden "
export CXXFLAGS

mkdir -p cmake_build
cd cmake_build
cmake .. \
      -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
      -DCMAKE_INSTALL_INCLUDEDIR=%{_includedir} \
      -DCMAKE_INSTALL_BINDIR=%{_bindir} \
      -DCMAKE_INSTALL_DATADIR=%{_datadir} \
      -DA2UI_INSTALL_EXAMPLES=ON

make %{?jobs:-j%jobs}

##############################
# Installation
##############################
%install
rm -rf %{buildroot}
cd cmake_build
%make_install
cd ..
cp packaging/%{name}.manifest %{name}.manifest

##############################
# Files
##############################
%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/a2ui-basic-renderer
%{_bindir}/a2ui-gallery-demo
%{_bindir}/a2ui-custom-component
%{_datadir}/a2ui-dali/res
%{_datadir}/a2ui-dali/screens
%{_datadir}/a2ui-dali/samples

%files devel
%defattr(-,root,root,-)
%{_libdir}/liba2ui-dali.a
%{_libdir}/pkgconfig/a2ui-dali.pc
%{_includedir}/a2ui-dali
