AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = glog-0.4.0

bin_PROGRAMS = edge test fuzz_test

noinst_PROGRAMS = lemon

EXTRA_DIST = \
src/vm/internal/cpp.y

COMMON_SOURCES = \
src/audio.cc \
src/audio.h \
src/args.cc \
src/args.h \
src/buffer.cc \
src/buffer.h \
src/buffer_contents.cc \
src/buffer_contents.h \
src/buffer_output_producer.cc \
src/buffer_output_producer.h \
src/buffer_tree.cc \
src/buffer_tree.h \
src/buffer_variables.cc \
src/buffer_variables.h \
src/cpp_command.cc \
src/cpp_parse_tree.cc \
src/cpp_parse_tree.h \
src/char_buffer.cc \
src/close_buffer_command.cc \
src/command_mode.cc \
src/command_mode.h \
src/command_with_modifiers.cc \
src/command_with_modifiers.h \
src/cursors.cc \
src/cursors.h \
src/cursors_highlighter.cc \
src/cursors_highlighter.h \
src/cursors_transformation.cc \
src/cursors_transformation.h \
src/delegating_output_receiver.cc \
src/delegating_output_receiver.h \
src/delegating_output_receiver_with_internal_modifiers.cc \
src/delegating_output_receiver_with_internal_modifiers.h \
src/delete_mode.cc \
src/delete_mode.h \
src/dirname.cc \
src/dirname.h \
src/direction.cc \
src/editor.cc \
src/framed_output_producer.cc \
src/framed_output_producer.h \
src/file_link_mode.cc \
src/find_mode.cc \
src/goto_command.cc \
src/help_command.cc \
src/insert_mode.cc \
src/lazy_string.cc \
src/lazy_string_append.cc \
src/lazy_string_trim.cc \
src/lazy_string_trim.h \
src/line.cc \
src/line.h \
src/line_modifier.cc \
src/line_modifier.h \
src/line_column.cc \
src/line_marks.cc \
src/line_prompt_mode.cc \
src/list_buffers_command.cc \
src/lowercase.cc \
src/map_mode.cc \
src/modifiers.cc \
src/modifiers.h \
src/navigation_buffer.cc \
src/navigation_buffer.h \
src/navigate_command.cc \
src/open_directory_command.cc \
src/open_file_command.cc \
src/output_producer.h \
src/output_receiver.h \
src/output_receiver_optimizer.cc \
src/output_receiver_optimizer.h \
src/parse_tools.cc \
src/parse_tools.h \
src/parse_tree.cc \
src/parse_tree.h \
src/parsers/diff.cc \
src/parsers/diff.h \
src/parsers/markdown.cc \
src/parsers/markdown.h \
src/predictor.cc \
src/quit_command.cc \
src/record_command.cc \
src/repeat_mode.cc \
src/run_command_handler.cc \
src/run_cpp_command.cc \
src/run_cpp_command.h \
src/run_cpp_file.cc \
src/run_cpp_file.h \
src/screen.h \
src/screen_curses.cc \
src/screen_curses.h \
src/screen_output_receiver.cc \
src/screen_output_receiver.h \
src/screen_vm.cc \
src/screen_vm.h \
src/search_command.cc \
src/search_handler.cc \
src/seek.cc \
src/seek.h \
src/send_end_of_file_command.cc \
src/server.cc \
src/set_variable_command.cc \
src/shapes.cc \
src/shapes.h \
src/structure.cc \
src/substring.cc \
src/terminal.cc \
src/transformation.cc \
src/transformation_delete.cc \
src/transformation_move.cc \
src/vertical_split_output_producer.cc \
src/vertical_split_output_producer.h \
src/vm_transformation.cc \
src/vm_transformation.h \
src/vm/public/set.h \
src/vm/public/vector.h \
src/vm/internal/callbacks.cc \
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
src/vm/internal/wstring.cc \
src/wstring.cc

CLEANFILES = src/vm/internal/cpp.cc src/vm/internal/cpp.h

BUILT_SOURCES = src/vm/internal/cpp.cc src/vm/internal/cpp.h

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
dist_doc_DATA = README.md

edge_SOURCES = $(COMMON_SOURCES) src/main.cc

test_SOURCES = \
$(COMMON_SOURCES) \
src/test/buffer_contents_test.cc \
src/test/buffer_contents_test.h \
src/test/line_test.cc \
src/test/line_test.h \
src/test.cc

fuzz_test_SOURCES = $(COMMON_SOURCES) src/fuzz_test.cc

lemon_SOURCES = src/vm/internal/lemon.c

src/vm/internal/cpp.h: src/vm/internal/cpp.y lemon
	./lemon -Tsrc/vm/internal/lempar.c src/vm/internal/cpp.y

src/vm/internal/cpp.cc: src/vm/internal/cpp.h

GLOG=glog-0.4.0/libglog_la-demangle.o \
glog-0.4.0/libglog_la-logging.o \
glog-0.4.0/libglog_la-symbolize.o \
glog-0.4.0/libglog_la-raw_logging.o \
glog-0.4.0/libglog_la-utilities.o \
glog-0.4.0/libglog_la-vlog_is_on.o

AM_CPPFLAGS=-std=c++17 -g -Wall -Wextra -Iglog-0.4.0/src -DDOCDIR='"$(docdir)"'
AM_LDFLAGS=-std=c++17 -g
edge_LDADD=glog-0.4.0/libglog.la
test_LDADD=glog-0.4.0/libglog.la
fuzz_test_LDADD=glog-0.4.0/libglog.la
