
TARGET = mb

CFLAGS =
LFLAGS = -lSDL2 -lOpenCL

SRC = src
OBJ = obj
BIN = bin

SRCS = $(wildcard $(SRC)/*.c)
OBJS = $(SRCS:$(SRC)/%.c=$(OBJ)/%.o)

$(BIN)/$(TARGET): $(OBJS)
	@ mkdir -p $(@D)
	$(CC) $(OBJS) $(LFLAGS) -o $@

$(OBJS): $(OBJ)/%.o : $(SRC)/%.c
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BIN) $(OBJ)
