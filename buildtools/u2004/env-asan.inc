source buildtools/u2004/env.inc
export CFLAGS="\
 -Wall\
 -Werror\
 -Wno-deprecated-declarations\
 -Wno-unused-variable\
 -Wno-unused-function\
 -g3\
 -ggdb3\
 -fsanitize=address\
 -fsanitize-recover=address"
export CXXFLAGS="$CXXFLAGS -g3 -ggdb3 -fsanitize=address -fsanitize-recover=address"
export LDFLAGS="$LDFLAGS -fsanitize=address -fsanitize-recover=address"
export CC="clang"
export CXX="clang++"
export LD="clang"
export ASAN_OPTIONS=detect_leaks=0,alloc_dealloc_mismatch=1
export BUILD_ASAN=1
