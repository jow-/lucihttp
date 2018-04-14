/*
 * lucihttp - HTTP utility library - Lua binding
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

#include <lucihttp/lua.h>
#include <lucihttp/utils.h>
#include <lucihttp/multipart-parser.h>
#include <lucihttp/urlencoded-parser.h>

#include <stdlib.h>


/*
 * multipart parser binding
 * -------------------------------------------------------------------------
 */

struct lh_L_mpart {
	lua_State *L;
	int callback;
	void *parser;
};

static bool
lh_L_mpart_cb(struct lh_mpart *p, enum lh_mpart_callback_type type,
              const char *buf, size_t len, void *priv)
{
	struct lh_L_mpart *pu = priv;
	bool rv = false;

	if (pu->callback != -1) {
		/* function itself */
		lua_rawgeti(pu->L, LUA_REGISTRYINDEX, pu->callback);

		/* arg #1: callback type */
		lua_pushnumber(pu->L, type);

		/* arg #2: buffer data or nil */
		if (buf)
			lua_pushlstring(pu->L, buf, len);
		else
			lua_pushnil(pu->L);

		/* arg #3: buffer length */
		lua_pushnumber(pu->L, len);

		/* call, expect one boolean return */
		lua_call(pu->L, 3, 1);

		/* fetch result */
		rv = lua_toboolean(pu->L, -1);

		/* pop result */
		lua_pop(pu->L, 1);
	}

	return rv;
}


static int
lh_L_mpart_new(lua_State *L)
{
	const char *boundary_header = luaL_checkstring(L, 1);
	struct lh_L_mpart *pu;
	struct lh_mpart *p;

	p = lh_mpart_new(NULL);

	if (!p) {
		lua_pushnil(L);
		lua_pushstring(L, "Out of memory");
		return 2;
	}

	pu = lua_newuserdata(L, sizeof(*pu));

	if (!pu) {
		lh_mpart_free(p);
		return 0;
	}

	luaL_getmetatable(L, LUCIHTTP_MPART_META);
	lua_setmetatable(L, -2);

	lh_mpart_set_callback(p, lh_L_mpart_cb, pu);
	lh_mpart_parse_boundary(p, boundary_header, NULL);

	if (lua_type(L, 2) == LUA_TFUNCTION) {
		lua_pushvalue(L, 2);
		pu->callback = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	else {
		pu->callback = -1;
	}

	pu->L = L;
	pu->parser = p;
	return 1;
}

static int
lh_L_mpart_parse(lua_State *L)
{
	size_t len = 0;
	struct lh_L_mpart *pu = luaL_checkudata(L, 1, LUCIHTTP_MPART_META);
	const char *buf = luaL_optlstring(L, 2, NULL, &len);

	if (pu->parser)
		lua_pushboolean(L, lh_mpart_parse(pu->parser, buf, len));
	else
		lua_pushnil(L);

	return 1;
}

static int
lh_L_mpart__gc(lua_State *L)
{
	struct lh_L_mpart *pu = luaL_checkudata(L, 1, LUCIHTTP_MPART_META);

	if (pu && pu->parser) {
		lh_mpart_free(pu->parser);
		pu->parser = NULL;
	}

	return 0;
}


/*
 * urlencoded parser binding
 * -------------------------------------------------------------------------
 */

struct lh_L_urldec {
	lua_State *L;
	int callback;
	struct lh_urldec *parser;
};

static bool
lh_L_urldec_cb(struct lh_urldec *p, enum lh_urldec_callback_type type,
               const char *buf, size_t len, void *priv)
{
	struct lh_L_urldec *pu = priv;
	bool rv = false;

	if (pu->callback != -1) {
		/* function itself */
		lua_rawgeti(pu->L, LUA_REGISTRYINDEX, pu->callback);

		/* arg #1: callback type */
		lua_pushnumber(pu->L, type);

		/* arg #2: buffer data or nil */
		if (buf)
			lua_pushlstring(pu->L, buf, len);
		else
			lua_pushnil(pu->L);

		/* arg #3: buffer length */
		lua_pushnumber(pu->L, len);

		/* call, expect one boolean return */
		lua_call(pu->L, 3, 1);

		/* fetch result */
		rv = lua_toboolean(pu->L, -1);

		/* pop result */
		lua_pop(pu->L, 1);
	}

	return rv;
}


static int
lh_L_urldec_new(lua_State *L)
{
	struct lh_L_urldec *pu;
	struct lh_urldec *p;

	p = lh_urldec_new(NULL);

	if (!p) {
		lua_pushnil(L);
		lua_pushstring(L, "Out of memory");
		return 2;
	}

	pu = lua_newuserdata(L, sizeof(*pu));

	if (!pu) {
		lh_urldec_free(p);
		return 0;
	}

	luaL_getmetatable(L, LUCIHTTP_URLDEC_META);
	lua_setmetatable(L, -2);

	lh_urldec_set_callback(p, lh_L_urldec_cb, pu);

	if (lua_type(L, 1) == LUA_TFUNCTION) {
		lua_pushvalue(L, 1);
		pu->callback = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	else {
		pu->callback = -1;
	}

	pu->L = L;
	pu->parser = p;
	return 1;
}

static int
lh_L_urldec_parse(lua_State *L)
{
	size_t len = 0;
	struct lh_L_urldec *pu = luaL_checkudata(L, 1, LUCIHTTP_URLDEC_META);
	const char *buf = luaL_optlstring(L, 2, NULL, &len);

	if (pu->parser)
		lua_pushboolean(L, lh_urldec_parse(pu->parser, buf, len));
	else
		lua_pushnil(L);

	return 1;
}

static int
lh_L_urldec__gc(lua_State *L)
{
	struct lh_L_urldec *pu = luaL_checkudata(L, 1, LUCIHTTP_URLDEC_META);

	if (pu && pu->parser) {
		lh_urldec_free(pu->parser);
		pu->parser = NULL;
	}

	return 0;
}


/*
 * utility function bindings
 * -------------------------------------------------------------------------
 */

static int
lh_L_urlencode(lua_State *L)
{
	size_t len;
	char *encoded;
	const char *str = luaL_optlstring(L, 1, NULL, &len);
	unsigned int flags = luaL_optnumber(L, 2, 0);

	if (!str)
		return 0;

	encoded = lh_urlencode(str, len, &len, flags);

	if (!encoded) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushlstring(L, encoded, len);
	free(encoded);
	return 1;
}

static int
lh_L_urldecode(lua_State *L)
{
	size_t len;
	char *decoded;
	const char *str = luaL_optlstring(L, 1, NULL, &len);
	unsigned int flags = luaL_optnumber(L, 2, 0);

	if (!str)
		return 0;

	decoded = lh_urldecode(str, len, &len, flags);

	if (!decoded) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushlstring(L, decoded, len);
	free(decoded);
	return 1;
}

static int
lh_L_header_attribute(lua_State *L)
{
	size_t len;
	char *value;
	const char *hval = luaL_optlstring(L, 1, NULL, &len);
	const char *attr = luaL_optstring(L, 2, NULL);

	if (!hval)
		return 0;

	value = lh_header_attribute(hval, len, attr, &len);

	if (!value) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushlstring(L, value, len);
	return 1;
}


/*
 * module tables
 * -------------------------------------------------------------------------
 */

static const luaL_reg R_mpart[] = {
	{ "parse", lh_L_mpart_parse },
	{ "__gc",  lh_L_mpart__gc   },
	{ }
};

static const luaL_reg R_urldec[] = {
	{ "parse", lh_L_urldec_parse },
	{ "__gc",  lh_L_urldec__gc   },
	{ }
};

static const luaL_reg R[] = {
	{ "multipart_parser",  lh_L_mpart_new        },
	{ "urlencoded_parser", lh_L_urldec_new       },
	{ "urlencode",         lh_L_urlencode        },
	{ "urldecode",         lh_L_urldecode        },
	{ "header_attribute",  lh_L_header_attribute },
	{ }
};

LUALIB_API int luaopen_lucihttp(lua_State *L) {
	luaL_newmetatable(L, LUCIHTTP_MPART_META);
	luaL_register(L, NULL, R_mpart);

	lua_pushnumber(L, LH_MP_CB_BODY_BEGIN);
	lua_setfield(L, -2, "BODY_BEGIN");

	lua_pushnumber(L, LH_MP_CB_PART_INIT);
	lua_setfield(L, -2, "PART_INIT");

	lua_pushnumber(L, LH_MP_CB_HEADER_NAME);
	lua_setfield(L, -2, "HEADER_NAME");

	lua_pushnumber(L, LH_MP_CB_HEADER_VALUE);
	lua_setfield(L, -2, "HEADER_VALUE");

	lua_pushnumber(L, LH_MP_CB_PART_BEGIN);
	lua_setfield(L, -2, "PART_BEGIN");

	lua_pushnumber(L, LH_MP_CB_PART_DATA);
	lua_setfield(L, -2, "PART_DATA");

	lua_pushnumber(L, LH_MP_CB_PART_END);
	lua_setfield(L, -2, "PART_END");

	lua_pushnumber(L, LH_MP_CB_BODY_END);
	lua_setfield(L, -2, "BODY_END");

	lua_pushnumber(L, LH_MP_CB_EOF);
	lua_setfield(L, -2, "EOF");

	lua_pushnumber(L, LH_MP_CB_ERROR);
	lua_setfield(L, -2, "ERROR");

	lua_pushvalue(L, -1);
	lua_setfield(L, -1, "__index");


	luaL_newmetatable(L, LUCIHTTP_URLDEC_META);
	luaL_register(L, NULL, R_urldec);

	lua_pushnumber(L, LH_UD_CB_TUPLE);
	lua_setfield(L, -2, "TUPLE");

	lua_pushnumber(L, LH_UD_CB_NAME);
	lua_setfield(L, -2, "NAME");

	lua_pushnumber(L, LH_UD_CB_VALUE);
	lua_setfield(L, -2, "VALUE");

	lua_pushnumber(L, LH_UD_CB_EOF);
	lua_setfield(L, -2, "EOF");

	lua_pushnumber(L, LH_UD_CB_ERROR);
	lua_setfield(L, -2, "ERROR");

	lua_pushvalue(L, -1);
	lua_setfield(L, -1, "__index");


	luaL_newmetatable(L, LUCIHTTP_META);
	luaL_register(L, LUCIHTTP_META, R);

	lua_pushnumber(L, LH_URLENCODE_FULL);
	lua_setfield(L, -2, "ENCODE_FULL");

	lua_pushnumber(L, LH_URLENCODE_IF_NEEDED);
	lua_setfield(L, -2, "ENCODE_IF_NEEDED");

	lua_pushnumber(L, LH_URLENCODE_SPACE_PLUS);
	lua_setfield(L, -2, "ENCODE_SPACE_PLUS");

	lua_pushnumber(L, LH_URLDECODE_STRICT);
	lua_setfield(L, -2, "DECODE_STRICT");

	lua_pushnumber(L, LH_URLDECODE_IF_NEEDED);
	lua_setfield(L, -2, "DECODE_IF_NEEDED");

	lua_pushnumber(L, LH_URLDECODE_KEEP_PLUS);
	lua_setfield(L, -2, "DECODE_KEEP_PLUS");

	lua_pushvalue(L, -1);
	lua_setfield(L, -1, "__index");

	return 1;
};
