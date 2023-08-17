cc_library(
    name = "benchmarks",
    srcs = ["benchmarks.cc"],
    hdrs = ["benchmarks.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/infrastructure:time",
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
    name = "tests",
    srcs = ["tests.cc"],
    hdrs = ["tests.h"],
    visibility = ["//visibility:public"],
    deps = ["//src/language:wstring"],
)
