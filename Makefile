CPPFLAGS=-std=c++0x -g -Wall
LDFLAGS=-std=c++0x -g
LDLIBS=-lncurses

all: edge

token.o: token.cc token.h Makefile
line_parser.o: line_parser.cc line_parser.h token.h Makefile
main.o: main.cc file_link_mode.h line_parser.h token.h terminal.h Makefile
terminal.o: terminal.cc terminal.h Makefile

memory_mapped_file.o: memory_mapped_file.cc memory_mapped_file.h buffer.h lazy_string.h Makefile
char_buffer.o: char_buffer.cc char_buffer.h buffer.h lazy_string.h
substring.o: substring.cc substring.h lazy_string.h Makefile

buffer.o: buffer.cc buffer.h editor.h Makefile
editor.o: editor.cc editor.h substring.h memory_mapped_file.h lazy_string.h Makefile

advanced_mode.o: advanced_mode.h advanced_mode.cc buffer.h char_buffer.h command_mode.h editor_mode.h editor.h file_link_mode.h help_command.h line_prompt_mode.h map_mode.h Makefile
command_mode.o: command_mode.cc command_mode.h advanced_mode.h command.h editor_mode.h editor.h find_mode.h help_command.h insert_mode.h lazy_string_append.h map_mode.h noop_command.o repeat_mode.h substring.h terminal.h Makefile
file_link_mode.o: file_link_mode.cc file_link_mode.h buffer.h char_buffer.h editor.h editor_mode.h Makefile
find_mode.o: editor_mode.h editor.h command_mode.h find_mode.h find_mode.cc Makefile
insert_mode.o: insert_mode.cc insert_mode.h command_mode.h editor.h lazy_string_append.h substring.h terminal.h Makefile
line_prompt_mode.o: line_prompt_mode.cc line_prompt_mode.h command.h command_mode.h editor.h terminal.h Makefile

map_mode.o: editor_mode.h map_mode.h map_mode.cc
repeat_mode.o: repeat_mode.cc repeat_mode.h editor_mode.h editor.h command_mode.h Makefile

help_command.o: help_command.cc help_command.h buffer.h char_buffer.h editor.h command.h Makefile
noop_command.o: noop_command.cc noop_command.h char_buffer.h editor.h command.h Makefile

run_command_handler.o: run_command_handler.cc run_command_handler.h buffer.h char_buffer.h command_mode.h editor.h Makefile
search_handler.o: search_handler.cc search_handler.h Makefile

lazy_string.o: lazy_string.cc lazy_string.h Makefile
lazy_string_append.o: lazy_string_append.cc lazy_string_append.h lazy_string.h Makefile

OBJS=token.o line_parser.o advanced_mode.o buffer.o char_buffer.o command_mode.o editor.o file_link_mode.o find_mode.o help_command.o insert_mode.o lazy_string.o lazy_string_append.o line_prompt_mode.o main.o map_mode.o memory_mapped_file.o noop_command.o repeat_mode.o run_command_handler.o search_handler.o substring.o terminal.o
edge: $(OBJS)
	$(CXX) $(LDFLAGS) -o edge $(OBJS) $(LDLIBS)
