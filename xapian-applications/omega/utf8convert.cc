/** @file
 * @brief convert a string to UTF-8 encoding.
 */
/* Copyright (C) 2006,2007,2008,2010,2013,2017,2019,2021,2023 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>

#include "utf8convert.h"

#include <algorithm>
#include <cerrno>
#include <string>

#ifdef HAVE_ICONV
# include <iconv.h>
#endif
#include <xapian.h>
#include "strcasecmp.h"
#include "stringutils.h"

using namespace std;

bool
convert_to_utf8_(string_view text, const string& charset, string& output)
{
    // Shortcut if it's already in utf8!
    if (charset.size() == 5 && strcasecmp(charset.c_str(), "utf-8") == 0)
	return false;
    if (charset.size() == 4 && strcasecmp(charset.c_str(), "utf8") == 0)
	return false;
    if (charset.size() == 8 && strcasecmp(charset.c_str(), "us-ascii") == 0)
	return false;

    // Nobody has told us what charset it's in, so do as little work as
    // possible!
    if (charset.empty())
	return false;

    char buf[1024];
    string tmp;

    /* Handle iso-8859-1/iso-8859-15//windows-1252/cp-1252, utf-16/ucs-2,
     * utf-16be/ucs-2be, and utf-16le/ucs-2le. */
    const char * p = charset.c_str();

    bool utf16 = false;
    if (strncasecmp(p, "utf", 3) == 0) {
	p += 3;
	if (*p == '-' || *p == '_' || *p == ' ') ++p;
	if (*p != '1' || p[1] != '6') goto try_iconv;
	p += 2;
	utf16 = true;
    } else if (strncasecmp(p, "ucs", 3) == 0) {
	p += 3;
	if (*p == '-' || *p == '_' || *p == ' ') ++p;
	if (*p != '2') goto try_iconv;
	++p;
	utf16 = true;
    }

    if (utf16) {
	if (text.size() < 2) return false;

	bool big_endian = true;
	auto i = text.begin();
	if (*p == '\0') {
	    // GNU iconv doesn't seem to handle BOMs.
	    if (startswith(text, "\xfe\xff")) {
		i += 2;
	    } else if (startswith(text, "\xff\xfe")) {
		big_endian = false;
		i += 2;
	    }
	    // UTF-16 with no BOM is meant to be assumed to be BE.  Strictly
	    // speaking, we're not meant to assume anything for UCS-2 with
	    // no BOM, but we've got to do something, so we might as well
	    // assume it's UTF-16 mislabelled, which is easy and sane.
	} else if (strcasecmp(p, "LE") == 0) {
	    big_endian = false;
	} else if (!(strcasecmp(p, "BE") == 0)) {
	    goto try_iconv;
	}

	tmp.reserve(text.size() / 2);

	size_t start = 0;
	auto text_end = text.end();
	if (text.size() & 1) {
	    // If there's a half-character at the end, nuke it now to make the
	    // conversion loop below simpler.
	    --text_end;
	}

	while (i != text_end) {
	    unsigned ch = static_cast<unsigned char>(*i++);
	    unsigned ch2 = static_cast<unsigned char>(*i++);
	    if (big_endian) {
		ch = (ch << 8) | ch2;
	    } else {
		ch = (ch2 << 8) | ch;
	    }
	    if (ch >> 10 == 0xd800 >> 10) {
		// Surrogate pair.
		if (i == text_end) break;
		unsigned hi = (ch & 0x3ff);
		ch = static_cast<unsigned char>(*i++);
		ch2 = static_cast<unsigned char>(*i++);
		if (big_endian) {
		    ch = (ch << 8) | ch2;
		} else {
		    ch = (ch2 << 8) | ch;
		}
		if (ch >> 10 == 0xdc00 >> 10) {
		    ch &= 0x3ff;
		    ch |= (hi << 10);
		    ch += 0x10000;
		}
	    }
	    start += Xapian::Unicode::to_utf8(ch, buf + start);
	    if (start >= sizeof(buf) - 4) {
		tmp.append(buf, start);
		start = 0;
	    }
	}
	if (start) tmp.append(buf, start);
    } else {
	// Assume windows-1252 if iso-8859-1 is specified.  The only
	// differences are in the range 128-159 which are control characters in
	// iso-8859-1, and a lot of content is mislabelled.  We use our own
	// conversion code for this case, as GNU iconv fails if it sees one of
	// the unassigned code points in windows-1252, whereas it would accept
	// the same input as iso-8859-1, and it seems undesirable to be
	// rejecting input due to this behind-the-scenes character set
	// shenanigans.
	const char * q = NULL;
	if (strncasecmp(p, "windows", 7) == 0) {
	    q = p + 7;
	} else if (strncasecmp(p, "cp", 2) == 0) {
	    q = p + 2;
	}
	if (q) {
	    if (*q == '-' || *q == '_' || *q == ' ') ++q;
	    if (strcmp(q, "1252") != 0)
		goto try_iconv;
	} else {
	    if (strncasecmp(p, "iso", 3) == 0) {
		p += 3;
		if (*p == '-' || *p == '_' || *p == ' ') ++p;
	    }
	    if (strncmp(p, "8859", 4) != 0) goto try_iconv;
	    p += 4;
	    if (*p == '-' || *p == '_' || *p == ' ') ++p;
	    if (*p != '1') goto try_iconv;
	    if (strcmp(p + 1, "5") == 0) goto iso8859_15;
	    if (p[1] != '\0') goto try_iconv;
	}

	// FIXME: pull this out as a standard "normalise utf-8" function?
	tmp.reserve(text.size());

	size_t start = 0;
	for (unsigned char ch : text) {
	    static const unsigned cp1252_to_unicode[32] = {
		0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
		0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
		0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
	    };
	    unsigned code_point = ch;
	    unsigned i = code_point - 128;
	    if (i < std::size(cp1252_to_unicode))
		code_point = cp1252_to_unicode[i];
	    start += Xapian::Unicode::to_utf8(code_point, buf + start);
	    if (start >= sizeof(buf) - 4) {
		tmp.append(buf, start);
		start = 0;
	    }
	}
	if (start) tmp.append(buf, start);
    }

    if (false) {
try_iconv:
#ifdef HAVE_ICONV
	iconv_t conv = iconv_open("UTF-8", charset.c_str());
	if (conv == reinterpret_cast<iconv_t>(-1))
	    return false;
	ICONV_CONST char* in = const_cast<char *>(text.data());
	size_t in_len = text.size();
	while (in_len) {
	    char * out = buf;
	    size_t out_len = sizeof(buf);
	    if (iconv(conv, &in, &in_len, &out, &out_len) == size_t(-1) &&
		errno != E2BIG) {
		// FIXME: how to handle this?
		break;
	    }
	    tmp.append(buf, out - buf);
	}

	(void)iconv_close(conv);
#else
	return false;
#endif
    }

    if (false) {
iso8859_15:
	tmp.reserve(text.size());

	size_t start = 0;
	for (unsigned char ch : text) {
	    static const unsigned iso8859_15_to_unicode[] = {
		0x20ac, 0x00a5, 0x0160, 0x00a7, 0x0161, 0x00a9, 0x00aa, 0x00ab,
		0x00ac, 0x00ad, 0x00ae, 0x00af, 0x00b0, 0x00b1, 0x00b2, 0x00b3,
		0x017d, 0x00b5, 0x00b6, 0x00b7, 0x017e, 0x00b9, 0x00ba, 0x00bb,
		0x0152, 0x0153, 0x0178
	    };
	    unsigned code_point = ch;
	    unsigned i = code_point - 164;
	    if (i < std::size(iso8859_15_to_unicode))
		code_point = iso8859_15_to_unicode[i];
	    start += Xapian::Unicode::to_utf8(code_point, buf + start);
	    if (start >= sizeof(buf) - 4) {
		tmp.append(buf, start);
		start = 0;
	    }
	}
	if (start) tmp.append(buf, start);
    }

    // `output` may be a reference to the same string object as `text` so we
    // only switch after we've done converting.
    output = std::move(tmp);
    return true;
}
