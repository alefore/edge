cc_library(
    name = "bag",
    srcs = ["bag.cc"],
    hdrs = ["bag.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operation",
        "//src/infrastructure:tracker",
    ],
)

cc_library(
    name = "operation",
    srcs = ["operation.cc"],
    hdrs = ["operation.h"],
    deps = [
        ":protected",
        ":thread_pool",
    ],
)

cc_library(
    name = "protected",
    srcs = ["protected.cc"],
    hdrs = ["protected.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/tests",
    ],
)

cc_library(
    name = "thread_pool",
    srcs = ["thread_pool.cc"],
    hdrs = ["thread_pool.h"],
    visibility = ["//visibility:public"],
    deps = [":work_queue"],
)

cc_library(
    name = "work_queue",
    srcs = ["work_queue.cc"],
    hdrs = ["work_queue.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":protected",
        "//src/futures:delete_notification",
        "//src/infrastructure:time",
        "//src/language:observers",
        "//src/math:decaying_counter",
    ],
)
