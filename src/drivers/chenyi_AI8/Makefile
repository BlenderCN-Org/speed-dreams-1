##############################################################################
#
#    file                 : Makefile
#    created              : 2014年 09月 01日 星期一 13:08:48 EDT
#    copyright            : (C) 2002 Chenyi Chen
#
##############################################################################
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
##############################################################################

ROBOT       = chenyi_AI8
MODULE      = ${ROBOT}.so
MODULEDIR   = drivers/${ROBOT}
SOURCES     = ${ROBOT}.cpp

SHIPDIR     = drivers/${ROBOT}
SHIP        = ${ROBOT}.xml pw-evoviwrc.rgb logo.rgb
SHIPSUBDIRS = 

PKGSUBDIRS  = ${SHIPSUBDIRS}
src-robots-chenyi_AI8_PKGFILES = $(shell find * -maxdepth 0 -type f -print)
src-robots-chenyi_AI8_PKGDIR   = ${PACKAGE}-${VERSION}/$(subst ${TORCS_BASE},,$(shell pwd))

include ${MAKE_DEFAULT}
