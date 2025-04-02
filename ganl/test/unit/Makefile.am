# Top level Makefile.am for test/unit directory

# List subdirectories if any (none in this case yet)
# SUBDIRS =

# Define the test programs to be built and run by 'make check'
check_PROGRAMS = io_buffer_test

# Define source files for each test program
io_buffer_test_SOURCES = io_buffer_test.cpp

# Define common C++ flags (adjust as necessary for your project)
# -I$(top_srcdir)/include tells it where to find ganl/io_buffer.h
# -I/path/to/gtest/include might be needed if gtest isn't in standard paths
# -DGTEST_HAS_PTHREAD=1 might be needed depending on gtest build/platform
# Use C++17 standard as required by the project spec/code
AM_CPPFLAGS = \
    -I$(top_srcdir)/include \
    -DGTEST_HAS_PTHREAD=1 \
    -pthread \
    -std=c++17

# Define libraries to link against for each test program
# $(top_builddir)/src/libganl.la assumes your IoBuffer implementation is part
#   of a libganl library built in the src directory. Adjust if needed.
# If io_buffer.cpp is *not* part of a library, you might need to link
#   the object file directly: $(top_builddir)/src/ganl/io_buffer.o
# Link against Google Test libraries (-lgtest, -lgtest_main) and pthreads.
# The order of libraries can matter.
io_buffer_test_LDADD = \
    ../../src/common/libganlcommon.a \
    -lgtest_main \
    -lgtest \
    -lpthread

# Define linker flags (adjust as necessary)
# -L/path/to/gtest/lib might be needed if gtest isn't in standard paths
AM_LDFLAGS = -pthread

# List the programs to be executed by 'make check'
TESTS = $(check_PROGRAMS)

# Optional: Set environment for tests, e.g., for verbose output
# AM_TESTS_ENVIRONMENT = CK_VERBOSITY=verbose

# Optional: Cleanup generated files
# CLEANFILES = $(check_PROGRAMS)
