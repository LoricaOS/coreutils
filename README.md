# coreutils

The unprivileged file / text / process utilities for **AspisOS**, a
capability-based, no-ambient-authority x86-64 operating system built on the
from-scratch [Aegis](https://github.com/AspisOS/Aegis) kernel.

These are the leaf commands — `ls`, `cat`, `cp`, `grep`, `ps`, `find`, … — that
make up the base userland. They hold **no capability policy and no privilege**:
the kernel gates every privileged operation (chown to another uid, killing
another process, setting the clock) at the syscall boundary regardless of which
binary calls it, so compromising one of these grants nothing the caller didn't
already hold. That's exactly why they're peeled out of the trusted base — the
things that *do* carry authority (init, the shell's elevation path, login/auth,
the package manager, system-control and network-config tools) stay in the OS
repo; these don't.

A clean coreutils built against the Aegis syscall ABI also doubles as a
**reference userland** for that ABI.

## Build

```sh
make MUSL_CC=/path/to/musl-gcc        # build every util + pack coreutils.hpkg
```

`make` compiles each `src/<name>/*.c` against musl and packs a `class=system`
herald package that installs each binary at `/bin/<name>` (plus `/bin/[`, the
same program as `/bin/test`). `HERALD_KEY` optionally signs it.

## Layout

```
src/<name>/   one directory per utility (single C source each)
tools/pack.sh build the signed .hpkg (every binary -> /bin/<name>)
Makefile      build-all -> pack
VERSION       package version
```

The OS consumes `coreutils.hpkg` as a fetched artifact and unpacks it into the
base rootfs — see [AspisOS](https://github.com/AspisOS/AspisOS).
