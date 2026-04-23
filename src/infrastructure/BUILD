load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tests",
    visibility = ["//visibility:public"],
    deps = [
        ":dirname_tests",
        ":glob_tests",
    ],
)

cc_library(
    name = "audio",
    srcs = ["audio.cc"],
    hdrs = ["audio.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:protected",
        "//src/infrastructure:tracker",
        "//src/language:ghost_type",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "command_line",
    srcs = ["command_line.cc"],
    hdrs = ["command_line.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/language:wstring",
        "//src/language/error:value_or_error",
        "//src/language/lazy_string:single_line",
        "//src/tests",
    ],
)

cc_library(
    name = "glob",
    srcs = ["glob.cc"],
    hdrs = ["glob.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/language:ghost_type_class",
        "//src/language/lazy_string:column_number",
    ],
)

cc_library(
    name = "glob_tests",
    srcs = ["glob_tests.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/language/lazy_string:column_number",
        "//src/tests",
    ],
    alwayslink = 1,
)

cc_library(
    name = "dirname",
    srcs = ["dirname.cc"],
    hdrs = ["dirname.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:ghost_type",
        "//src/language/error:value_or_error",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
    ],
)

cc_library(
    name = "dirname_tests",
    srcs = [
        "dirname_tests.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/tests",
    ],
    alwayslink = 1,
)

cc_library(
    name = "dirname_vm",
    srcs = ["dirname_vm.cc"],
    hdrs = ["dirname_vm.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/language:gc",
        "//src/vm:callbacks",
    ],
)

cc_library(
    name = "execution",
    srcs = ["execution.cc"],
    hdrs = ["execution.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":file_system_driver",
    ],
)

cc_library(
    name = "extended_char",
    srcs = [
        "extended_char.cc",
        "extended_char_vm.cc",
    ],
    hdrs = [
        "extended_char.h",
        "extended_char_vm.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:protected",
        "//src/language:gc",
        "//src/language/lazy_string",
        "//src/vm:callbacks",
        "//src/vm:container",
        "//src/vm:environment",
    ],
)

cc_library(
    name = "file_adapter",
    hdrs = ["file_adapter.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":file_system_driver",
        "//src/infrastructure/screen:line_modifier",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "regular_file_adapter",
    srcs = ["regular_file_adapter.cc"],
    hdrs = ["regular_file_adapter.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":file_adapter",
        "//src/futures",
        "//src/language/text:line",
        "//src/language/text:line_builder",
    ],
)

cc_library(
    name = "file_descriptor_reader",
    srcs = ["file_descriptor_reader.cc"],
    hdrs = ["file_descriptor_reader.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":execution",
        "//src/concurrent:thread_pool",
        "//src/language/text:line",
    ],
)

cc_library(
    name = "file_system_driver",
    srcs = ["file_system_driver.cc"],
    hdrs = ["file_system_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/concurrent:thread_pool",
        "//src/language/error:view",
    ],
)

cc_library(
    name = "path_suffix_map",
    srcs = ["path_suffix_map.cc"],
    hdrs = ["path_suffix_map.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "terminal_adapter",
    srcs = ["terminal_adapter.cc"],
    hdrs = ["terminal_adapter.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":file_adapter",
        "//src/futures",
        "//src/language/text:line_sequence",
        "//src/language/text:mutable_line_sequence",
    ],
)

cc_library(
    name = "time",
    srcs = ["time.cc"],
    hdrs = ["time.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:wstring",
    ],
)

cc_library(
    name = "time_human",
    srcs = ["time_human.cc"],
    hdrs = ["time_human.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language/error:value_or_error",
        "//src/language/lazy_string:single_line",
    ],
)

cc_library(
    name = "tracker",
    srcs = ["tracker.cc"],
    hdrs = ["tracker.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:protected",
        "//src/infrastructure:time",
        "//src/language:container",
        "//src/language:safe_types",
    ],
)
