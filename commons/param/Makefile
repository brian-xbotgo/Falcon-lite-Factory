SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))
TARGET = param


.PHONY: all factory
all:
#	$(CC) $(CFLAGS) -I$(INC) -c $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -I. -c $(SRCS)
	cp -f $(OBJS) ../$(OUTPUT_PATH)/

factory:
#	$(CC) $(CFLAGS) -I$(INC) -c $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -I. -DFACTORY_MODE -c $(SRCS)
	cp -f $(OBJS) ../$(OUTPUT_PATH)/

clean:
	 rm -f $(OBJS) $(TARGET)