cc_library(
    name = "const_tree",
    srcs = ["const_tree.cc"],
    hdrs = ["const_tree.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/infrastructure:time",
        "//src/tests",
        "//src/tests:benchmarks",
    ],
)

cc_library(
    name = "function_traits",
    hdrs = ["function_traits.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "gc",
    srcs = ["gc.cc"],
    hdrs = ["gc.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:bag",
    ],
)

cc_library(
    name = "ghost_type",
    srcs = ["ghost_type.cc"],
    hdrs = ["ghost_type.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "hash",
    srcs = ["hash.cc"],
    hdrs = ["hash.h"],
    visibility = ["//visibility:public"],
    deps = ["//src/tests"],
)

cc_library(
    name = "observers_gc",
    hdrs = ["observers_gc.h"],
)

cc_library(
    name = "observers",
    srcs = ["observers.cc"],
    hdrs = ["observers.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:protected",
        "//src/futures",
    ],
)

cc_library(
    name = "overload",
    hdrs = ["overload.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "safe_types",
    hdrs = ["safe_types.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "value_or_error",
    srcs = ["value_or_error.cc"],
    hdrs = ["value_or_error.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ghost_type",
        ":overload",
        ":safe_types",
        ":wstring",
        "//src/tests",
    ],
)

cc_library(
    name = "wstring",
    srcs = ["wstring.cc"],
    hdrs = ["wstring.h"],
    visibility = ["//visibility:public"],
)
