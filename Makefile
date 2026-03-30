CC = clang
CFLAGS = -Wall -Wextra -O2
LDFLAGS = 

TARGET = zenith_os
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
