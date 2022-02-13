/*
 * lucihttp - HTTP utility library - Lua binding
 *
 * Copyright 2022 Jo-Philipp Wich <jo@mein.io>
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
#include <lucihttp/multipart-parser.h>
#include <lucihttp/urlencoded-parser.h>

#include <stdlib.h>
#include <errno.h>

#include <ucode/module.h>


static uc_resource_type_t *mpart_type, *urldec_type;

static inline uc_value_t *
uc_raise(uc_vm_t *vm, const char *msg)
{
	uc_vm_raise_exception(vm, EXCEPTION_RUNTIME, "%s", msg);

	return NULL;
}

/*
 * multipart parser binding
 * -------------------------------------------------------------------------
 */

struct lh_uc_mpart {
	struct lh_mpart parser;
	uc_vm_t *vm;
	uc_value_t *callback;
	bool exception;
};

static bool
lh_uc_mpart_cb(struct lh_mpart *p, enum lh_mpart_callback_type type,
               const char *buf, size_t len, void *priv)
{
	struct lh_uc_mpart *pu = priv;
	uc_value_t *res;
	bool rv = false;

	if (pu->callback && !pu->exception) {
		/* function itself */
		uc_vm_stack_push(pu->vm, ucv_get(pu->callback));

		/* arg #1: callback type */
		uc_vm_stack_push(pu->vm, ucv_uint64_new(type));

		/* arg #2: buffer data or nil */
		uc_vm_stack_push(pu->vm, buf ? ucv_string_new_length(buf, len) : NULL);

		/* arg #3: buffer length */
		uc_vm_stack_push(pu->vm, ucv_uint64_new(len));

		if (uc_vm_call(pu->vm, false, 3)) {
			pu->exception = true;

			return false;
		}

		/* pop return value from stack */
		res = uc_vm_stack_pop(pu->vm);
		rv = ucv_is_truish(res);

		/* free return value */
		ucv_put(res);
	}

	return rv;
}

static uc_value_t *
lh_uc_mpart_new(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *boundary = uc_fn_arg(0);
	uc_value_t *callback = uc_fn_arg(1);
	uc_value_t *limitarg = uc_fn_arg(2);
	struct lh_uc_mpart *pu;
	size_t limit;

	pu = calloc(1, sizeof(*pu));

	if (!pu)
		return uc_raise(vm, "Out of memory");

	lh_mpart_init(&pu->parser, NULL);

	if (ucv_type(boundary) != UC_STRING ||
	    !lh_mpart_parse_boundary(&pu->parser, ucv_string_get(boundary), NULL)) {
		lh_mpart_free(&pu->parser);

		return uc_raise(vm, "Invalid boundary argument");
	}

	if (callback && !ucv_is_callable(callback)) {
		lh_mpart_free(&pu->parser);

		return uc_raise(vm, "Invalid callback argument");
	}

	if (limitarg) {
		limit = ucv_uint64_get(limitarg);

		if (errno) {
			lh_mpart_free(&pu->parser);

			return uc_raise(vm, "Invalid limit argument");
		}

		lh_mpart_set_size_limit(&pu->parser, limit);
	}

	pu->vm = vm;
	pu->callback = ucv_get(callback);

	lh_mpart_set_callback(&pu->parser, lh_uc_mpart_cb, pu);

	return uc_resource_new(mpart_type, pu);
}

static uc_value_t *
lh_uc_mpart_parse(uc_vm_t *vm, size_t nargs)
{
	struct lh_uc_mpart **pu = uc_fn_this("lucihttp.parser.multipart");
	uc_value_t *buf = uc_fn_arg(0);

	if (buf && ucv_type(buf) != UC_STRING)
		return uc_raise(vm, "Invalid input string");

	return ucv_boolean_new(lh_mpart_parse(&(*pu)->parser,
		ucv_string_get(buf), ucv_string_length(buf)));
}

static void
lh_uc_mpart__gc(void *ud)
{
	struct lh_uc_mpart *pu = ud;

	ucv_put(pu->callback);
	lh_mpart_free(&pu->parser);
}


/*
 * urlencoded parser binding
 * -------------------------------------------------------------------------
 */

struct lh_uc_urldec {
	struct lh_urldec parser;
	uc_vm_t *vm;
	uc_value_t *callback;
	bool exception;
};

static bool
lh_uc_urldec_cb(struct lh_urldec *p, enum lh_urldec_callback_type type,
               const char *buf, size_t len, void *priv)
{
	struct lh_uc_urldec *pu = priv;
	bool rv = false;
	uc_value_t *res;

	if (pu->callback && !pu->exception) {
		/* function itself */
		uc_vm_stack_push(pu->vm, ucv_get(pu->callback));

		/* arg #1: callback type */
		uc_vm_stack_push(pu->vm, ucv_uint64_new(type));

		/* arg #2: buffer data or nil */
		uc_vm_stack_push(pu->vm, buf ? ucv_string_new_length(buf, len) : NULL);

		/* arg #3: buffer length */
		uc_vm_stack_push(pu->vm, ucv_uint64_new(len));

		if (uc_vm_call(pu->vm, false, 3)) {
			pu->exception = true;

			return false;
		}

		/* pop return value from stack */
		res = uc_vm_stack_pop(pu->vm);
		rv = ucv_is_truish(res);

		/* free return value */
		ucv_put(res);
	}

	return rv;
}


static uc_value_t *
lh_uc_urldec_new(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *callback = uc_fn_arg(0);
	uc_value_t *limitarg = uc_fn_arg(1);
	struct lh_uc_urldec *pu;
	size_t limit;

	pu = calloc(1, sizeof(*pu));

	if (!pu)
		return uc_raise(vm, "Out of memory");

	lh_urldec_init(&pu->parser, NULL);

	if (callback && !ucv_is_callable(callback)) {
		lh_urldec_free(&pu->parser);

		return uc_raise(vm, "Invalid callback argument");
	}

	if (limitarg) {
		limit = ucv_uint64_get(limitarg);

		if (errno) {
			lh_urldec_free(&pu->parser);

			return uc_raise(vm, "Invalid limit argument");
		}

		lh_urldec_set_size_limit(&pu->parser, limit);
	}

	pu->vm = vm;
	pu->callback = ucv_get(callback);

	lh_urldec_set_callback(&pu->parser, lh_uc_urldec_cb, pu);

	return uc_resource_new(urldec_type, pu);
}

static uc_value_t *
lh_uc_urldec_parse(uc_vm_t *vm, size_t nargs)
{
	struct lh_uc_urldec **pu = uc_fn_this("lucihttp.parser.urlencoded");
	uc_value_t *buf = uc_fn_arg(0);

	if (buf && ucv_type(buf) != UC_STRING)
		return uc_raise(vm, "Invalid input string");

	return ucv_boolean_new(lh_urldec_parse(&(*pu)->parser,
		ucv_string_get(buf), ucv_string_length(buf)));
}

static void
lh_uc_urldec__gc(void *ud)
{
	struct lh_uc_urldec *pu = ud;

	ucv_put(pu->callback);
	lh_urldec_free(&pu->parser);
}


/*
 * utility function bindings
 * -------------------------------------------------------------------------
 */

static uc_value_t *
lh_uc_urlencode(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *input = uc_fn_arg(0);
	uc_value_t *flags = uc_fn_arg(1);
	uc_value_t *rv;
	unsigned int f;
	char *encoded;
	size_t len;

	if (!input)
		return NULL;

	if (ucv_type(input) != UC_STRING)
		return uc_raise(vm, "Invalid input string");

	f = flags ? (unsigned int)ucv_uint64_get(flags) : 0;

	if (errno)
		return uc_raise(vm, "Invalid flags argument");

	encoded = lh_urlencode(
		ucv_string_get(input), ucv_string_length(input), &len, f);

	if (!encoded)
		return NULL;

	rv = ucv_string_new_length(encoded, len);

	free(encoded);

	return rv;
}

static uc_value_t *
lh_uc_urldecode(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *input = uc_fn_arg(0);
	uc_value_t *flags = uc_fn_arg(1);
	uc_value_t *rv;
	unsigned int f;
	char *decoded;
	size_t len;

	if (!input)
		return NULL;

	if (ucv_type(input) != UC_STRING)
		return uc_raise(vm, "Invalid input string");

	f = flags ? (unsigned int)ucv_uint64_get(flags) : 0;

	if (errno)
		return uc_raise(vm, "Invalid flags argument");

	decoded = lh_urldecode(
		ucv_string_get(input), ucv_string_length(input), &len, f);

	if (!decoded)
		return NULL;

	rv = ucv_string_new_length(decoded, len);

	free(decoded);

	return rv;
}

static uc_value_t *
lh_uc_header_attribute(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *input = uc_fn_arg(0);
	uc_value_t *attr = uc_fn_arg(1);
	uc_value_t *rv;
	char *value;
	size_t len;

	if (!input)
		return NULL;

	if (ucv_type(input) != UC_STRING)
		return uc_raise(vm, "Invalid input string");

	if (attr && ucv_type(attr) != UC_STRING)
		return uc_raise(vm, "Invalid attribute string");

	value = lh_header_attribute(
		ucv_string_get(input), ucv_string_length(input),
		ucv_string_get(attr), &len);

	if (!value)
		return NULL;

	rv = ucv_string_new_length(value, len);

	free(value);

	return rv;
}


/*
 * module tables
 * -------------------------------------------------------------------------
 */

static const uc_function_list_t mpart_fns[] = {
	{ "parse", lh_uc_mpart_parse }
};

static const uc_function_list_t urldec_fns[] = {
	{ "parse", lh_uc_urldec_parse }
};

static const uc_function_list_t global_fns[] = {
	{ "multipart_parser",  lh_uc_mpart_new        },
	{ "urlencoded_parser", lh_uc_urldec_new       },
	{ "urlencode",         lh_uc_urlencode        },
	{ "urldecode",         lh_uc_urldecode        },
	{ "header_attribute",  lh_uc_header_attribute }
};


#define add_const(obj, key, defkey) \
	ucv_object_add(obj, #key, ucv_uint64_new(defkey))

#define add_const_global(obj, key) add_const(obj, key, LH_URL ## key)
#define add_const_mpart(obj, key) add_const(obj, key, LH_MP_CB_ ## key)
#define add_const_urldec(obj, key) add_const(obj, key, LH_UD_CB_ ## key)

void uc_module_init(uc_vm_t *vm, uc_value_t *scope)
{
	uc_function_list_register(scope, global_fns);

	add_const_global(scope, ENCODE_FULL);
	add_const_global(scope, ENCODE_IF_NEEDED);
	add_const_global(scope, ENCODE_SPACE_PLUS);
	add_const_global(scope, DECODE_STRICT);
	add_const_global(scope, DECODE_IF_NEEDED);
	add_const_global(scope, DECODE_KEEP_PLUS);
	add_const_global(scope, DECODE_PLUS);


	mpart_type = uc_type_declare(vm, "lucihttp.parser.multipart", mpart_fns, lh_uc_mpart__gc);

	add_const_mpart(mpart_type->proto, BODY_BEGIN);
	add_const_mpart(mpart_type->proto, PART_INIT);
	add_const_mpart(mpart_type->proto, HEADER_NAME);
	add_const_mpart(mpart_type->proto, HEADER_VALUE);
	add_const_mpart(mpart_type->proto, PART_BEGIN);
	add_const_mpart(mpart_type->proto, PART_DATA);
	add_const_mpart(mpart_type->proto, PART_END);
	add_const_mpart(mpart_type->proto, BODY_END);
	add_const_mpart(mpart_type->proto, EOF);
	add_const_mpart(mpart_type->proto, ERROR);


	urldec_type = uc_type_declare(vm, "lucihttp.parser.urlencoded", urldec_fns, lh_uc_urldec__gc);

	add_const_urldec(urldec_type->proto, TUPLE);
	add_const_urldec(urldec_type->proto, NAME);
	add_const_urldec(urldec_type->proto, VALUE);
	add_const_urldec(urldec_type->proto, EOF);
	add_const_urldec(urldec_type->proto, ERROR);
}
