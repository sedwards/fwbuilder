include(../tests_common.pri)
QT += gui network

HEADERS = generatedScriptTestsLinux.h
SOURCES = main_generatedScriptTestsLinux.cpp \
	  generatedScriptTestsLinux.cpp
TARGET = generatedScriptTestsLinux

run_tests.commands = echo "Running tests..." && \
    rm -f *.fw && \
    ./${TARGET}

