#!/bin/bash
# The converters' Python tests that need no disc (docs/MAC.md "Mac pass for 0.7.0"): made-up data only.
#
#   tools/mac/python_tests.sh <python with numpy and Pillow> <logs folder>
#
# Writes <logs>/pytests_summary.txt and <logs>/pytest_<name>.log. Exit 0 = every test exited 0.
set +e -u
PY="${1:?python}"
LOGS="${2:?logs folder}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$LOGS"
LOGS="$(cd "$LOGS" && pwd)"
SUMMARY="$LOGS/pytests_summary.txt"
: > "$SUMMARY"
FAIL=0
run() { # <folder to run in> <script>
  dir="$ROOT/$1"; script="$2"
  name="$(basename "$script" .py)"
  ( cd "$dir" && PYTHONDONTWRITEBYTECODE=1 PYTHONIOENCODING=utf-8 perl -e 'alarm 900; exec @ARGV' "$PY" "$script" ) > "$LOGS/pytest_$name.log" 2>&1
  rc=$?
  if [ $rc = 0 ]; then echo "PASS  $name" | tee -a "$SUMMARY"
  else echo "FAIL  $name (exit $rc)" | tee -a "$SUMMARY"; tail -n 15 "$LOGS/pytest_$name.log"; FAIL=$((FAIL + 1)); fi
}
for t in test_usum_anim test_usum_broken test_usum_fx test_usum_gen4 test_usum_hand test_usum_look test_usum_match test_usum_models; do
  run tools/n3ds "$t.py"
done
run tools/pbr_convert gen4_text_i18n_test.py
grep -q "^0 problems" "$LOGS/pytest_gen4_text_i18n_test.log" || { echo "FAIL  gen4_text_i18n_test (no '0 problems' line)" | tee -a "$SUMMARY"; FAIL=$((FAIL + 1)); }
run tools/model_convert test_model_convert.py
grep -q "ALL OK" "$LOGS/pytest_test_model_convert.log" || { echo "FAIL  test_model_convert (no 'ALL OK' line)" | tee -a "$SUMMARY"; FAIL=$((FAIL + 1)); }
echo "python tests: $FAIL failed" | tee -a "$SUMMARY"
[ "$FAIL" = 0 ]
