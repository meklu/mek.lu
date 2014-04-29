DEBUG := 0
DEBUG_UTF8 := 0

CC := gcc
CFLAGS := -Wall -Wextra -Wshadow -Wstrict-prototypes -pedantic -Os
LDFLAGS := -pthread

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

LOGHDEP := main server worker log net request
SRVHDEP := main server
WRKHDEP := server
NETHDEP := main server worker net
REQHDEP := worker request

LOGHDEP := $(addprefix src/, $(addsuffix .o, $(LOGHDEP)))
SRVHDEP := $(addprefix src/, $(addsuffix .o, $(SRVHDEP)))
WRKHDEP := $(addprefix src/, $(addsuffix .o, $(WRKHDEP)))
NETHDEP := $(addprefix src/, $(addsuffix .o, $(NETHDEP)))
REQHDEP := $(addprefix src/, $(addsuffix .o, $(REQHDEP)))

.PHONY : all fall clean

all : $(TARGETS)

fall : clean $(TARGETS)

clean :
	$(RM) $(OBJ)
	$(RM) $(TARGETS)

$(LOGHDEP) : src/log.h
$(SRVHDEP) : src/server.h
$(WRKHDEP) : src/worker.h
$(NETHDEP) : src/net.h
$(REQHDEP) : src/request.h

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

mekdotlu : $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: src/test.c src/request.o src/log.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
