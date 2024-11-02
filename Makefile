CC = clang
CFLAGS = -std=c23 -O3 -march=native -mtune=native -g $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --static-libs)
SRC = $(wildcard src/**/*.c) $(wildcard src/*.c) $(wildcard src/**/**/*.c) $(wildcard src/**/**/**/*.c)
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
