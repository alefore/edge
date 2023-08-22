AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = glog-0.4.0

bin_PROGRAMS = edge benchmark test fuzz_test test_fuzz_parser test_fuzz_compiler test_fuzz_classes test_fuzz_vm

noinst_PROGRAMS = lemon

EXTRA_DIST = \
src/vm/internal/cpp.y

COMMON_SOURCES = \
src/audio.cc \
src/audio.h \
src/args.cc \
src/args.h \
src/naive_bayes.cc \
src/naive_bayes.h \
src/buffer.cc \
src/buffer.h \
src/buffer_contents.cc \
src/buffer_contents.h \
src/buffer_contents_util.cc \
src/buffer_contents_util.h \
src/buffer_contents_view_layout.h \
src/buffer_contents_view_layout.cc \
src/buffer_display_data.cc \
src/buffer_display_data.h \
src/buffer_flags.cc \
src/buffer_flags.h \
src/buffer_name.cc \
src/buffer_name.h \
src/buffer_metadata_output_producer.cc \
src/buffer_metadata_output_producer.h \
src/buffer_output_producer.cc \
src/buffer_output_producer.h \
src/buffer_tests.cc \
src/widget_list.cc \
src/widget_list.h \
src/buffer_syntax_parser.cc \
src/buffer_syntax_parser.h \
src/buffer_variables.cc \
src/buffer_variables.h \
src/buffer_vm.cc \
src/buffer_vm.h \
src/buffer_widget.cc \
src/buffer_widget.h \
src/buffers_list.cc \
src/buffers_list.h \
src/columns_vector.cc \
src/columns_vector.h \
src/command_argument_mode.h \
src/cpp_command.cc \
src/cpp_parse_tree.cc \
src/cpp_parse_tree.h \
src/command.cc \
src/command.h \
src/command_mode.cc \
src/command_mode.h \
src/command_with_modifiers.cc \
src/command_with_modifiers.h \
src/concurrent/bag.cc \
src/concurrent/bag.h \
src/concurrent/operation.cc \
src/concurrent/operation.h \
src/concurrent/protected.cc \
src/concurrent/protected.h \
src/concurrent/thread_pool.cc \
src/concurrent/thread_pool.h \
src/concurrent/work_queue.cc \
src/concurrent/work_queue.h \
src/cursors.cc \
src/cursors.h \
src/direction.cc \
src/directory_listing.h \
src/directory_listing.cc \
src/editor.cc \
src/editor.h \
src/editor_vm.cc \
src/editor_vm.h \
src/editor_variables.cc \
src/editor_variables.h \
src/frame_output_producer.cc \
src/frame_output_producer.h \
src/file_link_mode.cc \
src/file_descriptor_reader.cc \
src/file_descriptor_reader.h \
src/find_mode.cc \
src/futures/futures.cc \
src/futures/futures.h \
src/futures/listenable_value.h \
src/futures/delete_notification.cc \
src/futures/delete_notification.h \
src/futures/serializer.cc \
src/futures/serializer.h \
src/goto_command.cc \
src/help_command.cc \
src/horizontal_center_output_producer.cc \
src/horizontal_center_output_producer.h \
src/infrastructure/command_line.h \
src/infrastructure/command_line.cc \
src/infrastructure/dirname.cc \
src/infrastructure/dirname.h \
src/infrastructure/file_system_driver.cc \
src/infrastructure/file_system_driver.h \
src/infrastructure/time.cc \
src/infrastructure/time.h \
src/infrastructure/screen/line_modifier.cc \
src/infrastructure/screen/line_modifier.h \
src/infrastructure/tracker.cc \
src/infrastructure/tracker.h \
src/insert_history.cc \
src/insert_history.h \
src/insert_history_buffer.cc \
src/insert_history_buffer.h \
src/insert_mode.cc \
src/language/const_tree.cc \
src/language/const_tree.h \
src/language/function_traits.h \
src/language/ghost_type.cc \
src/language/ghost_type.h \
src/language/ghost_type_test.cc \
src/language/gc.cc \
src/language/gc.h \
src/language/hash.cc \
src/language/hash.h \
src/language/lazy_string/append.cc \
src/language/lazy_string/append.h \
src/language/lazy_string/char_buffer.cc \
src/language/lazy_string/char_buffer.h \
src/language/lazy_string/functional.cc \
src/language/lazy_string/functional.h \
src/language/lazy_string/lazy_string.cc \
src/language/lazy_string/lazy_string.h \
src/language/lazy_string/lowercase.cc \
src/language/lazy_string/lowercase.h \
src/language/lazy_string/padding.cc \
src/language/lazy_string/padding.h \
src/language/lazy_string/substring.cc \
src/language/lazy_string/substring.h \
src/language/lazy_string/trim.cc \
src/language/lazy_string/trim.h \
src/language/observers.h \
src/language/observers.cc \
src/language/observers_gc.h \
src/language/safe_types.h \
src/language/value_or_error.cc \
src/language/value_or_error.h \
src/language/wstring.cc \
src/language/wstring.h \
src/line.cc \
src/line.h \
src/line_output.cc \
src/line_output.h \
src/line_with_cursor.cc \
src/line_with_cursor.h \
src/line_column.cc \
src/line_column.h \
src/line_column_vm.cc \
src/line_column_vm.h \
src/line_marks.cc \
src/line_marks.h \
src/line_marks_test.cc \
src/line_number_output_producer.h \
src/line_number_output_producer.cc \
src/line_prompt_mode.cc \
src/log.cc \
src/log.h \
src/lru_cache.h \
src/map_mode.cc \
src/math/decaying_counter.cc \
src/math/decaying_counter.h \
src/modifiers.cc \
src/modifiers.h \
src/navigation_buffer.cc \
src/navigation_buffer.h \
src/navigate_command.cc \
src/open_directory_command.cc \
src/open_file_command.cc \
src/operation.cc \
src/operation.h \
src/operation_scope.cc \
src/operation_scope.h \
src/operation_scope_buffer_information.h \
src/parse_tools.cc \
src/parse_tools.h \
src/parse_tree.cc \
src/parse_tree.h \
src/parsers/diff.cc \
src/parsers/diff.h \
src/parsers/markdown.cc \
src/parsers/markdown.h \
src/paste.cc \
src/paste.h \
src/path_flags.cc \
src/path_flags.h \
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
src/screen_vm.cc \
src/screen_vm.h \
src/search_command.cc \
src/search_handler.cc \
src/section_brackets_producer.cc \
src/section_brackets_producer.h \
src/seek.cc \
src/seek.h \
src/server.cc \
src/set_buffer_mode.cc \
src/set_buffer_mode.h \
src/set_mode_command.cc \
src/set_mode_command.h \
src/set_variable_command.cc \
src/set_variable_command.h \
src/shapes.cc \
src/shapes.h \
src/status.cc \
src/status.h \
src/status_tests.cc \
src/status_output_producer.cc \
src/status_output_producer.h \
src/structure.cc \
src/structure.h \
src/structure_move.cc \
src/structure_move.h \
src/terminal.cc \
src/terminal.h \
src/terminal_input_parser.cc \
src/terminal_input_parser.h \
src/tests/benchmarks.cc \
src/tests/benchmarks.h \
src/tests/fuzz.h \
src/tests/fuzz.cc \
src/tests/fuzz_testable.h \
src/tests/fuzz_testable.cc \
src/tests/tests.cc \
src/tests/tests.h \
src/tokenize.cc \
src/tokenize.h \
src/tokenize_test.cc \
src/transformation/bisect.cc \
src/transformation/bisect.h \
src/transformation/composite.cc \
src/transformation/composite.h \
src/transformation/cursors.cc \
src/transformation/cursors.h \
src/transformation/delete.cc \
src/transformation/delete.h \
src/transformation/expand.cc \
src/transformation/expand.h \
src/transformation/insert.cc \
src/transformation/insert.h \
src/transformation/move.cc \
src/transformation/move.h \
src/transformation/noop.cc \
src/transformation/noop.h \
src/transformation/reach_query.cc \
src/transformation/reach_query.h \
src/transformation/repetitions.cc \
src/transformation/repetitions.h \
src/transformation/set_position.cc \
src/transformation/set_position.h \
src/transformation/stack.cc \
src/transformation/stack.h \
src/transformation/switch_case.cc \
src/transformation/switch_case.h \
src/transformation/tree_navigate.cc \
src/transformation/tree_navigate.h \
src/transformation/type.cc \
src/transformation/type.h \
src/transformation/visual_overlay.cc \
src/transformation/visual_overlay.h \
src/transformation/vm.cc \
src/transformation/vm.h \
src/transformation.cc \
src/transformation.h \
src/undo_state.cc \
src/undo_state.h \
src/url.cc \
src/url.h \
src/variables.h \
src/visual_overlay.h \
src/vm/public/container.h \
src/vm/public/escape.h \
src/vm/public/optional.h \
src/vm/internal/callbacks.cc \
src/vm/internal/escape.cc \
src/vm/internal/lambda.cc \
src/vm/internal/lambda.h \
src/vm/internal/numbers.cc \
src/vm/internal/numbers.h \
src/vm/internal/vm.cc \
src/vm/internal/value.cc \
src/vm/internal/string.cc \
src/vm/internal/time.cc \
src/vm/internal/time.h \
src/vm/internal/types.cc \
src/vm/internal/binary_operator.cc \
src/vm/internal/class_expression.cc \
src/vm/internal/class_expression.h \
src/vm/internal/compilation.h \
src/vm/internal/compilation.cc \
src/vm/internal/filter_similar_names.cc \
src/vm/internal/filter_similar_names.h \
src/vm/internal/if_expression.cc \
src/vm/internal/return_expression.cc \
src/vm/internal/types_promotion.cc \
src/vm/internal/types_promotion.h \
src/vm/internal/while_expression.cc \
src/vm/internal/constant_expression.cc \
src/vm/internal/function_call.cc \
src/vm/internal/environment.cc \
src/vm/internal/assign_expression.cc \
src/vm/internal/append_expression.cc \
src/vm/internal/namespace_expression.cc \
src/vm/internal/namespace_expression.h \
src/vm/internal/negate_expression.cc \
src/vm/internal/logical_expression.cc \
src/vm/internal/variable_lookup.cc \
src/widget.h

BENCHMARK_SOURCES = \
src/language/const_tree.cc \
src/language/const_tree.h \
src/infrastructure/time.cc \
src/infrastructure/time.h \
src/language/wstring.cc \
src/language/wstring.h \
src/tests/benchmarks.cc \
src/tests/benchmarks.h \
src/tests/tests.cc \
src/tests/tests.h

CLEANFILES = src/vm/internal/cpp.cc src/vm/internal/cpp.h

BUILT_SOURCES = src/vm/internal/cpp.cc src/vm/internal/cpp.h

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
dist_doc_DATA = README.md

edge_SOURCES = $(COMMON_SOURCES) src/main.cc

benchmark_SOURCES = $(BENCHMARK_SOURCES) src/tests/benchmarks_main.cc

test_SOURCES = \
$(COMMON_SOURCES) \
src/test/buffer_contents_test.cc \
src/test/buffer_contents_test.h \
src/test/line_test.cc \
src/test/line_test.h \
src/test.cc

fuzz_test_SOURCES = $(COMMON_SOURCES) src/fuzz_test.cc

test_fuzz_parser_SOURCES = $(COMMON_SOURCES) src/test/fuzz/parser.cc

test_fuzz_compiler_SOURCES = $(COMMON_SOURCES) src/test/fuzz/compiler.cc

test_fuzz_classes_SOURCES = $(COMMON_SOURCES) src/test/fuzz/classes.cc

test_fuzz_vm_SOURCES = $(COMMON_SOURCES) src/vm/internal/fuzz.cc

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

AM_CPPFLAGS=-std=c++20 -g -Wall -Wextra -Iglog-0.4.0/src -DDOCDIR='"$(docdir)"' -Werror=return-type -Werror=switch -Werror=unused-function -Werror=missing-field-initializers -Werror=unused-but-set-variable -Werror=range-loop-construct -Wshadow
AM_LDFLAGS=-std=c++20 -g
edge_LDADD=glog-0.4.0/libglog.la
benchmark_LDADD=glog-0.4.0/libglog.la
test_LDADD=glog-0.4.0/libglog.la
fuzz_test_LDADD=glog-0.4.0/libglog.la
test_fuzz_parser_LDADD=glog-0.4.0/libglog.la
test_fuzz_compiler_LDADD=glog-0.4.0/libglog.la
test_fuzz_classes_LDADD=glog-0.4.0/libglog.la
test_fuzz_vm_LDADD=glog-0.4.0/libglog.la
