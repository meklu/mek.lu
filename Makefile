DEBUG := 0
DEBUG_UTF8 := 0

CC := gcc
CFLAGS := $(CFLAGS) -Wall -Wextra -Wshadow -Wstrict-prototypes -pedantic -Os

ifeq ($(DEBUG_UTF8), 1)
	DEBUG := 1
	CFLAGS := $(CFLAGS) -DDEBUG_UTF8
endif

ifeq ($(DEBUG), 1)
	CFLAGS := $(CFLAGS) -g
endif

ifeq ($(shell uname -s), Linux)
	LDFLAGS := $(LDFLAGS) -lcap
endif

SRC := \
	main.c \
	server.c \
	worker.c \
	log.c \
	net.c \
	request.c

TARGETS := mekdotlu

SRC := $(addprefix src/, $(SRC))
OBJ := $(SRC:%.c=%.o)

.PHONY : all fall clean

all : $(TARGETS)

fall : clean $(TARGETS)

clean :
	$(RM) $(OBJ)
	$(RM) $(TARGETS)

%.o : %.c
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@cp -f $*.d $*.d.tmp
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

mekdotlu : $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: src/test.c src/request.o src/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

-include $(OBJ:.o=.d)
