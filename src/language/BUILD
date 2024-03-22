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
        ":safe_types",
        "//src/futures:delete_notification",
        "//src/tests",
    ],
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
        "@com_github_google_glog//:glog",
    ],
)
