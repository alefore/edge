CPPFLAGS=-std=c++0x -g
LDFLAGS=-std=c++0x -g
LDLIBS=-lncurses

all: editor

token.o: token.cc token.h Makefile
line_parser.o: line_parser.cc line_parser.h token.h Makefile
main.o: main.cc line_parser.h token.h terminal.h Makefile
terminal.o: terminal.cc terminal.h Makefile
memory_mapped_file.o: memory_mapped_file.cc memory_mapped_file.h lazy_string.h Makefile
substring.o: substring.cc substring.h lazy_string.h Makefile
editor.o: substring.h memory_mapped_file.h lazy_string.h editor.cc editor.h Makefile
command_mode.o: editor_mode.h editor.h command_mode.h command_mode.cc Makefile
find_mode.o: editor_mode.h editor.h find_mode.h find_mode.cc Makefile

OBJS=token.o line_parser.o command_mode.o editor.o find_mode.o main.o memory_mapped_file.o substring.o terminal.o
editor: $(OBJS)
	g++ $(LDFLAGS) -o editor $(OBJS) $(LDLIBS)
