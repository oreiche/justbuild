Name:           justbuild
Version:        VERSION
Release:        1
Summary:        Justbuild generic build system

License:        Apache-2.0
URL:            https://github.com/just-buildsystem/justbuild
Source0:        justbuild-VERSION.tar.gz

BuildRequires:  make BUILD_DEPENDS
Recommends:     python3, bash-completion, git >= 2.29

%description
Justbuild is a generic build system supporting multi-repository builds. A
peculiarity of the tool is the separation between global names and physical
location on the one hand, and logical paths used for actions and installation
on the other hand (sometimes referred to as "staging"). The language-specific
information to translate high-level concepts (libraries, binaries) into
individual compile action is taken from user-defined rules described by
functional expressions.


%global debug_package %{nil}
%global _build_id_links none


%prep
%autosetup


%build


%install
mkdir -p $RPM_BUILD_ROOT/usr
tar -xvf rpmbuild/justbuild.tar.gz --strip-components=1 -C $RPM_BUILD_ROOT/usr


%files
%license LICENSE
%{_bindir}/*
%{_datadir}/bash-completion/completions/*
%{_mandir}/man1/*
%{_mandir}/man5/*


%changelog
* Thu Mar 06 2025 Oliver Reiche <oliver.reiche@gmail.com>
- Initial release
