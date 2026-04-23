#include "src/infrastructure/dirname.h"
#include "src/tests/tests.h"

using afc::language::Error;
using afc::language::FromByteString;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::FindLastOf;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;

namespace afc::infrastructure {
namespace {
const bool path_component_constructor_good_inputs_tests_registration =
    tests::Register(
        L"PathComponentConstructorGoodInputs",
        {
            {.name = L"Simple",
             .callback = [] { PathComponent{LazyString{L"foo"}}; }},
            {.name = L"WithExtension",
             .callback = [] { PathComponent{LazyString{L"foo.md"}}; }},
        });

const bool path_component_constructor_bad_inputs_tests_registration =
    tests::Register(
        L"PathComponentConstructorBadInputs",
        {
            {.name = L"Empty",
             .callback =
                 [] { CHECK(IsError(PathComponent::New(LazyString{}))); }},
            {.name = L"EmptyCrash",
             .callback =
                 [] {
                   tests::ForkAndWaitForFailure(
                       [] { PathComponent{LazyString{}}; });
                 }},
            {.name = L"TooLarge",
             .callback =
                 [] {
                   CHECK(IsError(PathComponent::New(LazyString{L"foo/bar"})));
                 }},
            {.name = L"TooLargeCrash",
             .callback =
                 [] {
                   tests::ForkAndWaitForFailure(
                       [] { PathComponent{LazyString{L"foo/bar"}}; });
                 }},

        });

const bool path_component_with_extension_tests_registration = tests::Register(
    L"PathComponentWithExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo"), LazyString{L"md"}),
                     PathComponent::FromString(L"foo.md"));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK_EQ(PathComponent::WithExtension(
                         PathComponent::FromString(L"foo"), LazyString{}),
                     PathComponent::FromString(L"foo."));
          }},
     {.name = L"Present",
      .callback =
          [] {
            CHECK_EQ(
                PathComponent::WithExtension(
                    PathComponent::FromString(L"foo.txt"), LazyString{L"md"}),
                PathComponent::FromString(L"foo.md"));
          }},
     {.name = L"MultipleReplacesOnlyLast", .callback = [] {
        CHECK_EQ(
            PathComponent::WithExtension(
                PathComponent::FromString(L"foo.blah.txt"), LazyString{L"md"}),
            PathComponent::FromString(L"foo.blah.md"));
      }}});

const bool path_component_remove_extension_tests_registration = tests::Register(
    L"PathComponentRemoveExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK(
                ValueOrDie(PathComponent::FromString(L"foo").remove_extension(),
                           L"tests") == PathComponent::FromString(L"foo"));
          }},
     {.name = L"hidden",
      .callback =
          [] {
            CHECK(std::holds_alternative<Error>(
                PathComponent::FromString(L".blah").remove_extension()));
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(ValueOrDie(
                      PathComponent::FromString(L"foo.").remove_extension(),
                      L"tests") == PathComponent::FromString(L"foo"));
          }},
     {.name = L"Present", .callback = [] {
        CHECK(
            ValueOrDie(PathComponent::FromString(L"foo.md").remove_extension(),
                       L"tests") == PathComponent::FromString(L"foo"));
      }}});

const bool path_component_extension_tests_registration = tests::Register(
    L"PathComponentExtension",
    {{.name = L"Absent",
      .callback =
          [] {
            CHECK(!PathComponent::FromString(L"foo").extension().has_value());
          }},
     {.name = L"Empty",
      .callback =
          [] {
            CHECK(
                PathComponent::FromString(L"foo.").extension().value().empty());
          }},
     {.name = L"Present", .callback = [] {
        CHECK(PathComponent::FromString(L"foo.md").extension().value() ==
              LazyString{L"md"});
      }}});

const bool path_join_tests_registration = tests::Register(
    L"PathJoinTests",
    {{.name = L"LocalRedundant",
      .callback =
          [] {
            CHECK_EQ(Path::Join(Path::LocalDirectory(),
                                ValueOrDie(Path::New(LazyString{L"alejo.txt"}),
                                           L"tests")),
                     ValueOrDie(Path::New(LazyString{L"alejo.txt"}), L"tests"));
          }},
     {.name = L"LocalImportant",
      .callback =
          [] {
            CHECK_EQ(
                Path::Join(
                    Path::LocalDirectory(),
                    ValueOrDie(Path::New(LazyString{L"/alejo.txt"}), L"tests")),
                ValueOrDie(Path::New(LazyString{L"/alejo.txt"}), L"tests"));
          }},
     {.name = L"RootRedundant", .callback = [] {
        CHECK_EQ(Path::Join(Path::Root(),
                            ValueOrDie(Path::New(LazyString{L"/foo/bar"}))),
                 ValueOrDie(Path::New(LazyString{L"/foo/bar"})));
      }}});

const bool expand_home_directory_tests_registration = tests::Register(
    L"ExpandHomeDirectoryTests",
    {
        {.name = L"NoExpansionRelative",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"foo/bar"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"foo/bar"}), L"tests"));
             }},
        {.name = L"NoExpansionAbsoluteTilde",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"/~/bar"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/~/bar"}), L"tests"));
             }},
        {.name = L"NoExpansionAbsolute",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"/foo/bar"}),
                                  L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/foo/bar"}), L"tests"));
             }},
        {.name = L"MinimalExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo"}), L"tests"));
             }},
        {.name = L"SmallExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~/"}), L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo"}), L"tests"));
             }},
        {.name = L"LongExpansion",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~/foo/bar"}),
                                  L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo/foo/bar"}),
                              L"tests"));
             }},
        {.name = L"LongExpansionRedundantSlash",
         .callback =
             [] {
               CHECK_EQ(
                   Path::ExpandHomeDirectory(
                       ValueOrDie(Path::New(LazyString{L"/home/alejo/"}),
                                  L"tests"),
                       ValueOrDie(Path::New(LazyString{L"~/foo/bar"}),
                                  L"tests")),
                   ValueOrDie(Path::New(LazyString{L"/home/alejo/foo/bar"}),
                              L"tests"));
             }},
    });

const bool directory_split_tests_registration = tests::Register(
    L"DirectorySplitTests",
    {
        {.name = L"NoSplit",
         .callback =
             [] {
               std::list<PathComponent> result = ValueOrDie(
                   ValueOrDie(Path::New(LazyString{L"alejo.txt"}), L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(),
                        PathComponent::FromString(L"alejo.txt"));
             }},
        {.name = L"Directory",
         .callback =
             [] {
               auto result = ValueOrDie(
                   ValueOrDie(Path::New(LazyString{L"alejo/"}), L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result.size(), 1ul);
               CHECK_EQ(result.front(), PathComponent::FromString(L"alejo"));
             }},
        {.name = L"LongSplit",
         .callback =
             [] {
               auto result_list = ValueOrDie(
                   Path{ValueOrDie(Path::New(LazyString{L"aaa/b/cc/ddd"}),
                                   L"tests")}
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], PathComponent::FromString(L"aaa"));
               CHECK_EQ(result[1], PathComponent::FromString(L"b"));
               CHECK_EQ(result[2], PathComponent::FromString(L"cc"));
               CHECK_EQ(result[3], PathComponent::FromString(L"ddd"));
             }},
        {.name = L"LongSplitMultiSlash",
         .callback =
             [] {
               auto result_list = ValueOrDie(
                   ValueOrDie(Path::New(LazyString{L"aaa////b////cc/////ddd"}),
                              L"tests")
                       .DirectorySplit(),
                   L"tests");
               CHECK_EQ(result_list.size(), 4ul);
               std::vector<PathComponent> result(result_list.begin(),
                                                 result_list.end());
               CHECK_EQ(result[0], PathComponent::FromString(L"aaa"));
               CHECK_EQ(result[1], PathComponent::FromString(L"b"));
               CHECK_EQ(result[2], PathComponent::FromString(L"cc"));
               CHECK_EQ(result[3], PathComponent::FromString(L"ddd"));
             }},
    });

}  // namespace
}  // namespace afc::infrastructure
