cc_library(
    name = "bisect",
    srcs = ["bisect.cc"],
    hdrs = ["bisect.h"],
    deps = [
        ":composite",
    ],
)

cc_library(
    name = "composite",
    srcs = ["composite.cc"],
    hdrs = ["composite.h"],
    deps = [
        ":stack",
        "//src:buffer_contents",
    ],
)

cc_library(
    name = "cursors",
    srcs = ["cursors.cc"],
    hdrs = ["cursors.h"],
    deps = [
        ":input",
        ":result",
        "//src:cursors",
        "//src/futures",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "delete",
    srcs = ["delete.cc"],
    hdrs = ["delete.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":input",
        ":result",
        "//src:modifiers",
        "//src/infrastructure/screen:line_modifier",
    ],
)

cc_library(
    name = "expand",
    srcs = ["expand.cc"],
    hdrs = ["expand.h"],
    deps = [
        "//src:transformation",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "input",
    hdrs = ["input.h"],
    deps = [
        "//src/infrastructure/screen:line_modifier",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "insert",
    srcs = ["insert.cc"],
    hdrs = ["insert.h"],
    deps = [
        ":composite",
        ":input",
        ":result",
        "//src:modifiers",
        "//src:transformation",
        "//src/infrastructure/screen:line_modifier",
        "//src/language:safe_types",
        "//src/language/text:line_column_vm",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "move",
    srcs = ["move.cc"],
    hdrs = ["move.h"],
    deps = [
        ":input",
        ":result",
        "//src:modifiers",
        "//src/language:safe_types",
    ],
)

cc_library(
    name = "noop",
    srcs = ["noop.cc"],
    hdrs = ["noop.h"],
    deps = [
        ":composite",
    ],
)

cc_library(
    name = "reach_query",
    srcs = ["reach_query.cc"],
    hdrs = ["reach_query.h"],
    deps = [
        ":composite",
        "//src:buffer_contents",
        "//src:buffer_display_data",
    ],
)

cc_library(
    name = "repetitions",
    srcs = ["repetitions.cc"],
    hdrs = ["repetitions.h"],
    deps = [
        ":input",
    ],
)

cc_library(
    name = "result",
    hdrs = ["result.h"],
)

cc_library(
    name = "set_position",
    srcs = ["set_position.cc"],
    hdrs = ["set_position.h"],
    deps = [
        ":composite",
        ":input",
        ":result",
        "//src/futures",
        "//src/language/text:line_column",
        "//src/language/text:line_column_vm",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "stack",
    srcs = ["stack.cc"],
    hdrs = ["stack.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":input",
        ":result",
    ],
)

cc_library(
    name = "switch_case",
    srcs = ["switch_case.cc"],
    hdrs = ["switch_case.h"],
    deps = [
        "//src:modifiers",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "tree_navigate",
    srcs = ["tree_navigate.cc"],
    hdrs = ["tree_navigate.h"],
    deps = [
        "//src:transformation",
    ],
)

cc_library(
    name = "type",
    srcs = ["type.cc"],
    hdrs = ["type.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":cursors",
        ":delete",
        ":set_position",
        "//src/vm/public:environment",
    ],
)

cc_library(
    name = "visual_overlay",
    srcs = ["visual_overlay.cc"],
    hdrs = ["visual_overlay.h"],
    deps = [
        "//src/futures",
    ],
)

cc_library(
    name = "vm",
    srcs = ["vm.cc"],
    hdrs = ["vm.h"],
)
