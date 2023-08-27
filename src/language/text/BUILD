cc_library(
    name = "line",
    srcs = [
        "line.cc",
    ],
    hdrs = [
        "line.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":line_column",
        "//src/concurrent:protected",
        "//src/futures",
        "//src/futures:listenable_value",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:observers",
        "//src/language:safe_types",
        "//src/language/lazy_string",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:substring",
    ],
)

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
