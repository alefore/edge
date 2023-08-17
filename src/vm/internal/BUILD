cc_binary(
    name = "lemon",
    srcs = ["lemon.c"],
    copts = [
        "-std=c99",
        "-Wno-error=range-loop-construct",
    ],
)

cc_library(
    name = "append_expression",
    srcs = ["append_expression.cc"],
    hdrs = ["append_expression.h"],
    deps = [
        ":compilation",
        "//src/language:safe_types",
        "//src/language:value_or_error",
        "//src/vm/public:value",
    ],
)

cc_library(
    name = "assign_expression",
    srcs = ["assign_expression.cc"],
    hdrs = ["assign_expression.h"],
    deps = [
        ":compilation",
        "//src/vm/public:types",
        "//src/vm/public:value",
    ],
)

cc_library(
    name = "binary_operator",
    srcs = ["binary_operator.cc"],
    hdrs = ["binary_operator.h"],
    deps = [
        ":compilation",
        "//src/vm/public:value",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "class_expression",
    srcs = ["class_expression.cc"],
    hdrs = ["class_expression.h"],
    deps = [
        ":append_expression",
        ":compilation",
        "//src/vm/public:constant_expression",
        "//src/vm/public:value",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "compilation",
    srcs = ["compilation.cc"],
    hdrs = ["compilation.h"],
    deps = [
        "//src/infrastructure:dirname",
        "//src/language:gc",
        "//src/vm/public:environment",
        "//src/vm/public:vm",
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
    name = "filter_similar_names",
    srcs = ["filter_similar_names.cc"],
    hdrs = ["filter_similar_names.h"],
)

cc_library(
    name = "if_expression",
    srcs = ["if_expression.cc"],
    hdrs = ["if_expression.h"],
    deps = [
        ":compilation",
        "//src/vm/public:value",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "lambda",
    srcs = ["lambda.cc"],
    hdrs = ["lambda.h"],
    deps = [
        ":compilation",
        ":types_promotion",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "logical_expression",
    srcs = ["logical_expression.cc"],
    hdrs = ["logical_expression.h"],
    deps = [
        ":compilation",
        "//src/language:safe_types",
        "//src/language:value_or_error",
        "//src/vm/public:value",
    ],
)

cc_library(
    name = "namespace_expression",
    srcs = ["namespace_expression.cc"],
    hdrs = ["namespace_expression.h"],
    deps = [
        ":compilation",
        "//src/vm/public:value",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "negate_expression",
    srcs = ["negate_expression.cc"],
    hdrs = ["negate_expression.h"],
    deps = [
        ":compilation",
        "//src/language:value_or_error",
        "//src/vm/public:types",
        "//src/vm/public:value",
    ],
)

cc_library(
    name = "numbers",
    srcs = ["numbers.cc"],
    hdrs = ["numbers.h"],
    deps = [
        "//src/vm/public:callbacks",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "return_expression",
    srcs = ["return_expression.cc"],
    hdrs = ["return_expression.h"],
    deps = [
        ":compilation",
        "//src/vm/public:value",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "string",
    srcs = ["string.cc"],
    hdrs = ["string.h"],
    deps = [
        "//src/vm/public:callbacks",
        "//src/vm/public:container",
    ],
)

cc_library(
    name = "time",
    srcs = ["time.cc"],
    hdrs = ["time.h"],
    deps = [
        "//src/infrastructure:time",
        "//src/vm/public:callbacks",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "types_promotion",
    srcs = ["types_promotion.cc"],
    hdrs = ["types_promotion.h"],
    deps = [
        "//src/language:safe_types",
        "//src/vm/public:callbacks",
        "//src/vm/public:environment",
        "//src/vm/public:types",
        "//src/vm/public:value",
    ],
)

cc_library(
    name = "variable_lookup",
    srcs = ["variable_lookup.cc"],
    hdrs = ["variable_lookup.h"],
    deps = [
        ":compilation",
        "//src/vm/public:environment",
        "//src/vm/public:value",
    ],
)

cc_library(
    name = "vm",
    srcs = ["vm.cc"],
    # The `cpp.c` file is #included directly from `vm.cc`.
    hdrs = [
        "cpp.c",
        "cpp.h",
    ],
    deps = [
        ":append_expression",
        ":assign_expression",
        ":binary_operator",
        ":class_expression",
        ":compilation",
        ":cpp_c",
        ":if_expression",
        ":lambda",
        ":logical_expression",
        ":namespace_expression",
        ":negate_expression",
        ":return_expression",
        ":variable_lookup",
        ":while_expression",
        "//src/vm/public:function_call",
        "//src/vm/public:vm",
    ],
)

cc_library(
    name = "while_expression",
    srcs = ["while_expression.cc"],
    hdrs = ["while_expression.h"],
    deps = [
        ":append_expression",
        "//src/language:safe_types",
        "//src/language:value_or_error",
        "//src/vm/public:value",
    ],
)
