export PATH="\
/opt/circonus/perl/bin\
:/opt/circonus/bin\
:/usr/local/sbin\
:/usr/local/bin\
:/usr/sbin\
:/usr/bin\
:/sbin\
:/bin"
CPPFLAGS="\
 -I/opt/circonus/include\
 -I/opt/circonus/include/luajit\
 -DGTEST_HAS_PTHREAD=1"
export CFLAGS="\
 -Wall\
 -Werror"
export CXXFLAGS="\
 -Wall\
 -Werror"
export LDFLAGS="\
 -L/opt/circonus/lib\
 -Wl,-rpath=/opt/circonus/lib\
 -L/usr/lib64/atlas\
 -Wl,-rpath=/usr/lib64/atlas"
export NPM="/opt/circonus/bin/npm"
export SHCFLAGS="-g"
export MAKE="make"
export CPPFLAGS
