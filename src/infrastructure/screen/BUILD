cc_library(
    name = "cursors",
    srcs = ["cursors.cc"],
    hdrs = ["cursors.h"],
    visibility = ["//src/transformation:__pkg__"],
    deps = [
        "//src/futures",
        "//src/language/lazy_string:append",
        "//src/language/lazy_string:char_buffer",
        "//src/language/lazy_string:substring",
        "//src/language/text:line_column",
    ],
)

cc_library(
    name = "line_modifier",
    srcs = ["line_modifier.cc"],
    hdrs = ["line_modifier.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/language:hash",
    ],
)

cc_library(
    name = "screen",
    hdrs = ["screen.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "visual_overlay",
    srcs = ["visual_overlay.cc"],
    hdrs = ["visual_overlay.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":line_modifier",
        "//src/language:ghost_type",
        "//src/language/lazy_string",
        "//src/language/text:line",
    ],
)
