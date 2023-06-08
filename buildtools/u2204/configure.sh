autoreconf -i

PATH=/opt/circonus/java/bin:/opt/circonus/bin:$PATH
LDFLAGS="$LDFLAGS -Wl,-L/opt/circonus/lib -Wl,-rpath=/opt/circonus/lib"
CPPFLAGS="$CPPFLAGS -I/opt/circonus/include -I/opt/circonus/include/luajit -I/opt/circonus/include/mysql"
CFLAGS="$CFLAGS -O2 -ggdb"
CXXFLAGS="$CXXFLAGS -O2 -ggdb3"

./configure \
  --prefix=/opt/noit/prod \
  --exec-prefix=/opt/noit/prod \
  --libdir=/opt/noit/prod/lib \
  --libexecdir=/opt/noit/prod/libexec
