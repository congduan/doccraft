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

TEST(WmlValidatorTest, AcceptsLeadingZeroIntegersBeyondNineteenDigits) {
  // Leading zeros carry no magnitude: a 22-character value that spells 1 is
  // in Int32 range, and an all-zero string of any length is a valid 0. The
  // range gate must count significant digits, not characters.
  for (const char* value :
       {"0000000000000000000001", "-00000000000000000001",
        "000000000000000000000", "00000000000000000000000000000000000"}) {
    const auto violations = ValidatePart(
        "word/document.xml",
        Wrap(std::string(R"(<w:p><w:pPr><w:spacing w:afterLines=")") + value +
             R"("/></w:pPr>)"
             R"(<w:r><w:t>x</w:t></w:r></w:p>)"));
    EXPECT_TRUE(violations.empty()) << value << ": " << Describe(violations);
  }
}

TEST(WmlValidatorTest, RejectsLeadingZeroIntegersStillOutOfRange) {
  // Twenty-five significant digits stay a range violation even when padded
  // with leading zeros.
  const auto violations = ValidatePart(
      "word/document.xml",
      Wrap(R"(<w:p><w:pPr><w:spacing w:afterLines=)"
           R"("0000000000000000000000001000000000000000000000001"/>)"
           R"(</w:pPr><w:r><w:t>x</w:t></w:r></w:p>)"));

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
  // "AA==" and "QQ==" encode one byte with zero pad bits.
  for (const char* value : {"AA==", "QQ=="}) {
    const auto violations = ValidatePart(
        "word/settings.xml",
        WrapSettings(std::string(R"(<w:documentProtection w:hash=")") + value +
                     R"("/>)"));
    EXPECT_TRUE(violations.empty()) << value << ": " << Describe(violations);
  }
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
  // leading, trailing or embedded spaces is still valid. " \t\n " strips to
  // the empty string, which is in the lexical space. The refs sample builds
  // its whitespace from character references, so the raw tab/newline/CR
  // characters reach the validator inside the attribute value.
  const std::string mixed = std::string("\tQU\nJD\r ");
  const std::string refs = std::string("&#9;QU&#10;JD&#13; ");
  for (const std::string& value : {std::string(" QUJD "), std::string("QU JD"),
                                   mixed, refs, std::string(" \t\n ")}) {
    const auto violations = ValidatePart(
        "word/settings.xml",
        WrapSettings(std::string(R"(<w:documentProtection w:hash=")") + value +
                     R"("/>)"));
    EXPECT_TRUE(violations.empty()) << value << ": " << Describe(violations);
  }
}

TEST(WmlValidatorTest, RejectsUnpaddedBase64BinaryLengthNotMultipleOfFour) {
  // "QUJDRA" has no padding and its length is not a multiple of four.
  const auto violations =
      ValidatePart("word/settings.xml",
                   WrapSettings(R"(<w:documentProtection w:hash="QUJDRA"/>)"));

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("base64Binary"), std::string::npos)
      << violations[0].message;
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

constexpr char kRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr char kPackageRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/package/2006/relationships";

std::string Rels(const std::string& body) {
  return std::string(R"(<?xml version="1.0"?><Relationships xmlns=")") +
         kPackageRelationshipsNamespace + R"(">)" + body + "</Relationships>";
}

constexpr char kRelationshipTypeBase[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";

std::string Relationship(const std::string& id, const std::string& type,
                         const std::string& target,
                         const std::string& mode = std::string()) {
  std::string out = "<Relationship Id=\"" + id + "\" Type=\"" +
                    kRelationshipTypeBase + type + "\" Target=\"" + target +
                    "\"";
  if (!mode.empty())
    out += " TargetMode=\"" + mode + "\"";
  out += "/>";
  return out;
}

std::string DrawingWithEmbed(const std::string& rid) {
  return Wrap(std::string(R"(<w:p><w:r><w:drawing><a:blip xmlns:a=")"
                          R"(http://schemas.openxmlformats.org/drawingml/2006/)"
                          R"(main" xmlns:r=")") +
              kRelationshipsNamespace + R"(" r:embed=")" + rid +
              R"("/></w:drawing></w:r></w:p>)");
}

TEST(WmlValidatorTest, DanglingRelationshipIdReportsViolation) {
  const std::string document = DrawingWithEmbed("rId9");
  const std::string rels =
      Rels(Relationship("rId1", "image", "media/image1.png"));
  const std::string image = "not-xml";

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
      {"word/media/image1.png", image},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].part, "word/document.xml");
  EXPECT_NE(violations[0].message.find("rId9"), std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].path.find("drawing"), std::string::npos)
      << violations[0].path;
}

TEST(WmlValidatorTest, DanglingInternalRelationshipTargetReportsViolation) {
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId4", "image", "media/missing.png"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].part, "word/_rels/document.xml.rels");
  EXPECT_NE(violations[0].message.find("media/missing.png"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, ExternalRelationshipTargetNeedsNoPart) {
  const std::string document = Wrap(
      std::string("<w:p><w:hyperlink xmlns:r=\"") + kRelationshipsNamespace +
      "\" r:id=\"rId2\"><w:r><w:t>x</w:t></w:r></w:hyperlink></w:p>");
  const std::string rels = Rels(Relationship(
      "rId2", "hyperlink", "https://example.com/page", "External"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, DuplicateRelationshipIdReportsViolation) {
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId5", "hyperlink", "https://a/", "External") +
           Relationship("rId5", "hyperlink", "https://b/", "External"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].part, "word/_rels/document.xml.rels");
  EXPECT_NE(violations[0].message.find("rId5"), std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].message.find("duplicate"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, EmptyRelationshipTargetReportsViolation) {
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels = Rels(Relationship("rId3", "image", std::string()));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("empty"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, ResolvesRelativeTargetFromPartDirectory) {
  // A header lives in word/, so media/image1.png resolves to
  // word/media/image1.png.
  const std::string header =
      std::string(
          "<?xml version=\"1.0\"?><w:hdr xmlns:w=\""
          "http://schemas.openxmlformats.org/wordprocessingml/2006/"
          "main\" xmlns:r=\"") +
      kRelationshipsNamespace +
      "\"><w:p><w:hyperlink r:id=\"rId1\"><w:r><w:t>x</w:t></w:r>"
      "</w:hyperlink></w:p></w:hdr>";
  const std::string rels =
      Rels(Relationship("rId1", "image", "media/image1.png"));
  const std::string image = "not-xml";

  const auto violations = ValidatePackage({
      {"word/header1.xml", header},
      {"word/_rels/header1.xml.rels", rels},
      {"word/media/image1.png", image},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, ResolvesParentRelativeTarget) {
  // A part one directory down climbs back to the shared media directory.
  const std::string header =
      "<?xml version=\"1.0\"?><w:hdr xmlns:w=\"http://schemas."
      "openxmlformats.org/wordprocessingml/2006/main\"><w:p/></w:hdr>";
  const std::string rels =
      Rels(Relationship("rId1", "image", "../media/image1.png"));
  const std::string image = "not-xml";

  const auto violations = ValidatePackage({
      {"word/chapters/header1.xml", header},
      {"word/chapters/_rels/header1.xml.rels", rels},
      {"word/media/image1.png", image},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, FragmentTargetResolvesToExistingPart) {
  // A fragment selects an anchor inside the target part; only the part name
  // before '#' takes part in existence checking.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId1", "image", "document.xml#section1"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, FragmentOnlyTargetResolvesToOwnerPart) {
  // RFC 3986 §5.2.2: an empty relative path inherits the base path, so a
  // Target of "#section1" names the part that owns the .rels file.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels = Rels(Relationship("rId1", "image", "#section1"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, FragmentOnlyTargetStillNeedsOwnerPart) {
  // Inheritance names word/document.xml; without that part the Target is
  // dangling even though it carries only a fragment.
  const std::string rels = Rels(Relationship("rId1", "image", "#section1"));

  const auto violations = ValidatePackage({
      {"word/_rels/document.xml.rels", rels},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_NE(violations[0].message.find("word/document.xml"), std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, QueryTargetResolvesToExistingPart) {
  // A query is a URI component, not part of the part name (RFC 3986 §3.4):
  // "document.xml?rev=1" names word/document.xml.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId1", "image", "document.xml?rev=1"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, QueryAndFragmentTargetResolvesToExistingPart) {
  // The query precedes the fragment; both delimiters are stripped before the
  // path is resolved.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId1", "image", "document.xml?a=1#section1"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, PercentEncodedTargetResolvesToExistingPart) {
  // OPC stores the space in "image one.png" as "%20" in the relationship
  // Target; the decoded name is what has to match the package entry.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId1", "image", "media/image%20one.png"));
  const std::string image = "not-xml";

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
      {"word/media/image one.png", image},
  });

  EXPECT_TRUE(violations.empty()) << Describe(violations);
}

TEST(WmlValidatorTest, PercentEncodedMissingTargetReportsViolation) {
  // Decoding must not make a missing part look present: there is no
  // "media/image two.png" entry in the package.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId1", "image", "media/image%20two.png"));

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].part, "word/_rels/document.xml.rels");
  EXPECT_NE(violations[0].message.find("media/image%20two.png"),
            std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, TargetEscapingPackageRootReportsViolation) {
  // The owner part lives in word/, so the first ".." reaches the package
  // root and the second one climbs past it; OPC part names must stay inside.
  const std::string document = Wrap(R"(<w:p><w:r><w:t>x</w:t></w:r></w:p>)");
  const std::string rels =
      Rels(Relationship("rId1", "image", "../../media/image1.png"));
  const std::string image = "not-xml";

  const auto violations = ValidatePackage({
      {"word/document.xml", document},
      {"word/_rels/document.xml.rels", rels},
      {"media/image1.png", image},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].part, "word/_rels/document.xml.rels");
  EXPECT_NE(violations[0].message.find("../../media/image1.png"),
            std::string::npos)
      << violations[0].message;
  EXPECT_NE(violations[0].message.find("outside the package root"),
            std::string::npos)
      << violations[0].message;
}

TEST(WmlValidatorTest, DanglingReferenceInNonWmlPartReportsViolation) {
  // The relationship-reference scan is independent of the root element's
  // namespace: a VML root part is still an XML content part.
  const std::string drawing =
      R"(<?xml version="1.0"?><v:shape xmlns:v="urn:schemas-microsoft-com:vml" )"
      R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/)"
      R"(relationships" r:id="rId9"/>)";

  const auto violations = ValidatePackage({
      {"word/vmlDrawing1.xml", drawing},
  });

  ASSERT_EQ(violations.size(), 1u) << Describe(violations);
  EXPECT_EQ(violations[0].part, "word/vmlDrawing1.xml");
  EXPECT_NE(violations[0].message.find("rId9"), std::string::npos)
      << violations[0].message;
}

}  // namespace
}  // namespace doccraft::ooxml
