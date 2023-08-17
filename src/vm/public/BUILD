cc_library(
    name = "callbacks",
    hdrs = ["callbacks.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":value",
        "//src/language:function_traits",
        "//src/language:value_or_error",
    ],
)

cc_library(
    name = "constant_expression",
    hdrs = ["constant_expression.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "container",
    hdrs = ["container.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":environment",
    ],
)

cc_library(
    name = "environment",
    hdrs = ["environment.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":types",
    ],
)

cc_library(
    name = "escape",
    hdrs = ["escape.h"],
)

cc_library(
    name = "function_call",
    hdrs = ["function_call.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "optional",
    hdrs = ["optional.h"],
)

cc_library(
    name = "types",
    hdrs = ["types.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:gc",
    ],
)

cc_library(
    name = "value",
    hdrs = ["value.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":types",
        ":vm",
        "//src/futures",
        "//src/language:gc",
    ],
)

cc_library(
    name = "vm",
    hdrs = ["vm.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":types",
        "//src/futures",
        "//src/infrastructure:dirname",
        "//src/language:gc",
    ],
)
