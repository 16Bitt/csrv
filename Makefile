CFLAGS 	= -g -I. -DUSE_BSD_API
CC 			= gcc
TARGET	= csrv
SOURCES = $(patsubst %.c,%.o,$(wildcard *.c))

all: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

run: all
	./$(TARGET)

clean:
	-rm $(SOURCES)
	-rm $(TARGET)
