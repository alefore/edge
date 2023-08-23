cc_library(
    name = "audio",
    srcs = ["audio.cc"],
    hdrs = ["audio.h"],
    deps = [
        "//src/concurrent:protected",
        "//src/infrastructure:tracker",
        "//src/language:ghost_type",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "command_line",
    srcs = ["command_line.cc"],
    hdrs = ["command_line.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:wstring",
        "//src/tests",
    ],
)

cc_library(
    name = "dirname",
    srcs = ["dirname.cc"],
    hdrs = ["dirname.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:ghost_type",
        "//src/language:value_or_error",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
    ],
)

cc_library(
    name = "file_system_driver",
    srcs = ["file_system_driver.cc"],
    hdrs = ["file_system_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dirname",
        "//src/concurrent:thread_pool",
    ],
)

cc_library(
    name = "time",
    srcs = ["time.cc"],
    hdrs = ["time.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:value_or_error",
    ],
)

cc_library(
    name = "tracker",
    srcs = ["tracker.cc"],
    hdrs = ["tracker.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:protected",
        "//src/infrastructure:time",
    ],
)
