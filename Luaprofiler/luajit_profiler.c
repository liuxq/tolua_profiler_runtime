/*
** LuaProfiler
** Copyright Kepler Project 2005-2007 (http://www.keplerproject.org/luaprofiler)
** $Id: lua50_profiler.c,v 1.16 2008-05-20 21:16:36 mascarenhas Exp $
*/

/*****************************************************************************
lua50_profiler.c:
Lua version dependent profiler interface
*****************************************************************************/
/*
	解决跨平台编译时宏控制import/export的问题 lennon.c
	2016-08-11 lennon.c
	*/
#define LUA_CORE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lp.h"
#if defined(linux)
#define __USE_GNU 1
#include <dlfcn.h>
#endif

#include "clocks.h"
#include "core_profiler.h"
#include "function_meter.h"

#include "lua.h"
#include "lauxlib.h"
#include "luaprofiler.h"
#include "queue.h"


/* Indices for the main profiler stack and for the original exit function */
static int is_pause = 0;
static int is_start = 0;
static int exit_id = 0;
static int profstate_id = 0;

double stat_hook_cost_ts = 0;
double stat_frame_cost_ts = 0;
int    stat_hook_call_cnt = 0;

lprofP_STATE* g_S = NULL;

double    curMem = 0;

/* Forward declaration */
static int profiler_stop(lua_State *L);
static int profiler_frame(lua_State *L);

#if defined(linux)
#define DWORD int
#define WINAPI
#endif

//static int profiler_clear(lua_State *L);

void handle_dbg_info(lprof_DebugInfo* dbg_info) {

	if (dbg_info->type == FUNCTION_HOOK) {
		if (!dbg_info->event) {
			/* entering a function */
			lprofP_callhookIN(g_S, (char *)dbg_info->p_name,
				(char *)dbg_info->p_source, dbg_info->linedefined,
				dbg_info->currentline, (char *)dbg_info->p_what, dbg_info->ccallname, dbg_info);
		}
		else { /* ar->event == "return" */
			lprofP_callhookOUT(g_S, dbg_info);
		}

	}
	else if (dbg_info->type == FRAME) {
		lprofT_frame(dbg_info->frameid, dbg_info->currentMem, dbg_info->hook_cost_cs, dbg_info->hook_call_cnt);
	}
	else {

		printf("Error type=%d!", dbg_info->type);
	}

	free(dbg_info);
}

#if defined(linux)
static const char* get_base_name(const char* fullpath){

	if (NULL == fullpath)
		return "";

	int len = strlen(fullpath);

	if(len == 0)
		return fullpath;

	int i = 0;

	for(i = len - 1; i > 0; i--){

		if(fullpath[i] == '/'){
			if(i < len - 1){
				return fullpath + i + 1;
			}
		}
	}

	return fullpath;

}
#endif


/* called by Lua (via the callhook mechanism) */
static void callhook(lua_State *L, lua_Debug *ar) {

	static int profilerLevelOffset = 0;
	LARGE_INTEGER nBeginTime;

	lprofC_start_timer2(&nBeginTime);

	int currentline;
	lua_Debug previous_ar;

	if (lua_getstack(L, 1, &previous_ar) == 0) {
		currentline = -1;
	}
	else {
		lua_getinfo(L, "l", &previous_ar);
		currentline = previous_ar.currentline;
	}

	lua_getinfo(L, "nSf", ar);

	//过滤c调用，因为没有return
	static char *cCall = "=[C]";
	if (strcmp(ar->source, cCall) == 0)
	{
		//debugLog("=[c]return %d  m:%s f:%s\n", ar->event, ar->source, ar->name);
		lua_pop(L, 1);
		return;
	}

	// 过滤begin和endsample
	/*static char *beginSample = "p_begin";
	static char *endSample = "p_end";
	int isSampleFunc = 0;
	if (ar->name && strcmp(ar->name, beginSample) == 0)
	{
	if (ar->event == 0)
	{
	isSampleFunc = 1;
	}
	else
	{
	++profilerLevelOffset;
	lua_pop(L, 1);
	return;
	}
	}

	if (ar->name && ar->event == 0 && strcmp(ar->name, endSample) == 0)
	{
	--profilerLevelOffset;
	lua_pop(L, 1);
	return;
	}*/
	//end 过滤begin和endsample

	lua_Debug sdebug;
	int level = 0;
	while (lua_getstack(L, level, &sdebug)) {
		level++;
	}
	lprof_DebugInfo* dbg_info = (lprof_DebugInfo*)malloc(sizeof(lprof_DebugInfo));
	dbg_info->type = FUNCTION_HOOK;
	LPROF_COPY_LUA_DEBUG_INFO(dbg_info, ar, name, LP_MAX_NAME_LEN);
	LPROF_COPY_LUA_DEBUG_INFO(dbg_info, ar, source, LP_MAX_SOURCE_LEN);
	LPROF_COPY_LUA_DEBUG_INFO(dbg_info, ar, what, LP_MAX_WHAT_LEN);
	dbg_info->event = ar->event;
	dbg_info->currentline = currentline;
	dbg_info->ccallname[0] = '\0';
	dbg_info->linedefined = ar->linedefined;
	dbg_info->currentMem = (double)lua_gc(L, LUA_GCCOUNTB, 0) + (double)(lua_gc(L, LUA_GCCOUNT, 0) * 1024);

	curMem = dbg_info->currentMem;

	/*if (isSampleFunc)
	{
	static char *defaultSampleMod = "p_sampler";
	strcpy(dbg_info->source, defaultSampleMod);

	lua_getlocal(L, ar, 1);
	const char* v = lua_tostring(L, -1);
	if (v)
	{
	strcpy(dbg_info->name, v);
	}
	else
	{
	static char *defaultSample = "sample";
	strcpy(dbg_info->name, defaultSample);
	}
	lua_pop(L, 1);
	}*/

	lprofC_start_timer2(&dbg_info->currenttime);

#if defined(linux)
	if (ar->source &&  strcmp(ar->source, "=[C]") == 0)
	{
		dbg_info->ccallname[99] = '\0';
		void* cfun = lua_tocfunction(L, -1);
		Dl_info info;
		dladdr(cfun, &info);
		int xx = (int)((unsigned  int)cfun - (unsigned int)info.dli_fbase);
		snprintf(dbg_info->ccallname, 98, "(CFUN(%s 0x%x %p))\0", get_base_name(info.dli_fname), xx, cfun);
	}
#endif

	lua_pop(L, 1);

	dbg_info->level = level + profilerLevelOffset;

	//debugLog("%d  m:%s f:%s level:%d\n", dbg_info->event, dbg_info->p_source, dbg_info->p_name, level);

	handle_dbg_info(dbg_info);

	double hookTime = lprofC_get_seconds2(&nBeginTime);
	stat_hook_cost_ts += hookTime;

	stat_hook_call_cnt += 1;

}


/* Lua function to exit politely the profiler                               */
/* redefines the lua exit() function to not break the log file integrity    */
/* The log file is assumed to be valid if the last entry has a stack level  */
/* of 1 (meaning that the function 'main' has been exited)                  */
static void exit_profiler(lua_State *L) {
	lprofP_STATE* S;
	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	S = (lprofP_STATE*)lua_touserdata(L, -1);
	/* leave all functions under execution */
	// while (lprofP_callhookOUT(S)) ;
	/* call the original Lua 'exit' function */
	lua_pushlightuserdata(L, &exit_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	lua_call(L, 0, 0);
}

static int profiler_pause(lua_State *L) {
	lprofP_STATE* S;
	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	S = (lprofP_STATE*)lua_touserdata(L, -1);
	lprofM_pause_function(S);
	is_pause = 1;
	return 0;
}

static int profiler_resume(lua_State *L) {
	lprofP_STATE* S;
	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	S = (lprofP_STATE*)lua_touserdata(L, -1);
	lprofM_pause_function(S);
	is_pause = 0;
	return 0;
}

static void iterate_and_save(lua_State *L, int index)
{
	int modFunFilterIndex = 0;

	lua_pushvalue(L, index);
	// stack now contains: -1 => table  
	lua_pushnil(L);

	static char* delims = ".";
	static char buff[256];
	// stack now contains: -1 => nil; -2 => table  
	while (lua_next(L, -2))
	{
		// stack now contains: -1 => value; -2 => key; -3 => table  
		// copy the key so that lua_tostring does not modify the original  
		lua_pushvalue(L, -2);
		// stack now contains: -1 => key; -2 => value; -3 => key; -4 => table  

		strcpy(buff, lua_tostring(L, -2));
		char* result = strtok(buff, delims);
		strcpy(modFunFilter[modFunFilterIndex][1], result);
		result = strtok(NULL, delims);
		strcpy(modFunFilter[modFunFilterIndex][0], result);

		//printf("%s => %s\n", lua_tostring(L, -1), lua_tostring(L, -2));
		// pop value + copy of key, leaving original key  
		lua_pop(L, 2);
		// stack now contains: -1 => key; -2 => table  
		++modFunFilterIndex;
	}
	// stack now contains: -1 => table (when lua_next returns 0 it pops the key  
	// but does not push anything.)  
	// Pop table  
	lua_pop(L, 1);
	modFunFilterNum = modFunFilterIndex;
	// Stack is now the same as it was on entry to this function  
}

static int profiler_start(lua_State *L) {

	if (is_start){
		return 0;
	}

#ifdef DEBUG_PROFILER
	logfile = fopen("Testlog.log", "w");
	if (!logfile) {
		return 0;
	}
#endif

	lprofP_STATE* S;
	const char* outfile;

	lua_getfield(L, LUA_GLOBALSINDEX, "ProfFilterTable");
	if (lua_istable(L, -1))
	{
		iterate_and_save(L, -1);
	}
	lua_pop(L, 1);

	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if (!lua_isnil(L, -1)) {
		profiler_stop(L);
	}
	lua_pop(L, 1);

	outfile = NULL;
	if (lua_gettop(L) >= 1)
		outfile = luaL_checkstring(L, 1);

#if defined(linux)	
#include <sys/types.h>
#include <unistd.h>
	char tmpbuff[256];

	static int s_index = 0;

	sprintf(tmpbuff, "%s.%d.%d", outfile, (int)GETPID(), ++s_index);

	outfile = tmpbuff;
#endif

	/* init with default file name and printing a header line */
	if (!(S = lprofP_init_core_profiler(outfile, 1, 0))) {
		return luaL_error(L, "LuaProfiler error: output file could not be opened!");
	}

	g_S = S;

	lua_sethook(L, (lua_Hook)callhook, LUA_MASKCALL | LUA_MASKRET, 0);

	lua_pushlightuserdata(L, &profstate_id);
	lua_pushlightuserdata(L, S);
	lua_settable(L, LUA_REGISTRYINDEX);

	/* use our own exit function instead */
	lua_getglobal(L, "os");
	lua_pushlightuserdata(L, &exit_id);
	lua_pushstring(L, "exit");
	lua_gettable(L, -3);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_pushstring(L, "exit");
	lua_pushcfunction(L, (lua_CFunction)exit_profiler);
	lua_settable(L, -3);

	/* the following statement is to simulate how the execution stack is */
	/* supposed to be by the time the profiler is activated when loaded  */
	/* as a library.                                                     */


	lprof_DebugInfo* dbg_info = (lprof_DebugInfo*)malloc(sizeof(lprof_DebugInfo));

	dbg_info->type = FUNCTION_HOOK;

	strcpy(dbg_info->name, "profiler_start");
	dbg_info->p_name = dbg_info->name;

	strcpy(dbg_info->what, "C");
	dbg_info->p_what = dbg_info->what;

	strcpy(dbg_info->source, "(C)");
	dbg_info->p_source = dbg_info->source;
	dbg_info->event = 0;
	dbg_info->currentline = -1;
	dbg_info->ccallname[0] = '\0';

	//不要记录了
	//dispatch_dbg_info(dbg_info);

	//lprofP_callhookIN(S, "profiler_start", "(C)", -1, -1,"C", "\0");
	is_start = 1;
	lua_pushboolean(L, 1);
	return 1;
}


static int is_profiler_pause(lua_State *L)
{
	lua_pushboolean(L, is_pause);
	return 1;
}

static int profiler_stop(lua_State *L) {
	lprofP_STATE* S;
	lua_sethook(L, (lua_Hook)callhook, 0, 0);
	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if (!lua_isnil(L, -1))
	{
		S = (lprofP_STATE*)lua_touserdata(L, -1);
		/* leave all functions under execution */
		//while (lprofP_callhookOUT(S))
		;
		lprofP_close_core_profiler(S);
		lua_pushlightuserdata(L, &profstate_id);
		lua_pushnil(L);
		lua_settable(L, LUA_REGISTRYINDEX);
		lua_pushboolean(L, 1);
	}
	else
	{
		lua_pushboolean(L, 0);
	}
	is_start = 0;

#ifdef DEBUG_PROFILER
	if (logfile)
		fclose(logfile);
#endif

	return 1;
}

static int profiler_frame(lua_State *L)
{
	int frameid = 0;
	int frametime = 0;
	if (is_start == 1)
	{
		lua_pushlightuserdata(L, &profstate_id);
		lua_gettable(L, LUA_REGISTRYINDEX);
		frameid = (int)luaL_checkinteger(L, -2);
		frametime = (int)luaL_checkinteger(L, -1);

		lprof_DebugInfo* dbg_info = (lprof_DebugInfo*)malloc(sizeof(lprof_DebugInfo));
		dbg_info->type = FRAME;
		dbg_info->frameid = frameid;
		dbg_info->unitytime = frametime;
		dbg_info->hook_cost_cs = stat_hook_cost_ts;
		dbg_info->hook_call_cnt = stat_hook_call_cnt;
		stat_hook_cost_ts = 0;
		stat_hook_call_cnt = 0;
		handle_dbg_info(dbg_info);
		//lprofT_frame(frameid,frametime);
	}
	return 0;
}

/************************************************************************/
/*         Lua Register Function                                        */
/************************************************************************/
int profiler_open(lua_State *L)
{
	is_pause = 0;
	is_start = 0;
	lua_register(L, "profiler_start", profiler_start);
	lua_register(L, "profiler_pause", profiler_pause);
	lua_register(L, "profiler_resume", profiler_resume);
	lua_register(L, "profiler_stop", profiler_stop);
	lua_register(L, "profiler_frame", profiler_frame);
	//lua_register(L, "profiler_snapshot",memory_snapshot);
	// 增加一个判断是否暂停的函数 2016-08-10 lennon.c
	lua_register(L, "is_profiler_pause", is_profiler_pause);

	return 1;
}


LUA_API void init_profiler(lua_State *L)
{
	profiler_open(L);
	pOutputCallback = NULL;
	pUnityObject = NULL;
	pUnityMethod = NULL;
	iRealtimeOrFile = 0;
	lprofT_init();
}

LUA_API void luaprofiler_setting(int realtimeOfFile, int filterMask, int filterLevel)
{
	iRealtimeOrFile = realtimeOfFile;
	ifilterMask = filterMask;
	iFunFilterLevel = filterLevel;
}

LUA_API void frame_profiler(int id, int unitytime)
{
	static LARGE_INTEGER s_last_ts;
	static int s_has_set_ts = 0;
	if (is_start)
	{
		if (!s_has_set_ts){
			lprofC_start_timer2(&s_last_ts);
			s_has_set_ts = 1;
		}

		lprof_DebugInfo* dbg_info = (lprof_DebugInfo*)malloc(sizeof(lprof_DebugInfo));
		dbg_info->type = FRAME;
		dbg_info->frameid = id;
		dbg_info->unitytime = unitytime;
		dbg_info->hook_cost_cs = stat_hook_cost_ts;
		dbg_info->hook_call_cnt = stat_hook_call_cnt;
		dbg_info->currentMem = curMem;
		stat_hook_cost_ts = 0;
		stat_hook_call_cnt = 0;

		handle_dbg_info(dbg_info);
		lprofC_start_timer2(&s_last_ts);
		//lprofT_frame(id, unitytime);
	}
}

LUA_API void register_callback(void* pcallback)
{
	if (pcallback)
	{
		pOutputCallback = (pfnoutputCallback)pcallback;
	}
}

LUA_API int isregister_callback()
{
	if (pOutputCallback)
		return 1;
	else
		return 0;
}

LUA_API void unregister_callback()
{
	pOutputCallback = NULL;
	if (pUnityObject)
		free(pUnityObject);
	if (pUnityMethod)
		free(pUnityMethod);
	pUnityObject = pUnityMethod = NULL;

}




