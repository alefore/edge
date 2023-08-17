cc_library(
    name = "append",
    srcs = ["append.cc"],
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
    name = "lazy_string",
    srcs = [
        "functional.cc",
        "lazy_string.cc",
    ],
    hdrs = [
        "functional.h",
        "lazy_string.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//src/infrastructure:tracker",
        "//src/language:ghost_type",
        "//src/language:hash",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "lowercase",
    srcs = ["lowercase.cc"],
    hdrs = ["lowercase.h"],
    deps = [
        ":char_buffer",
        ":lazy_string",
    ],
)

cc_library(
    name = "padding",
    srcs = ["padding.cc"],
    hdrs = ["padding.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":char_buffer",
        ":lazy_string",
    ],
)

cc_library(
    name = "substring",
    srcs = ["substring.cc"],
    hdrs = ["substring.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":lazy_string",
    ],
)

cc_library(
    name = "trim",
    srcs = ["trim.cc"],
    hdrs = ["trim.h"],
    deps = [
        ":lazy_string",
        ":substring",
    ],
)
