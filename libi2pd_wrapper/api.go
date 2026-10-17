package api

/*
* Copyright (c) 2021-2022, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
 */

/*
#cgo CXXFLAGS: -I${SRCDIR}/../i18n -I${SRCDIR}/../libi2pd_client -I${SRCDIR}/../libi2pd -g -Wall -Wextra -Wno-unused-parameter -pedantic -Wno-psabi -fPIC -D__AES__ -maes
#cgo LDFLAGS: -L${SRCDIR}/ -l:../libi2pdwrapper.a -l:../libi2pd.a -l:../libi2pdlang.a -latomic -lcrypto -lssl -lz -lboost_system -lboost_date_time -lboost_filesystem -lboost_program_options -lpthread -lstdc++
*/
import "C"
