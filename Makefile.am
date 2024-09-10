AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = glog-0.4.0

bin_PROGRAMS = edge benchmark test fuzz_test test_fuzz_parser test_fuzz_compiler test_fuzz_classes test_fuzz_vm

noinst_PROGRAMS = lemon

EXTRA_DIST = \
src/vm/cpp.y

COMMON_SOURCES = \
src/args.cc \
src/args.h \
src/args_tests.cc \
src/buffer.cc \
src/buffer.h \
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
src/buffer_state.cc \
src/buffer_state.h \
src/buffer_tests.cc \
src/widget_list.cc \
src/widget_list.h \
src/buffer_registry.cc \
src/buffer_registry.h \
src/buffer_subtypes.cc \
src/buffer_subtypes.h \
src/buffer_syntax_parser.cc \
src/buffer_syntax_parser.h \
src/buffer_transformation_adapter.cc \
src/buffer_transformation_adapter.h \
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
src/completion_model.cc \
src/completion_model.h \
src/concurrent/bag.cc \
src/concurrent/bag.h \
src/concurrent/operation.cc \
src/concurrent/operation.h \
src/concurrent/protected.cc \
src/concurrent/protected.h \
src/concurrent/thread_pool.cc \
src/concurrent/thread_pool.h \
src/concurrent/version_property_receiver.cc \
src/concurrent/version_property_receiver.h \
src/concurrent/work_queue.cc \
src/concurrent/work_queue.h \
src/delay_input_receiver.cc \
src/delay_input_receiver.h \
src/direction.cc \
src/directory_listing.h \
src/directory_listing.cc \
src/editor.cc \
src/editor.h \
src/editor_mode.cc \
src/editor_mode.h \
src/editor_vm.cc \
src/editor_vm.h \
src/editor_variables.cc \
src/editor_variables.h \
src/frame_output_producer.cc \
src/frame_output_producer.h \
src/file_link_mode.cc \
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
src/infrastructure/audio.cc \
src/infrastructure/audio.h \
src/infrastructure/command_line.h \
src/infrastructure/command_line.cc \
src/infrastructure/dirname.cc \
src/infrastructure/dirname.h \
src/infrastructure/dirname_vm.cc \
src/infrastructure/dirname_vm.h \
src/infrastructure/execution.cc \
src/infrastructure/execution.h \
src/infrastructure/extended_char.cc \
src/infrastructure/extended_char.h \
src/infrastructure/extended_char_vm.cc \
src/infrastructure/extended_char_vm.h \
src/infrastructure/file_adapter.h \
src/infrastructure/file_descriptor_reader.cc \
src/infrastructure/file_descriptor_reader.h \
src/infrastructure/file_system_driver.cc \
src/infrastructure/file_system_driver.h \
src/infrastructure/regular_file_adapter.h \
src/infrastructure/regular_file_adapter.cc \
src/infrastructure/terminal_adapter.cc \
src/infrastructure/terminal_adapter.h \
src/infrastructure/time.cc \
src/infrastructure/time.h \
src/infrastructure/time_human.cc \
src/infrastructure/time_human.h \
src/infrastructure/screen/cursors.cc \
src/infrastructure/screen/cursors.h \
src/infrastructure/screen/line_modifier.cc \
src/infrastructure/screen/line_modifier.h \
src/infrastructure/screen/screen.h \
src/infrastructure/screen/visual_overlay.cc \
src/infrastructure/screen/visual_overlay.h \
src/infrastructure/tracker.cc \
src/infrastructure/tracker.h \
src/insert_history.cc \
src/insert_history.h \
src/insert_history_buffer.cc \
src/insert_history_buffer.h \
src/insert_mode.cc \
src/key_commands_map.cc \
src/key_commands_map.h \
src/language/container.h \
src/language/const_tree.cc \
src/language/const_tree.h \
src/language/error/log.cc \
src/language/error/log.h \
src/language/error/value_or_error.cc \
src/language/error/value_or_error.h \
src/language/error/view.h \
src/language/function_traits.h \
src/language/ghost_type.cc \
src/language/ghost_type.h \
src/language/ghost_type_class.h \
src/language/ghost_type_test.cc \
src/language/gc.cc \
src/language/gc.h \
src/language/gc_container.h \
src/language/gc_tests.cc \
src/language/gc_util.cc \
src/language/gc_util.h \
src/language/gc_view.h \
src/language/hash.cc \
src/language/hash.h \
src/language/lazy_string/append.h \
src/language/lazy_string/char_buffer.cc \
src/language/lazy_string/char_buffer.h \
src/language/lazy_string/functional.cc \
src/language/lazy_string/functional.h \
src/language/lazy_string/lazy_string.cc \
src/language/lazy_string/lazy_string.h \
src/language/lazy_string/lowercase.cc \
src/language/lazy_string/lowercase.h \
src/language/lazy_string/tokenize.cc \
src/language/lazy_string/tokenize.h \
src/language/lazy_string/tokenize_test.cc \
src/language/lazy_string/trim.cc \
src/language/lazy_string/trim.h \
src/language/observers.h \
src/language/observers.cc \
src/language/observers_gc.h \
src/language/safe_types.h \
src/language/text/delegating_mutable_line_sequence_observer.cc \
src/language/text/delegating_mutable_line_sequence_observer.h \
src/language/text/line.cc \
src/language/text/line.h \
src/language/text/line_builder.cc \
src/language/text/line_builder.h \
src/language/text/line_column.cc \
src/language/text/line_column.h \
src/language/text/line_column_vm.cc \
src/language/text/line_column_vm.h \
src/language/text/line_processor_map.cc \
src/language/text/line_processor_map.h \
src/language/text/line_sequence.cc \
src/language/text/line_sequence.h \
src/language/text/line_sequence_functional.cc \
src/language/text/line_sequence_functional.h \
src/language/text/mutable_line_sequence.cc \
src/language/text/mutable_line_sequence.h \
src/language/text/mutable_line_sequence_observer.h \
src/language/text/range.cc \
src/language/text/range.h \
src/language/text/sorted_line_sequence.cc \
src/language/text/sorted_line_sequence.h \
src/language/wstring.cc \
src/language/wstring.h \
src/line_output.cc \
src/line_output.h \
src/line_with_cursor.cc \
src/line_with_cursor.h \
src/line_marks.cc \
src/line_marks.h \
src/line_marks_buffer.cc \
src/line_marks_buffer.h \
src/line_marks_test.cc \
src/line_number_output_producer.h \
src/line_number_output_producer.cc \
src/line_prompt_mode.cc \
src/log.cc \
src/log.h \
src/lru_cache.cc \
src/lru_cache.h \
src/map_mode.cc \
src/math/bigint.cc \
src/math/bigint.h \
src/math/checked_operation.cc \
src/math/checked_operation.h \
src/math/decaying_counter.cc \
src/math/decaying_counter.h \
src/math/naive_bayes.cc \
src/math/naive_bayes.h \
src/math/numbers.cc \
src/math/numbers.h \
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
src/parsers/csv.cc \
src/parsers/csv.h \
src/parsers/diff.cc \
src/parsers/diff.h \
src/parsers/markdown.cc \
src/parsers/markdown.h \
src/parsers/util.cc \
src/parsers/util.h \
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
src/tests/benchmarks.cc \
src/tests/benchmarks.h \
src/tests/concurrent.cc \
src/tests/concurrent.h \
src/tests/concurrent_interfaces.cc \
src/tests/concurrent_interfaces.h \
src/tests/fuzz.h \
src/tests/fuzz.cc \
src/tests/fuzz_testable.h \
src/tests/fuzz_testable.cc \
src/tests/tests.cc \
src/tests/tests.h \
src/token_predictor.cc \
src/token_predictor.h \
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
src/transformation/paste.cc \
src/transformation/paste.h \
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
src/vm/container.h \
src/vm/optional.h \
src/vm/callbacks.cc \
src/vm/default_environment.cc \
src/vm/default_environment.h \
src/vm/escape.cc \
src/vm/escape.h \
src/vm/expression.cc \
src/vm/expression.h \
src/vm/lambda.cc \
src/vm/lambda.h \
src/vm/natural.cc \
src/vm/natural.h \
src/vm/numbers.cc \
src/vm/numbers.h \
src/vm/vm.cc \
src/vm/value.cc \
src/vm/string.cc \
src/vm/time.cc \
src/vm/time.h \
src/vm/types.cc \
src/vm/binary_operator.cc \
src/vm/class_expression.cc \
src/vm/class_expression.h \
src/vm/compilation.h \
src/vm/compilation.cc \
src/vm/file_system.cc \
src/vm/file_system.h \
src/vm/filter_similar_names.cc \
src/vm/filter_similar_names.h \
src/vm/if_expression.cc \
src/vm/return_expression.cc \
src/vm/types_promotion_tests.cc \
src/vm/while_expression.cc \
src/vm/constant_expression.cc \
src/vm/function_call.cc \
src/vm/environment.cc \
src/vm/assign_expression.cc \
src/vm/append_expression.cc \
src/vm/namespace_expression.cc \
src/vm/namespace_expression.h \
src/vm/negate_expression.cc \
src/vm/logical_expression.cc \
src/vm/variable_lookup.cc \
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

CLEANFILES = src/vm/cpp.c src/vm/cpp.h

BUILT_SOURCES = src/vm/cpp.c src/vm/cpp.h

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

test_fuzz_vm_SOURCES = $(COMMON_SOURCES) src/vm/fuzz.cc

lemon_SOURCES = src/vm/lemon.c

src/vm/cpp.h: src/vm/cpp.y lemon
	./lemon -Tsrc/vm/lempar.c src/vm/cpp.y

src/vm/cpp.c: src/vm/cpp.h

src/vm/vm.o: src/vm/vm.cc src/vm/cpp.c src/vm/cpp.h

GLOG=glog-0.4.0/libglog_la-demangle.o \
glog-0.4.0/libglog_la-logging.o \
glog-0.4.0/libglog_la-symbolize.o \
glog-0.4.0/libglog_la-raw_logging.o \
glog-0.4.0/libglog_la-utilities.o \
glog-0.4.0/libglog_la-vlog_is_on.o

AM_CPPFLAGS=-std=c++23 -g -Wall -Wextra -Iglog-0.4.0/src -DDOCDIR='"$(docdir)"' -Werror=return-type -Werror=switch -Werror=unused-function -Werror=missing-field-initializers -Werror=unused-but-set-variable -Werror=range-loop-construct -Wshadow -Wtrampolines -Wnull-dereference
AM_LDFLAGS=-std=c++23 -g
edge_LDADD=glog-0.4.0/libglog.la
benchmark_LDADD=glog-0.4.0/libglog.la
test_LDADD=glog-0.4.0/libglog.la
fuzz_test_LDADD=glog-0.4.0/libglog.la
test_fuzz_parser_LDADD=glog-0.4.0/libglog.la
test_fuzz_compiler_LDADD=glog-0.4.0/libglog.la
test_fuzz_classes_LDADD=glog-0.4.0/libglog.la
test_fuzz_vm_LDADD=glog-0.4.0/libglog.la
