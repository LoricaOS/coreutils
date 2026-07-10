#!/bin/sh
# Smallest check that fails if chmod's mode parser breaks. Usage: test.sh [path-to-chmod]
chmod_bin=${1:-../../bin/chmod}
d=$(mktemp -d); f=$d/f; fail=0
m() { stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1"; }   # GNU / BSD
t() { exp=$1; got=$(m "$f"); [ "$got" = "$exp" ] || { echo "FAIL: expected $exp got $got ($2)"; fail=1; }; }

touch "$f"
chmod 600 "$f"; "$chmod_bin" +x       "$f"; t 711 "+x on 600"
chmod 644 "$f"; "$chmod_bin" u+w,go-r "$f"; t 600 "u+w,go-r on 644"
chmod 600 "$f"; "$chmod_bin" a=rx     "$f"; t 555 "a=rx"
"$chmod_bin" 755 "$f";                       t 755 "octal 755"
rm -rf "$d"
[ $fail = 0 ] && echo "chmod: all checks pass"
exit $fail
