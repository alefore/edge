cc_library(
    name = "delete_notification",
    srcs = ["delete_notification.cc"],
    hdrs = ["delete_notification.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":futures",
        ":listenable_value",
    ],
)

cc_library(
    name = "futures",
    srcs = ["futures.cc"],
    hdrs = ["futures.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/concurrent:protected",
        "//src/language:function_traits",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "listenable_value",
    hdrs = ["listenable_value.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "serializer",
    srcs = ["serializer.cc"],
    hdrs = ["serializer.h"],
    deps = [
        ":futures",
    ],
)
