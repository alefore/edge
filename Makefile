CPPFLAGS=-std=c++0x -g
LDFLAGS=-std=c++0x -g
LDLIBS=-lncurses

all: editor

token.o: token.cc token.h Makefile
line_parser.o: line_parser.cc line_parser.h token.h Makefile
main.o: main.cc file_link_mode.h line_parser.h token.h terminal.h Makefile
terminal.o: terminal.cc terminal.h Makefile

memory_mapped_file.o: memory_mapped_file.cc memory_mapped_file.h lazy_string.h Makefile
char_buffer.o: char_buffer.h lazy_string.h
substring.o: substring.cc substring.h lazy_string.h Makefile

editor.o: substring.h memory_mapped_file.h lazy_string.h editor.cc editor.h Makefile

advanced_mode.o: advanced_mode.h advanced_mode.cc command_mode.h editor_mode.h editor.h help_command.h map_mode.h Makefile
command_mode.o: advanced_mode.h command.h command_mode.h editor_mode.h editor.h find_mode.h help_command.h map_mode.h Makefile

file_link_mode.o: char_buffer.h editor.h editor_mode.h file_link_mode.cc file_link_mode.h Makefile
find_mode.o: editor_mode.h editor.h command_mode.h find_mode.h find_mode.cc Makefile
map_mode.o: editor_mode.h map_mode.h map_mode.cc

help_command.o: help_command.cc help_command.h char_buffer.h editor.h command.h Makefile

OBJS=token.o line_parser.o advanced_mode.o char_buffer.o command_mode.o editor.o file_link_mode.o find_mode.o help_command.o main.o map_mode.o memory_mapped_file.o substring.o terminal.o
editor: $(OBJS)
	g++ $(LDFLAGS) -o editor $(OBJS) $(LDLIBS)
