# Makefile for kLIBC/GNU Make
.PHONY : all

.SUFFIXES : .exe .a .lib .o .c .h

ifeq ($(PREFIX),)
PREFIX=/usr
endif
LIBDIR=$(PREFIX)/lib
INCDIR=$(PREFIX)/include

ifeq ($(INSTALL),)
INSTALL=ginstall
endif

CC = gcc
CFLAGS = -Wall -O3
LDFLAGS = -Zomf

RM = rm -f

.c.o :
	$(CC) $(CFLAGS) -c -o $@ $<

.a.lib :
	emxomf -o $@ $<

all : dart.a dart.lib darttest.exe

dart.a : dart.o
	ar rc $@ $^

dart.o : dart.c dart.h

darttest.exe : darttest.o dart.lib
	$(CC) $(LDFLAGS) -o $@ $^ -lmmpm2

darttest.o : darttest.c dart.h

clean :
	-$(RM) *.bak
	-$(RM) *.o
	-$(RM) *.a
	-$(RM) *.obj
	-$(RM) *.lib
	-$(RM) *.exe

dist : src
	mkdir dart_dist
	$(MAKE) install PREFIX=$(shell pwd)/dart_dist
	( cd dart_dist && zip -rpSm ../libdart$(VER).zip * )
	rmdir dart_dist
	zip -m libdart$(VER).zip src.zip

distclean : clean
	-$(RM) *.zip

src : dart.c dart.h darttest.c daudio.wav Makefile Makefile.icc Makefile.wat
	-$(RM) src.zip
	zip src.zip $^

install : dart.a dart.lib dart.h
	$(INSTALL) -d $(LIBDIR)
	$(INSTALL) -d $(INCDIR)
	$(INSTALL) dart.a $(LIBDIR)
	$(INSTALL) dart.lib $(LIBDIR)
	$(INSTALL) dart.h $(INCDIR)

uninstall :
	-$(RM) $(LIBDIR)/dart.a $(LIBDIR)/dart.lib
	-$(RM) $(INCDIR)/dart.h

