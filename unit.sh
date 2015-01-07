#!/usr/bin/env bash

comp='c++ -Wall -pedantic -std=c++11 unit.cpp -g -o unit -lpthread'
prog='./unit'
threads='2 4 8 16'
methods='0 1 2 3'
locks='0 1 2'
auths='0 1 2 3'

method_names=(
  'unsafe'
  'auth'
  'multi'
  'ordered'
)

lock_names=(
  'rw_lock'
  'w_lock'
  'dumb_lock'
)

auth_names=(
  'rw_lock'
  'w_lock'
  'ordered_lock <rw_lock>'
  'ordered_lock <w_lock>'
)

exit_names=(
  'SUCCESS'
  'ERROR_ARGS'
  'ERROR_THREAD'
  'ERROR_DEADLOCK'
  'ERROR_LOGIC'
  'ERROR_SYSTEM'
)

expected_result() {
  local m=$1
  local l=$2
  local a=$3
  #empty argument(s)
  { [ "$m" ] && [ "$l" ] && [ "$a" ]; } || return 1
  #unsafe locking with auth. type
  [ "$m" -eq 0 ] && [ "$a" -gt 0 ] && return 1
  #dumb_lock with non-unsafe locking
  [ "$l" -eq 2 ] && [ "$m" -gt 0 ] && return 1
  #ordered locks without ordered auth.
  [ "$m" -eq 3 ] && [ "$a" -lt 2 ] && return 1
  #unsage locking
  [ "$m" -eq 0 ] && return 3
  return 0
}

cd "$(dirname "$0")" || exit 1

echo "// $comp //"
eval $comp || exit 1

for t in $threads; do
  for m in $methods; do
    for l in $locks; do
      for a in $auths; do
        cmd="$prog $t $m $l $a"
        label="threads: $t; lock method: ${method_names[m]}; lock type: ${lock_names[l]}; auth type: ${auth_names[a]}"
        echo "##### $label >>>>>"
        echo "// $cmd //"
        $cmd
        result=$?
        [ "${exit_names[$result]}" ] && result="${exit_names[$result]}"
        expected_result "$m" "$l" "$a"
        expected=$?
        [ "${exit_names[$expected]}" ] && expected="${exit_names[$expected]}"
        echo "[exit: $result; expected: $expected]"
        echo "<<<<< $label #####"
      done
    done
  done
done
