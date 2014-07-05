DEBUG := 0
DEBUG_UTF8 := 0
USE_CAPABILITIES := 1

CFLAGS := $(CFLAGS) -Wall -Wextra -Wshadow -Wstrict-prototypes -pedantic -Os
LDFLAGS := -lrt

KERNEL := $(shell uname -s)

ifeq ($(origin CC), default)
	ifeq ($(KERNEL), Darwin)
		CC := clang
	else
		CC := gcc
	endif
endif

ifeq ($(DEBUG_UTF8), 1)
	DEBUG := 1
	CFLAGS := $(CFLAGS) -DDEBUG_UTF8
endif

ifeq ($(DEBUG), 1)
	CFLAGS := $(CFLAGS) -g
endif

ifeq ($(KERNEL), Linux)
	ifeq ($(USE_CAPABILITIES), 1)
		LDFLAGS := $(LDFLAGS) -lcap
		CFLAGS := $(CFLAGS) -DUSE_CAPABILITIES
	endif
endif

SRC := \
	main.c \
	server.c \
	worker.c \
	log.c \
	net.c \
	request.c

ifeq ($(KERNEL), Darwin)
	SRC := $(SRC) clock.c
endif

TARGETS := mekdotlu

SRC := $(addprefix src/, $(SRC))
OBJ := $(SRC:%.c=%.o)
DEP := $(OBJ:%.o=%.d)

.PHONY : all fall clean

all : $(TARGETS)

fall : clean $(TARGETS)

clean :
	$(RM) $(OBJ)
	$(RM) $(DEP)
	$(RM) $(TARGETS)

%.o : %.c
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@cp -f $*.d $*.d.tmp
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@$(RM) $*.d.tmp

mekdotlu : $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: src/test.c src/request.o src/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

-include $(DEP)
