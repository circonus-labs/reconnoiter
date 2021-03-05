source buildtools/el7/env.inc
export PATH="/opt/rh/devtoolset-9/root/usr/bin:$PATH"
export CFLAGS="$CFLAGS -Wno-unused-function -fsanitize=address"
export CXXFLAGS="$CXXFLAGS -fsanitize=address"
export LDFLAGS="$LDFLAGS -fsanitize=address -static-libasan -L/opt/rh/devtoolset-9/root/usr/lib/gcc/x86_64-redhat-linux/9 -Wl,-rpath=/opt/rh/devtoolset-9/root/usr/lib/gcc/x86_64-redhat-linux/9"
