test_number=11
description="user-specified key labels"

for i in `seq 0 $((${#test_variants[*]} - 1))`; do
  outfile="$test_dir/test_$test_number.${test_variants[$i]}.actual"
  expected=$test_dir/test_$test_number.expected
  $bin ${test_variants[$i]} \
       -A Key-Field-1,Key-Field-2 -B Key-Field-1,Key-Field-2 \
       -o "$outfile" \
       "$test_dir/test_$test_number.a" \
       "$test_dir/test_$test_number.b"

  if [ $? -ne 0 ] ||
     [ "`diff -q $outfile $expected`" ]; then
    test_status $test_number $i "$description (${variant_desc[$i]})" FAIL
  else
    test_status $test_number $i "$description (${variant_desc[$i]})" PASS
    rm "$outfile"
  fi
done
