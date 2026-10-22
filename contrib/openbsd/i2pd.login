# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: i2pd.login.
#
# OpenBSD login class for the i2pd service account, raising the soft and hard
# open-file limits to 8192 and setting a private creation mask.
#

i2pd:\
        :openfiles-cur=8192:\
        :umask=0077:\
        :openfiles-max=8192:\
        :tc=daemon:
