#pragma once

#include <cstdint>
#include <cstddef>

namespace ft {

// Validate UTF-8 byte sequence. Returns true if valid.
bool is_valid_utf8(const unsigned char* s, size_t len);

// Unescape iw-style hex escapes (\xNN) to raw bytes. Returns 0 on success.
int unescape_iw_ssid(const char* src, char* dst, size_t dst_size);

// Convert GBK encoding to UTF-8 using iconv. Returns 0 on success.
int gbk_to_utf8(const char* src, char* dst, size_t dst_size);

// Convert raw bytes to \xNN hex visible string.
void bytes_to_hex_visible(const unsigned char* src, size_t len, char* dst, size_t dst_size);

} // namespace ft
