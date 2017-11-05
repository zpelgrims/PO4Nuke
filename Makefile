# Compiler Info ('clang++ --version')
# Apple LLVM version 8.0.0 (clang-800.0.42.1)
# Target: x86_64-apple-darwin16.6.0
# Thread model: posix
# InstalledDir: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin
# End Compiler Info Output


CXX = /usr/local/Cellar/llvm/5.0.0/bin/clang++
LINK = /usr/local/Cellar/llvm/5.0.0/bin/clang++


# Nuke jazz
NDKDIR ?= /Applications/Nuke11.0v1/Nuke11.0v1.app/Contents/MacOS
CXXFLAGS ?= -g -c \
            -DUSE_GLEW \
            -I$(NDKDIR)/include \
            -F/Applications/Nuke11.0v1/Nuke11.0v1.app/Contents/Frameworks \
            -DFN_EXAMPLE_PLUGIN

LINKFLAGS ?= -L$(NDKDIR) \
             -L./ 
LIBS ?= -lDDImage -lGLEW # DDImage and its dependencies
LINKFLAGS += -bundle
FRAMEWORKS ?= -framework QuartzCore \
              -framework IOKit \
              -framework CoreFoundation \
              -framework Carbon \
              -framework ApplicationServices \
              -framework OpenGL \
              -framework AGL


# polynomial optics compiler flags
CXXFLAGSPOLYNOMIALOPTICS=-fPIC -D_REENTRANT -D_THREAD_SAFE -D_GNU_SOURCE -fopenmp 
CXXFLAGSPOLYNOMIALOPTICS+=-I. -ITruncPoly -IOpticalElements -Iinclude -g -Wall -fno-strict-aliasing -I/usr/local/opt/llvm/include -I/usr/local/opt/llvm/include/c++/v1/
OPTFLAGSPOLYNOMIALOPTICS=-O3 -ffast-math -mfpmath=sse -march=native -DNDEBUG
LDFLAGSPOLYNOMIALOPTICS=-lm -L/usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib

all: /Users/zeno/polynomialOpticsNuke/src/polynomialOpticsBlur.dylib

.PRECIOUS : %.os
%.os: %.cpp
	$(CXX) $(CXXFLAGS) $(CXXFLAGSPOLYNOMIALOPTICS) ${OPTFLAGSPOLYNOMIALOPTICS} -o $(@) $<
%.dylib: %.os
	$(LINK) $(LDFLAGSPOLYNOMIALOPTICS) $(LINKFLAGS) $(LIBS) $(FRAMEWORKS) -fopenmp -o $(@) $<
%.a: %.cpp
	$(CXX) $(CXXFLAGS) $(CXXFLAGSPOLYNOMIALOPTICS) ${OPTFLAGSPOLYNOMIALOPTICS} -o lib$(@) $<

clean:
	rm -rf *.os \
	       *.o \
	       *.a \
	       *.dylib
