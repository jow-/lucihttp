/*
 * lucihttp - HTTP utility library - utility functions
 *
 * Copyright 2018 Jo-Philipp Wich <jo@mein.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lucihttp/utils.h>

#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>

#include <stdio.h> /* XXX: remove */
#include <string.h>

static const char *hexdigits = "0123456789ABCDEF";

static inline bool
is_urlencode_char(char c, bool full)
{
	if (c == '!' || c == '\'' || c == '(' || c == ')' || c == '*' ||
	    c == '-' || c == '.'  || c == '_' || c == '~' ||
		(c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z'))
		return false;

	if (c == '#' || c == '$' || c == '&' || c == '+' || c == ',' ||
	    c == '/' || c == ':' || c == ';' || c == '=' || c == '?' ||
	    c == '@')
	    return full;

	return true;
}

#define hex_to_dec(x) \
	(((x) <= '9') ? ((x) - '0') : \
		(((x) <= 'F') ? ((x) - 'A' + 10) : \
			((x) - 'a' + 10)))

/*
 * URL-encode given string and return encoded copy.
 *
 * Returns a newly allocated string containing the encoded contents of the
 * input string. If a length pointer is provided, it is set to the length
 * of the encoded string.
 *
 * If a non-zero length is specified, decodes at most length bytes, else
 * decodes until the first null byte.
 *
 * In case memory allocation fails, returns NULL and sets the length pointer
 * to zero.
 *
 * Takes a number of possible flags to influence encoding:
 *
 *  LH_URLENCODE_FULL
 *  Additionally encode the characters '#', '$', '&', '+', ',', '/', ':',
 *  ';', '=', '?' and '@'.
 *
 *  LH_URLENCODE_SPACE_PLUS
 *  Encode space characters using the plus ('+') character instead of the
 *  usual %20 escape sequence.
 *
 *  LH_URLENCODE_IF_NEEDED
 *  Only return a string if any actual encoding was nescessary, otherwise
 *  return NULL but still set the length pointer.
 */

char *
lh_urlencode(const char *s, size_t len, size_t *encoded_len,
             unsigned int flags)
{
	bool changed = false;
	size_t i, enc_len;
	char *enc, *ptr;

	for (i = 0, enc_len = 0; len ? (i < len) : (s[i] != 0); i++)  {
		if ((s[i] == ' ') && (flags & LH_URLENCODE_SPACE_PLUS)) {
			changed = true;
			enc_len++;
		}
		else if (is_urlencode_char(s[i], (flags & LH_URLENCODE_FULL))) {
			changed = true;
			enc_len += 3;
		}
		else {
			enc_len++;
		}
	}

	if (encoded_len)
		*encoded_len = enc_len;

	if (changed || !(flags & LH_URLENCODE_IF_NEEDED))
	{
		enc = calloc(1, enc_len + 1);

		if (!enc) {
			if (encoded_len)
				*encoded_len = 0;

			return NULL;
		}

		for (i = 0, ptr = enc; len ? (i < len) : (s[i] != 0); i++)
			if ((s[i] == ' ') && (flags & LH_URLENCODE_SPACE_PLUS)) {
				*ptr++ = '+';
			}
			else if (is_urlencode_char(s[i], (flags & LH_URLENCODE_FULL))) {
				*ptr++ = '%';
				*ptr++ = hexdigits[(unsigned char)s[i] / 16];
				*ptr++ = hexdigits[(unsigned char)s[i] % 16];
			}
			else {
				*ptr++ = s[i];
			}

		return enc;
	}

	return NULL;
}

/*
 * URL-decode given string and return decoded copy.
 *
 * Returns a newly allocated string containing the decoded contents of the
 * input string. If a length pointer is provided, it is set to the length
 * of the decoded string.
 *
 * If a non-zero length is specified, decodes at most length bytes, else
 * decodes until the first null byte.
 *
 * In case memory allocation fails, or if strict decoding is requested and an
 * invalid input string is passed, returns NULL and sets the length pointer
 * to zero.
 *
 * Takes a number of possible flags to influence decoding:
 *
 *  LH_URLDECODE_STRICT
 *  Return NULL if the input string contains any invalid escape sequence.
 *
 *  LH_URLDECODE_KEEP_PLUS
 *  Do not decode plus ('+') characters into spaces, instead keep them as-is.
 *
 *  LH_URLDECODE_IF_NEEDED
 *  Only return a string if any actual decoding was nescessary, otherwise
 *  return NULL but still set the length pointer.
 */

char *
lh_urldecode(const char *s, size_t len, size_t *decoded_len,
             unsigned int flags)
{
	bool changed = false;
	size_t i, dec_len;
	char *dec, *ptr;

	if (decoded_len)
		*decoded_len = 0;

	for (i = 0, dec_len = 0; len ? (i < len) : (s[i] != 0); i++, dec_len++) {
		if (s[i] == '%') {
			if (isxdigit(s[i+1]) && isxdigit(s[i+2])) {
				changed = true;
				i += 2;
			}
			else if (flags & LH_URLDECODE_STRICT) {
				return NULL;
			}
		}
		else if ((s[i] == '+') && !(flags & LH_URLDECODE_KEEP_PLUS)) {
			changed = true;
		}
	}

	if (decoded_len)
		*decoded_len = dec_len;

	if (changed || !(flags & LH_URLDECODE_IF_NEEDED)) {
		dec = calloc(1, dec_len + 1);

		if (!dec) {
			if (decoded_len)
				*decoded_len = 0;

			return NULL;
		}

		for (i = 0, ptr = dec; len ? (i < len) : (s[i] != 0); i++) {
			if (s[i] == '%' && isxdigit(s[i+1]) && isxdigit(s[i+2])) {
				*ptr++ = (char)(16 * hex_to_dec(s[i+1]) + hex_to_dec(s[i+2]));
				i += 2;
			}
			else if ((s[i] == '+') && !(flags & LH_URLDECODE_KEEP_PLUS)) {
				*ptr++ = ' ';
			}
			else {
				*ptr++ = s[i];
			}
		}

		return dec;
	}

	return NULL;
}

/*
 * Extract the given named attribute from the header value and perform various
 * decoding quirks.
 *
 * Returns a newly allocated string containing the decoded value of the found
 * named attribute of the input string. If a length pointer is provided, it is
 * set to the length of the decoded string.
 *
   If a non-zero length is specified, decodes at most length bytes, else
 * decodes until the first null byte.
 *
 * If the input string cannot be parsed, if the named attribute is not found
 * or if memory allocation fails, returns NULL and sets the length to 0.
 *
 * The found attribute value is first non-strictly URL-decoded, then any
 * literal '\"' (backslash, quote) character sequence is replaced with just
 * a quote. This is needed to accomodate for various client specific encodings
 * caused by a lack of clear specification.
 */

char *
lh_header_attribute(const char *s, size_t len, const char *attr,
                    size_t *attr_len)
{
	enum { TYPE, NSTART, NAME, VALUE, QUOTED, QEND } state = TYPE;
	const char *tspecial = "()<>@,;:\\\"/[]?=";
	const char *nameptr = NULL, *valueptr = NULL;
	size_t i = 0, namelen = 0, valuelen = 0;
	char *value = NULL;
	int c = 0;

	if (attr_len)
		*attr_len = 0;

	while (c != EOF) {
		c = (len ? (i < len) : s[i]) ? (unsigned char)s[i] : EOF;

		switch (state) {
		case TYPE:
			if (!valueptr && (c == ' ' || c == '\t'))
				break;

			if (c == ';' || c == '\r' || c == EOF) {
				state = NSTART;

				if (!valuelen)
					valuelen = s + i - valueptr;

				if (!attr)
					goto found;
			}
			else if (c == ' ' || c == '\t') {
				if (!valuelen)
					valuelen = s + i - valueptr;
			}
			else if (c == '/') {
				if (!namelen)
					namelen = s + i - nameptr;
				else
					return NULL;
			}
			else if (valuelen || strchr(tspecial, c) || c <= ' ' || c > '~') {
				return NULL;
			}
			else if (!valueptr) {
				nameptr = s + i;
				valueptr = s + i;
			}

			break;

		case NSTART:
			if (c == ' ' || c == '\t' || c == '\r')
				break;

			state = NAME;
			namelen = 0;
			nameptr = s + i;
			valuelen = 0;
			valueptr = NULL;
			/* fall through */

		case NAME:
			if (c == '=') {
				state = VALUE;
				namelen = s + i - nameptr;
				valueptr = s + i + 1;
			}
			else if (strchr(tspecial, c) || c <= ' ' || c > '~') {
				/* RFC 2045 section 5.1 */
				return NULL;
			}

			break;

		case VALUE:
			if (c == '"') {
				state = QUOTED;
				valueptr = s + i + 1;
			}
			else if (c == ';' || c == '\r' || c == EOF) {
				state = NSTART;
				valuelen = s + i - valueptr;

				if (attr && nameptr && namelen && valueptr &&
				    !strncasecmp(nameptr, attr, namelen))
					goto found;
			}
			else if (strchr(tspecial, c) || c <= ' ' || c > '~') {
				/* RFC 2045 section 5.1 */
				return NULL;
			}

			break;

		case QUOTED:
			if (c == '"' && s[i-1] != '\\') {
				state = QEND;
				valuelen = s + i - valueptr;
			}

			break;

		case QEND:
			if (c == ';' || c == '\r' || c == EOF) {
				state = NSTART;

				if (attr && nameptr && namelen && valueptr &&
				    !strncasecmp(nameptr, attr, namelen))
					goto found;
			}
			else if (c != ' ' && c != '\t') {
				return NULL;
			}

			break;
		}

		i++;
	}

	return NULL;

found:
	value = lh_urldecode(valueptr, valuelen, &valuelen,
	                     LH_URLDECODE_KEEP_PLUS);

	if (!value) {
		if (attr_len)
			*attr_len = valuelen;

		return NULL;
	}

	for (i = 0, namelen = 0; i < valuelen; i++, namelen++) {
		if (i && value[i] == '"' && value[i-1] == '\\')
			namelen--;

		value[namelen] = value[i];
	}

	value[namelen] = 0;

	if (attr_len)
		*attr_len = namelen;

	return value;
}
