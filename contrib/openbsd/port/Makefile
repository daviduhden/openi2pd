# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: OpenBSD port template.
#
# This file is a template for the external OpenBSD ports tree. It is not
# consumed by the repository CMake build, and no package validation is
# implied by its presence.

COMMENT = client for the I2P anonymous network

GH_ACCOUNT = daviduhden
GH_PROJECT = openi2pd
GH_TAGNAME = 2.61.0
DISTNAME = openi2pd-${GH_TAGNAME}
PKGNAME = i2pd-${GH_TAGNAME}

CATEGORIES = net
HOMEPAGE = https://github.com/daviduhden/openi2pd
MAINTAINER = openi2pd maintainers <daviduhden@users.noreply.github.com>

# BSD-3-Clause
PERMIT_PACKAGE = Yes

# WANTLIB must name every shared library the binaries link. Boost.JSON,
# Boost.URL and Boost.Container are compiled libraries, not header-only
# substitutes, and ncursesw is required by the native TUI.
WANTLIB += ${COMPILER_LIBCXX} boost_container-mt boost_json-mt
WANTLIB += boost_program_options-mt boost_url-mt
WANTLIB += c crypto m miniupnpc ncursesw ssl util z

COMPILER = base-clang ports-gcc
MODULES = devel/cmake
BUILD_DEPENDS = devel/boost
LIB_DEPENDS = devel/boost \
		devel/ncurses \
		net/miniupnp/miniupnpc

CONFIGURE_STYLE = cmake
# Argument rationale, in order: the TUI ships in this package, so
# I2PD_TUI_REQUIRED makes configure fail if ncursesw is absent.
# I2PD_BUILD_TOOLS=OFF keeps auxiliary tools out; PLIST lists neither
# those tools nor `man/man1/i2pdctl.1`. WITH_LIBRARY=OFF installs no
# shared libi2pd or headers. WITH_HARDENING=ON keeps project hardening
# flags. BUILD_TESTING=OFF matches NO_TEST below. I2PD_INSTALL_DOCS=OFF
# and I2PD_INSTALL_CONFIG=OFF leave docs and sample config to this
# port's share/examples and share/doc rules. I2PD_SERVICE_MANAGER=none
# avoids a second service authority because this port installs its
# rc.d template. I2PD_OPENSSL_EXPECT=LibreSSL makes configure fail if
# base LibreSSL is below 4.0 instead of silently dropping features.
CONFIGURE_ARGS = -DI2PD_BUILD_TUI=ON \
			-DI2PD_BUILD_TOOLS=OFF \
			-DI2PD_TUI_REQUIRED=ON \
			-DWITH_LIBRARY=OFF \
			-DWITH_HARDENING=ON \
			-DBUILD_TESTING=OFF \
			-DI2PD_INSTALL_DOCS=OFF \
			-DI2PD_INSTALL_CONFIG=OFF \
			-DI2PD_SERVICE_MANAGER=none \
			-DI2PD_OPENSSL_EXPECT=LibreSSL

# The CTest suite needs a second build of the test tree; it is not a
# package-build requirement. The manual OpenBSD workflow owns it.
NO_TEST = Yes

# The canonical rc.d template uses ports-substitution variables.
SUBST_VARS = TRUEPREFIX SYSCONFDIR LOCALSTATEDIR
SUBST_FILES = ../contrib/openbsd/i2pd.rc

WRKDIST = ${WRKDIR}/${DISTNAME}
# The project's CMakeLists.txt lives in build/, not at root. Keep the
# ports build tree in obj/ so CMake never writes into the
# extracted source and a re-extract stays clean.
SEPARATE_BUILD = Yes
WRKSRC = ${WRKDIST}/build
WRKBUILD = ${WRKDIST}/obj

# @sample resolves an absolute target to share/examples/<absolute path>.
# The etc and var sources must mirror the target tree, or pkg_add
# installs no config and never creates /var/i2pd. The rc.d script is
# installed to ${WRKINST}/etc/rc.d (outside ${PREFIX}) because PLIST
# registers it with @rcscript. It is the substituted copy, not this
# directory's template. Notices ship with LICENSE under share/doc/i2pd
# so the package keeps its attribution after pkg_delete of the source.
post-install:
	${INSTALL_DATA_DIR} ${PREFIX}/share/examples/etc/i2pd \
		${PREFIX}/share/examples/etc/i2pd/tunnels.conf.d \
		${PREFIX}/share/examples/etc/login.conf.d \
		${PREFIX}/share/examples/var/i2pd \
		${PREFIX}/share/doc/i2pd
	${INSTALL_DATA} ${WRKDIST}/contrib/openbsd/i2pd.conf \
		${PREFIX}/share/examples/etc/i2pd/i2pd.conf
	${INSTALL_DATA} ${WRKDIST}/contrib/tunnels.conf \
		${PREFIX}/share/examples/etc/i2pd/tunnels.conf
	${INSTALL_DATA} ${WRKDIST}/contrib/tunnels.d/README \
		${PREFIX}/share/examples/etc/i2pd/tunnels.conf.d/README
	${INSTALL_DATA} ${WRKDIST}/contrib/openbsd/i2pd.login \
		${PREFIX}/share/examples/etc/login.conf.d/i2pd
	${INSTALL_DATA} ${.CURDIR}/pkg/var-i2pd.README \
		${PREFIX}/share/examples/var/i2pd/README
	${INSTALL_DATA} ${WRKDIST}/contrib/subscriptions.txt \
		${PREFIX}/share/examples/var/i2pd/subscriptions.txt
	${INSTALL_DATA} ${WRKDIST}/LICENSE ${PREFIX}/share/doc/i2pd/LICENSE
	${INSTALL_DATA} ${WRKDIST}/NOTICE ${PREFIX}/share/doc/i2pd/NOTICE
	${INSTALL_DATA} ${WRKDIST}/tools/LICENSE \
		${PREFIX}/share/doc/i2pd/LICENSE.i2pd-tools
	${INSTALL_DATA_DIR} ${WRKINST}/etc/rc.d
	${INSTALL_SCRIPT} ${WRKDIST}/contrib/openbsd/i2pd.rc \
		${WRKINST}/etc/rc.d/i2pd

.include <bsd.port.mk>
