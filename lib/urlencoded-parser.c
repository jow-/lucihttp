/*
 * lucihttp - HTTP utility library - urlencoded data parser component
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

#include <lucihttp/urlencoded-parser.h>

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>


static const char *lh_urldec_state_descriptions[] = {
	"start of tuple name",
	"reading tuple name",
	"start of tuple value",
	"reading tuple value",
	"end of body",
	"parser error state"
};


static void
lh_urldec_dump(FILE *fp, const char *prefix, const char *buf, size_t len)
{
	size_t i;

	fprintf(fp, "%s=(%ld)[", prefix, len);

	for (i = 0; i < len; i++)
		fprintf(fp, "%c", buf[i] < 0x20 ? '.' : buf[i]);

	fprintf(fp, "]\n");
}

static void
lh_urldec_set_state(struct lh_urldec *p, enum lh_urldec_state stateval)
{
	if (stateval == p->state)
		return;

	if (p->trace)
		fprintf(p->trace, "State %d (%s) -> %d (%s)\n",
		        p->state, lh_urldec_state_descriptions[p->state],
		        stateval, lh_urldec_state_descriptions[stateval]);

	p->state = stateval;
}

static bool
_lh_urldec_invoke(struct lh_urldec *p, enum lh_urldec_callback_type type,
                const char *typename, const char *buf, size_t len)
{
	if (p->trace) {
		fprintf(p->trace, "Callback %d (%s) ", type, typename);
		lh_urldec_dump(p->trace, "data", buf, len);
	}

	if (p->cb)
		return p->cb(p, type, buf, len, p->priv);

	return true;
}

#define lh_urldec_invoke(p, type, buf, len) \
	_lh_urldec_invoke(p, LH_UD_CB_##type, #type, buf, len)

static bool
lh_urldec_set_token(struct lh_urldec *p, enum lh_urldec_token_type type,
                    bool clear, const char *buf, size_t len)
{
	struct lh_urldec_token *tok = &p->token[type];
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
lh_urldec_get_token(struct lh_urldec *p, enum lh_urldec_token_type type,
                    size_t *len)
{
	struct lh_urldec_token *tok = &p->token[type];

	if (len)
		*len = tok->len;

	if (tok->len)
		return tok->value;

	return NULL;
}

static bool
lh_urldec_error(struct lh_urldec *p, size_t off, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	int rv;

	va_start(ap, fmt);
	rv = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (rv != -1) {
		rv = asprintf(&p->error, "At %s, byte offset %ld, %s",
		              lh_urldec_state_descriptions[p->state],
		              p->total + off, msg);
		free(msg);
	}

	lh_urldec_invoke(p, ERROR, (rv == -1) ? "Out of memory" : p->error,
	                           (rv == -1) ? 13 : rv);

	lh_urldec_set_state(p, LH_UD_S_ERROR);

	return false;
}

struct lh_urldec *
lh_urldec_new(FILE *trace)
{
	struct lh_urldec *p;

	p = calloc(1, sizeof(*p));

	if (!p)
		return NULL;

	p->trace = trace;

	lh_urldec_set_state(p, LH_UD_S_NAME_START);

	return p;
}

void
lh_urldec_set_callback(struct lh_urldec *p, lh_urldec_callback cb, void *priv)
{
	p->cb = cb;
	p->priv = priv;
}

#define EOB (-2)

static bool
lh_urldec_step(struct lh_urldec *p, const char *buf, size_t off, int c)
{
	size_t l, keylen, vallen;
	char *key, *val;

	switch (p->state) {
	case LH_UD_S_NAME_START:
		p->offset = off;
		p->flags &= ~LH_UD_F_GOT_NAME;
		p->flags &= ~LH_UD_F_GOT_VALUE;

		if (lh_urldec_invoke(p, TUPLE, NULL, 0))
			p->flags |= LH_UD_F_BUFFERING;
		else
			p->flags &= LH_UD_F_BUFFERING;

		lh_urldec_set_token(p, LH_UD_T_NAME, true, NULL, 0);
		lh_urldec_set_token(p, LH_UD_T_VALUE, true, NULL, 0);
		lh_urldec_set_state(p, LH_UD_S_NAME);

		/* fall through */

	case LH_UD_S_NAME:
		if (c == '=' || c == '&' || c <= EOF) {
			keylen = (off - p->offset);

			if (p->flags & LH_UD_F_BUFFERING) {
				lh_urldec_get_token(p, LH_UD_T_NAME, &l);

				if (l + keylen > LH_UD_T_SIZE_LIMIT)
					return lh_urldec_error(p, off, "the key exceeds the "
					                               "maximum allowed size");

				lh_urldec_set_token(p, LH_UD_T_NAME, false,
				                    buf + p->offset, keylen);

				if ((c == '&' || c == EOF) && (p->flags & LH_UD_F_GOT_NAME)) {
					key = lh_urldec_get_token(p, LH_UD_T_NAME, &keylen);
					val = lh_urldec_get_token(p, LH_UD_T_VALUE, &vallen);

					lh_urldec_invoke(p, NAME, key, keylen);
					lh_urldec_invoke(p, VALUE, val, vallen);
				}
			}
			else {
				lh_urldec_invoke(p, NAME, buf + p->offset, keylen);
			}

			if (c == '=')
				lh_urldec_set_state(p, LH_UD_S_VALUE_START);
			else if (c == '&')
				lh_urldec_set_state(p, LH_UD_S_NAME_START);
			else if (c == EOF)
				lh_urldec_set_state(p, LH_UD_S_END);
		}
		else {
			p->flags |= LH_UD_F_GOT_NAME;
		}

		break;

	case LH_UD_S_VALUE_START:
		p->offset = off;
		p->flags |= LH_UD_F_GOT_VALUE;
		lh_urldec_set_state(p, LH_UD_S_VALUE);

		/* fall through */

	case LH_UD_S_VALUE:
		if (c == '&' || c <= EOF) {
			vallen = (off - p->offset);

			if (p->flags & LH_UD_F_BUFFERING) {
				lh_urldec_get_token(p, LH_UD_T_VALUE, &l);

				if (l + vallen > LH_UD_T_SIZE_LIMIT)
					return lh_urldec_error(p, off, "the value exceeds the "
					                               "maximum allowed size");

				lh_urldec_set_token(p, LH_UD_T_VALUE, false,
				                    buf + p->offset, vallen);

				if ((c != EOB) && (p->flags & LH_UD_F_GOT_NAME)) {
					key = lh_urldec_get_token(p, LH_UD_T_NAME, &keylen);
					val = lh_urldec_get_token(p, LH_UD_T_VALUE, &vallen);

					lh_urldec_invoke(p, NAME, key, keylen);
					lh_urldec_invoke(p, VALUE, val, vallen);
				}
			}
			else {
				lh_urldec_invoke(p, VALUE, buf + p->offset, vallen);
			}

			if (c > EOF)
				lh_urldec_set_state(p, LH_UD_S_NAME_START);
			else if (c == EOF)
				lh_urldec_set_state(p, LH_UD_S_END);
		}

		break;

	case LH_UD_S_END:
		if (c > EOF) {
			return lh_urldec_error(p, off, "expected EOF, but got "
			                               "trailing junk");
		}

		break;

	default:
		return lh_urldec_error(p, off, "parser is in unrecoverable "
		                               "error state");
	}

	return true;
}

bool
lh_urldec_parse(struct lh_urldec *p, const char *buf, size_t len)
{
	size_t i;

	p->offset = 0;

	if (p->trace)
		lh_urldec_dump(p->trace, "Parsing buffer", buf, len);

	for (i = 0; i < len; i++)
		if (!lh_urldec_step(p, buf, i, (unsigned char)buf[i]))
			return false;

	if (!lh_urldec_step(p, buf, len, buf ? EOB : EOF))
		return false;

	p->total += i;

	return true;
}

void
lh_urldec_free(struct lh_urldec *p)
{
	int i;

	if (p->error)
		free(p->error);

	for (i = 0; i < __LH_UD_T_COUNT; i++)
		if (p->token[i].value)
			free(p->token[i].value);

	free(p);
}
