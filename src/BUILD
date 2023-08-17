cc_binary(
    name = "edge",
    srcs = ["main.cc"],
    deps = [
        ":args",
        ":audio",
        ":buffer_variables",
    ],
)

cc_library(
    name = "args",
    srcs = ["args.cc"],
    hdrs = ["args.h"],
    deps = [
        ":server",
        "//src/infrastructure:command_line",
        "//src/infrastructure:dirname",
        "//src/language:ghost_type",
        "//src/tests:benchmarks",
    ],
)

cc_library(
    name = "audio",
    srcs = ["audio.cc"],
    hdrs = ["audio.h"],
    deps = [
        "//src/concurrent:protected",
        "//src/infrastructure:tracker",
        "//src/language:ghost_type",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "buffer_contents",
    srcs = ["buffer_contents.cc"],
    hdrs = ["buffer_contents.h"],
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        ":cursors",
        ":line",
        ":line_column",
        "//src/language:const_tree",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "buffer_contents_util",
    srcs = ["buffer_contents_util.cc"],
    hdrs = ["buffer_contents_util.h"],
    deps = [
        ":buffer_contents",
        ":line_column",
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
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        ":line_column",
        "//src/language:observers",
    ],
)

cc_library(
    name = "buffer_flags",
    srcs = ["buffer_flags.cc"],
    hdrs = ["buffer_flags.h"],
)

cc_library(
    name = "buffer",
    srcs = ["buffer.cc"],
    hdrs = ["buffer.h"],
    deps = [
        ":buffer_contents",
        ":buffer_name",
        ":buffer_syntax_parser",
        ":buffer_terminal",
        ":cursors",
        ":file_descriptor_reader",
        ":line",
        ":line_column",
        ":line_marks",
        ":log",
        ":parse_tree",
        ":status",
        ":transformation",
        ":undo_state",
        ":variables",
        ":visual_overlay",
        "//src/concurrent:work_queue",
        "//src/futures",
        "//src/infrastructure:dirname",
        "//src/language:ghost_type",
        "//src/language:observers",
        "//src/language/lazy_string",
        "//src/language/lazy_string:substring",
        "//src/transformation:type",
        "//src/vm/public:environment",
        "//src/vm/public:value",
        "//src/vm/public:vm",
    ],
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
        ":parse_tree",
        "//src/concurrent:protected",
        "//src/concurrent:thread_pool",
        "//src/futures:delete_notification",
        "//src/language:observers",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "buffer_terminal",
    srcs = ["buffer_terminal.cc"],
    hdrs = ["buffer_terminal.h"],
    deps = [
        ":fuzz_testable",
        ":line_column",
        ":line_modifier",
        "//src/language:safe_types",
        "//src/language/lazy_string",
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
    name = "command_with_modifiers",
    srcs = ["command_with_modifiers.cc"],
    hdrs = ["command_with_modifiers.h"],
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
)

cc_library(
    name = "cursors",
    srcs = ["cursors.cc"],
    hdrs = ["cursors.h"],
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        ":line_column",
        "//src/futures",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:char_buffer",
        "//src/language/lazy_string:substring",
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
        ":line",
        ":line_modifier",
        "//src/concurrent:thread_pool",
        "//src/infrastructure:file_system_driver",
        "//src/infrastructure:time",
        "//src/infrastructure:tracker",
        "//src/language:wstring",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
        "//src/language/lazy_string:substring",
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
)

cc_library(
    name = "frame_output_producer",
    srcs = ["frame_output_producer.cc"],
    hdrs = ["frame_output_producer.h"],
)

cc_library(
    name = "fuzz",
    srcs = ["fuzz.cc"],
    hdrs = ["fuzz.h"],
    deps = [
        ":fuzz_testable",
    ],
)

cc_library(
    name = "fuzz_testable",
    srcs = ["fuzz_testable.cc"],
    hdrs = ["fuzz_testable.h"],
    deps = [
        "//src/language:wstring",
        "@com_github_google_glog//:glog",
    ],
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
    name = "line_column",
    srcs = ["line_column.cc"],
    hdrs = ["line_column.h"],
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        ":fuzz",
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language:value_or_error",
        "//src/language:wstring",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "line_column_vm",
    srcs = ["line_column_vm.cc"],
    hdrs = ["line_column_vm.h"],
    deps = [
        ":line_column",
        "//src/language:safe_types",
        "//src/vm/public:callbacks",
    ],
)

# TODO(cycles): Move line_with_cursor to its own module.
cc_library(
    name = "line",
    srcs = [
        "line.cc",
        "line_with_cursor.cc",
    ],
    hdrs = [
        "line.h",
        "line_with_cursor.h",
    ],
    deps = [
        ":line_column",
        ":line_modifier",
        "//src/concurrent:protected",
        "//src/futures",
        "//src/futures:listenable_value",
        "//src/language:observers",
        "//src/language:safe_types",
        "//src/language/lazy_string",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:padding",
        "//src/language/lazy_string:substring",
    ],
)

cc_library(
    name = "line_marks",
    srcs = ["line_marks.cc"],
    hdrs = ["line_marks.h"],
    deps = [
        ":buffer_name",
        ":line_column",
        "//src/language/lazy_string",
    ],
)

cc_library(
    name = "line_modifier",
    srcs = ["line_modifier.cc"],
    hdrs = ["line_modifier.h"],
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        "//src/language:hash",
        "//src/vm/public:environment",
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
    name = "log",
    srcs = ["log.cc"],
    hdrs = ["log.h"],
    deps = [
        "//src/futures",
        "//src/infrastructure:file_system_driver",
        "//src/language:safe_types",
        "//src/language:value_or_error",
    ],
)

cc_library(
    name = "lru_cache",
    srcs = ["lru_cache.cc"],
    hdrs = ["lru_cache.h"],
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
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        ":buffer_name",
        ":direction",
        ":line_column",
        ":structure",
        "//src/vm/public:callbacks",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "naive_bayes",
    srcs = ["naive_bayes.cc"],
    hdrs = ["naive_bayes.h"],
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
    name = "operation_scope",
    srcs = ["operation_scope.cc"],
    hdrs = ["operation_scope.h"],
    deps = [
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "parse_tools",
    srcs = ["parse_tools.cc"],
    hdrs = ["parse_tools.h"],
)

cc_library(
    name = "parse_tree",
    srcs = ["parse_tree.cc"],
    hdrs = ["parse_tree.h"],
    deps = [
        ":buffer_contents",
        ":line_column",
        ":line_modifier",
        "//src/language:ghost_type",
        "//src/language:safe_types",
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
    name = "predictor",
    srcs = ["predictor.cc"],
    hdrs = ["predictor.h"],
    deps = [
        ":buffer_name",
        ":status",
        "//src/concurrent:work_queue",
    ],
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
    name = "screen",
    srcs = ["screen.cc"],
    hdrs = ["screen.h"],
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
    ],
)

cc_library(
    name = "server",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    deps = [
        ":buffer",
        "//src/infrastructure:dirname",
        "//src/infrastructure:file_system_driver",
        "//src/language:gc",
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
        ":audio",
        # ":buffer",
        ":line",
        ":line_modifier",
        "//src/infrastructure:time",
        "//src/language:gc",
        "//src/language:ghost_type",
        "//src/language:overload",
        "//src/tests",
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
        ":buffer",
        ":buffer_contents",
        ":buffer_variables",
        ":direction",
        ":editor",
        ":line_column",
        ":operation_scope",
        ":parse_tree",
        ":seek",
        "//src/language/lazy_string",
        "//src/tests",
        "//src/transformation:delete",
    ],
)

cc_library(
    name = "terminal",
    srcs = ["terminal.cc"],
    hdrs = ["terminal.h"],
)

cc_library(
    name = "tokenize",
    srcs = ["tokenize.cc"],
    hdrs = ["tokenize.h"],
)

cc_library(
    name = "transformation",
    srcs = ["transformation.cc"],
    hdrs = ["transformation.h"],
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        ":direction",
        ":line",
        "//src/futures",
    ],
)

cc_library(
    name = "undo_state",
    srcs = ["undo_state.cc"],
    hdrs = ["undo_state.h"],
    deps = [
        "//src/futures",
        "//src/language:safe_types",
        "//src/transformation:stack",
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
        "//src/vm/public:types",
    ],
)

cc_library(
    name = "visual_overlay",
    hdrs = ["visual_overlay.h"],
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
