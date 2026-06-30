#!/bin/sh
# pack.sh — build the class=system herald package (.hpkg) for coreutils.
#
# A .hpkg is a manifest-first uncompressed POSIX ustar + an optional detached
# ECDSA-P256/SHA-256 signature. coreutils is class=system (first-party,
# signature-trusted, installed verbatim into /bin). Payload = every built
# binary at /bin/<name>, plus /bin/[ (the same program as /bin/test).
set -eu
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
VER="$(cat VERSION)"
KEY="${HERALD_KEY:-}"
STRIP="${STRIP:-strip}"

ID=coreutils
NAME="Core utilities"
DEPENDS=                     # foundational base userland — depends on nothing

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/bin"
for b in bin/*; do
    name="$(basename "$b")"
    if ! "$STRIP" -o "$STAGE/bin/$name" "$b" 2>/dev/null; then
        cp "$b" "$STAGE/bin/$name"
    fi
    chmod 0755 "$STAGE/bin/$name"
done
# `test` and `[` are the same program installed under two names.
if [ -f "$STAGE/bin/test" ]; then
    cp "$STAGE/bin/test" "$STAGE/bin/["
    chmod 0755 "$STAGE/bin/["
fi

printf 'id=%s\nname=%s\nversion=%s\nclass=system\ndepends=%s\n' \
    "$ID" "$NAME" "$VER" "$DEPENDS" > "$STAGE/manifest"

roots="$(cd "$STAGE" && ls -A | grep -v '^manifest$' | tr '\n' ' ')"
cd "$STAGE" && tar --format=ustar -cf "$ROOT/$ID.hpkg" manifest $roots
cd "$ROOT"
if [ -n "$KEY" ]; then
    openssl dgst -sha256 -sign "$KEY" -out "$ID.hpkg.sig" "$ID.hpkg"
else
    rm -f "$ID.hpkg.sig"; echo "[$ID] unsigned (no HERALD_KEY set)"
fi
echo "[$ID] $ID.hpkg $VER ($(wc -c < "$ID.hpkg") bytes, $(ls bin | wc -l) binaries + [)"
