source buildtools/el7/env.inc
export PATH="$PATH:/opt/llvm-5.0.0/bin"
export CFLAGS="$CFLAGS -Wno-unused-function -fsanitize=address"
export CXXFLAGS="$CXXFLAGS -fsanitize=address"
export LDFLAGS="$LDFLAGS -fsanitize=address -static-libasan"
export ASAN_SYMBOLIZER_PATH=/opt/llvm-5.0.0/bin/llvm-symbolizer
export ASAN_OPTIONS=detect_leaks=0,alloc_dealloc_mismatch=1,verify_asan_link_order=0
