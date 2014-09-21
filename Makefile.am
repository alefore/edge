SUBDIRS = glog-0.3.3

bin_PROGRAMS = edge test

COMMON_SOURCES = \
advanced_mode.cc \
buffer.cc \
char_buffer.cc \
command_mode.cc \
direction.cc \
editable_string.cc \
editor.cc \
file_link_mode.cc \
find_mode.cc \
help_command.cc \
insert_mode.cc \
lazy_string.cc \
lazy_string_append.cc \
line.cc \
line_prompt_mode.cc \
map_mode.cc \
memory_mapped_file.cc \
noop_command.cc \
predictor.cc \
repeat_mode.cc \
run_command_handler.cc \
search_handler.cc \
secondary_mode.cc \
server.cc \
substring.cc \
terminal.cc \
transformation.cc \
vm/internal/vm.cc \
vm/internal/value.cc \
vm/internal/string.cc \
vm/internal/types.cc \
vm/internal/binary_operator.cc \
vm/internal/if_expression.cc \
vm/internal/return_expression.cc \
vm/internal/while_expression.cc \
vm/internal/constant_expression.cc \
vm/internal/function_call.cc \
vm/internal/environment.cc \
vm/internal/assign_expression.cc \
vm/internal/append_expression.cc \
vm/internal/negate_expression.cc \
vm/internal/logical_expression.cc \
vm/internal/variable_lookup.cc \
vm/internal/evaluation.cc

edge_SOURCES=$(COMMON_SOURCES) main.cc
test_SOURCES=$(COMMON_SOURCES) test.cc

GLOG=glog-0.3.3/libglog_la-demangle.o \
glog-0.3.3/libglog_la-logging.o \
glog-0.3.3/libglog_la-symbolize.o \
glog-0.3.3/libglog_la-raw_logging.o \
glog-0.3.3/libglog_la-utilities.o \
glog-0.3.3/libglog_la-vlog_is_on.o

AM_CPPFLAGS=-std=c++0x -g -Wall -Wextra -Iglog-0.3.3/src
AM_LDFLAGS=-std=c++0x -g
edge_LDADD=glog-0.3.3/libglog.la
test_LDADD=glog-0.3.3/libglog.la
