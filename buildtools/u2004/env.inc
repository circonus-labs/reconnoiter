export PATH="\
/opt/circonus/perl/bin\
:/opt/circonus/java/bin\
:/opt/circonus/bin\
:/usr/local/sbin\
:/usr/local/bin\
:/usr/sbin\
:/usr/bin\
:/sbin\
:/bin"
export CPPFLAGS="\
 -I/opt/circonus/include\
 -I/opt/circonus/include/luajit\
 -I/opt/circonus/include/mysql\
 -DGTEST_HAS_PTHREAD=1"
export CFLAGS="\
 -ggdb3\
 -O2\
 -Wall\
 -Werror\
 -Wno-deprecated-declarations\
 -Wno-unused-variable\
 -Wno-format-truncation"
export CXXFLAGS="\
 -ggdb3\
 -O2\
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
export LIBS="-lm"
export CC="gcc-11"
export CXX="g++-11"
export LD="gcc-11"
export MODULECXX="g++-11"
