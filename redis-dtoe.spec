Name:           redis-dtoe
Version:        1.0.0
Release:        1%{?dist}
Summary:        Redis DTOE user-space library (kbdtoe)

%global debug_package %{nil}

License:        MulanPSL-2.0 AND CC-BY-4.0
URL:            https://gitcode.com/boostkit/redis-dtoe
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  glibc-devel

Requires:       libboundscheck
Requires:       rsyslog
Requires:       logrotate

%description
redis-dtoe provides the user-space DTOE library (libkbdtoe) for Redis
DTOE acceleration scenarios.

%prep
%setup -q

%build
cmake . -B build -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build

%install
cmake --install build --prefix %{buildroot}/usr
install -d %{buildroot}/var/log/kbdtoe

# Install rsyslog + logrotate configs
install -D -m 0644 conf/kbdtoe_rsyslog.conf %{buildroot}%{_sysconfdir}/rsyslog.d/kbdtoe.conf
install -D -m 0644 conf/logrotate/kbdtoe %{buildroot}%{_sysconfdir}/logrotate.d/kbdtoe

%post
if [ ! -d /var/log/kbdtoe ]; then
    mkdir -p /var/log/kbdtoe || true
fi
if getent passwd syslog >/dev/null 2>&1 && getent group adm >/dev/null 2>&1; then
    chown syslog:adm /var/log/kbdtoe || true
fi
if [ ! -f /.dockerenv ]; then
    systemctl restart rsyslog
fi

%postun
if [ ! -f /.dockerenv ] && [ "$1" = "0" ]; then
    systemctl stop rsyslog
fi

%files
%license docs/LICENSE
%doc README.md
%config(noreplace) %{_sysconfdir}/rsyslog.d/kbdtoe.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/kbdtoe
%{_libdir}/libkbdtoe.so*
%{_includedir}/kbdtoe.h
%dir /var/log/kbdtoe

%changelog
* Fri Mar 21 2026 Your Name <you@example.com> - 26.0.0-1
- Initial package
