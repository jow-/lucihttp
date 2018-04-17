cmake_minimum_required(VERSION 2.6)

PROJECT(liblucihttp C)

OPTION(BUILD_LUA "build Lua binding" ON)
OPTION(BUILD_TESTS "build test programs" ON)

ADD_DEFINITIONS(-Os -ggdb -Wall -Werror --std=gnu99 -Wmissing-declarations -Wno-format-truncation)

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

INCLUDE_DIRECTORIES("include")


ADD_LIBRARY(liblucihttp SHARED
	lib/utils.c
	lib/multipart-parser.c
	lib/urlencoded-parser.c)

SET_TARGET_PROPERTIES(liblucihttp PROPERTIES
	OUTPUT_NAME lucihttp
	PREFIX lib)

INSTALL(TARGETS
	liblucihttp
	LIBRARY DESTINATION lib)

INSTALL(FILES
	include/lucihttp/utils.h
	include/lucihttp/multipart-parser.h
	include/lucihttp/urlencoded-parser.h
	DESTINATION include/lucihttp)


IF(BUILD_LUA)
	IF(NOT LUA_CFLAGS)
		INCLUDE(FindPkgConfig)
		pkg_search_module(LUA lua5.1 lua-5.1 lua)
	ENDIF()

	IF(NOT LUAPATH)
		EXECUTE_PROCESS(
			COMMAND  lua -e "for k in string.gmatch(package.cpath .. \";\", \"([^;]+)/..so;\") do if k:sub(1,1) == \"/\" then print(k) break end end"
			OUTPUT_VARIABLE LUAPATH
			RESULT_VARIABLE LUA_CHECK_RES
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		IF(NOT ${LUA_CHECK_RES} EQUAL 0 OR "${LUAPATH}" EQUAL "")
			MESSAGE(SEND_ERROR "Lua was not found on your system")
		ENDIF()
	ENDIF()

	ADD_DEFINITIONS(${LUA_CFLAGS})

	ADD_LIBRARY(liblucihttp-lua SHARED
		lib/lua.c)

	SET_TARGET_PROPERTIES(liblucihttp-lua PROPERTIES
		OUTPUT_NAME lucihttp
		PREFIX "")

	TARGET_LINK_LIBRARIES(liblucihttp-lua liblucihttp ${LUA_LIBRARIES})

	INSTALL(TARGETS
		liblucihttp-lua
		LIBRARY DESTINATION ${LUAPATH})

	INSTALL(FILES
		include/lucihttp/lua.h
		DESTINATION include/lucihttp)
ENDIF()


IF(BUILD_TESTS)
	ADD_EXECUTABLE(test-utils src/test-utils.c)
	TARGET_LINK_LIBRARIES(test-utils liblucihttp)

	ADD_EXECUTABLE(test-multipart-parser src/test-multipart-parser.c)
	TARGET_LINK_LIBRARIES(test-multipart-parser liblucihttp)

	ADD_EXECUTABLE(test-urlencoded-parser src/test-urlencoded-parser.c)
	TARGET_LINK_LIBRARIES(test-urlencoded-parser liblucihttp)

	INSTALL(TARGETS
		test-utils
		test-multipart-parser
		test-urlencoded-parser
		RUNTIME DESTINATION bin)
endif()
