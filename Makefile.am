AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = glog-0.3.3

bin_PROGRAMS = edge test

noinst_PROGRAMS = lemon

EXTRA_DIST = \
src/vm/internal/cpp.y

COMMON_SOURCES = \
src/advanced_mode.cc \
src/buffer.cc \
src/char_buffer.cc \
src/command_mode.cc \
src/direction.cc \
src/editable_string.cc \
src/editor.cc \
src/file_link_mode.cc \
src/find_mode.cc \
src/help_command.cc \
src/insert_mode.cc \
src/lazy_string.cc \
src/lazy_string_append.cc \
src/line.cc \
src/line_column.cc \
src/line_marks.cc \
src/line_prompt_mode.cc \
src/map_mode.cc \
src/memory_mapped_file.cc \
src/modifiers.cc \
src/modifiers.h \
src/navigate_command.cc \
src/noop_command.cc \
src/predictor.cc \
src/record_command.cc \
src/repeat_mode.cc \
src/run_command_handler.cc \
src/search_handler.cc \
src/server.cc \
src/structure.cc \
src/substring.cc \
src/terminal.cc \
src/transformation.cc \
src/transformation_delete.cc \
src/transformation_move.cc \
src/vm/internal/vm.cc \
src/vm/internal/value.cc \
src/vm/internal/string.cc \
src/vm/internal/types.cc \
src/vm/internal/binary_operator.cc \
src/vm/internal/if_expression.cc \
src/vm/internal/return_expression.cc \
src/vm/internal/while_expression.cc \
src/vm/internal/constant_expression.cc \
src/vm/internal/function_call.cc \
src/vm/internal/environment.cc \
src/vm/internal/assign_expression.cc \
src/vm/internal/append_expression.cc \
src/vm/internal/negate_expression.cc \
src/vm/internal/logical_expression.cc \
src/vm/internal/variable_lookup.cc \
src/vm/internal/evaluation.cc \
src/vm/internal/wstring.cc \
src/wstring.cc

CLEANFILES = src/vm/internal/cpp.cc src/vm/internal/cpp.h

BUILT_SOURCES = src/vm/internal/cpp.cc src/vm/internal/cpp.h

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
dist_doc_DATA = README

edge_SOURCES = $(COMMON_SOURCES) src/main.cc

test_SOURCES = $(COMMON_SOURCES) src/test.cc

lemon_SOURCES = src/vm/internal/lemon.c

src/vm/internal/cpp.h: src/vm/internal/cpp.y lemon
	./lemon -Tsrc/vm/internal/lempar.c src/vm/internal/cpp.y

src/vm/internal/cpp.cc: src/vm/internal/cpp.h

GLOG=glog-0.3.3/libglog_la-demangle.o \
glog-0.3.3/libglog_la-logging.o \
glog-0.3.3/libglog_la-symbolize.o \
glog-0.3.3/libglog_la-raw_logging.o \
glog-0.3.3/libglog_la-utilities.o \
glog-0.3.3/libglog_la-vlog_is_on.o

AM_CPPFLAGS=-std=c++0x -g -Wall -Wextra -Iglog-0.3.3/src -DDOCDIR='"$(docdir)"'
AM_LDFLAGS=-std=c++0x -g
edge_LDADD=glog-0.3.3/libglog.la
test_LDADD=glog-0.3.3/libglog.la
