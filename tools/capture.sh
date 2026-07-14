#!/bin/bash
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
prefix="${1:-shot}"
shift || true
sel="$*"
if [ "${SKIP_BUILD:-0}" != "1" ]; then
  make -C "$root" macos || exit 1
fi
out="$root/build/shots"
mkdir -p "$out"
log="$out/${prefix}.log"
: > "$log"
fail=0
cd "$root/build" || exit 1
while IFS=$'\t' read -r name cam frames tier reps; do
  case "$name" in ""|\#*) continue ;; esac
  if [ -n "$sel" ]; then
    case " $sel " in *" $name "*) ;; *) continue ;; esac
  fi
  r=1
  while [ "$r" -le "${reps:-1}" ]; do
    png="${prefix}_${name}.png"
    [ "$r" -gt 1 ] && png="${prefix}_${name}_run${r}.png"
    env=(VISTA_SMOKE=1 "VISTA_SMOKE_FRAMES=$frames" "VISTA_SHOT=$out/$png")
    [ "$cam" != "-" ] && env+=("VISTA_CAM=$cam")
    [ "$tier" != "-" ] && env+=("VISTA_TIER=$tier")
    echo "== $png ${env[*]}" >> "$log"
    if env "${env[@]}" ./vista >> "$log" 2>&1; then
      status=ok
    else
      status=FAIL
      fail=1
    fi
    echo "$status $out/$png"
    r=$((r + 1))
  done
done < "$root/tools/vantages.tsv"
if grep -iE 'validation|VUID|ERROR' "$log" | grep -v 'SMOKE OK' > "$out/${prefix}.validation"; then
  echo "VALIDATION MESSAGES in $out/${prefix}.validation"
  fail=1
else
  rm -f "$out/${prefix}.validation"
fi
echo "log: $log"
if [ "$fail" = 0 ]; then
  echo "CAPTURE OK"
else
  echo "CAPTURE FAIL"
fi
exit "$fail"
