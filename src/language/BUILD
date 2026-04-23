load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tests",
    visibility = ["//visibility:public"],
    deps = [
        ":const_tree_tests",
        ":gc_tests",
        ":ghost_type_tests",
        ":hash_tests",
    ],
)

cc_library(
    name = "container",
    hdrs = ["container.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "const_tree",
    hdrs = ["const_tree.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":overload",
        ":safe_types",
    ],
)

cc_library(
    name = "const_tree_tests",
    srcs = ["const_tree_tests.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":const_tree",
        "//src/tests",
    ],
    alwayslink = 1,
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
    srcs = ["lazy_value.cc"],
    hdrs = ["lazy_value.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":once_only_function",
        ":overload",
        ":safe_types",
        "//src/concurrent:protected",
        "//src/tests",
    ],
    alwayslink = 1,
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
    alwayslink = 1,
)

cc_library(
    name = "gc",
    srcs = [
        "gc.cc",
    ],
    hdrs = ["gc.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:bag",
    ],
    alwayslink = 1,
)

cc_library(
    name = "gc_tests",
    srcs = ["gc_tests.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":container",
        ":gc",
        ":gc_view",
        ":safe_types",
        "//src/concurrent:protected",
        "//src/futures:delete_notification",
        "//src/language/lazy_string",
        "//src/tests",
        "//src/tests:concurrent",
    ],
    alwayslink = 1,
)

cc_library(
    name = "ghost_type",
    srcs = [
        "ghost_type.cc",
    ],
    hdrs = ["ghost_type.h"],
    visibility = ["//visibility:public"],
    alwayslink = 1,
)

cc_library(
    name = "ghost_type_tests",
    srcs = ["ghost_type_test.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":ghost_type",
        "//src/tests",
    ],
    alwayslink = 1,
)

cc_library(
    name = "ghost_type_class",
    hdrs = ["ghost_type_class.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "hash",
    hdrs = ["hash.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "hash_tests",
    srcs = ["hash_tests.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":hash",
        "//src/tests",
    ],
    alwayslink = 1,
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
