#

CC 			= g++
RM			= rm -f

CPPFLAGS	= -Wall -fexceptions -Os -std=c++11 -march=core2
SOURCE		= dreorder.cpp
PROGS		= dreorder
LDFLAGS		= -s


ifeq ($(OS), Windows_NT)
	GCC_V	:= $(shell $(CC) -v 2>&1)

	ifneq (,$(findstring cygwin, $(GCC_V)))
#		CC		 = x86_64-w64-mingw32-gcc
#		CC		 = i686-w64-mingw32-g++.exe
#		LIBS	 += -lpthread -lpsapi -lws2_32
        CPPFLAGS += -D CYGWIN
	endif

	ifneq (,$(findstring mingw, $(GCC_V)))
        CPPFLAGS += -D MINGW
		RM		 = del /F /Q $(PROGS).exe
	endif


    CPPFLAGS += -D WIN32
    ifeq ($(PROCESSOR_ARCHITECTURE), AMD64)
        CPPFLAGS += -D AMD64
    endif
    ifeq ($(PROCESSOR_ARCHITECTURE), x86)
        CPPFLAGS += -D IA32
    endif

else
    UNAME	:= $(shell uname)

	ifeq ($(UNAME), Linux)
#		LIBS	+= -lpthread -ldl -lrt -laio
#		LDFLAGS	+= -rdynamic
		CPPFLAGS += -D LINUX
	endif

#    ifeq ($(UNAME), Darwin)
#        CPPFLAGS += -D OSX
#    endif


    UNAME_P := $(shell uname -p)
    ifeq ($(UNAME_P), x86_64)
        CPPFLAGS += -D AMD64
    endif
    ifneq ($(filter %86, $(UNAME_P)),)
        CPPFLAGS += -D IA32
    endif
    ifneq ($(filter arm%, $(UNAME_P)),)
        CPPFLAGS += -D ARM
    endif
endif



OBJS		:= $(SOURCE:.cpp=.o)
OBJS		:= $(OBJS:.c=.o)


all: .depend $(PROGS) FORCE

.PHONY: all clean
.PHONY: FORCE


.c.o: .depend FORCE
		$(CC) -o $@ -c $(CFLAGS) $(CPPFLAGS) $<
.cpp.o: .depend FORCE
		$(CC) -o $@ -c $(CFLAGS) $(CPPFLAGS) $<

dreorder: .depend $(OBJS)
		$(CC) -o $@ $(OBJS) $(LIBS) $(LDFLAGS)

.depend: $(SOURCE)
		$(CC) -MM $(CFLAGS) $(CPPFLAGS) $(SOURCE) 1> .depend

$(PROGS): .depend

clean:
		@$(RM) .depend $(OBJS) $(PROGS)

ifneq ($(wildcard .depend),)
include .depend
endif
