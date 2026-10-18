/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package api


/*
#cgo CXXFLAGS: -I${SRCDIR}/../i18n -I${SRCDIR}/../libi2pd_client -I${SRCDIR}/../libi2pd -g -Wall -Wextra -Wno-unused-parameter -pedantic -Wno-psabi -fPIC -D__AES__ -maes
#cgo LDFLAGS: -L${SRCDIR}/ -l:../libi2pdwrapper.a -l:../libi2pd.a -l:../libi2pdlang.a -latomic -lcrypto -lssl -lz -lboost_system -lboost_date_time -lboost_filesystem -lboost_program_options -lpthread -lstdc++
*/
import "C"
