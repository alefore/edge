#!/bin/sh -e
touch NEWS ChangeLog
libtoolize --copy --ltdl --force
aclocal
autoconf
autoheader
automake --add-missing --copy
