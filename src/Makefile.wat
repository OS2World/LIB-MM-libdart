# Makefile for OpenWatcom/WMAKE
.ERASE

.SUFFIXES :
.SUFFIXES : .exe .lib .obj .c .h

CC = wcc386
CFLAGS = -zq -wx -bm -d0 -oaxt

LINK = wlink
LFLAGS = option quiet

RM = del

.c.obj :
    $(CC) $(CFLAGS) -fo=$@ $[@

all : .SYMBOLIC dart.lib darttest.exe

dart.lib : dart.obj
    -$(RM) $@
    wlib -b $@ $<

dart.obj : dart.c dart.h

darttest.exe : darttest.obj dart.lib
    $(LINK) $(LFLAGS) system os2v2 name $@ file { $< } library mmpm2

darttest.obj : darttest.c dart.h

clean : .SYMBOLIC
    -$(RM) *.bak
    -$(RM) *.obj
    -$(RM) *.lib
    -$(RM) *.exe
