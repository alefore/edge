#include "src/tokenize.h"

#include <glog/logging.h>

#include "src/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {

inline std::ostream& operator<<(std::ostream& out, const std::wstring& str) {
  return out;
}

const bool tokenize_by_spaces_tests_registration = tests::Register(
    L"TokenizeBySpaces",
    {{.name = L"EmptyString",
      .callback =
          [] { CHECK_EQ(TokenizeBySpaces(*EmptyString()).size(), 0ul); }},
     {.name = L"SingleToken",
      .callback =
          [] {
            auto value = TokenizeBySpaces(*NewLazyString(L"alejandro"));
            CHECK_EQ(value.size(), 1ul);
            // TODO: Why can't we use CHECK_EQ? Why can't the compiler find
            // the operator<<?
            CHECK(value[0].value == L"alejandro");
            CHECK_EQ(value[0].begin, ColumnNumber(0));
            CHECK_EQ(value[0].end, ColumnNumber(9));
          }},
     {.name = L"ThreeSimpleTokens",
      .callback =
          [] {
            auto value =
                TokenizeBySpaces(*NewLazyString(L"alejandro forero cuervo"));
            CHECK_EQ(value.size(), 3ul);

            CHECK(value[0].value == L"alejandro");
            CHECK_EQ(value[0].begin, ColumnNumber(0));
            CHECK_EQ(value[0].end, ColumnNumber(9));

            CHECK(value[1].value == L"forero");
            CHECK_EQ(value[1].begin, ColumnNumber(10));
            CHECK_EQ(value[1].end, ColumnNumber(16));

            CHECK(value[2].value == L"cuervo");
            CHECK_EQ(value[2].begin, ColumnNumber(17));
            CHECK_EQ(value[2].end, ColumnNumber(23));
          }},
     {.name = L"SpaceSurroundedSingleToken",
      .callback =
          [] {
            auto value = TokenizeBySpaces(*NewLazyString(L"  alejandro  "));
            CHECK_EQ(value.size(), 1ul);
            CHECK(value[0].value == L"alejandro");
            CHECK_EQ(value[0].begin, ColumnNumber(2));
            CHECK_EQ(value[0].end, ColumnNumber(11));
          }},
     {.name = L"MultipleSpacesBetweenTokens",
      .callback =
          [] {
            auto value = TokenizeBySpaces(
                *NewLazyString(L"  alejandro   forero   cuervo   "));
            CHECK_EQ(value.size(), 3ul);

            CHECK(value[0].value == L"alejandro");
            CHECK_EQ(value[0].begin, ColumnNumber(2));
            CHECK_EQ(value[0].end, ColumnNumber(11));

            CHECK(value[1].value == L"forero");
            CHECK_EQ(value[1].begin, ColumnNumber(14));
            CHECK_EQ(value[1].end, ColumnNumber(20));

            CHECK(value[2].value == L"cuervo");
            CHECK_EQ(value[2].begin, ColumnNumber(23));
            CHECK_EQ(value[2].end, ColumnNumber(29));
          }},
     {.name = L"SingleQuotedString",
      .callback =
          [] {
            auto value = TokenizeBySpaces(*NewLazyString(L"\"alejandro\""));
            CHECK_EQ(value.size(), 1ul);

            CHECK(value[0].value == L"alejandro");
            CHECK_EQ(value[0].begin, ColumnNumber(0));
            CHECK_EQ(value[0].end, ColumnNumber(11));
          }},
     {.name = L"SpaceSurroundedSingleQuotedString",
      .callback =
          [] {
            auto value = TokenizeBySpaces(*NewLazyString(L"  \"alejandro\"  "));
            CHECK_EQ(value.size(), 1ul);

            CHECK(value[0].value == L"alejandro");
            CHECK_EQ(value[0].begin, ColumnNumber(2));
            CHECK_EQ(value[0].end, ColumnNumber(13));
          }},
     {.name = L"MultiWordQuotedString",
      .callback =
          [] {
            auto value = TokenizeBySpaces(
                *NewLazyString(L"\"alejandro forero cuervo\""));
            CHECK_EQ(value.size(), 1ul);

            CHECK(value[0].value == L"alejandro forero cuervo");
            CHECK_EQ(value[0].begin, ColumnNumber(0));
            CHECK_EQ(value[0].end, ColumnNumber(25));
          }},
     {.name = L"SeveralQuotedStrings",
      .callback =
          [] {
            auto value = TokenizeBySpaces(*NewLazyString(
                L"\"a l e j a n d r o\"   \"f o r e r o\" cuervo"));
            CHECK_EQ(value.size(), 3ul);

            CHECK(value[0].value == L"a l e j a n d r o");
            CHECK_EQ(value[0].begin, ColumnNumber(0));
            CHECK_EQ(value[0].end, ColumnNumber(19));

            CHECK(value[1].value == L"f o r e r o");
            CHECK_EQ(value[1].begin, ColumnNumber(22));
            CHECK_EQ(value[1].end, ColumnNumber(35));

            CHECK(value[2].value == L"cuervo");
            CHECK_EQ(value[2].begin, ColumnNumber(36));
            CHECK_EQ(value[2].end, ColumnNumber(42));
          }},
     {.name = L"RunawayQuote", .callback = [] {
        auto value = TokenizeBySpaces(*NewLazyString(L"alejandro for\"ero"));
        CHECK_EQ(value.size(), 2ul);

        CHECK(value[0].value == L"alejandro");
        CHECK_EQ(value[0].begin, ColumnNumber(0));
        CHECK_EQ(value[0].end, ColumnNumber(9));

        CHECK(value[1].value == L"forero");
        CHECK_EQ(value[1].begin, ColumnNumber(10));
        CHECK_EQ(value[1].end, ColumnNumber(17));
      }}});
}  // namespace
}  // namespace afc::editor
