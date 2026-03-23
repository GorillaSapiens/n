LEX = flex
YACC = bison
CC = g++
CFLAGS = -Wall -MMD -MP
CFLAGS += -g # for debugging
#CFLAGS += -DYYDEBUG # for yacc debugging
#CFLAGS += -O2 # for obtimization

TARGET = ns

SRCSRC = \
   mos6502/mos6502.cpp \
   main.cpp

SRC = $(SRCSRC) $(GENSRC)
OBJ = $(SRC:.cpp=.o)
DEP = $(OBJ:.o=.d)

all:
	@make --no-print-directory $(TARGET)

$(TARGET): $(GENHDR) $(GENSRC) $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.cpp
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)

-include $(DEP)

