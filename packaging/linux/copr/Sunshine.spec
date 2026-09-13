%global build_timestamp %(date +"%Y%m%d")

# use sed to replace these values
%global build_version 0
%global branch 0
%global commit 0
%global sunshine_python_version 3.14

%undefine _hardened_build

# Define _metainfodir for openSUSE if not already defined
%if 0%{?suse_version}
%if !0%{?_metainfodir:1}
%global _metainfodir %{_datadir}/metainfo
%endif
%endif

Name: Sunshine
Version: %{build_version}
Release: 1%{?dist}
Summary: Self-hosted game stream host for Moonlight.
License: GPLv3-only
URL: https://github.com/LizardByte/Sunshine
Source0: tarball.tar.gz

# Common BuildRequires
BuildRequires: cmake >= 3.25.0
BuildRequires: desktop-file-utils
BuildRequires: git
BuildRequires: glib2-devel
BuildRequires: libcurl-devel
BuildRequires: openssl-devel
BuildRequires: rpm-build
BuildRequires: systemd-rpm-macros
BuildRequires: wget
BuildRequires: which

%if 0%{?fedora}
# Fedora-specific BuildRequires
BuildRequires: appstream
# BuildRequires: boost-devel >= 1.86.0
BuildRequires: libappstream-glib
%if 0%{fedora} > 43
# needed for npm from nvm
BuildRequires: libatomic
%endif
BuildRequires: miniupnpc-devel
%if 0%{?fedora} < 44
BuildRequires: nodejs-npm
%endif
BuildRequires: opus-devel
BuildRequires: uv
%{?sysusers_requires_compat}
# for unit tests
BuildRequires: ImageMagick
%endif

%if 0%{?suse_version}
# openSUSE-specific BuildRequires
BuildRequires: AppStream
BuildRequires: appstream-glib
BuildRequires: libminiupnpc-devel
BuildRequires: libopus-devel
BuildRequires: npm
BuildRequires: python313
BuildRequires: xz
# for unit tests
BuildRequires: ImageMagick
%endif

%if 0%{?fedora}
%if 0%{?fedora} <= 41
BuildRequires: gcc13
BuildRequires: gcc13-c++
%global gcc_version 13
%elif 0%{?fedora} >= 42 && 0%{?fedora} <= 43
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%elif 0%{?fedora} >= 44
BuildRequires: gcc15
BuildRequires: gcc15-c++
%global gcc_version 15
%endif
%endif

%if 0%{?suse_version}
BuildRequires: gcc15
BuildRequires: gcc15-c++
%global gcc_version 15
%endif


# Common runtime requirements
Requires: miniupnpc >= 2.2.4
Requires: which >= 2.21

%if 0%{?fedora}
# Fedora runtime requirements
Requires: libcap >= 2.22
Requires: libcurl >= 7.0
Requires: libopusenc >= 0.2.1
Requires: openssl >= 3.0.2
%endif

%if 0%{?suse_version}
# openSUSE runtime requirements
Requires: libcap2
Requires: libcurl4
Requires: libopusenc0
Requires: libopenssl3
%endif

%description
Self-hosted game stream host for Moonlight.

%prep
# extract tarball to current directory
mkdir -p %{_builddir}/Sunshine
tar -xzf %{SOURCE0} -C %{_builddir}/Sunshine

# list directory
ls -a %{_builddir}/Sunshine

%build
# exit on error
set -e

# Detect the architecture and Fedora version
architecture=$(uname -m)


# prepare CMAKE args
cmake_args=(
  "-B=%{_builddir}/Sunshine/build"
  "-G=Unix Makefiles"
  "-S=."
  "-DBUILD_DOCS=OFF"
  "-DBUILD_WERROR=ON"
  "-DCMAKE_BUILD_TYPE=Release"
  "-DCMAKE_INSTALL_PREFIX=%{_prefix}"
  "-DSUNSHINE_ASSETS_DIR=%{_datadir}/sunshine"
  "-DSUNSHINE_EXECUTABLE_PATH=%{_bindir}/sunshine"
  "-DSUNSHINE_PUBLISHER_NAME=LizardByte"
  "-DSUNSHINE_PUBLISHER_WEBSITE=https://app.lizardbyte.dev"
  "-DSUNSHINE_PUBLISHER_ISSUE_URL=https://app.lizardbyte.dev/support"
)



export CC=gcc-%{gcc_version}
export CXX=g++-%{gcc_version}

# Install and setup NVM for Fedora 44+
%if 0%{?fedora} > 43
echo "Installing NVM for Fedora 44+..."
export HOME=${HOME:-/builddir}
export NVM_DIR="$HOME/.nvm"

# Install NVM
if [ ! -d "$NVM_DIR" ]; then
  wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh | bash
fi

# Load NVM
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"

# Install and use Node.js
nvm install node
nvm use node

echo "Node.js version: $(node --version)"
echo "npm version: $(npm --version)"
echo "npm location: $(which npm)"
echo "node location: $(which node)"

# Add npm and node path to cmake args
NPM_PATH=$(which npm)
NODE_PATH=$(which node)
cmake_args+=("-DNPM=${NPM_PATH}")

# Add node bin directory to PATH for make
export PATH="$(dirname ${NODE_PATH}):${PATH}"
%endif

# setup the version
export BRANCH=%{branch}
export BUILD_VERSION=v%{build_version}
export COMMIT=%{commit}

# cmake
cd %{_builddir}/Sunshine
%if 0%{?fedora}
uv python install %{sunshine_python_version}
%endif
echo "cmake args:"
echo "${cmake_args[@]}"
cmake "${cmake_args[@]}"
make -j$(nproc) -C "%{_builddir}/Sunshine/build"

%check
# validate the metainfo file
appstreamcli validate %{buildroot}%{_metainfodir}/*.metainfo.xml
appstream-util validate --nonet %{buildroot}%{_metainfodir}/*.metainfo.xml
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop

# run tests
cd %{_builddir}/Sunshine/build
./tests/test_sunshine

%install
# Load NVM for Fedora 44+ so npm is available during make install
%if 0%{?fedora} > 43
export HOME=${HOME:-/builddir}
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
nvm use node

# Add node bin directory to PATH for make install
NODE_PATH=$(which node)
export PATH="$(dirname ${NODE_PATH}):${PATH}"

echo "Node.js version: $(node --version)"
echo "npm version: $(npm --version)"
%endif

cd %{_builddir}/Sunshine/build
%make_install

%files
# Executables
%caps(cap_sys_nice+p) %{_bindir}/sunshine

# Systemd unit files for user services
%{_userunitdir}/*.service

# Desktop entries
%{_datadir}/applications/*.desktop

# Icons
%{_datadir}/icons/hicolor/scalable/apps/*.Sunshine.svg

# Metainfo
%{_datadir}/metainfo/*.metainfo.xml

# Assets
%{_datadir}/sunshine/**

%changelog
