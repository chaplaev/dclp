#

RRD_DIR		= M:\cygwin\home\user\relacy_2_4

CC 			= g++
RM		 	= del /F /Q $(PROGS).exe

CPPFLAGS	= -Wall -fexceptions -O2 -std=c++11 -march=core2 -Wno-deprecated -fexpensive-optimizations -I$(RRD_DIR)
SOURCE		= dclp.cpp
PROGS		= dclp
LDFLAGS		= -s



OBJS		:= $(SOURCE:.cpp=.o)
OBJS		:= $(OBJS:.c=.o)


all: .depend $(PROGS) FORCE

.PHONY: all clean
.PHONY: FORCE


.c.o: .depend FORCE
		$(CC) -o $@ -c $(CFLAGS) $(CPPFLAGS) $<
.cpp.o: .depend FORCE
		$(CC) -o $@ -c $(CFLAGS) $(CPPFLAGS) $<

dclp: .depend $(OBJS)
		$(CC) -o $@ $(OBJS) $(LIBS) $(LDFLAGS)

.depend: $(SOURCE)
		$(CC) -MM $(CFLAGS) $(CPPFLAGS) $(SOURCE) 1> .depend

$(PROGS): .depend

clean:
		@$(RM) .depend $(OBJS) $(PROGS)

ifneq ($(wildcard .depend),)
include .depend
endif
