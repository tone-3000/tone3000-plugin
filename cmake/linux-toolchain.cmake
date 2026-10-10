# cmake/linux-toolchain.cmake

# Ensure pkg-config is available
find_package(PkgConfig REQUIRED)

# GTK3: JUCE's native file dialogs on Linux.
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
# GTK's transitive cflags (pango/cairo/harfbuzz) carry -I/usr/include/freetype2.
# FreeType is built from source and linked statically (see the freetype
# block in the root CMakeLists.txt), and a directory-level include would
# shadow its headers with the system ones, so keep that one out. Nothing
# in gtk.h's own include graph needs ft2build.h.
list(FILTER GTK3_INCLUDE_DIRS EXCLUDE REGEX "freetype2$")
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
