/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: api.
 *
 * Go cgo package that supplies the compile and link flags for the C++
 * libi2pdwrapper, libi2pd and libi2pdlang libraries.
 *
 */

package api

/*
#cgo CXXFLAGS: -I${SRCDIR}/../i18n -I${SRCDIR}/../libi2pd_client -I${SRCDIR}/../libi2pd -g -Wall -Wextra -Wno-unused-parameter -pedantic -Wno-psabi -fPIC -D__AES__ -maes
#cgo LDFLAGS: -L${SRCDIR}/ -l:../libi2pdwrapper.a -l:../libi2pd.a -l:../libi2pdlang.a -latomic -lcrypto -lssl -lz -lboost_system -lboost_date_time -lboost_filesystem -lboost_program_options -lpthread -lstdc++
*/
import "C"
