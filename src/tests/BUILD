cc_library(
    name = "benchmarks",
    srcs = ["benchmarks.cc"],
    hdrs = ["benchmarks.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/infrastructure:time",
        "//src/language:container",
        "//src/language:wstring",
    ],
)

cc_binary(
    name = "benchmarks_main",
    srcs = ["benchmarks_main.cc"],
    deps = [
        ":benchmarks",
        "@com_github_google_glog//:glog",
    ],
)

cc_library(
    name = "concurrent_interfaces",
    srcs = ["concurrent_interfaces.cc"],
    hdrs = ["concurrent_interfaces.h"],
    visibility = ["//visibility:public"],
    deps = [
    ],
)

cc_library(
    name = "fuzz",
    srcs = ["fuzz.cc"],
    hdrs = ["fuzz.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":fuzz_testable",
        "//src/language/lazy_string",
    ],
)

cc_library(
    name = "fuzz_testable",
    srcs = ["fuzz_testable.cc"],
    hdrs = ["fuzz_testable.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:wstring",
        "@com_github_google_glog//:glog",
    ],
)

cc_library(
    name = "tests",
    srcs = ["tests.cc"],
    hdrs = ["tests.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:container",
        "//src/language:wstring",
    ],
)
