load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "edge",
    srcs = [
        "args.cc",
        "args.h",
        "args_tests.cc",
        "buffer.cc",
        "buffer.h",
        "buffer_contents_util.cc",
        "buffer_contents_util.h",
        "buffer_contents_view_layout.cc",
        "buffer_contents_view_layout.h",
        "buffer_display_data.cc",
        "buffer_display_data.h",
        "buffer_filter.cc",
        "buffer_filter.h",
        "buffer_flags.cc",
        "buffer_flags.h",
        "buffer_metadata_output_producer.cc",
        "buffer_metadata_output_producer.h",
        "buffer_name.cc",
        "buffer_name.h",
        "buffer_output_producer.cc",
        "buffer_output_producer.h",
        "buffer_registry.cc",
        "buffer_registry.h",
        "buffer_state.cc",
        "buffer_state.h",
        "buffer_subtypes.cc",
        "buffer_subtypes.h",
        "buffer_syntax_parser.cc",
        "buffer_syntax_parser.h",
        "buffer_tests.cc",
        "buffer_transformation_adapter.cc",
        "buffer_transformation_adapter.h",
        "buffer_variables.cc",
        "buffer_variables.h",
        "buffer_vm.cc",
        "buffer_vm.h",
        "buffer_widget.cc",
        "buffer_widget.h",
        "buffers_list.cc",
        "buffers_list.h",
        "columns_vector.cc",
        "columns_vector.h",
        "command.cc",
        "command.h",
        "command_argument_mode.h",
        "command_mode.cc",
        "command_mode.h",
        "completion_model.cc",
        "completion_model.h",
        "cpp_command.cc",
        "cpp_command.h",
        "delay_input_receiver.cc",
        "delay_input_receiver.h",
        "direction.cc",
        "direction.h",
        "directory_listing.cc",
        "directory_listing.h",
        "editor.cc",
        "editor.h",
        "editor_mode.cc",
        "editor_mode.h",
        "editor_variables.cc",
        "editor_variables.h",
        "editor_vm.cc",
        "editor_vm.h",
        "execution_context.cc",
        "execution_context.h",
        "file_link_mode.cc",
        "file_link_mode.h",
        "file_predictor.cc",
        "file_predictor.h",
        "file_tags.cc",
        "file_tags.h",
        "find_mode.cc",
        "find_mode.h",
        "flashcard.cc",
        "flashcard.h",
        "fragments.cc",
        "fragments.h",
        "frame_output_producer.cc",
        "frame_output_producer.h",
        "goto_command.cc",
        "goto_command.h",
        "help_command.cc",
        "help_command.h",
        "horizontal_center_output_producer.cc",
        "horizontal_center_output_producer.h",
        "insert_history.cc",
        "insert_history.h",
        "insert_history_buffer.cc",
        "insert_history_buffer.h",
        "insert_mode.cc",
        "insert_mode.h",
        "key_commands_map.cc",
        "key_commands_map.h",
        "line_marks.cc",
        "line_marks.h",
        "line_marks_buffer.cc",
        "line_marks_buffer.h",
        "line_marks_test.cc",
        "line_number_output_producer.cc",
        "line_number_output_producer.h",
        "line_output.cc",
        "line_output.h",
        "line_prompt_mode.cc",
        "line_prompt_mode.h",
        "line_with_cursor.cc",
        "line_with_cursor.h",
        "log.cc",
        "log.h",
        "lru_cache.cc",
        "lru_cache.h",
        "main.cc",
        "map_mode.cc",
        "map_mode.h",
        "modifiers.cc",
        "modifiers.h",
        "navigate_command.cc",
        "navigate_command.h",
        "navigation_buffer.cc",
        "navigation_buffer.h",
        "open_directory_command.cc",
        "open_directory_command.h",
        "open_file_command.cc",
        "open_file_command.h",
        "open_file_position.cc",
        "open_file_position.h",
        "open_files.cc",
        "open_files.h",
        "operation.cc",
        "operation.h",
        "operation_scope.cc",
        "operation_scope.h",
        "operation_scope_buffer_information.h",
        "parse_tools.cc",
        "parse_tools.h",
        "parse_tree.cc",
        "parse_tree.h",
        "parsers/cpp.cc",
        "parsers/cpp.h",
        "parsers/css.cc",
        "parsers/css.h",
        "parsers/csv.cc",
        "parsers/csv.h",
        "parsers/diff.cc",
        "parsers/diff.h",
        "parsers/markdown.cc",
        "parsers/markdown.h",
        "parsers/py.cc",
        "parsers/py.h",
        "parsers/util.cc",
        "parsers/util.h",
        "parsers/util_test.cc",
        "path_flags.cc",
        "path_flags.h",
        "predictor.cc",
        "predictor.h",
        "quit_command.cc",
        "quit_command.h",
        "record_command.cc",
        "record_command.h",
        "repeat_mode.cc",
        "repeat_mode.h",
        "run_command_handler.cc",
        "run_command_handler.h",
        "run_cpp_command.cc",
        "run_cpp_command.h",
        "run_cpp_file.cc",
        "run_cpp_file.h",
        "screen_curses.cc",
        "screen_curses.h",
        "screen_vm.cc",
        "screen_vm.h",
        "search_command.cc",
        "search_command.h",
        "search_handler.cc",
        "search_handler.h",
        "search_handler_vm.cc",
        "search_handler_vm.h",
        "section_brackets_producer.cc",
        "section_brackets_producer.h",
        "seek.cc",
        "seek.h",
        "server.cc",
        "server.h",
        "set_buffer_mode.cc",
        "set_buffer_mode.h",
        "set_mode_command.cc",
        "set_mode_command.h",
        "set_variable_command.cc",
        "set_variable_command.h",
        "shapes.cc",
        "shapes.h",
        "status.cc",
        "status.h",
        "status_output_producer.cc",
        "status_output_producer.h",
        "status_tests.cc",
        "structure.cc",
        "structure.h",
        "structure_move.cc",
        "structure_move.h",
        "terminal.cc",
        "terminal.h",
        "token_predictor.cc",
        "token_predictor.h",
        "transformation.cc",
        "transformation.h",
        "transformation_bisect.cc",
        "transformation_bisect.h",
        "transformation_composite.cc",
        "transformation_composite.h",
        "transformation_cursors.cc",
        "transformation_cursors.h",
        "transformation_delete.cc",
        "transformation_delete.h",
        "transformation_expand.cc",
        "transformation_expand.h",
        "transformation_input.h",
        "transformation_insert.cc",
        "transformation_insert.h",
        "transformation_move.cc",
        "transformation_move.h",
        "transformation_noop.cc",
        "transformation_noop.h",
        "transformation_paste.cc",
        "transformation_paste.h",
        "transformation_reach_query.cc",
        "transformation_reach_query.h",
        "transformation_repetitions.cc",
        "transformation_repetitions.h",
        "transformation_result.h",
        "transformation_set_position.cc",
        "transformation_set_position.h",
        "transformation_stack.cc",
        "transformation_stack.h",
        "transformation_switch_case.cc",
        "transformation_switch_case.h",
        "transformation_tree_navigate.cc",
        "transformation_tree_navigate.h",
        "transformation_type.cc",
        "transformation_type.h",
        "transformation_variant.h",
        "transformation_visual_overlay.cc",
        "transformation_visual_overlay.h",
        "transformation_vm.cc",
        "transformation_vm.h",
        "undo_state.cc",
        "undo_state.h",
        "url.cc",
        "url.h",
        "variables.h",
        "widget.h",
        "widget_list.cc",
        "widget_list.h",
    ],
    linkopts = [
        "-lncursesw",
        "-lpthread",
        # "-lao",
    ],
    deps = [
        "//src/concurrent:version_property_receiver",
        "//src/futures:serializer",
        "//src/infrastructure:command_line",
        "//src/infrastructure:dirname_vm",
        "//src/infrastructure:execution",
        "//src/infrastructure:extended_char",
        "//src/infrastructure:file_adapter",
        "//src/infrastructure:file_descriptor_reader",
        "//src/infrastructure:file_system_driver",
        "//src/infrastructure:glob",
        "//src/infrastructure:path_suffix_map",
        "//src/infrastructure:regular_file_adapter",
        "//src/infrastructure:terminal_adapter",
        "//src/infrastructure/screen",
        "//src/infrastructure/screen:cursors",
        "//src/infrastructure/screen:line_modifier",
        "//src/infrastructure/screen:visual_overlay",
        "//src/language:observers",
        "//src/language:observers_gc",
        "//src/language/lazy_string",
        "//src/language/lazy_string:convert",
        "//src/language/lazy_string:hash",
        "//src/language/lazy_string:single_line",
        "//src/language/lazy_string:trim",
        "//src/language/text:delegating_mutable_line_sequence_observer",
        "//src/language/text:line",
        "//src/language/text:line_builder",
        "//src/language/text:line_column_vm",
        "//src/language/text:line_processor_map",
        "//src/language/text:line_sequence",
        "//src/language/text:mutable_line_sequence",
        "//src/language/text:sorted_line_sequence",
        "//src/math:naive_bayes",
        "//src/tests:benchmarks",
        "//src/vm",
        "//src/vm:callbacks_gc",
        "//src/vm:default_environment",
        "//src/vm:file_system",
        "//src/vm:natural",
    ],
)

cc_library(
    name = "buffer_contents",
    srcs = ["buffer_contents.cc"],
    hdrs = ["buffer_contents.h"],
    deps = [
        "//src/language:const_tree",
        "//src/language:safe_types",
        "//src/language/text:line",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "buffer_contents_util",
    srcs = ["buffer_contents_util.cc"],
    hdrs = ["buffer_contents_util.h"],
    deps = [
        ":buffer_contents",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "buffer_contents_view_layout",
    srcs = ["buffer_contents_view_layout.cc"],
    hdrs = ["buffer_contents_view_layout.h"],
)

cc_library(
    name = "buffer_display_data",
    srcs = ["buffer_display_data.cc"],
    hdrs = ["buffer_display_data.h"],
    deps = [
        "//src/language:observers",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "buffer_flags",
    srcs = ["buffer_flags.cc"],
    hdrs = ["buffer_flags.h"],
)

cc_library(
    name = "buffer_metadata_output_producer",
    srcs = ["buffer_metadata_output_producer.cc"],
    hdrs = ["buffer_metadata_output_producer.h"],
)

cc_library(
    name = "buffer_name",
    srcs = ["buffer_name.cc"],
    hdrs = ["buffer_name.h"],
    deps = [
        "//src/infrastructure:dirname",
    ],
)

cc_library(
    name = "buffer_output_producer",
    srcs = ["buffer_output_producer.cc"],
    hdrs = ["buffer_output_producer.h"],
)

cc_library(
    name = "buffers_list",
    srcs = ["buffers_list.cc"],
    hdrs = ["buffers_list.h"],
)

cc_library(
    name = "buffer_syntax_parser",
    srcs = ["buffer_syntax_parser.cc"],
    hdrs = ["buffer_syntax_parser.h"],
    deps = [
        ":cpp_parse_tree",
        ":parse_tree",
        "//src/concurrent:protected",
        "//src/concurrent:thread_pool",
        "//src/futures:delete_notification",
        "//src/language:observers",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "buffer_variables",
    srcs = ["buffer_variables.cc"],
    hdrs = ["buffer_variables.h"],
    deps = [
        ":variables",
    ],
)

cc_library(
    name = "buffer_vm",
    srcs = ["buffer_vm.cc"],
    hdrs = ["buffer_vm.h"],
)

cc_library(
    name = "buffer_widget",
    srcs = ["buffer_widget.cc"],
    hdrs = ["buffer_widget.h"],
)

cc_library(
    name = "columns_vector",
    srcs = ["columns_vector.cc"],
    hdrs = ["columns_vector.h"],
)

cc_library(
    name = "command_argument_mode",
    srcs = ["command_argument_mode.cc"],
    hdrs = ["command_argument_mode.h"],
)

cc_library(
    name = "command",
    srcs = ["command.cc"],
    hdrs = ["command.h"],
)

cc_library(
    name = "command_mode",
    srcs = ["command_mode.cc"],
    hdrs = ["command_mode.h"],
)

cc_library(
    name = "cpp_command",
    srcs = ["cpp_command.cc"],
    hdrs = ["cpp_command.h"],
)

cc_library(
    name = "cpp_parse_tree",
    srcs = ["cpp_parse_tree.cc"],
    hdrs = ["cpp_parse_tree.h"],
    deps = [
        ":buffer_contents",
        ":lru_cache",
        ":parse_tools",
        ":parse_tree",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "completion_model",
    srcs = ["completion_model.cc"],
    hdrs = ["completion_model.h"],
    deps = [
        ":buffer_contents",
        "//src/concurrent:protected",
        "//src/futures",
        "//src/language/lazy_string:lowercase",
    ],
)

cc_library(
    name = "direction",
    srcs = ["direction.cc"],
    hdrs = ["direction.h"],
)

cc_library(
    name = "directory_listing",
    srcs = ["directory_listing.cc"],
    hdrs = ["directory_listing.h"],
)

cc_library(
    name = "editor",
    srcs = ["editor.cc"],
    hdrs = ["editor.h"],
)

cc_library(
    name = "editor_mode",
    srcs = ["editor_mode.cc"],
    hdrs = ["editor_mode.h"],
)

cc_library(
    name = "editor_variables",
    srcs = ["editor_variables.cc"],
    hdrs = ["editor_variables.h"],
)

cc_library(
    name = "editor_vm",
    srcs = ["editor_vm.cc"],
    hdrs = ["editor_vm.h"],
)

cc_library(
    name = "file_descriptor_reader",
    srcs = ["file_descriptor_reader.cc"],
    hdrs = ["file_descriptor_reader.h"],
    deps = [
        ":buffer_name",
        "//src/concurrent:thread_pool",
        "//src/infrastructure:file_system_driver",
        "//src/infrastructure:time",
        "//src/infrastructure:tracker",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:wstring",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
        "//src/language/text:line",
    ],
)

cc_library(
    name = "file_link_mode",
    srcs = ["file_link_mode.cc"],
    hdrs = ["file_link_mode.h"],
)

cc_library(
    name = "find_mode",
    srcs = ["find_mode.cc"],
    hdrs = ["find_mode.h"],
    deps = [
        "//src/transformation_composite",
    ],
)

cc_library(
    name = "frame_output_producer",
    srcs = ["frame_output_producer.cc"],
    hdrs = ["frame_output_producer.h"],
)

cc_library(
    name = "goto_command",
    srcs = ["goto_command.cc"],
    hdrs = ["goto_command.h"],
)

cc_library(
    name = "help_command",
    srcs = ["help_command.cc"],
    hdrs = ["help_command.h"],
)

cc_library(
    name = "horizontal_center_output_producer",
    srcs = ["horizontal_center_output_producer.cc"],
    hdrs = ["horizontal_center_output_producer.h"],
)

cc_library(
    name = "insert_history_buffer",
    srcs = ["insert_history_buffer.cc"],
    hdrs = ["insert_history_buffer.h"],
)

cc_library(
    name = "insert_history",
    srcs = ["insert_history.cc"],
    hdrs = ["insert_history.h"],
)

cc_library(
    name = "insert_mode",
    srcs = ["insert_mode.cc"],
    hdrs = ["insert_mode.h"],
)

cc_library(
    name = "line_marks",
    srcs = ["line_marks.cc"],
    hdrs = ["line_marks.h"],
    deps = [
        ":buffer_contents",
        ":buffer_name",
        "//src/language/lazy_string",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "line_number_output_producer",
    srcs = ["line_number_output_producer.cc"],
    hdrs = ["line_number_output_producer.h"],
)

cc_library(
    name = "line_output",
    srcs = ["line_output.cc"],
    hdrs = ["line_output.h"],
)

cc_library(
    name = "line_prompt_mode",
    srcs = ["line_prompt_mode.cc"],
    hdrs = ["line_prompt_mode.h"],
)

cc_library(
    name = "line_with_cursor",
    srcs = [
        "line_with_cursor.cc",
    ],
    hdrs = [
        "line_with_cursor.h",
    ],
    deps = [
        "//src/infrastructure:tracker",
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language/lazy_string",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:padding",
        "//src/language/text:line",
        "//src/language/text:line_column",
        "//src/tests",
    ],
)

cc_library(
    name = "log",
    srcs = ["log.cc"],
    hdrs = ["log.h"],
    deps = [
        "//src/futures",
        "//src/infrastructure:file_system_driver",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "lru_cache",
    hdrs = ["lru_cache.h"],
    deps = [
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "map_mode",
    srcs = ["map_mode.cc"],
    hdrs = ["map_mode.h"],
)

cc_library(
    name = "modifiers",
    srcs = ["modifiers.cc"],
    hdrs = ["modifiers.h"],
    deps = [
        ":buffer_name",
        ":direction",
        ":structure",
        "//src/language/text:line_column",
        "//src/vm:callbacks",
        "//src/vm:environment",
    ],
)

cc_library(
    name = "navigate_command",
    srcs = ["navigate_command.cc"],
    hdrs = ["navigate_command.h"],
)

cc_library(
    name = "navigation_buffer",
    srcs = ["navigation_buffer.cc"],
    hdrs = ["navigation_buffer.h"],
)

cc_library(
    name = "open_directory_command",
    srcs = ["open_directory_command.cc"],
    hdrs = ["open_directory_command.h"],
)

cc_library(
    name = "open_file_command",
    srcs = ["open_file_command.cc"],
    hdrs = ["open_file_command.h"],
)

cc_library(
    name = "operation",
    srcs = ["operation.cc"],
    hdrs = ["operation.h"],
)

cc_library(
    name = "operation_scope_buffer_information",
    hdrs = ["operation_scope_buffer_information.h"],
    deps = [
        ":line_marks",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "operation_scope",
    srcs = ["operation_scope.cc"],
    hdrs = ["operation_scope.h"],
    deps = [
        ":buffer",
        ":buffer_variables",
        ":operation_scope_buffer_information",
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "parse_tools",
    srcs = ["parse_tools.cc"],
    hdrs = ["parse_tools.h"],
    deps = [
        ":seek",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "parse_tree",
    srcs = ["parse_tree.cc"],
    hdrs = ["parse_tree.h"],
    deps = [
        ":buffer_contents",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:ghost_type",
        "//src/language:safe_types",
        "//src/language/text:line_column",
        "//src/vm:environment",
    ],
)

cc_library(
    name = "paste",
    srcs = ["paste.cc"],
    hdrs = ["paste.h"],
)

cc_library(
    name = "path_flags",
    srcs = ["path_flags.cc"],
    hdrs = ["path_flags.h"],
)

cc_library(
    name = "quit_command",
    srcs = ["quit_command.cc"],
    hdrs = ["quit_command.h"],
)

cc_library(
    name = "record_command",
    srcs = ["record_command.cc"],
    hdrs = ["record_command.h"],
)

cc_library(
    name = "repeat_mode",
    srcs = ["repeat_mode.cc"],
    hdrs = ["repeat_mode.h"],
)

cc_library(
    name = "run_command_handler",
    srcs = ["run_command_handler.cc"],
    hdrs = ["run_command_handler.h"],
)

cc_library(
    name = "run_cpp_command",
    srcs = ["run_cpp_command.cc"],
    hdrs = ["run_cpp_command.h"],
)

cc_library(
    name = "run_cpp_file",
    srcs = ["run_cpp_file.cc"],
    hdrs = ["run_cpp_file.h"],
)

cc_library(
    name = "screen_curses",
    srcs = ["screen_curses.cc"],
    hdrs = ["screen_curses.h"],
)

cc_library(
    name = "screen_union",
    srcs = ["screen_union.cc"],
    hdrs = ["screen_union.h"],
)

cc_library(
    name = "screen_vm",
    srcs = ["screen_vm.cc"],
    hdrs = ["screen_vm.h"],
)

cc_library(
    name = "search_command",
    srcs = ["search_command.cc"],
    hdrs = ["search_command.h"],
)

cc_library(
    name = "search_handler",
    srcs = ["search_handler.cc"],
    hdrs = ["search_handler.h"],
)

cc_library(
    name = "section_brackets_producer",
    srcs = ["section_brackets_producer.cc"],
    hdrs = ["section_brackets_producer.h"],
)

cc_library(
    name = "seek",
    srcs = ["seek.cc"],
    hdrs = ["seek.h"],
    deps = [
        ":buffer_contents",
        ":direction",
    ],
)

cc_library(
    name = "set_buffer_mode",
    srcs = ["set_buffer_mode.cc"],
    hdrs = ["set_buffer_mode.h"],
)

cc_library(
    name = "set_mode_command",
    srcs = ["set_mode_command.cc"],
    hdrs = ["set_mode_command.h"],
)

cc_library(
    name = "set_variable_command",
    srcs = ["set_variable_command.cc"],
    hdrs = ["set_variable_command.h"],
)

cc_library(
    name = "shapes",
    srcs = ["shapes.cc"],
    hdrs = ["shapes.h"],
)

cc_library(
    name = "status",
    srcs = ["status.cc"],
    hdrs = ["status.h"],
    deps = [
        "//src/infrastructure:time",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:gc",
        "//src/language:ghost_type",
        "//src/language:overload",
        "//src/language/text:line",
    ],
)

cc_library(
    name = "status_output_producer",
    srcs = ["status_output_producer.cc"],
    hdrs = ["status_output_producer.h"],
)

cc_library(
    name = "structure",
    srcs = ["structure.cc"],
    hdrs = ["structure.h"],
    deps = [
        ":buffer_contents",
        ":direction",
        ":parse_tree",
        ":seek",
        "//src/language/lazy_string",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "structure_move",
    srcs = ["structure_move.cc"],
    hdrs = ["structure_move.h"],
    deps = [
        ":buffer_contents",
        ":direction",
        ":modifiers",
        ":operation_scope_buffer_information",
        "//src/language/lazy_string",
        "//src/language/text:line_column",
        "//src/tests",
    ],
)

cc_library(
    name = "terminal",
    srcs = ["terminal.cc"],
    hdrs = ["terminal.h"],
)

cc_library(
    name = "terminal_input_parser",
    srcs = ["terminal_input_parser.cc"],
    hdrs = ["terminal_input_parser.h"],
    deps = [
        ":buffer_contents",
        ":buffer_name",
        "//src/infrastructure:file_system_driver",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
        "//src/language/text:line_column",
        "//src/tests:fuzz_testable",
    ],
)

cc_library(
    name = "transformation",
    srcs = ["transformation.cc"],
    hdrs = ["transformation.h"],
    deps = [
        ":direction",
        ":modifiers",
        "//src/futures",
        "//src/language/text:line",
    ],
)

cc_library(
    name = "url",
    srcs = ["url.cc"],
    hdrs = ["url.h"],
)

cc_library(
    name = "variables",
    hdrs = ["variables.h"],
    deps = [
        ":predictor",
        "//src/language:observers",
        "//src/vm:types",
    ],
)

cc_library(
    name = "widget",
    srcs = ["widget.cc"],
    hdrs = ["widget.h"],
)

cc_library(
    name = "widget_list",
    srcs = ["widget_list.cc"],
    hdrs = ["widget_list.h"],
)
