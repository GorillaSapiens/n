LEX = flex
YACC = bison
CC = g++
CFLAGS = -Wall -Wextra -pedantic -MMD -MP

# for debugging
CFLAGS += -g

# enable illegal opcodes
CFLAGS += -DILLEGAL_OPCODES

# for optimization
#CFLAGS += -O2

TARGET = n65sim

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755
INSTALL_DATA ?= $(INSTALL) -m 0644

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

install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)

-include $(DEP)

