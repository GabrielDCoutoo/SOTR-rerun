LIBS = -lSDL2

SDL2_CONFIG = sdl2-config
CFLAGS = $(shell $(SDL2_CONFIG) --cflags) -D_REENTRANT
LDFLAGS = $(shell $(SDL2_CONFIG) --libs) -lm -lrt -lpthread
CFLAGS += -g

# Sources and target
TARGET = rtsounds
OBJECTS = rtsounds.o fft/fft.o

# Compiler
CC = gcc

# Default target
all: $(TARGET)

# Linking the target
$(TARGET): $(OBJECTS)
	$(CC) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJECTS) $(TARGET)

# Run target
run: $(TARGET)
	clear
	./$(TARGET)
