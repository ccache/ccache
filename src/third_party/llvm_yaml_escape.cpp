// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// extracted escape() and the dependent functions decodeUTF8() and encodeUTF8()
// from YAMLParser.cpp

#include "llvm_yaml_escape.hpp"

#include "fmt/format.h"

#include <array>

using StringRef = std::string_view;

namespace llvm { namespace yaml {

std::string utohexstr(unsigned int c) {
  return fmt::format("{:02X}", +c);
}

/// The Unicode scalar value of a UTF-8 minimal well-formed code unit
///        subsequence and the subsequence's length in code units (uint8_t).
///        A length of 0 represents an error.
using UTF8Decoded = std::pair<uint32_t, unsigned>;

static UTF8Decoded decodeUTF8(StringRef Range) {
  StringRef::iterator Position= Range.begin();
  StringRef::iterator End = Range.end();
  // 1 byte: [0x00, 0x7f]
  // Bit pattern: 0xxxxxxx
  if ((*Position & 0x80) == 0) {
     return std::make_pair(*Position, 1);
  }
  // 2 bytes: [0x80, 0x7ff]
  // Bit pattern: 110xxxxx 10xxxxxx
  if (Position + 1 != End &&
      ((*Position & 0xE0) == 0xC0) &&
      ((*(Position + 1) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x1F) << 6) |
                          (*(Position + 1) & 0x3F);
    if (codepoint >= 0x80)
      return std::make_pair(codepoint, 2);
  }
  // 3 bytes: [0x8000, 0xffff]
  // Bit pattern: 1110xxxx 10xxxxxx 10xxxxxx
  if (Position + 2 != End &&
      ((*Position & 0xF0) == 0xE0) &&
      ((*(Position + 1) & 0xC0) == 0x80) &&
      ((*(Position + 2) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x0F) << 12) |
                         ((*(Position + 1) & 0x3F) << 6) |
                          (*(Position + 2) & 0x3F);
    // Codepoints between 0xD800 and 0xDFFF are invalid, as
    // they are high / low surrogate halves used by UTF-16.
    if (codepoint >= 0x800 &&
        (codepoint < 0xD800 || codepoint > 0xDFFF))
      return std::make_pair(codepoint, 3);
  }
  // 4 bytes: [0x10000, 0x10FFFF]
  // Bit pattern: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
  if (Position + 3 != End &&
      ((*Position & 0xF8) == 0xF0) &&
      ((*(Position + 1) & 0xC0) == 0x80) &&
      ((*(Position + 2) & 0xC0) == 0x80) &&
      ((*(Position + 3) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x07) << 18) |
                         ((*(Position + 1) & 0x3F) << 12) |
                         ((*(Position + 2) & 0x3F) << 6) |
                          (*(Position + 3) & 0x3F);
    if (codepoint >= 0x10000 && codepoint <= 0x10FFFF)
      return std::make_pair(codepoint, 4);
  }
  return std::make_pair(0, 0);
}


/// encodeUTF8 - Encode \a UnicodeScalarValue in UTF-8 and append it to result.
size_t encodeUTF8( uint32_t UnicodeScalarValue
                 , std::array<unsigned char, 4> &Result) {
  if (UnicodeScalarValue <= 0x7F) {
    Result[0] = (UnicodeScalarValue & 0x7F);
    return 1;
  } else if (UnicodeScalarValue <= 0x7FF) {
    uint8_t FirstByte = 0xC0 | ((UnicodeScalarValue & 0x7C0) >> 6);
    uint8_t SecondByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result[0] = (FirstByte);
    Result[1] = (SecondByte);
    return 2;
  } else if (UnicodeScalarValue <= 0xFFFF) {
    uint8_t FirstByte = 0xE0 | ((UnicodeScalarValue & 0xF000) >> 12);
    uint8_t SecondByte = 0x80 | ((UnicodeScalarValue & 0xFC0) >> 6);
    uint8_t ThirdByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result[0] = (FirstByte);
    Result[1] = (SecondByte);
    Result[2] = (ThirdByte);
    return 3;
  } else if (UnicodeScalarValue <= 0x10FFFF) {
    uint8_t FirstByte = 0xF0 | ((UnicodeScalarValue & 0x1F0000) >> 18);
    uint8_t SecondByte = 0x80 | ((UnicodeScalarValue & 0x3F000) >> 12);
    uint8_t ThirdByte = 0x80 | ((UnicodeScalarValue & 0xFC0) >> 6);
    uint8_t FourthByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result[0] = (FirstByte);
    Result[1] = (SecondByte);
    Result[2] = (ThirdByte);
    Result[3] = (FourthByte);
    return 4;
  }
  return ~size_t(0);
}

std::string escape(StringRef Input) {
  std::string EscapedInput;
  for (StringRef::iterator i = Input.begin(), e = Input.end(); i != e; ++i) {
    if (*i == '\\')
      EscapedInput += "\\\\";
    else if (*i == '"')
      EscapedInput += "\\\"";
    else if (*i == 0)
      EscapedInput += "\\0";
    else if (*i == 0x07)
      EscapedInput += "\\a";
    else if (*i == 0x08)
      EscapedInput += "\\b";
    else if (*i == 0x09)
      EscapedInput += "\\t";
    else if (*i == 0x0A)
      EscapedInput += "\\n";
    else if (*i == 0x0B)
      EscapedInput += "\\v";
    else if (*i == 0x0C)
      EscapedInput += "\\f";
    else if (*i == 0x0D)
      EscapedInput += "\\r";
    else if (*i == 0x1B)
      EscapedInput += "\\e";
    else if ((unsigned char)*i < 0x20) { // Control characters not handled above.
      std::string HexStr = utohexstr(*i);
      EscapedInput += "\\x" + std::string(2 - HexStr.size(), '0') + HexStr;
    } else if (*i & 0x80) { // UTF-8 multiple code unit subsequence.
      UTF8Decoded UnicodeScalarValue
        = decodeUTF8(StringRef(i, Input.end() - i));
      if (UnicodeScalarValue.second == 0) {
        // Found invalid char.
        std::array<unsigned char, 4> Val;
        size_t len = encodeUTF8(0xFFFD, Val);
        EscapedInput.insert(EscapedInput.end(), Val.begin(), Val.begin() + len);
        // FIXME: Error reporting.
        return EscapedInput;
      }
      if (UnicodeScalarValue.first == 0x85)
        EscapedInput += "\\N";
      else if (UnicodeScalarValue.first == 0xA0)
        EscapedInput += "\\_";
      else if (UnicodeScalarValue.first == 0x2028)
        EscapedInput += "\\L";
      else if (UnicodeScalarValue.first == 0x2029)
        EscapedInput += "\\P";
      else {
        std::string HexStr = utohexstr(UnicodeScalarValue.first);
        if (HexStr.size() <= 2)
          EscapedInput += "\\x" + std::string(2 - HexStr.size(), '0') + HexStr;
        else if (HexStr.size() <= 4)
          EscapedInput += "\\u" + std::string(4 - HexStr.size(), '0') + HexStr;
        else if (HexStr.size() <= 8)
          EscapedInput += "\\U" + std::string(8 - HexStr.size(), '0') + HexStr;
      }
      i += UnicodeScalarValue.second - 1;
    } else
      EscapedInput.push_back(*i);
  }
  return EscapedInput;
}

} // namespace yaml
} // namespace llvm
