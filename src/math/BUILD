load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bigint",
    srcs = ["bigint.cc"],
    hdrs = ["bigint.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":checked_operation",
        "//src/language/error:value_or_error",
        "//src/tests",
    ],
)

cc_library(
    name = "checked_operation",
    srcs = ["checked_operation.cc"],
    hdrs = ["checked_operation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language/error:value_or_error",
        "//src/tests",
    ],
    alwayslink = 1,
)

cc_library(
    name = "decaying_counter",
    srcs = ["decaying_counter.cc"],
    hdrs = ["decaying_counter.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/infrastructure:time",
    ],
)

cc_library(
    name = "naive_bayes",
    srcs = ["naive_bayes.cc"],
    hdrs = ["naive_bayes.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/infrastructure:tracker",
        "//src/language:ghost_type_class",
        "//src/language:wstring",
        "//src/language/error:value_or_error",
        "//src/language/lazy_string",
        "//src/tests",
    ],
)

cc_library(
    name = "numbers",
    srcs = ["numbers.cc"],
    hdrs = ["numbers.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":bigint",
        ":checked_operation",
        "//src/language:overload",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/error:value_or_error",
        "//src/tests",
    ],
)

cc_library(
    name = "graph_non_deterministic",
    hdrs = ["graph_non_deterministic.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":graph_deterministic",
        "//src/language:ghost_type_class",
    ],
)

cc_library(
    name = "graph_deterministic",
    hdrs = ["graph_deterministic.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:ghost_type_class",
    ],
)
