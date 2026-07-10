#!/bin/sh
# Smallest check that fails if sed's parser/engine breaks. Usage: test.sh [path-to-sed]
# Defaults to ../../bin/sed (the built binary).
sed=${1:-../../bin/sed}
fail=0
t() { # t "desc" "expected" actual...
  desc=$1; exp=$2; shift 2
  got=$("$@")
  [ "$got" = "$exp" ] || { echo "FAIL: $desc"; echo "  exp: [$exp]"; echo "  got: [$got]"; fail=1; }
}
p() { printf "$1"; }

t "s///g"        "h0h0"        sh -c "printf 'hoho' | '$sed' 's/o/0/g'"
t "s/// first"   "Xoho"        sh -c "printf 'hoho' | '$sed' 's/h/X/'"
t "ERE backref"  "b a"         sh -c "printf 'a b' | '$sed' -E 's/(a) (b)/\2 \1/'"
t "& whole"      "[42]"        sh -c "printf '42'  | '$sed' 's/[0-9]*/[&]/'"
t "nth s///2"    "a X a"       sh -c "printf 'a a a' | '$sed' 's/a/X/2'"
t "-n /re/p"     "yes"         sh -c "printf 'no\nyes\n' | '$sed' -n '/yes/p'"
t "2d"           "a
c"                             sh -c "printf 'a\nb\nc\n' | '$sed' '2d'"
t "range /re/d"  "1
4"                             sh -c "printf '1\nA\nB\nC\n4\n' | '$sed' '/A/,/C/d'"
t "empty g"      "-a-b-"       sh -c "printf 'ab' | '$sed' 's/x*/-/g'"

[ $fail = 0 ] && echo "sed: all checks pass"
exit $fail
