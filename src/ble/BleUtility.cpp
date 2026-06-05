#include "ble/BleUtility.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <iconv.h>

namespace ft {

bool is_valid_utf8(const unsigned char* s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (s[i] <= 0x7F) {
            i++;
        } else if ((s[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((s[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 ||
                (s[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((s[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 ||
                (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80)
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

static int hex_val(char c)
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

int unescape_iw_ssid(const char* src, char* dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return -1;

    size_t len = strlen(src);
    size_t i = 0, j = 0;

    while (i < len && j < dst_size - 1) {
        if (i + 3 < len && src[i] == '\\' && src[i + 1] == 'x' &&
            isxdigit((unsigned char)src[i + 2]) &&
            isxdigit((unsigned char)src[i + 3])) {
            int h = hex_val(src[i + 2]);
            int l = hex_val(src[i + 3]);
            dst[j++] = (char)((h << 4) | l);
            i += 4;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return (i < len) ? -1 : 0;
}

int gbk_to_utf8(const char* src, char* dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return -1;

    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == (iconv_t)-1) return -1;

    size_t inlen = strlen(src);
    size_t outleft = dst_size - 1;
    char* in = const_cast<char*>(src);
    char* out = dst;

    if (iconv(cd, &in, &inlen, &out, &outleft) == (size_t)-1) {
        iconv_close(cd);
        return -1;
    }
    iconv_close(cd);
    *out = '\0';
    return 0;
}

void bytes_to_hex_visible(const unsigned char* src, size_t len,
                          char* dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; i < len && j + 4 < dst_size; i++) {
        snprintf(dst + j, dst_size - j, "\\x%02X", src[i]);
        j += 4;
    }
    dst[j] = '\0';
}

} // namespace ft
