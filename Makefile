CC := gcc
CFLAGS := -Wall -Wextra -O2
LDFLAGS := -pthread

TARGET := simplescheduler
SHELL := simpleshell
WORKLOADS := dummy_a dummy_b

.PHONY: all clean

all: $(TARGET) $(SHELL) $(WORKLOADS)

$(TARGET): simplescheduler.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(SHELL): simpleshell.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

dummy_a: dummy_a.c dummy_main.h
	$(CC) $(CFLAGS) -o $@ dummy_a.c

dummy_b: dummy_b.c dummy_main.h
	$(CC) $(CFLAGS) -o $@ dummy_b.c

clean:
	rm -f $(TARGET) $(SHELL) $(WORKLOADS) *.o
