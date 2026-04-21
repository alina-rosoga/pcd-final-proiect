# PCD T31 - Makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow \
          -Wstrict-prototypes -Wmissing-prototypes       \
          -Wno-unused-parameter                          \
          -std=c11 -D_POSIX_C_SOURCE=200809L             \
          -Iinclude -Isrc

DEBUG ?= 0
ifeq ($(DEBUG),1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

LIBS_SOAP    = -lgsoap
LIBS_CONFIG  = -lconfig
LIBS_LIBROSA = -lrosacpp
LIBS_THREAD  = -lpthread
LIBS_MATH    = -lm

SOAP_GEN = src/soapC.c src/soapServer.c src/soapClient.c

LIB_SRC = src/config_loader.c src/processing.c

SERVERDS_SRC    = src/server.c src/soapds.c $(LIB_SRC) $(SOAP_GEN)
INETCLIENT_SRC  = src/client.c $(SOAP_GEN)
INETDS2_SRC     = src/inetds2.c src/processing.c
INETSAMPLE2_SRC = src/inetsample2.c src/processing.c
DEMO_SRC        = src/demo_milestone1.c src/config_loader.c src/processing.c

all: output serverds inetclient inetds2 inetsample2 demo_milestone1

output:
	mkdir -p output

serverds: $(SERVERDS_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_SOAP) $(LIBS_CONFIG) $(LIBS_LIBROSA) $(LIBS_THREAD) $(LIBS_MATH)

inetclient: $(INETCLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_SOAP) $(LIBS_MATH)

inetds2: $(INETDS2_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_LIBROSA) $(LIBS_MATH)

inetsample2: $(INETSAMPLE2_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_LIBROSA) $(LIBS_MATH)

demo_milestone1: $(DEMO_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_CONFIG) $(LIBS_LIBROSA) $(LIBS_MATH)

demo: demo_milestone1

soap_gen:
	soapcpp2 -c -S -Iinclude -Isrc src/proto.h -d src/

tidy:
	clang-tidy src/server.c src/config_loader.c src/processing.c \
	           src/soapds.c src/client.c src/demo_milestone1.c   \
	           src/inetds2.c src/inetsample2.c -- $(CFLAGS)

clean:
	rm -f serverds inetclient inetds2 inetsample2 demo_milestone1
	rm -f src/soapC.c src/soapServer.c src/soapClient.c
	rm -f src/soap*.h src/*.nsmap src/*.wsdl src/*.xsd
	rm -f output/*.bin

.PHONY: all demo soap_gen tidy clean output
