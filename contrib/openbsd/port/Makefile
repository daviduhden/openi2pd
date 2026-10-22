# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: OpenBSD port template.
#
# This file is a template for the external OpenBSD ports tree. It is not
# consumed by the repository CMake build, and no port/package validation is
# implied by its presence.

COMMENT =	client for the I2P anonymous network

GH_ACCOUNT =	daviduhden
GH_PROJECT =	openi2pd
GH_TAGNAME =	2.61.0
DISTNAME =	openi2pd-${GH_TAGNAME}
PKGNAME =	i2pd-${GH_TAGNAME}

CATEGORIES =	net
HOMEPAGE =	https://github.com/daviduhden/openi2pd
MAINTAINER =	openi2pd maintainers <daviduhden@users.noreply.github.com>

# BSD-3-Clause
PERMIT_PACKAGE =	Yes

WANTLIB +=	${COMPILER_LIBCXX} boost_atomic-mt boost_container-mt
WANTLIB +=	boost_filesystem-mt boost_program_options-mt c crypto m
WANTLIB +=	miniupnpc ncursesw ssl z

COMPILER =	base-clang ports-gcc
MODULES =	devel/cmake
BUILD_DEPENDS =	devel/boost
LIB_DEPENDS =	devel/boost \
		devel/ncurses \
		net/miniupnp/miniupnpc

CONFIGURE_STYLE =	cmake
CONFIGURE_ARGS =	-DI2PD_BUILD_TUI=ON \
			-DI2PD_BUILD_TOOLS=OFF \
			-DI2PD_TUI_REQUIRED=ON \
			-DWITH_LIBRARY=OFF \
			-DWITH_HARDENING=ON \
			-DBUILD_TESTING=OFF \
			-DI2PD_INSTALL_DOCS=OFF \
			-DI2PD_SERVICE_MANAGER=none

# The CTest suite needs a full second build of the test tree and is not a
# package-build requirement. The manual OpenBSD workflow owns it.
NO_TEST =	Yes

# The canonical rc.d template uses ports-substitution variables.
SUBST_VARS =	TRUEPREFIX SYSCONFDIR LOCALSTATEDIR
SUBST_FILES =	../contrib/openbsd/i2pd.rc

WRKDIST =	${WRKDIR}/${DISTNAME}
WRKSRC =	${WRKDIST}/build

post-install:
	${INSTALL_DATA_DIR} ${PREFIX}/share/examples/i2pd \
		${PREFIX}/share/examples/i2pd/tunnels.conf.d \
		${PREFIX}/share/examples/i2pd/login.conf.d
	${INSTALL_DATA} ${WRKDIST}/contrib/openbsd/i2pd.conf \
		${PREFIX}/share/examples/i2pd/i2pd.conf
	${INSTALL_DATA} ${WRKDIST}/contrib/tunnels.conf \
		${PREFIX}/share/examples/i2pd/tunnels.conf
	${INSTALL_DATA} ${WRKDIST}/contrib/tunnels.d/README \
		${PREFIX}/share/examples/i2pd/tunnels.conf.d/README
	${INSTALL_DATA} ${WRKDIST}/contrib/openbsd/i2pd.login \
		${PREFIX}/share/examples/i2pd/login.conf.d/i2pd
	${INSTALL_DATA_DIR} ${PREFIX}/man/man1
	${INSTALL_MAN} ${WRKDIST}/man/i2pd.1 ${PREFIX}/man/man1/i2pd.1
	${INSTALL_MAN} ${WRKDIST}/man/i2pd-tui.1 ${PREFIX}/man/man1/i2pd-tui.1
	${INSTALL_DATA_DIR} ${WRKINST}/etc/rc.d
	${INSTALL_SCRIPT} ${WRKDIST}/contrib/openbsd/i2pd.rc \
		${WRKINST}/etc/rc.d/i2pd

.include <bsd.port.mk>
