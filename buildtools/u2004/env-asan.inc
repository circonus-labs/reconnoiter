source buildtools/u2004/env.inc
export CFLAGS="$CFLAGS -Wno-unused-function -fsanitize=address"
export CXXFLAGS="$CXXFLAGS -fsanitize=address"
export LDFLAGS="$LDFLAGS -fsanitize=address"
export CC="clang"
export CXX="clang++"
export LD="clang"
export ASAN_OPTIONS=detect_leaks=0,alloc_dealloc_mismatch=1
export BUILD_ASAN=1
