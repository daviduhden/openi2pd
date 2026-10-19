# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: i2pd.login.
#
# OpenBSD login class for the i2pd service account, raising the soft and hard
# open-file limits to 4096.
#

i2pd:\
        :openfiles-cur=4096:\
        :openfiles-max=4096:\
        :tc=daemon:
