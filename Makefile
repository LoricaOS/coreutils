# coreutils — the unprivileged file/text/process utilities for LoricaOS.
#
# Builds every util under src/<name>/ against musl, then packs a class=system
# herald package that installs each binary at /bin/<name>. These are leaf
# utilities: no capability policy, no privilege — the kernel gates every
# privileged operation at the syscall boundary regardless of which binary calls
# it, so compromising `cat` grants nothing the caller didn't already hold.
#
#   make                build every util + pack the .hpkg
#   MUSL_CC=<musl-gcc>  musl cross-compiler (defaults to PATH musl-gcc)
#   HERALD_KEY=<key>    optional signing key for the package
MUSL_CC ?= musl-gcc
VERSION := $(shell cat VERSION)
CFLAGS   = -O2 -s -fno-pie -no-pie -Wl,--build-id=none -Wall

all: package

build-all:
	@mkdir -p bin
	@for d in src/*/; do n=$$(basename $$d); \
	  $(MUSL_CC) $(CFLAGS) -o bin/$$n $$d*.c || exit 1; done
	@echo "[coreutils] built $$(ls bin | wc -l) binaries"

package: build-all
	sh tools/pack.sh

clean:
	rm -rf bin *.hpkg *.hpkg.sig
