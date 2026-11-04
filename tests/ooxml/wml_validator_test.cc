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

constexpr char kSettingsHeader[] =
    R"(<?xml version="1.0"?><w:settings xmlns:w="http://schemas.openxmlformats.org/)"
    R"(wordprocessingml/2006/main">)";
constexpr char kSettingsFooter[] = "</w:settings>";

std::string Wrap(const std::string& body) {
  return std::string(kHeader) + body + kFooter;
}

std::string WrapSettings(const std::string& body) {
  return std::string(kSettingsHeader) + body + kSettingsFooter;
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

TEST(WmlValidatorTest, RejectsUnknownAttribute) {
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p w:bogus="1"><w:r><w:t>x</w:t></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].path, "/document[1]/body[1]/p[1]");
  EXPECT_NE(violations[0].message.find("bogus"), std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].message.find("attribute"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsInvalidOnOffAttribute) {
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p><w:pPr><w:keepNext w:val="maybe"/></w:pPr>)"
                        R"(<w:r><w:t>x</w:t></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  // The schema table keys attributes by local name, so the message names the
  // offending attribute and the type that rejected the value.
  EXPECT_NE(violations[0].message.find("val"), std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].message.find("OnOff"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsNonNumericIntegerAttribute) {
  // w:spacing/@afterLines is ST_DecimalNumberOrPercent's Int32 row in the
  // SDK metadata (the signed-twips attributes are StringValue because the
  // transitional schema allows "auto", so they are outside this check).
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p><w:pPr><w:spacing w:afterLines="abc"/></w:pPr>)"
                        R"(<w:r><w:t>x</w:t></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("afterLines"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsOutOfRangeIntegerAttribute) {
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(R"(<w:p><w:pPr><w:spacing w:afterLines="3000000000"/></w:pPr>)"
           R"(<w:r><w:t>x</w:t></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("afterLines"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsNegativeUnsignedAttribute) {
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:sectPr><w:pgSz w:code="-1"/></w:sectPr>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("code"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsInvalidHexBinaryAttribute) {
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p w:rsidR="xyz"><w:r><w:t>x</w:t></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("rsidR"), std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].message.find("hexBinary"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsInvalidBase64BinaryAttribute) {
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash="!!!!"/>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("hash"), std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, AcceptsLexicallyValidAttributes) {
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(R"(<w:p w:rsidR="00AB12CD"><w:pPr><w:keepNext w:val="on"/>)"
           R"(<w:spacing w:after="120"/></w:pPr>)"
           R"(<w:r><w:t xml:space="preserve">x</w:t></w:r></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, AcceptsValidBase64BinaryAttribute) {
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash="AA=="/>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, AcceptsOnePadCharacterBase64BinaryAttribute) {
  // "QQ8=" encodes two bytes: length a multiple of four with a single
  // trailing '=' is a legal xsd:base64Binary lexical form.
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash="QQ8="/>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, AcceptsEmptyBase64BinaryAttribute) {
  // xsd:base64Binary's lexical space includes the empty string.
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash=""/>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, RejectsBase64BinaryPaddingNotAtEnd) {
  // "AA=A" has the right length and '=' in the middle, but the byte after
  // the first '=' is not padding.
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash="AA=A"/>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsBase64BinaryPaddingBeforeData) {
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash="A=AA"/>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, RejectsShortBase64BinaryAttribute) {
  // Without padding the length must still be a multiple of four: "A" and
  // "AAA" are base64 alphabet characters but not complete quanta.
  for (const char* value : {"A", "AAA"}) {
    const auto violations = ValidatePart(
        "word/settings.xml",
        WrapSettings(std::string(R"(<w:documentProtection w:hash=")") + value +
                     R"("/>)"));
    ASSERT_EQ(violations.size(), 1u) << value << ": " << Describe(violations);
    EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
        << violations[0].message;
  }
}

TEST(WmlValidatorTest, RejectsBase64BinaryNonZeroPadBits) {
  // "AB==" and "AAB=" have legal shapes, but the discarded low bits of the
  // final quantum are non-zero, which the W3C lexical rule forbids.
  for (const char* value : {"AB==", "AAB="}) {
    const auto violations = ValidatePart(
        "word/settings.xml",
        WrapSettings(std::string(R"(<w:documentProtection w:hash=")") + value +
                     R"("/>)"));
    ASSERT_EQ(violations.size(), 1u) << value << ": " << Describe(violations);
    EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
        << violations[0].message;
  }
}

TEST(WmlValidatorTest, AcceptsWhitespaceSeparatedBase64BinaryAttribute) {
  // The W3C lexical rule strips whitespace between quanta, so a value with
  // leading, trailing or embedded spaces is still valid.
  const auto violations = ValidatePart(
      "word/settings.xml", WrapSettings(R"(<w:documentProtection w:hash=" QUJD "
/>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, RejectsTooShortPaddedBase64BinaryAttribute) {
  // A padded value needs at least two data characters before the padding, so
  // "==" and "=++=" have no final quantum to check and are rejected outright.
  for (const char* value : {"==", "=A", "A="}) {
    const auto violations = ValidatePart(
        "word/settings.xml",
        WrapSettings(std::string(R"(<w:documentProtection w:hash=")") + value +
                     R"("/>)"));
    ASSERT_EQ(violations.size(), 1u) << value << ": " << Describe(violations);
    EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
        << violations[0].message;
  }
}

TEST(WmlValidatorTest, RejectsBase64BinaryOnlyPaddingOrShortTail) {
  for (const char* value : {"====", "A==="}) {
    const auto violations = ValidatePart(
        "word/settings.xml",
        WrapSettings(std::string(R"(<w:documentProtection w:hash=")") + value +
                     R"("/>)"));
    ASSERT_EQ(violations.size(), 1u) << value << ": " << Describe(violations);
    EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
        << violations[0].message;
  }
}

TEST(WmlValidatorTest, IgnoresUnprefixedAttributeDespiteDefaultNamespace) {
  // XML namespaces do not apply to unprefixed attributes, so a default
  // namespace declaration must not pull "bogus" into the WML namespace.
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p xmlns="http://schemas.openxmlformats.org/)"
                        R"(wordprocessingml/2006/main" bogus="1"><w:r>)"
                        R"(<w:t>x</w:t></w:r></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest,
     StillRejectsPrefixedUnknownAttributeWithDefaultNamespace) {
  // Same document shape as the unprefixed case: the w:-prefixed unknown
  // attribute must still be reported.
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p xmlns="http://schemas.openxmlformats.org/)"
                        R"(wordprocessingml/2006/main" w:bogus="1"><w:r>)"
                        R"(<w:t>x</w:t></w:r></w:p>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("bogus"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, AcceptsUnboundedIntegerAttribute) {
  // w:endnoteReference/@id is IntegerValue in the SDK metadata: xsd:integer
  // is unbounded, so magnitudes beyond 19 digits are still lexically valid.
  const auto violations = ValidatePart(
      "word/document.xml", Wrap(R"(<w:p><w:r><w:endnoteReference w:id=")"
                                R"(999999999999999999999999"/></w:r></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, AcceptsZeroPaddedUnboundedIntegerAttribute) {
  const auto violations = ValidatePart(
      "word/document.xml", Wrap(R"(<w:p><w:r><w:endnoteReference w:id=")"
                                R"(000000000000000000000"/></w:r></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, RejectsNonNumericUnboundedIntegerAttribute) {
  // Sign-only, doubled sign, decimal point and empty are not xsd:integer
  // lexical forms.
  for (const char* value : {"+", "-", "++1", "1.0", ""}) {
    const auto violations = ValidatePart(
        "word/document.xml",
        Wrap(std::string(R"(<w:p><w:r><w:endnoteReference w:id=")") + value +
             R"("/></w:r></w:p>)"));
    ASSERT_EQ(violations.size(), 1u) << value << ": " << Describe(violations);
    EXPECT_NE(violations[0].message.find("id"), std::string::npos)
        << violations[0].message;
    // The unknown-attribute path would also name "id": assert the targeted
    // xsd:integer lexical branch was hit, not merely some violation.
    EXPECT_NE(violations[0].message.find("xsd:integer"), std::string::npos)
        << violations[0].message;
  }
}

TEST(WmlValidatorTest, AcceptsSignedUnboundedIntegerAttribute) {
  const auto violations =
      ValidatePart("word/document.xml",
                   Wrap(R"(<w:p><w:r><w:endnoteReference w:id="-42"/></w:r>)"
                        R"(</w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, SkipsXmlReservedAndExtensionAttributes) {
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(R"(<w:p xmlns:w14="http://schemas.microsoft.com/office/word/2010/)"
           R"(wordml" w14:paraId="00AB12CD"><w:r>)"
           R"(<w:t xml:space="preserve">x</w:t></w:r></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, SkipsAttributesFromOtherNamespaces) {
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(
          R"(<w:p><w:hyperlink xmlns:r=")"
          R"(http://schemas.openxmlformats.org/officeDocument/2006/relationships)"
          R"(" r:id="rId1"><w:r><w:t>x</w:t></w:r></w:hyperlink></w:p>)"));

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, ReportsMalformedXml) {
  const auto violations = ValidatePart("word/document.xml", "<w:document>");
  ASSERT_EQ(violations.size(), 1u);
  EXPECT_NE(violations[0].message.find("well-formed"), std::string::npos);
}

}  // namespace
}  // namespace doccraft::ooxml
