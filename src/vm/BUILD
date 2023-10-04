cc_library(
    name = "append_expression",
    srcs = ["append_expression.cc"],
    hdrs = ["append_expression.h"],
    deps = [
        ":compilation",
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
        ":value",
        "//src/vm",
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
        ":value",
        "//src/vm",
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
)

cc_library(
    name = "container",
    hdrs = ["container.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":environment",
    ],
)

genrule(
    name = "cpp_c",
    srcs = [
        "cpp.y",
        "lempar.c",
    ],
    outs = ["cpp.c"],
    cmd = "$(location :lemon) -Tsrc/vm/internal/lempar.c src/vm/internal/cpp.y -d$(GENDIR)/src/vm/internal",
    tools = [":lemon"],
)

cc_library(
    name = "environment",
    srcs = ["environment.cc"],
    hdrs = ["environment.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":callbacks",
        ":types",
    ],
)

cc_library(
    name = "escape",
    srcs = ["escape.cc"],
    hdrs = ["escape.h"],
)

cc_library(
    name = "expression",
    hdrs = ["expression.h"],
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
)

cc_binary(
    name = "fuzz",
    srcs = ["fuzz.cc"],
)

cc_library(
    name = "if_expression",
    srcs = ["if_expression.cc"],
    hdrs = ["if_expression.h"],
    deps = [
        ":compilation",
        ":value",
        "//src/vm",
    ],
)

cc_library(
    name = "lambda",
    srcs = ["lambda.cc"],
    hdrs = ["lambda.h"],
    deps = [
        ":compilation",
        ":types_promotion",
        "//src/vm",
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
    ],
)

cc_library(
    name = "time",
    srcs = ["time.cc"],
    hdrs = ["time.h"],
    deps = [
        ":callbacks",
        ":environment",
        "//src/infrastructure:time",
    ],
)

cc_library(
    name = "types_promotion",
    srcs = ["types_promotion.cc"],
    hdrs = ["types_promotion.h"],
    deps = [
        ":callbacks",
        ":environment",
        ":types",
        ":value",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "types",
    srcs = ["types.cc"],
    hdrs = ["types.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:gc",
    ],
)

cc_library(
    name = "value",
    srcs = ["value.cc"],
    hdrs = ["value.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":types",
        "//src/futures",
        "//src/language:gc",
    ],
)

cc_library(
    name = "variable_lookup",
    srcs = ["variable_lookup.cc"],
    hdrs = ["variable_lookup.h"],
    deps = [
        ":compilation",
        ":environment",
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
        ":value",
        "//src/language:safe_types",
        "//src/language/error:value_or_error",
    ],
)
