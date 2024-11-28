CC = clang
OPTFLAGS = -O3 -ffast-math -march=native -mtune=native
CFLAGS = -std=c23 $(OPTFLAGS) -g $(shell sdl2-config --cflags) -Iext
LDFLAGS = $(shell sdl2-config --static-libs)
SRC = $(wildcard src/**/*.c) $(wildcard src/*.c) $(wildcard src/**/**/*.c) $(wildcard src/**/**/**/*.c) ext/ffmpeg_linux.c
OBJ = $(SRC:.c=.o)
BINDIR = bin
BIN = drift

.PHONY: all

all: dirs make

run: $(BINDIR)/$(BIN) 
	@$(BINDIR)/$(BIN)

$(BINDIR): dirs

dirs:
	@mkdir -p ./$(BINDIR)
	@echo "made dir $(BINDIR)"

make: $(OBJ) dirs
	@$(CC) $(OBJ) $(LDFLAGS) -o $(BINDIR)/$(BIN)
	@echo "made exe $(LIB_BIN)"

%.o: %.c
	@echo "compiled $<"
	@$(CC) -o $@ -c $< $(CFLAGS)
clean:
	@rm -rf $(OBJ) $(BINDIR)
	@echo "cleaned"
