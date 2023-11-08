cc_library(
    name = "checked_operation",
    hdrs = ["checked_operation.h"],
    visibility = ["//visibility:public"],
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
        "//src/language:ghost_type",
        "//src/language:wstring",
    ],
)

cc_library(
    name = "numbers",
    srcs = ["numbers.cc"],
    hdrs = ["numbers.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":checked_operation",
        "//src/language:overload",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/error:value_or_error",
        "//src/tests",
    ],
)
