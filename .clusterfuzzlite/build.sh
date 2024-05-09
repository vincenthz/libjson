#!/bin/bash
for file in "jsonlint.c json.c"; do
  $CC $CFLAGS -c ${file}
done

rm -f ./test*.o
llvm-ar rcs libfuzz.a *.o


$CC $CFLAGS $LIB_FUZZING_ENGINE $SRC/fuzzer.c -Wl,--whole-archive $SRC/libjson/libfuzz.a -Wl,--allow-multiple-definition -I$SRC/libjson/  -o $OUT/fuzzer
