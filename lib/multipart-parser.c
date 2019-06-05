/*
 * lucihttp - HTTP utility library - multipart parser component
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

#define _GNU_SOURCE

#include <lucihttp/multipart-parser.h>
#include <lucihttp/utils.h>

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>


static const char *lh_mpart_state_descriptions[] = {
	"start of multipart body",
	"start of boundary",
	"start of header name",
	"reading header name",
	"finding header name end",
	"start of header value",
	"reading header value",
	"finding header value end",
	"start of part data",
	"reading part data",
	"start of part boundary",
	"reading part boundary",
	"finding part boundary end",
	"end of part data",
	"end of final part",
	"end of multipart body",
	"parser error state"
};


static char *
lh_mpart_char_esc(int c)
{
	static char buf[sizeof("\\xFF")];

	switch (c)
	{
	case EOF: return "<EOF>";
	case '\r': return "\\r";
	case '\n': return "\\n";
	case '\t': return "\\t";
	default:
		if ((unsigned char)c < ' ' || (unsigned char)c > '~') {
			snprintf(buf, sizeof(buf), "\\x%02X", (unsigned char)c);
			return buf;
		}

		snprintf(buf, sizeof(buf), "%c", c);
		return buf;
	}
}

static void
lh_mpart_dump(FILE *fp, const char *prefix, const char *buf, size_t len)
{
	size_t i;

	fprintf(fp, "%s=(%lu)[", prefix, (unsigned long)len);

	for (i = 0; i < len; i++)
		fprintf(fp, "%c", buf[i] < 0x20 ? '.' : buf[i]);

	fprintf(fp, "]\n");
}

static void
lh_mpart_set_state(struct lh_mpart *p, enum lh_mpart_state stateval)
{
	if (stateval == p->state)
		return;

	if (p->trace)
		fprintf(p->trace, "State %d (%s) -> %d (%s)\n",
		        p->state, lh_mpart_state_descriptions[p->state],
		        stateval, lh_mpart_state_descriptions[stateval]);

	p->state = stateval;
}

static bool
_lh_mpart_invoke(struct lh_mpart *p, enum lh_mpart_callback_type type,
                 const char *typename, const char *buf, size_t len)
{
	if (p->trace) {
		fprintf(p->trace, "Callback %d (%s) ", type, typename);
		lh_mpart_dump(p->trace, "data", buf, len);
	}

	if (p->cb)
		return p->cb(p, type, buf, len, p->priv);

	return true;
}

#define lh_mpart_invoke(p, type, buf, len) \
	_lh_mpart_invoke(p, LH_MP_CB_##type, #type, buf, len)

static bool
lh_mpart_set_token(struct lh_mpart *p, enum lh_mpart_token_type type,
                   bool clear, const char *buf, size_t len)
{
	struct lh_mpart_token *tok = &p->token[type];
	char *tmp;

	if (clear)
		tok->len = 0;

	if (len + tok->len + 1 > tok->size) {
		tmp = realloc(tok->value, len + tok->len + 1);

		if (!tmp)
			return false;

		tok->value = tmp;
		tok->size = len + tok->len + 1;
	}

	if (len) {
		memcpy(tok->value + tok->len, buf, len);
		tok->value[tok->len + len] = 0;
		tok->len += len;
	}

	return true;
}

static char *
lh_mpart_get_token(struct lh_mpart *p, enum lh_mpart_token_type type,
                   size_t *len)
{
	struct lh_mpart_token *tok = &p->token[type];

	if (len)
		*len = tok->len;

	if (tok->len)
		return tok->value;

	return NULL;
}

static char *
lh_mpart_push_boundary(struct lh_mpart *p, const char *boundary_string,
                       size_t boundary_len)
{
	size_t lookbehind_size;
	char *lookbehind;

	if (LH_MP_T_BOUNDARY1 + p->nesting + 1 > LH_MP_T_BOUNDARY3)
		return NULL;

	/* "\r\n" "--" boundary "--" "\r\n" */
	lookbehind_size = 2 + 2 + boundary_len + 2 + 2;

	if (lookbehind_size > p->lookbehind_size) {
		lookbehind = realloc(p->lookbehind, lookbehind_size);

		if (!lookbehind)
			return NULL;

		p->lookbehind = lookbehind;
		p->lookbehind_size = lookbehind_size;
	}

	p->nesting++;

	if (!lh_mpart_set_token(p, LH_MP_T_BOUNDARY1 + p->nesting,
	                           true, boundary_string, boundary_len))
		return NULL;

	return p->token[LH_MP_T_BOUNDARY1 + p->nesting].value;
}

static char *
lh_mpart_pop_boundary(struct lh_mpart *p, size_t *len)
{
	enum lh_mpart_token_type type = LH_MP_T_BOUNDARY1 + p->nesting;

	p->nesting--;

	lh_mpart_set_token(p, type, true, NULL, 0);

	if (type == LH_MP_T_BOUNDARY1)
		return NULL;

	return lh_mpart_get_token(p, type - 1, len);
}

static char *
lh_mpart_get_boundary(struct lh_mpart *p, size_t *len)
{
	enum lh_mpart_token_type type = LH_MP_T_BOUNDARY1 + p->nesting;

	if (type < LH_MP_T_BOUNDARY1)
		return NULL;

	return lh_mpart_get_token(p, type, len);
}

static bool
lh_mpart_error(struct lh_mpart *p, size_t off, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	int rv;

	va_start(ap, fmt);
	rv = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (rv != -1) {
		rv = asprintf(&p->error, "At %s, byte offset %lu, %s",
		              lh_mpart_state_descriptions[p->state],
		              (unsigned long)(p->total + off), msg);
		free(msg);
	}

	lh_mpart_invoke(p, ERROR, (rv == -1) ? "Out of memory" : p->error,
	                          (rv == -1) ? 13 : rv);

	lh_mpart_set_state(p, LH_MP_S_ERROR);

	return false;
}

struct lh_mpart *
lh_mpart_new(FILE *trace)
{
	struct lh_mpart *p;

	p = calloc(1, sizeof(*p));

	if (!p)
		return NULL;

	p->nesting = -1;
	p->trace = trace;
	p->size_limit = LH_MP_T_DEFAULT_SIZE_LIMIT;

	lh_mpart_set_state(p, LH_MP_S_START);

	return p;
}

void
lh_mpart_set_callback(struct lh_mpart *p, lh_mpart_callback cb, void *priv)
{
	p->cb = cb;
	p->priv = priv;
}

void
lh_mpart_set_size_limit(struct lh_mpart *p, size_t limit)
{
	if (limit >= 1024)
		p->size_limit = limit;
}

char *
lh_mpart_parse_boundary(struct lh_mpart *p, const char *value, size_t *len)
{
	char *ptr, *rv;
	size_t l;

	if (strncasecmp(value, "multipart/", 10))
		return NULL;

	ptr = lh_header_attribute(value, 0, "boundary", &l);

	if (!ptr)
		return NULL;

	if (len)
		*len = l;

	rv = lh_mpart_push_boundary(p, ptr, l);

	free(ptr);
	return rv;
}

static bool
lh_mpart_step(struct lh_mpart *p, const char *buf, size_t off, int c,
              bool buffer_end)
{
	size_t boundary_len = 0, l, namelen, valuelen;
	char *boundary, *hname, *hvalue, *s;

	boundary = lh_mpart_get_boundary(p, &boundary_len);

	switch (p->state) {
	case LH_MP_S_START:
		p->index = 0;
		lh_mpart_invoke(p, BODY_BEGIN, boundary, boundary_len);
		lh_mpart_set_state(p, LH_MP_S_BOUNDARY_START);

		/* fall through */

	case LH_MP_S_BOUNDARY_START:
		if (p->index < 2) {
			if (c != '-')
				return lh_mpart_error(p, off, "expected '-' but got '%s'",
				                      lh_mpart_char_esc(c));

			p->index++;
		}
		else if ((p->index - 2) == boundary_len) {
			if (c != '\r')
				return lh_mpart_error(p, off, "expected '\\r' but got '%s'",
				                      lh_mpart_char_esc(c));

			p->index++;
		}
		else if ((p->index - 2) == (boundary_len + 1)) {
			if (c != '\n')
				return lh_mpart_error(p, off, "expected '\\n' but got '%s'",
				                      lh_mpart_char_esc(c));

			p->index = 0;

			if (lh_mpart_invoke(p, PART_INIT, NULL, 0))
				p->flags |= LH_MP_F_BUFFERING;
			else
				p->flags &= ~LH_MP_F_BUFFERING;

			lh_mpart_set_state(p, LH_MP_S_HEADER_START);
		}
		else {
			if (c != boundary[p->index - 2])
				return lh_mpart_error(p, off, "expected '%c' but got '%s'",
				                      boundary[p->index - 2],
				                      lh_mpart_char_esc(c));

			p->index++;
		}

		break;

	case LH_MP_S_HEADER_START:
		if (c == ' ' || c == '\t') {
			if (!(p->flags & LH_MP_F_PAST_NAME))
				return lh_mpart_error(p, off, "found header continuation "
				                              "line without preceeding "
				                              "header name");
			else
				p->flags |= LH_MP_F_MULTILINE;

			lh_mpart_set_state(p, LH_MP_S_HEADER_VALUE_START);
			break;
		}

		hname = lh_mpart_get_token(p, LH_MP_T_HEADER_NAME, &namelen);
		hvalue = lh_mpart_get_token(p, LH_MP_T_HEADER_VALUE, &valuelen);

		if (hname && hvalue && !strcasecmp(hname, "Content-Type")) {
			s = lh_mpart_parse_boundary(p, hvalue, &l);

			if (s) {
				boundary = s;
				boundary_len = l;
				p->flags |= LH_MP_F_IS_NESTED;
			}
		}

		if (hname && (p->flags & LH_MP_F_BUFFERING)) {
			lh_mpart_invoke(p, HEADER_NAME, hname, namelen);
			lh_mpart_invoke(p, HEADER_VALUE, hvalue, valuelen);
		}

		lh_mpart_set_token(p, LH_MP_T_HEADER_NAME, true, NULL, 0);
		lh_mpart_set_token(p, LH_MP_T_HEADER_VALUE, true, NULL, 0);
		lh_mpart_set_state(p, LH_MP_S_HEADER);

		p->flags &= ~LH_MP_F_PAST_NAME;
		p->flags &= ~LH_MP_F_MULTILINE;
		p->offset = off;

		/* fall through */

	case LH_MP_S_HEADER:
		if (c == '\r') {
			lh_mpart_set_state(p, LH_MP_S_HEADER_END);
		}
		else if (c == ':' || buffer_end) {
			namelen = (off - p->offset) + (c != ':');

			if (p->flags & LH_MP_F_BUFFERING) {
				lh_mpart_get_token(p, LH_MP_T_HEADER_NAME, &l);

				if (l + namelen > p->size_limit)
					return lh_mpart_error(p, off, "the name exceeds the "
					                              "maximum allowed size");

				lh_mpart_set_token(p, LH_MP_T_HEADER_NAME, false,
				                   buf + p->offset, namelen);
			}
			else {
				lh_mpart_invoke(p, HEADER_NAME,
				                buf + p->offset, namelen);
			}

			if (c == ':') {
				lh_mpart_set_state(p, LH_MP_S_HEADER_VALUE_START);
				p->flags |= LH_MP_F_PAST_NAME;
			}
		}

		break;

	case LH_MP_S_HEADER_END:
		if (c != '\n')
			return lh_mpart_error(p, off, "expected '\\n' but got '%s'",
			                      lh_mpart_char_esc(c));

		if (p->flags & LH_MP_F_IS_NESTED) {
			p->flags &= ~LH_MP_F_IS_NESTED;
			lh_mpart_set_state(p, LH_MP_S_START);
		}
		else {
			lh_mpart_set_state(p, LH_MP_S_PART_START);
		}

		break;

	case LH_MP_S_HEADER_VALUE_START:
		if (c == ' ' || c == '\t')
			break;

		p->offset = off;
		lh_mpart_set_state(p, LH_MP_S_HEADER_VALUE);

		/* fall through */

	case LH_MP_S_HEADER_VALUE:
		if (c == '\r' || buffer_end) {
			valuelen = (off - p->offset) + (c != '\r');

			if (p->flags & LH_MP_F_BUFFERING) {
				lh_mpart_get_token(p, LH_MP_T_HEADER_VALUE, &l);

				if (p->flags & LH_MP_F_MULTILINE) {
					if (++l > p->size_limit)
						return lh_mpart_error(p, off,
						                      "the value exceeds the "
						                      "maximum allowed size");

					lh_mpart_set_token(p, LH_MP_T_HEADER_VALUE, false,
					                   " ", 1);
				}

				if (l + valuelen > p->size_limit)
					return lh_mpart_error(p, off, "the value exceeds the "
					                              "maximum allowed size");

				lh_mpart_set_token(p, LH_MP_T_HEADER_VALUE, false,
				                   buf + p->offset, valuelen);
			}
			else {
				lh_mpart_invoke(p, HEADER_VALUE,
				                buf + p->offset, valuelen);
			}

			if (c == '\r')
				lh_mpart_set_state(p, LH_MP_S_HEADER_VALUE_END);
		}

		break;

	case LH_MP_S_HEADER_VALUE_END:
		if (c != '\n')
			return lh_mpart_error(p, off, "expected '\\n' but got '%s'",
			                      lh_mpart_char_esc(c));

		lh_mpart_set_state(p, LH_MP_S_HEADER_START);
		break;

	case LH_MP_S_PART_START:
		if (lh_mpart_invoke(p, PART_BEGIN, NULL, 0))
			p->flags |= LH_MP_F_BUFFERING;
		else
			p->flags &= ~LH_MP_F_BUFFERING;

		lh_mpart_set_token(p, LH_MP_T_DATA, true, NULL, 0);
		lh_mpart_set_state(p, LH_MP_S_PART_DATA);

		p->flags |= LH_MP_F_IN_PART;
		p->offset = off;

		/* fall through */

	case LH_MP_S_PART_DATA:
		if (c == '\r' || buffer_end) {
			if (p->flags & LH_MP_F_IN_PART) {
				valuelen = (off - p->offset) + (c != '\r');

				if (p->flags & LH_MP_F_BUFFERING) {
					lh_mpart_get_token(p, LH_MP_T_DATA, &l);

					if (l + valuelen > p->size_limit)
						return lh_mpart_error(p, off,
						                      "the value exceeds the "
						                      "maximum allow size");

					lh_mpart_set_token(p, LH_MP_T_DATA, false,
					                   buf + p->offset, valuelen);
				}
				else {
					lh_mpart_invoke(p, PART_DATA,
					                buf + p->offset, valuelen);
				}
			}

			if (c == '\r') {
				p->offset = off;
				p->lookbehind[0] = c;
				lh_mpart_set_state(p, LH_MP_S_PART_BOUNDARY_START);
			}
		}

		break;

	case LH_MP_S_PART_BOUNDARY_START:
		p->lookbehind[1] = c;

		if (c == '\n') {
			p->index = 0;
			lh_mpart_set_state(p, LH_MP_S_PART_BOUNDARY);
		}
		else {
			if (p->flags & LH_MP_F_IN_PART) {
				if (p->flags & LH_MP_F_BUFFERING) {
					lh_mpart_get_token(p, LH_MP_T_DATA, &l);

					if (l + 2 > p->size_limit)
						return lh_mpart_error(p, off,
						                      "the value exceeds the "
						                      "maximum allow size");

					lh_mpart_set_token(p, LH_MP_T_DATA, false,
					                   p->lookbehind, 2);
				}
				else {
					lh_mpart_invoke(p, PART_DATA, p->lookbehind, 2);
				}
			}

			p->offset = off + 1;
			lh_mpart_set_state(p, LH_MP_S_PART_DATA);
		}

		break;

	case LH_MP_S_PART_BOUNDARY:
		if ((p->index < 2) ? (c != '-')
		                   : (c != boundary[p->index - 2])) {
			if (p->flags & LH_MP_F_IN_PART) {
				if (p->flags & LH_MP_F_BUFFERING) {
					lh_mpart_get_token(p, LH_MP_T_DATA, &l);

					if (l + p->index + 2 > p->size_limit)
						return lh_mpart_error(p, off,
						                      "the value exceeds the "
						                      "maximum allow size");

					lh_mpart_set_token(p, LH_MP_T_DATA, false,
					                   p->lookbehind, p->index + 2);
				}
				else {
					lh_mpart_invoke(p, PART_DATA, p->lookbehind,
					                              p->index + 2);
				}
			}

			p->offset = off;
			p->lookbehind[0] = c;

			if (c == '\r')
				lh_mpart_set_state(p, LH_MP_S_PART_BOUNDARY_START);
			else
				lh_mpart_set_state(p, LH_MP_S_PART_DATA);
		}
		else {
			p->lookbehind[p->index + 2] = c;
			p->index++;

			if ((p->index - 2) == boundary_len) {
				if (p->flags & LH_MP_F_BUFFERING) {
					hvalue = lh_mpart_get_token(p, LH_MP_T_DATA,
					                            &valuelen);

					lh_mpart_invoke(p, PART_DATA, hvalue, valuelen);
				}

				lh_mpart_invoke(p, PART_END, NULL, 0);
				lh_mpart_set_state(p, LH_MP_S_PART_BOUNDARY_END);
				p->flags &= ~LH_MP_F_IN_PART;
			}
		}

		break;

	case LH_MP_S_PART_BOUNDARY_END:
		if (c == '-') {
			lh_mpart_set_state(p, LH_MP_S_PART_FINAL);
		}
		else if (c == '\r') {
			lh_mpart_set_state(p, LH_MP_S_PART_END);
		}
		else {
			return lh_mpart_error(p, off, "expected '-' or '\\r' "
			                              "but got '%s'",
			                      lh_mpart_char_esc(c));
		}

		break;

	case LH_MP_S_PART_FINAL:
		if (c == '-') {
			lh_mpart_invoke(p, BODY_END, boundary, boundary_len);

			boundary = lh_mpart_pop_boundary(p, &boundary_len);
			p->index = 0;

			if (boundary)
				lh_mpart_set_state(p, LH_MP_S_PART_DATA);
			else
				lh_mpart_set_state(p, LH_MP_S_END);
		}
		else {
			return lh_mpart_error(p, off, "expected '-' but got '%s'",
			                      lh_mpart_char_esc(c));
		}

		break;

	case LH_MP_S_PART_END:
		if (c == '\n') {
			if (lh_mpart_invoke(p, PART_INIT, NULL, 0))
				p->flags |= LH_MP_F_BUFFERING;
			else
				p->flags &= ~LH_MP_F_BUFFERING;

			lh_mpart_set_state(p, LH_MP_S_HEADER_START);
		}
		else {
			return lh_mpart_error(p, off, "expected '\\n' but got '%s'",
			                      lh_mpart_char_esc(c));
		}

		break;

	case LH_MP_S_END:
		if (p->index == 0) {
			if (c != '\r')
				return lh_mpart_error(p, off, "expected '\\r' but got '%s'",
				                      lh_mpart_char_esc(c));

			p->index++;
		}
		else if (p->index == 1) {
			if (c != '\n')
				return lh_mpart_error(p, off, "expected '\\n' but got '%s'",
				                      lh_mpart_char_esc(c));

			p->index++;
			lh_mpart_invoke(p, EOF, NULL, 0);
		}
		else if (c > EOF) {
			return lh_mpart_error(p, off, "expected EOF, but got "
			                              "trailing junk");
		}

		break;

	default:
		return lh_mpart_error(p, 0, "parser is in unrecoverable "
		                            "error state");
	}

	return true;
}

bool
lh_mpart_parse(struct lh_mpart *p, const char *buf, size_t len)
{
	size_t i;

	p->offset = 0;

	if (p->trace)
		lh_mpart_dump(p->trace, "Parsing buffer", buf, len);

	for (i = 0; i < len; i++)
		if (!lh_mpart_step(p, buf, i, (unsigned char)buf[i], i + 1 == len))
			return false;

	if (!buf && !lh_mpart_step(p, NULL, 0, EOF, true))
		return false;

	p->total += i;

	return true;
}

void
lh_mpart_free(struct lh_mpart *p)
{
	int i;

	if (p->error)
		free(p->error);

	if (p->lookbehind)
		free(p->lookbehind);

	for (i = 0; i < __LH_MP_T_COUNT; i++)
		if (p->token[i].value)
			free(p->token[i].value);

	free(p);
}
