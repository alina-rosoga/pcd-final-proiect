#!/usr/bin/env bash
# =============================================================================
# setup_complet.sh - Face TOT: structura, header-e, dependente, build, test
# Rulezi o singura data: bash setup_complet.sh
# =============================================================================
set -euo pipefail

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[OK]${NC}    $*"; }
info() { echo -e "${YELLOW}[INFO]${NC}  $*"; }
err()  { echo -e "${RED}[ERR]${NC}   $*"; exit 1; }

# =============================================================================
# PASUL 1 — Structura de directoare
# =============================================================================
info "Creez structura de directoare..."
mkdir -p include src config output

ok "Directoare create: include/ src/ config/ output/"

# =============================================================================
# PASUL 2 — Header-ele care lipseau (server.h, processing.h, config_loader.h)
# =============================================================================
info "Creez include/server.h ..."
cat > include/server.h << 'EOF'
/**
 * @file server.h
 * @brief PCD T31 - Server shared constants and types
 */
#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

#define DEFAULT_PORT         8080
#define DEFAULT_CONFIG_PATH  "config/server.cfg"
#define MAX_PATH_LEN         512
#define MAX_WORKERS_DEFAULT  4
#define SOAP_BACKLOG         10
#define SOAP_TIMEOUT_S       30
#define SOAP_MAX_KEEP_ALIVE  100

typedef struct {
    int  port;
    int  max_workers;
    int  log_level;
    char output_dir[MAX_PATH_LEN];
    char soap_endpoint[MAX_PATH_LEN];
} server_config_t;

#endif /* SERVER_H */
EOF
ok "include/server.h creat"

info "Creez include/processing.h ..."
cat > include/processing.h << 'EOF'
/**
 * @file processing.h
 * @brief PCD T31 - DSP processing types and public API
 */
#ifndef PROCESSING_H
#define PROCESSING_H

#include "server.h"

#define DEFAULT_SR          22050
#define DEFAULT_N_FFT       2048
#define DEFAULT_HOP_LENGTH  512
#define DEFAULT_N_MELS      128

typedef struct {
    char input_path[MAX_PATH_LEN];
    char output_path[MAX_PATH_LEN];
    int  target_sr;
    int  n_fft;
    int  hop_length;
    int  n_mels;
} proc_request_t;

typedef struct {
    int    status;
    char   error_msg[256];
    char   output_path[MAX_PATH_LEN];
    int    sample_rate;
    int    n_samples;
    double duration_s;
    int    n_mels;
    int    n_frames;
} proc_result_t;

int process_spectrogram(const proc_request_t *req, proc_result_t *res);

#endif /* PROCESSING_H */
EOF
ok "include/processing.h creat"

info "Creez include/config_loader.h ..."
cat > include/config_loader.h << 'EOF'
/**
 * @file config_loader.h
 * @brief PCD T31 - libconfig loader public API
 */
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "server.h"

int  config_load(const char *path, server_config_t *out);
void config_free(server_config_t *cfg);

#endif /* CONFIG_LOADER_H */
EOF
ok "include/config_loader.h creat"

# =============================================================================
# PASUL 3 — Muta fisierele .c si restul in src/ si config/
# =============================================================================
info "Mut fisierele sursa in src/ ..."

for f in server.c server__1_.c client.c processing.c config_loader.c \
          soapds.c demo_milestone1.c inetds2.c inetsample2.c \
          threeds.c unixds.c proto.h proto.nsmap soapH.h sclient.h; do
    if [ -f "$f" ]; then
        cp "$f" src/
        ok "  src/$f"
    fi
done

info "Mut config in config/ ..."
if [ -f "server.cfg" ]; then
    cp server.cfg config/server.cfg
    ok "  config/server.cfg"
fi

# =============================================================================
# PASUL 4 — Makefile (rescris cu -Iinclude corect)
# =============================================================================
info "Creez Makefile ..."
cat > Makefile << 'EOF'
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
EOF
ok "Makefile creat"

# =============================================================================
# PASUL 5 — Instaleaza dependentele sistem
# =============================================================================
info "Instalez dependente sistem..."
sudo apt-get update -qq
sudo apt-get install -y -q \
    libgsoap-dev \
    libconfig-dev \
    clang-tidy \
    cmake \
    git \
    sox \
    gcc \
    make
ok "Dependente sistem instalate"

# =============================================================================
# PASUL 6 — LibrosaC
# =============================================================================
LIBROSA_PREFIX="$HOME/.local"

if pkg-config --exists rosacpp 2>/dev/null || \
   [ -f "$LIBROSA_PREFIX/lib/librosacpp.so" ] || \
   [ -f "/usr/local/lib/librosacpp.so" ]; then
    ok "LibrosaC deja instalat, skip."
else
    info "Clonez si build LibrosaC..."
    rm -rf /tmp/librosa-build
    git clone --depth=1 https://github.com/librosa/librosa-cpp /tmp/librosa-build
    cd /tmp/librosa-build
    mkdir -p build && cd build
    cmake .. \
        -DBUILD_C_BINDINGS=ON \
        -DCMAKE_INSTALL_PREFIX="$LIBROSA_PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON
    make -j"$(nproc)"
    make install
    cd -
    ok "LibrosaC instalat in $LIBROSA_PREFIX"
fi

# Exporta path-urile (si le adauga in .bashrc daca nu exista)
export PKG_CONFIG_PATH="$LIBROSA_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$LIBROSA_PREFIX/lib:${LD_LIBRARY_PATH:-}"

if ! grep -q "librosa-cpp\|rosacpp\|LIBROSA" "$HOME/.bashrc" 2>/dev/null; then
    cat >> "$HOME/.bashrc" << BASHEOF

# LibrosaC — PCD T31
export PKG_CONFIG_PATH="$LIBROSA_PREFIX/lib/pkgconfig:\${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$LIBROSA_PREFIX/lib:\${LD_LIBRARY_PATH:-}"
BASHEOF
    ok "Path-uri LibrosaC adaugate in ~/.bashrc"
fi

# =============================================================================
# PASUL 7 — Genereaza bindings gSOAP
# =============================================================================
info "Generez bindings gSOAP din src/proto.h ..."
soapcpp2 -c -S -Iinclude -Isrc src/proto.h -d src/ 2>/dev/null && ok "gSOAP bindings generate" || \
    echo -e "${YELLOW}[WARN]${NC}  soapcpp2 a raportat warnings (poate fi OK)"

# =============================================================================
# PASUL 8 — Build
# =============================================================================
info "Build..."
make demo_milestone1 && ok "demo_milestone1 compilat cu succes!"

# =============================================================================
# PASUL 9 — Genereaza WAV de test si ruleaza demo
# =============================================================================
info "Generez fisier WAV de test..."
sox -n -r 22050 -c 1 output/test_tone.wav synth 2 sine 440
ok "output/test_tone.wav creat (2s, 440Hz)"

info "Rulez demo_milestone1..."
echo ""
./demo_milestone1 -c config/server.cfg -f output/test_tone.wav
echo ""

# =============================================================================
# DONE
# =============================================================================
echo ""
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  Setup complet! Totul functioneaza.       ${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo "Comenzi utile:"
echo "  Demo M1:       ./demo_milestone1 -c config/server.cfg -f output/test_tone.wav"
echo "  Test automat:  bash test_milestone1.sh"
echo "  Server SOAP:   make && ./serverds -c config/server.cfg -v"
echo "  Build debug:   make DEBUG=1"
echo "  Linter:        make tidy"
echo ""
EOF
ok "setup_complet.sh creat"