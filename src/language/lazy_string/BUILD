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
        "//src/language:const_tree",
        "//src/language:ghost_type",
        "//src/language:hash",
        "//src/language:safe_types",
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
    name = "tokenize",
    srcs = ["tokenize.cc"],
    hdrs = ["tokenize.h"],
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
