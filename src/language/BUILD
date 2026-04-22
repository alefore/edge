load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "container",
    hdrs = ["container.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "const_tree",
    srcs = ["const_tree.cc"],
    hdrs = ["const_tree.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":overload",
        ":safe_types",
        "//src/tests",
    ],
)

cc_library(
    name = "const_tree_benchmarks",
    srcs = ["const_tree_benchmarks.cc"],
    deps = [
        ":const_tree",
        "//src/tests:benchmarks",
    ],
)

cc_library(
    name = "function_traits",
    hdrs = ["function_traits.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "gc_concepts",
    hdrs = ["gc_concepts.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":gc",
    ],
)

cc_library(
    name = "gc_container",
    hdrs = ["gc_container.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":gc",
        ":gc_view",
    ],
)

cc_library(
    name = "lazy_value",
    hdrs = ["lazy_value.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":once_only_function",
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "cached_supplier",
    hdrs = ["cached_supplier.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":once_only_function",
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "gc_expanders",
    hdrs = ["gc_expanders.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":gc",
        ":gc_concepts",
        ":gc_view",
        ":safe_types",
        "//src/concurrent:protected",
    ],
)

cc_library(
    name = "gc_view",
    hdrs = ["gc_view.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":gc",
        ":safe_types",
    ],
)

cc_library(
    name = "gc_util",
    srcs = ["gc_util.cc"],
    hdrs = ["gc_util.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":gc",
        ":gc_concepts",
        ":safe_types",
        "//src/futures:delete_notification",
        "//src/tests",
    ],
)

cc_library(
    name = "gc",
    srcs = [
        "gc.cc",
        # TODO(P2, 2026-04-22): Enable.
        # "gc_tests.cc",
    ],
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
    name = "ghost_type_class",
    hdrs = ["ghost_type_class.h"],
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
    visibility = ["//visibility:public"],
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
    name = "once_only_function",
    hdrs = ["once_only_function.h"],
    visibility = ["//visibility:public"],
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
    name = "wstring",
    srcs = ["wstring.cc"],
    hdrs = ["wstring.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@glog",
    ],
)
