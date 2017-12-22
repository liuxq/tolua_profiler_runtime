
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lp.h"
#if defined(linux)
#define __USE_GNU 1
#include <dlfcn.h>
#endif

#include "lua.h"
#include "lauxlib.h"


/* Indices for the main profiler stack and for the original exit function */
static int is_pause = 0;
static int is_start = 0;

static int lua_nTableCount = 0;
static int lua_nFuncCount = 0;
static int lua_nThreadCount = 0;
static int lua_nUserDataCount = 0;


#define TABLE 1
#define FUNCTION 2
#define SOURCE 3
#define THREAD 4
#define USERDATA 5
#define MARK 6

#define mark_function_env(L,dL,t)
//static int profresult_id;

static void mark_object(lua_State *L, lua_State *dL, const void * parent, const char * desc);

#if defined(linux)
	#define DWORD int
	#define WINAPI
#endif


static FILE* g_fMemorySnapshot = NULL;
//static int profiler_clear(lua_State *L);

/************************************************************************/
/* Lua memory snapshot                                                  */
/************************************************************************/
static int ismarked(lua_State *dL, const void *p) {
	lua_rawgetp(dL, MARK, p);
	if (lua_isnil(dL,-1)) {
		lua_pop(dL,1);
		lua_pushboolean(dL,1);
		lua_rawsetp(dL, MARK, p);
		return 0;
	}
	lua_pop(dL,1);
	return 1;
}

static const void *readobject(lua_State *L, lua_State *dL, const void *parent, const char *desc) {
	int t = lua_type(L, -1);
	const void * p = lua_topointer(L, -1);
	int tidx = 0;
	switch (t) {
	case LUA_TTABLE:
		tidx = TABLE;
		break;
	case LUA_TFUNCTION:
		tidx = FUNCTION;
		break;
	case LUA_TTHREAD:
		tidx = THREAD;
		break;
	case LUA_TUSERDATA:
		tidx = USERDATA;
		break;
	default:
		return NULL;
	}

	if (ismarked(dL, p)) {
		lua_rawgetp(dL, tidx, p);
		if (!lua_isnil(dL,-1)) {
			lua_pushstring(dL,desc);
			lua_rawsetp(dL, -2, parent);
		}
		lua_pop(dL,1);
		lua_pop(L,1);
		return NULL;
	}

	lua_newtable(dL);
	lua_pushstring(dL,desc);
	lua_rawsetp(dL, -2, parent);
	lua_rawsetp(dL, tidx, p);

	return p;
}

static const char *keystring(lua_State *L, int index, char * buffer) {
	int t = lua_type(L,index);
	switch (t) {
	case LUA_TSTRING:
		return lua_tostring(L,index);
	case LUA_TNUMBER:
		sprintf(buffer,"[%lg]",lua_tonumber(L,index));
		break;
	case LUA_TBOOLEAN:
		sprintf(buffer,"[%s]",lua_toboolean(L,index) ? "true" : "false");
		break;
	case LUA_TNIL:
		sprintf(buffer,"[nil]");
		break;
	default:
		sprintf(buffer,"[%s:%p]",lua_typename(L,t),lua_topointer(L,index));
		break;
	}
	return buffer;
}

static void mark_table(lua_State *L, lua_State *dL, const void * parent, const char * desc) {
	int type = lua_type(L, -1);
	const void * t = readobject(L, dL, parent, desc);
	int weakk = 0;
	int weakv = 0;
	const char *mode = NULL;
	const char *key = NULL;
	char* name = NULL;
	char* value = NULL;
	char addr[32];
	if (t == NULL)
		return;
	sprintf(addr, "%p",t );
	//output("%p:%s\n", t, desc);
	name = (char*)malloc(sizeof(char)*(strlen(desc) + strlen(addr) + 4));
	sprintf(name, "%p:%s", t, desc);
	if (lua_getmetatable(L, -1)) {
		lua_pushliteral(L, "__mode");
		lua_rawget(L, -2);
		if (lua_isstring(L,-1)) {
			mode = lua_tostring(L, -1);
			if (strchr(mode, 'k')) {
				weakk = 1;
			}
			if (strchr(mode, 'v')) {
				weakv = 1;
			}
		}
		lua_pop(L,1);

		luaL_checkstack(L, LUA_MINSTACK, NULL);
		mark_table(L, dL, t, "[metatable]");
	}
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (weakv) {
			lua_pop(L,1);
		} else {
			char temp[32];
			key = keystring(L, -2, temp);
			if (!value)
			{
				int count = (int)sizeof(char) * (int)(strlen(key) + 2);
				value = (char*)malloc(count);
				memset(value, 0, count);
				strcpy(value, key);
			}
			else
			{
				value = (char*)realloc(value, sizeof(char)*(strlen(value) + strlen(key) + 2));
				strcat(value, key);
			}
			strcat(value, "\n");
			//output("      ----- %s %d\n", key,type);
			mark_object(L, dL, t , key);
		}
		if (!weakk) {
			lua_pushvalue(L,-1);
			mark_object(L, dL, t , "[key]");
		}
	}
	if (name)
	{
		lprofP_outputToFile(g_fMemorySnapshot,"%s\n", name);
		free(name);
	}
	if (value)
	{
		lprofP_outputToFile(g_fMemorySnapshot,"%s\n", value);
		free(value);
	}
	lua_pop(L,1);
}

static void mark_userdata(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	int type = lua_type(L, -1);
	if (t == NULL)
		return;
	//output("--------------addr %p   %d    %s\n", t, type, desc);
	if (lua_getmetatable(L, -1)) {
		mark_table(L, dL, t, "[metatable]");
	}

	lua_getuservalue(L,-1);
	if (lua_isnil(L,-1)) {
		lua_pop(L,2);
	} else {
		mark_table(L, dL, t, "[uservalue]");
		lua_pop(L,1);
	}
}

static void mark_function(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	int type = lua_type(L, -1);
	
	const char *name = NULL;
	char tmp[16];
	int i;
	lua_Debug ar;
	luaL_Buffer b;
	if (t == NULL)
		return;
	//output("--------------addr %p   %d   %s\n", t, type, desc);
	mark_function_env(L,dL,t);

	for (i=1;;i++) {
		name = lua_getupvalue(L,-1,i);
		if (name == NULL)
			break;
		mark_object(L, dL, t, name[0] ? name : "[upvalue]");
	}
	if (lua_iscfunction(L,-1)) {
		if (i==1) {
			// light c function
			lua_pushnil(dL);
			lua_rawsetp(dL, FUNCTION, t);
		}
		lua_pop(L,1);
	} else {

		lua_getinfo(L, ">S", &ar);

		luaL_buffinit(dL, &b);
		luaL_addstring(&b, ar.short_src);

		sprintf(tmp,":%d",ar.linedefined);
		luaL_addstring(&b, tmp);
		luaL_pushresult(&b);
		//output("      ----- %s%s %d\n", ar.short_src,tmp, type);
		lua_rawsetp(dL, SOURCE, t);
	}
}

static void mark_thread(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	int type = lua_type(L, -1);
	
	int top = 0;
	int level = 0;
	lua_State *cL = NULL;
	char tmp[128];
	lua_Debug ar;
	luaL_Buffer b;
	const char * name = NULL;
	int i,j;
	if (t == NULL)
		return;
	//output("--------------addr %p   %d   %s\n", t, type, desc);
	cL = lua_tothread(L,-1);
	if (cL == L) {
		level = 1;
	} else {
		// mark stack
		top = lua_gettop(cL);
		luaL_checkstack(cL, 1, NULL);

		for (i=0;i<top;i++) {
			lua_pushvalue(cL, i+1);
			sprintf(tmp, "[%d]", i+1);
			mark_object(cL, dL, cL, tmp);
		}
	}

	luaL_buffinit(dL, &b);
	while (lua_getstack(cL, level, &ar)) {
		lua_getinfo(cL, "Sl", &ar);
		luaL_addstring(&b, ar.short_src);
		if (ar.currentline >=0) {
			memset(tmp,0x0,128);
			sprintf(tmp,":%d ",ar.currentline);
			//output("      ----- %s%s %d\n", ar.short_src, tmp, type);
			luaL_addstring(&b, tmp);
		}


		for (j=1;j>-1;j-=2) {
			for (i=j;;i+=j) {
				name = lua_getlocal(cL, &ar, i);
				if (name == NULL)
					break;
				//snprintf(tmp, sizeof(tmp), "%s : %s:%d",name,ar.short_src,ar.currentline);
				memset(tmp,0x0,128);
				sprintf(tmp, "%s : %s:%d",name,ar.short_src,ar.currentline);
				mark_object(cL, dL, t, tmp);
			}
		}

		++level;
	}
	luaL_pushresult(&b);
	lua_rawsetp(dL, SOURCE, t);
	lua_pop(L,1);
}

static void mark_object(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	int t;
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	t = lua_type(L, -1);
	switch (t) {
	case LUA_TTABLE:
		mark_table(L, dL, parent, desc);
		break;
	case LUA_TUSERDATA:
		mark_userdata(L, dL, parent, desc);
		break;
	case LUA_TFUNCTION:
		mark_function(L, dL, parent, desc);
		break;
	case LUA_TTHREAD:
		mark_thread(L, dL, parent, desc);
		break;
	default:
		lua_pop(L,1);
		break;
	}
}

static int count_table(lua_State *L, int idx) {
	int n = 0;
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		++n;
		lua_pop(L,1);
	}
	return n;
}

static void gen_table_desc(lua_State *dL, luaL_Buffer *b, const void * parent, const char *desc) {
	char tmp[32];
	size_t l = sprintf(tmp,"%p : ",parent);
	luaL_addlstring(b, tmp, l);
	luaL_addstring(b, desc);
	luaL_addchar(b, '\n');
}

static void pdesc(lua_State *L, lua_State *dL, int idx, const char * type_name) {
	size_t l = 0;
	const char* s = NULL;
	const void * parent = NULL;
	const char * desc = NULL;
	const void * key = NULL;
	lua_pushnil(dL);
	while (lua_next(dL, idx) != 0) {
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		key = lua_touserdata(dL, -2);
		if (idx == FUNCTION) {
			lua_rawgetp(dL, SOURCE, key);
			if (lua_isnil(dL, -1)) {
				luaL_addstring(&b,"cfunction\n");
			} else {
				l = 0;
				s = lua_tolstring(dL, -1, &l);
				luaL_addstring(&b, "function\n");
				luaL_addlstring(&b,s,l);
				luaL_addchar(&b,'\n');
			}
			lua_pop(dL, 1);
		} else if (idx == THREAD) {
			lua_rawgetp(dL, SOURCE, key);
			l = 0;
			s = lua_tolstring(dL, -1, &l);
			luaL_addstring(&b, "thread\n");
			luaL_addlstring(&b,s,l);
			luaL_addchar(&b,'\n');
			lua_pop(dL, 1);
		} else {
			luaL_addstring(&b, type_name);
			luaL_addchar(&b,'\n');
		}
		lua_pushnil(dL);
		while (lua_next(dL, -2) != 0) {
			parent = lua_touserdata(dL,-2);
			desc = luaL_checkstring(dL,-1);
			gen_table_desc(dL, &b, parent, desc);
			//output("--------------------------------------------------    %s\n", desc);
			lua_pop(dL,1);
		}
		luaL_pushresult(&b);
		lua_rawsetp(L, -2, key);
		lua_pop(dL,1);
	}
}

static void gen_result(lua_State *L, lua_State *dL) {
	int count = 0;
	count += count_table(dL, TABLE);
	count += count_table(dL, FUNCTION);
	count += count_table(dL, USERDATA);
	count += count_table(dL, THREAD);
	lua_createtable(L, 0, count);
	pdesc(L, dL, TABLE, "table");
	pdesc(L, dL, USERDATA, "userdata");
	pdesc(L, dL, FUNCTION, "function");
	pdesc(L, dL, THREAD, "thread");
}

static void count_result(lua_State* L,lua_State* dL)
{
	lua_nTableCount = count_table(dL,TABLE);
	lua_nFuncCount = count_table(dL,FUNCTION);
	lua_nUserDataCount = count_table(dL,USERDATA);
	lua_nThreadCount = count_table(dL,THREAD);
}

/*static int memory_snapshot(lua_State *L)
{
	traverse_table(L, -1);
	return 0;
}
*/


static int memory_snapshot(lua_State *L) {
	int i;
	const char* file = NULL;
	//char sz[160];
	lua_State *dL = NULL;
	if(lua_gettop(L) >= 1)
		file = luaL_checkstring(L, 1);
	if(file)
	{
		g_fMemorySnapshot = fopen(file,"w");
		if(g_fMemorySnapshot)
		{
			dL = luaL_newstate();
			for (i=0;i<MARK;i++) {
				lua_newtable(dL);
			}
			lua_pushvalue(L, LUA_REGISTRYINDEX);
			mark_table(L, dL, NULL, "[registry]");
			fclose(g_fMemorySnapshot);
			lua_close(dL);
			g_fMemorySnapshot = NULL;
		}
	}
	return 0;
}


/************************************************************************/
/*         Lua Register Function                                        */
/************************************************************************/
int memprofiler_open(lua_State *L)
{
	is_pause = 0;
	is_start = 0;
	lua_register(L, "profiler_snapshot",memory_snapshot);

	return 1;
}



