cc_library(
    name = "append_expression",
    srcs = ["append_expression.cc"],
    hdrs = ["append_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":value",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "assign_expression",
    srcs = ["assign_expression.cc"],
    hdrs = ["assign_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":types",
        ":value",
    ],
)

cc_library(
    name = "binary_operator",
    srcs = ["binary_operator.cc"],
    hdrs = ["binary_operator.h"],
    deps = [
        ":compilation",
        ":expression",
        ":value",
    ],
)

cc_library(
    name = "callbacks",
    srcs = ["callbacks.cc"],
    hdrs = ["callbacks.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":value",
        "//src/language:function_traits",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "class_expression",
    srcs = ["class_expression.cc"],
    hdrs = ["class_expression.h"],
    deps = [
        ":append_expression",
        ":compilation",
        ":constant_expression",
        ":expression",
        ":value",
    ],
)

cc_library(
    name = "compilation",
    srcs = ["compilation.cc"],
    hdrs = ["compilation.h"],
    deps = [
        ":environment",
        "//src/infrastructure:dirname",
        "//src/language:gc",
    ],
)

cc_library(
    name = "constant_expression",
    srcs = ["constant_expression.cc"],
    hdrs = ["constant_expression.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":expression",
        ":value",
        "//src/language:gc",
    ],
)

cc_library(
    name = "container",
    hdrs = ["container.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":environment",
        ":expression",
    ],
)

genrule(
    name = "cpp_c",
    srcs = [
        "cpp.y",
        "lempar.c",
    ],
    outs = [
        "cpp.c",
        "cpp.h",
    ],
    cmd = """
      mkdir -p $(@D)
      $(location :lemon) \
      -T$(location lempar.c) \
      $(location cpp.y) \
      -d$(@D)
    """,
    tools = [":lemon"],
)

cc_library(
    name = "default_environment",
    srcs = ["default_environment.cc"],
    hdrs = ["default_environment.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":callbacks",
        ":container",
        ":environment",
        ":numbers",
        ":string",
        ":time",
        "//src/futures",
        "//src/language:gc",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
        "//src/math:numbers",
    ],
)

cc_library(
    name = "environment",
    srcs = ["environment.cc"],
    hdrs = ["environment.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":callbacks",
        ":expression",
        ":types",
    ],
)

cc_library(
    name = "escape",
    srcs = ["escape.cc"],
    hdrs = ["escape.h"],
    deps = [
        "//src/language:ghost_type",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
    ],
)

cc_library(
    name = "expression",
    srcs = ["expression.cc"],
    hdrs = ["expression.h"],
    deps = [
        ":types",
        ":value",
        "//src/futures",
        "//src/language:gc",
    ],
)

cc_library(
    name = "filter_similar_names",
    srcs = ["filter_similar_names.cc"],
    hdrs = ["filter_similar_names.h"],
)

cc_library(
    name = "function_call",
    srcs = ["function_call.cc"],
    hdrs = ["function_call.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compilation",
        ":constant_expression",
        ":environment",
        ":expression",
        ":filter_similar_names",
        ":types",
        ":value",
        "//src/futures",
        "//src/language:gc",
    ],
)

cc_binary(
    name = "fuzz",
    srcs = ["fuzz.cc"],
    deps = [
        ":environment",
        ":types",
        ":value",
        ":vm",
        "//src/concurrent:operation",
        "//src/concurrent:thread_pool",
        "//src/futures",
        "//src/language:gc",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "if_expression",
    srcs = ["if_expression.cc"],
    hdrs = ["if_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":value",
    ],
)

cc_library(
    name = "lambda",
    srcs = ["lambda.cc"],
    hdrs = ["lambda.h"],
    deps = [
        ":compilation",
        ":expression",
    ],
)

cc_binary(
    name = "lemon",
    srcs = ["lemon.c"],
    copts = [
        "-std=c99",
        "-Wno-error=range-loop-construct",
    ],
)

cc_library(
    name = "logical_expression",
    srcs = ["logical_expression.cc"],
    hdrs = ["logical_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":value",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "namespace_expression",
    srcs = ["namespace_expression.cc"],
    hdrs = ["namespace_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":value",
    ],
)

cc_library(
    name = "negate_expression",
    srcs = ["negate_expression.cc"],
    hdrs = ["negate_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":types",
        ":value",
        "//src/language/error:value_or_error",
    ],
)

cc_library(
    name = "numbers",
    srcs = ["numbers.cc"],
    hdrs = ["numbers.h"],
    deps = [
        ":callbacks",
        ":environment",
    ],
)

cc_library(
    name = "optional",
    hdrs = ["optional.h"],
)

cc_library(
    name = "return_expression",
    srcs = ["return_expression.cc"],
    hdrs = ["return_expression.h"],
    deps = [
        ":compilation",
        ":expression",
        ":value",
    ],
)

cc_library(
    name = "string",
    srcs = ["string.cc"],
    hdrs = ["string.h"],
    deps = [
        ":callbacks",
        ":container",
        ":expression",
    ],
)

cc_library(
    name = "time",
    srcs = ["time.cc"],
    hdrs = ["time.h"],
    deps = [
        ":callbacks",
        ":environment",
        ":expression",
        "//src/infrastructure:time",
    ],
)

cc_library(
    name = "types_promotion_tests",
    srcs = ["types_promotion_tests.cc"],
    deps = [
        ":default_environment",
        "//src/tests",
    ],
)

cc_library(
    name = "types",
    srcs = ["types.cc"],
    hdrs = ["types.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:gc",
        "//src/language:hash",
    ],
)

cc_library(
    name = "value",
    srcs = ["value.cc"],
    hdrs = ["value.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":escape",
        ":types",
        "//src/futures",
        "//src/language:gc",
        "//src/language:gc_util",
        "//src/language/lazy_string:char_buffer",
        "//src/math:numbers",
    ],
)

cc_library(
    name = "variable_lookup",
    srcs = ["variable_lookup.cc"],
    hdrs = ["variable_lookup.h"],
    deps = [
        ":compilation",
        ":environment",
        ":expression",
        ":value",
    ],
)

cc_library(
    name = "vm",
    srcs = ["vm.cc"],
    # The `cpp.c` file is #included directly from `vm.cc`.
    hdrs = [
        "cpp.c",
        "cpp.h",
        "vm.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":append_expression",
        ":assign_expression",
        ":binary_operator",
        ":class_expression",
        ":compilation",
        ":cpp_c",
        ":function_call",
        ":if_expression",
        ":lambda",
        ":logical_expression",
        ":namespace_expression",
        ":negate_expression",
        ":return_expression",
        ":types",
        ":variable_lookup",
        ":while_expression",
        "//src/futures",
        "//src/infrastructure:dirname",
        "//src/language:gc",
    ],
)

cc_library(
    name = "while_expression",
    srcs = ["while_expression.cc"],
    hdrs = ["while_expression.h"],
    deps = [
        ":append_expression",
        ":expression",
        ":value",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
    ],
)
