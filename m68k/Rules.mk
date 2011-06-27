AR := ar
CC := gcc
CFLAGS = -O2
CFLAGS = -g
CFLAGS += -Werror -Wall -I$(ROOT)
CFLAGS += -MMD -MF .$(@F).d

DEPS = .*.d

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEPS)
