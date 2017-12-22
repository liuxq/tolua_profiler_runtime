﻿/*
** LuaProfiler
** Copyright Kepler Project 2005-2007 (http://www.keplerproject.org/luaprofiler)
** $Id: function_meter.c,v 1.9 2008-05-19 18:36:23 mascarenhas Exp $
*/

/*****************************************************************************
function_meter.c:
   Module to compute the times for functions (local times and total times)

Design:
   'lprofM_init'            set up the function times meter service
   'lprofM_enter_function'  called when the function stack increases one level
   'lprofM_leave_function'  called when the function stack decreases one level

   'lprofM_resume_function'   called when the profiler is returning from a time
                              consuming task
   'lprofM_resume_total_time' idem
   'lprofM_resume_local_time' called when a child function returns the execution 
                              to it's caller (current function)
   'lprofM_pause_function'    called when the profiler need to do things that
                              may take too long (writing a log, for example)
   'lprofM_pause_total_time'  idem
   'lprofM_pause_local_time'  called when the current function has called
                              another one or when the function terminates
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clocks.h"

/* #include "stack.h" is done by function_meter.h */
#include "function_meter.h"

#ifdef DEBUG
#include <stdlib.h>
#define ASSERT(e, msg) if (!e) {			\
    fprintf(stdout,                                     \
	    "function_meter.c: assertion failed: %s\n", \
	    msg);                                       \
    exit(1); }
#else
#define ASSERT(e, msg)
#endif

/* structures to receive stack elements, declared globals */
/* in the hope they will perform faster                   */
static lprofS_STACK_RECORD newf;       /* used in 'enter_function' */
static lprofS_STACK_RECORD leave_ret;   /* used in 'leave_function' */


/* sum the seconds based on the time marker */ 
static void compute_local_time(lprofS_STACK_RECORD *e) {
  ASSERT(e, "local time null");
  //e->local_time += lprofC_get_seconds(e->time_marker_function_local_time);
  e->local_time += (float)lprofC_get_seconds2(&e->time_maker_local_time_begin);
}


/* sum the seconds based on the time marker */ 
static void compute_total_time(lprofS_STACK_RECORD *e) {
  ASSERT(e, "total time null");
  //e->total_time += lprofC_get_seconds(e->time_marker_function_total_time);
}


/* compute the local time for the current function */
void lprofM_pause_local_time(lprofP_STATE* S) {
  //compute_local_time(S->stack_top);
}


/* pause the total timer for all the functions that are in the stack */
void lprofM_pause_total_time(lprofP_STATE* S) {
  lprofS_STACK aux;

  //ASSERT(S->stack_top, "pause_total_time: stack_top null");

  /* auxiliary stack */
  aux = S->stack_top;
        
  /* pause */
  while (aux) {
    compute_total_time(aux);
    aux = aux->next;
  }
}


/* pause the local and total timers for all functions in the stack */
void lprofM_pause_function(lprofP_STATE* S) {

  ASSERT(S->stack_top, "pause_function: stack_top null");

  lprofM_pause_local_time(S);
  lprofM_pause_total_time(S);
}


/* resume the local timer for the current function */
void lprofM_resume_local_time(lprofP_STATE* S) {

  ASSERT(S->stack_top, "resume_local_time: stack_top null");
                
  /* the function is in the top of the stack */
  //lprofC_start_timer(&(S->stack_top->time_marker_function_local_time));
}


/* resume the total timer for all the functions in the stack */
void lprofM_resume_total_time(lprofP_STATE* S) {
  lprofS_STACK aux;

  ASSERT(S->stack_top, "resume_total_time: stack_top null");
        
  /* auxiliary stack */
  aux = S->stack_top;
        
  /* resume */
  while (aux) {
    lprofC_start_timer(&(aux->time_marker_function_total_time));
    aux = aux->next;
  }
}


/* resume the local and total timers for all functions in the stack */
void lprofM_resume_function(lprofP_STATE* S) {

  ASSERT(S->stack_top, "resume_function: stack_top null");

  lprofM_resume_local_time(S);
  lprofM_resume_total_time(S);
}


/* the local time for the parent function is paused  */
/* and the local and total time markers are started */
void lprofM_enter_function(lprofP_STATE* S, char *file_defined, char *fcn_name, long linedefined, long currentline,char* what, char* cFun, lprof_DebugInfo* dbg_info) {
  char* prev_name = 0;
  char* cur_name = 0;
  char* cur_what = 0;
  /* the flow has changed to another function: */
  /* pause the parent's function timer timer   */
  if (S->stack_top) {
    lprofM_pause_local_time(S);
    prev_name = S->stack_top->function_name;
  } else prev_name = "top level";
  /* measure new function */

  if (file_defined != NULL)
  {
	  newf.file_defined = (char*)malloc(sizeof(char)*(strlen(file_defined) + 1));
	  if(newf.file_defined)
		sprintf(newf.file_defined, "%s", file_defined);
  }
  else{
	  newf.file_defined = (char*)malloc(sizeof(char)*(strlen("no file_defined") + 1));
	  if(newf.file_defined)
		sprintf(newf.file_defined, "%s", "no file_defined");
  }
	
  if(fcn_name != NULL && strncmp(fcn_name, "?", 1) != 0) {
  	cur_name = (char*)malloc( strlen(fcn_name)+1 );
	strcpy(cur_name, fcn_name);
    newf.function_name = cur_name;
  } else if(file_defined != NULL && strcmp(file_defined, "=[C]") == 0) {
    cur_name = (char*)malloc(sizeof(char)*(strlen("called from ")+strlen(prev_name)+1 + strlen(cFun) + 40));
	if(cur_name)
		sprintf(cur_name, "called from %s%s", prev_name, cFun);
    newf.function_name = cur_name;
  } else {
	  if(file_defined)
	  {
		cur_name = (char*)malloc(sizeof(char)*(strlen(file_defined)+16));
		if(cur_name)
		{
			sprintf(cur_name, "%s:%li", file_defined, linedefined);
			newf.function_name = cur_name;
		}
	  }
  }	
  if (what != NULL)
  {
	  cur_what = (char*)malloc(sizeof(char)*(strlen(what) + 1));
	  if(cur_what)
	  {
		memset(cur_what, 0x0, sizeof(char)*(strlen(what) + 1));
		sprintf(cur_what, "%s", what);
	  }
	  
  }
  else{
	  cur_what = (char*)malloc(sizeof(char)*(strlen("unknow") + 1));
	  if(cur_what)
	  {
		memset(cur_what, 0x0, sizeof(char)*(strlen("unknow") + 1));
		sprintf(cur_what, "%s", "unknow");
		
	  }
  }



  //printf("enter %s linedefined=%d\n",newf.function_name, linedefined);
  
  newf.what = cur_what;
  newf.line_defined = linedefined;
  newf.current_line = currentline;
  newf.local_time = 0.0;
  newf.interval_time = 0.0;
  newf.current_time = lprofC_get_current();
  newf.stack_level = S->stack_level;
  newf.level = dbg_info->level;
  lprofS_push(&(S->stack_top), newf, dbg_info);
}

/* computes times and remove the top of the stack         */
/* 'isto_resume' specifies if the parent function's timer */
/* should be restarted automatically. If it's false,      */
/* 'resume_local_time()' must be called when the resume   */
/* should be done                                         */
/* returns the funcinfo structure                         */
/* warning: use it before another call to this function,  */
/* because the funcinfo will be overwritten               */
lprofS_STACK_RECORD *lprofM_leave_function(lprofP_STATE* S, int isto_resume, lprof_DebugInfo* dbg_info) {

  ASSERT(S->stack_top, "leave_function: stack_top null");

  leave_ret = lprofS_pop(&(S->stack_top), dbg_info);

  /* resume the timer for the parent function ? */
  if (isto_resume)
    lprofM_resume_local_time(S);
  return &leave_ret;
}

lprofS_STACK_RECORD *lprofM_pop_invalid_function(lprofP_STATE* S) {

	ASSERT(S->stack_top, "leave_function: stack_top null");

	leave_ret = lprofS_pop_release(&(S->stack_top));

	return &leave_ret;
}


/* init stack */
lprofP_STATE* lprofM_init() {
  lprofP_STATE *S;
  S = (lprofP_STATE*)malloc(sizeof(lprofP_STATE));
  if(S) {
    S->stack_level = 0;
    S->stack_top = NULL;
    return S;
  } else return NULL;
}