// Copyright 2026 The doccraft Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>

#include "gtest/gtest.h"
#include "ooxml/validator.h"

namespace doccraft::ooxml {
namespace {

constexpr char kHeader[] =
    R"(<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/)"
    R"(wordprocessingml/2006/main"><w:body>)";
constexpr char kFooter[] = "</w:body></w:document>";

std::string Wrap(const std::string& body) {
  return std::string(kHeader) + body + kFooter;
}

std::string Describe(const std::vector<Violation>& violations) {
  if (violations.empty())
    return "no violations";
  std::string text;
  for (const auto& violation : violations) {
    text += violation.path + " -> " + violation.message + "\n";
  }
  return text;
}

TEST(WmlValidatorTest, AcceptsParagraphPropertiesInSchemaOrder) {
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(R"(<w:p><w:pPr><w:pStyle w:val="Normal"/>)"
           R"(<w:spacing w:after="120"/><w:jc w:val="both"/></w:pPr>)"
           R"(<w:r><w:t>hello</w:t></w:r></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, RejectsParagraphPropertiesOutOfOrder) {
  const auto violations = ValidatePart(
      "word/document.xml", Wrap(R"(<w:p><w:pPr><w:jc w:val="both"/>)"
                                R"(<w:pStyle w:val="Normal"/></w:pPr></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].path, "/document[1]/body[1]/p[1]/pPr[1]");
  EXPECT_NE(violations[0].message.find("order"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsElementOutsideItsParent) {
  const auto violations = ValidatePart(
      "word/document.xml", Wrap(R"(<w:p><w:pPr><w:bogus/></w:pPr></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("bogus"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsBlockElementInsideBodyRun) {
  const auto violations = ValidatePart(
      "word/document.xml", Wrap(R"(<w:p><w:r><w:tbl/></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 2u) << Describe(violations);
}

TEST(WmlValidatorTest, IgnoresExtensionNamespaces) {
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(R"(<w:p><w:pPr><w14:paraId xmlns:w14=")"
           R"(http://schemas.microsoft.com/office/word/2010/wordml"/>)"
           R"(</w:pPr></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, ReportsMalformedXml) {
  const auto violations = ValidatePart("word/document.xml", "<w:document>");
  ASSERT_EQ(violations.size(), 1u);
  EXPECT_NE(violations[0].message.find("well-formed"), std::string::npos);
}

}  // namespace
}  // namespace doccraft::ooxml
