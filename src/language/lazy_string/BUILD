load("@rules_cc//cc:defs.bzl", "cc_library")

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
    name = "lowercase",
    srcs = ["lowercase.cc"],
    hdrs = ["lowercase.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":char_buffer",
        ":lazy_string",
        ":single_line",
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
    name = "trim",
    srcs = ["trim.cc"],
    hdrs = ["trim.h"],
    visibility = ["//visibility:public"],
    # TODO(P3, 2026-04-22, easy): Trim should not depend on :single_line.
    deps = [
        ":lazy_string",
        ":single_line",
    ],
)
