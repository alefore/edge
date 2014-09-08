CPPFLAGS=-std=c++0x -g -Wall
LDFLAGS=-std=c++0x -g
LDLIBS=-lncurses

all: edge test

main.o: main.cc editor.h file_link_mode.h server.h terminal.h Makefile
terminal.o: terminal.cc terminal.h editor.h Makefile
server.o: server.cc server.h vm/public/vm.h Makefile

vm/internal/vm.o: vm/internal/vm.cc vm/public/vm.h vm/internal/cpp.c vm/internal/cpp.h Makefile
vm/internal/value.o: vm/internal/value.cc vm/public/value.h Makefile
vm/internal/string.o: vm/internal/string.cc vm/internal/string.h Makefile
vm/internal/types.o: vm/internal/types.cc vm/public/types.h Makefile
vm/internal/binary_operator.o: vm/internal/binary_operator.cc vm/internal/binary_operator.h Makefile
vm/internal/if_expression.o: vm/internal/if_expression.cc vm/internal/if_expression.h Makefile
vm/internal/return_expression.o: vm/internal/return_expression.cc vm/internal/return_expression.h Makefile
vm/internal/while_expression.o: vm/internal/while_expression.cc vm/internal/while_expression.h Makefile
vm/internal/constant_expression.o: vm/internal/constant_expression.cc vm/internal/constant_expression.h Makefile
vm/internal/function_call.o: vm/internal/function_call.cc vm/internal/function_call.h Makefile
vm/internal/environment.o: vm/internal/environment.cc vm/public/environment.h Makefile
vm/internal/append_expression.o: vm/internal/append_expression.cc vm/internal/append_expression.h Makefile
vm/internal/assign_expression.o: vm/internal/assign_expression.cc vm/internal/assign_expression.h Makefile
vm/internal/negate_expression.o: vm/internal/negate_expression.cc vm/internal/negate_expression.h Makefile
vm/internal/logical_expression.o: vm/internal/logical_expression.cc vm/internal/logical_expression.h Makefile
vm/internal/variable_lookup.o: vm/internal/variable_lookup.cc vm/internal/variable_lookup.h Makefile
vm/internal/evaluation.o: vm/internal/evaluation.cc vm/internal/evaluation.h Makefile

memory_mapped_file.o: memory_mapped_file.cc memory_mapped_file.h buffer.h lazy_string.h Makefile
char_buffer.o: char_buffer.cc char_buffer.h buffer.h lazy_string.h
substring.o: substring.cc substring.h lazy_string.h Makefile

buffer.o: buffer.cc buffer.h editor.h file_link_mode.h lazy_string_append.h run_command_handler.h substring.h variables.h Makefile
editor.o: editor.cc editor.h char_buffer.h substring.h memory_mapped_file.h lazy_string.h Makefile
direction.o: direction.cc direction.h Makefile
transformation.o: transformation.cc transformation.h buffer.h editor.h Makefile

advanced_mode.o: advanced_mode.h advanced_mode.cc buffer.h char_buffer.h command_mode.h editor_mode.h editor.h file_link_mode.h help_command.h line_prompt_mode.h map_mode.h Makefile
command_mode.o: command_mode.cc command_mode.h advanced_mode.h command.h editor_mode.h editor.h find_mode.h help_command.h insert_mode.h lazy_string_append.h map_mode.h noop_command.o repeat_mode.h substring.h terminal.h Makefile
file_link_mode.o: file_link_mode.cc file_link_mode.h buffer.h char_buffer.h editor.h editor_mode.h Makefile
find_mode.o: editor_mode.h editor.h command_mode.h find_mode.h find_mode.cc Makefile
insert_mode.o: insert_mode.cc insert_mode.h command_mode.h editable_string.h editor.h lazy_string_append.h substring.h terminal.h Makefile
line_prompt_mode.o: line_prompt_mode.cc line_prompt_mode.h char_buffer.h command.h command_mode.h editable_string.h editor.h terminal.h Makefile
preditor.o: predictor.cc predictor.h buffer.h editor.h Makefile

map_mode.o: editor_mode.h map_mode.h map_mode.cc
repeat_mode.o: repeat_mode.cc repeat_mode.h editor_mode.h editor.h command_mode.h Makefile

help_command.o: help_command.cc help_command.h buffer.h char_buffer.h editor.h command.h Makefile
noop_command.o: noop_command.cc noop_command.h char_buffer.h editor.h command.h Makefile

run_command_handler.o: run_command_handler.cc run_command_handler.h buffer.h char_buffer.h command.h command_mode.h editor.h line_prompt_mode.h Makefile
search_handler.o: search_handler.cc search_handler.h editor.h substring.h Makefile

lazy_string.o: lazy_string.cc lazy_string.h Makefile
editable_string.o: editable_string.cc editable_string.h Makefile
lazy_string_append.o: lazy_string_append.cc lazy_string_append.h lazy_string.h Makefile

vm/internal/cpp.h: vm/internal/cpp.y vm/internal/lemon
	./vm/internal/lemon vm/internal/cpp.y

vm/internal/cpp.c: vm/internal/cpp.y vm/internal/lemon
	./vm/internal/lemon vm/internal/cpp.y

LIB=advanced_mode.o buffer.o char_buffer.o command_mode.o direction.o \
editable_string.o editor.o file_link_mode.o find_mode.o help_command.o \
insert_mode.o lazy_string.o lazy_string_append.o line_prompt_mode.o map_mode.o \
memory_mapped_file.o noop_command.o predictor.o repeat_mode.o \
run_command_handler.o search_handler.o server.o substring.o terminal.o \
transformation.o vm/internal/vm.o vm/internal/value.o vm/internal/string.o \
vm/internal/types.o vm/internal/binary_operator.o vm/internal/if_expression.o \
vm/internal/return_expression.o vm/internal/while_expression.o \
vm/internal/constant_expression.o vm/internal/function_call.o \
vm/internal/environment.o vm/internal/assign_expression.o \
vm/internal/append_expression.o vm/internal/negate_expression.o \
vm/internal/logical_expression.o vm/internal/variable_lookup.o \
vm/internal/evaluation.o

edge: main.o $(LIB)
	$(CXX) $(LDFLAGS) -o edge $(LIB) main.o $(LDLIBS)
test: test.o $(LIB)
	$(CXX) $(LDFLAGS) -o test $(LIB) test.o $(LDLIBS)
vm/internal/lemon: vm/internal/lemon.o Makefile
	$(CC) -o vm/internal/lemon vm/internal/lemon.o
