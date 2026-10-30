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

#include "base/charset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/utf8.h"

#if defined(DOCCRAFT_HAVE_ICONV)
#include <iconv.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(DOCCRAFT_HAVE_ICONV) || defined(_WIN32)
// Exactly one platform decoder is compiled in: iconv where the C library has
// it (Linux, macOS, and any BSD or glibc-based target), MultiByteToWideChar on
// Windows. Android and current iOS SDKs ship a usable iconv too, so they pick
// the first branch; WebAssembly has neither and keeps the built-in table.
#define DOCCRAFT_CHARSET_HAS_BACKEND 1
#endif

namespace doccraft::base {
namespace {

// CP1252 keeps its five undefined positions and maps 0x80-0x9F through this
// table; it is the fallback decoder when no iconv is available.
constexpr uint32_t kCp1252High(uint8_t c) {
  switch (c) {
    case 0x80:
      return 0x20AC;
    case 0x82:
      return 0x201A;
    case 0x83:
      return 0x0192;
    case 0x84:
      return 0x201E;
    case 0x85:
      return 0x2026;
    case 0x86:
      return 0x2020;
    case 0x87:
      return 0x2021;
    case 0x88:
      return 0x02C6;
    case 0x89:
      return 0x2030;
    case 0x8A:
      return 0x0160;
    case 0x8B:
      return 0x2039;
    case 0x8C:
      return 0x0152;
    case 0x8E:
      return 0x017D;
    case 0x91:
      return 0x2018;
    case 0x92:
      return 0x2019;
    case 0x93:
      return 0x201C;
    case 0x94:
      return 0x201D;
    case 0x95:
      return 0x2022;
    case 0x96:
      return 0x2013;
    case 0x97:
      return 0x2014;
    case 0x98:
      return 0x02DC;
    case 0x99:
      return 0x2122;
    case 0x9A:
      return 0x0161;
    case 0x9B:
      return 0x203A;
    case 0x9C:
      return 0x0153;
    case 0x9E:
      return 0x017E;
    case 0x9F:
      return 0x0178;
    default:
      return c;  // 0x81/0x8D/0x8F/0x90/0x9D and 0xA0-0xFF pass through
  }
}

// The ANSI code pages a .doc can name, by Windows number. 10000 (Macintosh) is
// deliberately absent: it is not a Windows code page, only iconv knows it, and
// the Win32 backend must not claim it.
constexpr bool IsWindowsCodepage(uint32_t codepage) {
  switch (codepage) {
    case 874:
    case 932:
    case 936:
    case 949:
    case 950:
    case 1250:
    case 1251:
    case 1252:
    case 1253:
    case 1254:
    case 1255:
    case 1256:
    case 1257:
    case 1258:
      return true;
    default:
      return false;
  }
}

#if defined(DOCCRAFT_HAVE_ICONV)
const char* IconvName(uint32_t codepage) {
  switch (codepage) {
    case 874:
      return "CP874";
    case 932:
      return "CP932";
    case 936:
      return "CP936";
    case 949:
      return "CP949";
    case 950:
      return "CP950";
    case 1250:
      return "CP1250";
    case 1251:
      return "CP1251";
    case 1252:
      return "CP1252";
    case 1253:
      return "CP1253";
    case 1254:
      return "CP1254";
    case 1255:
      return "CP1255";
    case 1256:
      return "CP1256";
    case 1257:
      return "CP1257";
    case 1258:
      return "CP1258";
    case 10000:
      return "MACINTOSH";
    default:
      return nullptr;
  }
}

// One conversion descriptor per thread is enough: a document has a single
// code page and the decoder is not re-entrant anyway.
iconv_t CachedIconv(uint32_t codepage) {
  static thread_local uint32_t cached_key = 0;
  static thread_local iconv_t cached_cd = reinterpret_cast<iconv_t>(-1);
  // +1 so that the "nothing cached yet" state (0) never matches a real code
  // page; CP1252 stays the most common case and keeps its descriptor.
  const uint32_t key = codepage + 1;
  if (key == cached_key) {
    return cached_cd;
  }
  if (cached_cd != reinterpret_cast<iconv_t>(-1)) {
    iconv_close(cached_cd);
  }
  const char* name = IconvName(codepage);
  cached_cd = name ? iconv_open("UTF-8", name) : reinterpret_cast<iconv_t>(-1);
  cached_key = key;
  return cached_cd;
}

bool BackendSupports(uint32_t codepage) {
  return IconvName(codepage) != nullptr;
}

// Converts exactly `len` bytes (1 or 2) as one character; false when they do
// not form one character in `codepage`.
bool DecodeBackendChar(uint32_t codepage, const uint8_t* bytes, size_t len,
                       std::string* out) {
  iconv_t cd = CachedIconv(codepage);
  if (cd == reinterpret_cast<iconv_t>(-1)) {
    return false;
  }
  iconv(cd, nullptr, nullptr, nullptr, nullptr);  // reset shift state
  std::array<char, 8> buffer{};
  char* source = const_cast<char*>(reinterpret_cast<const char*>(bytes));
  size_t source_left = len;
  char* target = buffer.data();
  size_t target_left = buffer.size();
  const size_t result = iconv(cd, &source, &source_left, &target, &target_left);
  if (result == static_cast<size_t>(-1) || source_left != 0) {
    return false;
  }
  out->append(buffer.data(), buffer.size() - target_left);
  return true;
}
#elif defined(_WIN32)
// Windows numbers the same code pages; MultiByteToWideChar decodes them into
// UTF-16 and AppendUtf8 re-encodes, so no table is duplicated. MB_ERR_INVALID_
// CHARS is what makes an undefined byte or a lone lead byte fail, matching the
// EILSEQ/EINVAL behaviour of the iconv path above.
bool BackendSupports(uint32_t codepage) {
  return IsWindowsCodepage(codepage);
}

bool DecodeBackendChar(uint32_t codepage, const uint8_t* bytes, size_t len,
                       std::string* out) {
  wchar_t wide[2] = {};
  const int written = ::MultiByteToWideChar(
      static_cast<UINT>(codepage), MB_ERR_INVALID_CHARS,
      reinterpret_cast<const char*>(bytes), static_cast<int>(len), wide, 2);
  if (written <= 0) {
    return false;
  }
  for (int i = 0; i < written; ++i) {
    uint32_t cp = static_cast<uint32_t>(wide[i]);
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < written) {
      const uint32_t low = static_cast<uint32_t>(wide[i + 1]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      }
    }
    AppendUtf8(*out, cp);
  }
  return true;
}
#else
bool BackendSupports(uint32_t /*codepage*/) {
  return false;
}

bool DecodeBackendChar(uint32_t /*codepage*/, const uint8_t* /*bytes*/,
                       size_t /*len*/, std::string* /*out*/) {
  return false;
}
#endif

// A character is one byte or one DBCS pair: try a single byte first (every
// single-byte page, plus the half-width katakana bytes of CP932), then the pair
// that follows a lead byte.
size_t DecodePlatformChar(uint32_t codepage, Bytes in, std::string& out) {
  for (size_t len = 1; len <= 2 && len <= in.size(); ++len) {
    std::string decoded;
    if (DecodeBackendChar(codepage, in.data(), len, &decoded)) {
      out.append(decoded);
      return len;
    }
  }
  return 0;
}

}  // namespace

uint32_t CodepageForLid(uint16_t lid) {
  switch (lid) {
    // CJK: the ANSI code page of the document.
    case 0x0404:  // zh-TW
    case 0x0C04:  // zh-HK
    case 0x1404:  // zh-MO
      return 950;
    case 0x0804:  // zh-CN
    case 0x1004:  // zh-SG
      return 936;
    case 0x0411:  // ja-JP
      return 932;
    case 0x0412:  // ko-KR
      return 949;
    // Central European, Cyrillic, Greek, Turkish, Hebrew, Arabic, Baltic,
    // Vietnamese and Thai all have a Windows single-byte code page.
    case 0x0405:  // cs-CZ
    case 0x040E:  // hu-HU
    case 0x0415:  // pl-PL
    case 0x0418:  // ro-RO
    case 0x041A:  // hr-HR
    case 0x041B:  // sk-SK
    case 0x0424:  // sl-SI
      return 1250;
    case 0x0402:  // bg-BG
    case 0x0419:  // ru-RU
    case 0x0422:  // uk-UA
    case 0x0423:  // be-BY
    case 0x042F:  // mk-MK
      return 1251;
    case 0x0408:  // el-GR
      return 1253;
    case 0x041F:  // tr-TR
    case 0x042C:  // az-Latn-AZ
      return 1254;
    case 0x040D:  // he-IL
      return 1255;
    case 0x0401:  // ar-SA
    case 0x0429:  // fa-IR
    case 0x0801:  // ar-IQ
      return 1256;
    case 0x0425:  // et-EE
    case 0x0426:  // lv-LV
    case 0x0427:  // lt-LT
      return 1257;
    case 0x042A:  // vi-VN
      return 1258;
    case 0x041E:  // th-TH
      return 874;
    default:
      return kDefaultCodepage;
  }
}

bool SupportsCodepage(uint32_t codepage) {
  // CP1252 never needs a backend: the built-in table decodes it identically on
  // every platform.
  return codepage == kDefaultCodepage || BackendSupports(codepage);
}

size_t DecodeAnsiChar(uint32_t codepage, Bytes in, std::string& out) {
  if (in.empty()) {
    return 0;
  }
  if (codepage == 0 || codepage == kDefaultCodepage) {
    codepage = kDefaultCodepage;
  }
  if (in[0] < 0x80) {
    out.push_back(static_cast<char>(in[0]));
    return 1;
  }
  if (codepage == kDefaultCodepage) {
    // CP1252 always uses the built-in table, even when iconv is available: it
    // maps the five undefined positions to U+0080..U+009F instead of rejecting
    // them, and it is the one decoder that must not vary by platform.
    AppendUtf8(out, kCp1252High(in[0]));
    return 1;
  }
  if (BackendSupports(codepage)) {
    return DecodePlatformChar(codepage, in, out);
  }
#ifdef DOCCRAFT_CHARSET_HAS_BACKEND
  // A platform decoder is compiled in and does not know this code page, so
  // these bytes are undecodable: the caller emits U+FFFD and skips one byte.
  return 0;
#else
  // No decoder at all (WebAssembly): the built-in CP1252 table is the
  // documented fallback, which keeps Latin text readable and turns everything
  // else into its CP1252 approximation.
  AppendUtf8(out, kCp1252High(in[0]));
  return 1;
#endif
}

}  // namespace doccraft::base
