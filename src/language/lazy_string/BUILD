load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tests",
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string_tests",
        ":tokenize_tests",
    ],
)

cc_library(
    name = "append",
    hdrs = ["append.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
        "//src/language:const_tree",
    ],
)

cc_library(
    name = "char_buffer",
    srcs = ["char_buffer.cc"],
    hdrs = ["char_buffer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
    ],
)

cc_library(
    name = "convert",
    srcs = ["convert.cc"],
    hdrs = ["convert.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "column_number",
    hdrs = ["column_number.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:ghost_type_class",
    ],
)

cc_library(
    name = "hash",
    hdrs = ["hash.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
    ],
)

cc_library(
    name = "lazy_string",
    srcs = [
        "functional.cc",
        "lazy_string.cc",
    ],
    hdrs = [
        "functional.h",
        "lazy_string.h",
    ],
    copts = [
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
    ],
    features = ["-unwind_tables"],
    visibility = ["//visibility:public"],
    deps = [
        ":column_number",
        "//src/infrastructure:tracker",
        "//src/language:const_tree",
        "//src/language:ghost_type",
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language:wstring",
    ],
)

cc_library(
    name = "lazy_string_tests",
    srcs = [
        "functional_tests.cc",
        "lazy_string_tests.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
        "//src/tests",
        "//src/tests:factory",
    ],
    alwayslink = 1,
)

cc_library(
    name = "lowercase",
    srcs = ["lowercase.cc"],
    hdrs = ["lowercase.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":char_buffer",
        ":lazy_string",
        ":single_line",
        "//src/tests",
    ],
)

cc_library(
    name = "single_line",
    srcs = ["single_line.cc"],
    hdrs = ["single_line.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "tokenize",
    srcs = ["tokenize.cc"],
    hdrs = ["tokenize.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
        ":lowercase",
        ":single_line",
    ],
)

cc_library(
    name = "tokenize_tests",
    srcs = ["tokenize_tests.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":tokenize",
        "//src/tests",
    ],
    alwayslink = 1,
)

cc_library(
    name = "trim",
    srcs = ["trim.cc"],
    hdrs = ["trim.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
        "//src/language:safe_types",
        "//src/tests",
        # TODO(P2, 2026-05-11): Ugh, this should not depend on value_or_error;
        # instead, `:column_number` should have that dependency. But that gives
        # us circular dependencies. column_number -> ghost_type_class -> error.
        "//src/language/error:value_or_error",
    ],
    alwayslink = 1,
)
