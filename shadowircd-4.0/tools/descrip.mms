# MMS/MMK Makefile for OpenVMS
# Copyright (c) 2001 Edward Brocklesby
# $Id: descrip.mms,v 1.1 2004/07/29 15:26:51 nenolod Exp $

CC=	CC
CFLAGS=	/STANDARD=ISOC94
LDFLAGS=

ALL : CONVERTCONF,MKPASSWD

CRYPT.C : [-.SRC]CRYPT.C
	COPY [-.SRC]CRYPT.C []

CRYPT.OBJ : CRYPT.C
	$(CC)$(CFLAGS) CRYPT.C

MKPASSWD : MKPASSWD.OBJ CRYPT.OBJ 
	LINK MKPASSWD, CRYPT

CONVERTCONF : CONVERTCONF.OBJ
	LINK CONVERTCONF

CLEAN : 
	DELETE *.OBJ;*
	DELETE *.EXE;*