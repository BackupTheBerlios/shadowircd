# MMS/MMK Makefile for OpenVMS
# Copyright (c) 2001 Edward Brocklesby
#
# Usage: 
# $ SET DEF [.IRCD-HYBRID-7.src]
# $ EDIT [-.include]config.h 
#   < change settings in config.h appropriately >
# $ COPY [.include]setup.h_vms [.include]setup.h
# $ MMS IRCD.EXE
#
# $Id: descrip.mms,v 1.1 2004/07/29 15:28:35 nenolod Exp $

CC=	CC
CFLAGS=	/INCLUDE_DIRECTORY=([-.INCLUDE],[])/STANDARD=ISOC94
LDFLAGS=

OBJECTS=	CHECK,GENERAL,QUERY,SETUP,TYPES,EVENT,PARSE,REPLY,TRANSMIT

ALL : ADNS.OLB($(OBJECTS)) 

CLEAN : 
	DELETE *.OLB;*
