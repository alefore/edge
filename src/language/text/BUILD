cc_library(
    name = "line_column",
    srcs = ["line_column.cc"],
    hdrs = ["line_column.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:hash",
        "//src/language:safe_types",
        "//src/language:wstring",
        "//src/language/error:value_or_error",
        "//src/language/lazy_string",
        "//src/language/lazy_string:char_buffer",
        "//src/tests:fuzz",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "line_column_vm",
    srcs = ["line_column_vm.cc"],
    hdrs = ["line_column_vm.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:safe_types",
        "//src/language/text:line_column",
        "//src/vm/public:callbacks",
        "//src/vm/public:container",
    ],
)
