#!/usr/bin/env bash

comp='c++ -Wall -pedantic -std=c++11 -g -O2 -I../include unit.cpp -o unit -lpthread'
prog='./unit'
threads='2 4 8 16 256'
methods='0 1 2 3'
deadlocks='0 1'
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

deadlock_names=(
  'no'
  'yes'
)

auth_names=(
  'rw_lock'
  'w_lock'
  'ordered_lock <rw_lock>'
  'ordered_lock <w_lock>'
  '(none)'
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
  local d=$2
  local l=$3
  local a=$4
  #empty argument(s)
  { [ "$m" ] && [ "$d" ] && [ "$l" ] && [ "$a" ]; } || return 1
  #unsafe locking with auth. type
  [ "$m" -eq 0 ] && [ "$a" -gt 0 ] && return 1
  #ordered locks without ordered auth.
  [ "$m" -eq 3 ] && [ "$a" -lt 2 ] && return 1
  #trying to cause a deadlock with multi-locking
  [ "$m" -eq 2 ] && [ "$d" -ne 0 ] && return 1
  #unsafe locking
  [ "$m" -eq 0 ] && return 3
  return 0
}

cd "$(dirname "$0")" || exit 1

echo "// $comp //"
eval $comp || exit 1

for t in $threads; do
  for m in $methods; do
    for d in $deadlocks; do
      for l in $locks; do
          for a in $auths; do
              cmd="$prog $t $m $d $l $a"
              [ "$m" -eq 0 ] && a2=-1 || a2=$a
              label="threads: $t; lock method: ${method_names[m]}; try deadlock: ${deadlock_names[d]}; lock type: ${lock_names[l]}; auth type: ${auth_names[a2]}"
              echo "##### $label >>>>>"
              echo "// $cmd //"
              $cmd
              result=$?
              [ "${exit_names[$result]}" ] && result="${exit_names[$result]}"
              expected_result "$m" "$d" "$l" "$a"
              expected=$?
              [ "${exit_names[$expected]}" ] && expected="${exit_names[$expected]}"
              if [ "$result" = "${exit_names[0]}" ] || [ "$result" = "$expected" ]; then
                pass='PASSED'
              else
                pass='FAILED'
              fi
              echo "$pass [exit: $result; expected: $expected]"
              echo "<<<<< $label #####"
          done
      done
    done
  done
done
