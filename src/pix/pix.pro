#-*- mode: makefile; tab-width: 4; -*-
#
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
include(../../qmake.inc)
QT -= gui
SOURCES	 =  pix.cpp

HEADERS	 = ../../config.h

!win32 {
	QMAKE_COPY    = ../../install.sh -m 0755
}

win32:CONFIG += console

INCLUDEPATH += ../cisco_lib ../compiler_lib ../libfwbuilder/src
DEPENDPATH  += ../cisco_lib ../compiler_lib ../libfwbuilder/src

PRE_TARGETDEPS  = ../common/$$BINARY_SUBDIR/libcommon.a \
      ../cisco_lib/$$BINARY_SUBDIR/libfwbcisco.a \
      ../compiler_lib/$$BINARY_SUBDIR/libcompilerdriver.a \
      ../libfwbuilder/src/fwcompiler/$$BINARY_SUBDIR/libfwcompiler.a \
      ../libfwbuilder/src/fwbuilder/$$BINARY_SUBDIR/libfwbuilder.a \

LIBS  += $$PRE_TARGETDEPS $$LIBS

TARGET = fwb_pix

