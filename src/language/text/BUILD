load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "line_builder",
    srcs = ["line_builder.cc"],
    hdrs = ["line_builder.h"],
    deps = [
        "//src/futures",
        "//src/futures:listenable_value",
        "//src/infrastructure:tracker",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:gc",
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/lazy_string",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:char_buffer",
        "//src/language/lazy_string:single_line",
        "//src/language/text:line",
        "//src/language/text:line_column",
        "//src/tests",
    ],
)

cc_library(
    name = "line",
    srcs = [
        "line.cc",
    ],
    hdrs = [
        "line.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":line_column",
        "//src/concurrent:protected",
        "//src/futures",
        "//src/futures:listenable_value",
        "//src/infrastructure:dirname",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:cached_supplier",
        "//src/language:lazy_value",
        "//src/language:observers",
        "//src/language:safe_types",
        "//src/language/lazy_string",
        "//src/language/lazy_string:append",
    ],
)

cc_library(
    name = "line_column",
    srcs = ["line_column.cc"],
    hdrs = ["line_column.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/error:value_or_error",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
        "//src/tests:fuzz",
        "//src/vm:environment",
    ],
)

cc_library(
    name = "line_column_vm",
    srcs = ["line_column_vm.cc"],
    hdrs = ["line_column_vm.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":range",
        "//src/language:safe_types",
        "//src/language/text:line_column",
        "//src/vm:callbacks",
        "//src/vm:container",
        "//src/vm:optional",
    ],
)

cc_library(
    name = "range",
    srcs = ["range.cc"],
    hdrs = ["range.h"],
    deps = [
        "//src/language:ghost_type_class",
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "line_sequence",
    srcs = ["line_sequence.cc"],
    hdrs = ["line_sequence.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:const_tree",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:char_buffer",
        "//src/language/text:line",
        "//src/language/text:line_builder",
        "//src/language/text:line_column",
        "//src/language/text:range",
        "//src/tests",
        "//src/tests:fuzz_testable",
    ],
)
