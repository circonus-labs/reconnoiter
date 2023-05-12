source buildtools/u2004/env.inc
export CFLAGS="\
 -Wall\
 -Werror\
 -Wno-deprecated-declarations\
 -Wno-unused-variable\
 -Wno-unused-function\
 -g3\
 -ggdb3\
 -fno-omit-frame-pointer\
 -fsanitize=undefined"
export CXXFLAGS="$CXXFLAGS -g3 -ggdb3 -fno-omit-frame-pointer -fsanitize=undefined"
export LDFLAGS="$LDFLAGS -fsanitize=undefined"
export CC="clang"
export CXX="clang++"
export LD="clang"
export UBSAN_OPTIONS="print_stacktrace=1"
export BUILD_UBSAN=1
