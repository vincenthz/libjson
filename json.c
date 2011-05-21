/*
 * Copyright (C) 2009 Vincent Hanquez <vincent@snarc.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 or version 3.0 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * the class, states and state transition tables has been inspired by the JSON_parser.c
 * available at http://json.org, but are quite different on the way that the
 * parser handles its parse buffer and contains significant differences that affect
 * the JSON compliance.
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "json.h"

#ifdef _MSC_VER
#define inline _inline
#endif

#ifdef TRACING_ENABLE
#include <stdio.h>
#define TRACING(fmt, ...)	fprintf(stderr, "tracing: " fmt, ##__VA_ARGS__)
#else
#define TRACING(fmt, ...)	((void) 0)
#endif

enum classes {
	C_SPACE, /* space */
	C_NL,    /* newline */
	C_WHITE, /* tab, CR */
	C_LCURB, C_RCURB, /* object opening/closing */
	C_LSQRB, C_RSQRB, /* array opening/closing */
	/* syntax symbols */
	C_COLON,
	C_COMMA,
	C_QUOTE, /* " */
	C_BACKS, /* \ */
	C_SLASH, /* / */
	C_PLUS,
	C_MINUS,
	C_DOT,
	C_ZERO, C_DIGIT, /* digits */
	C_a, C_b, C_c, C_d, C_e, C_f, C_l, C_n, C_r, C_s, C_t, C_u, /* nocaps letters */
	C_ABCDF, C_E, /* caps letters */
	C_OTHER, /* all other */
	C_STAR, /* star in C style comment */
	C_HASH, /* # for YAML comment */
	C_ERROR = 0xfe,
};

/* map from character < 128 to classes. from 128 to 256 all C_OTHER */
static uint8_t character_class[128] = {
	C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR,
	C_ERROR, C_WHITE, C_NL,    C_ERROR, C_ERROR, C_WHITE, C_ERROR, C_ERROR,
	C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR,
	C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR,

	C_SPACE, C_OTHER, C_QUOTE, C_HASH,  C_OTHER, C_OTHER, C_OTHER, C_OTHER,
	C_OTHER, C_OTHER, C_STAR,  C_PLUS,  C_COMMA, C_MINUS, C_DOT,   C_SLASH,
	C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT,
	C_DIGIT, C_DIGIT, C_COLON, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER,

	C_OTHER, C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,     C_ABCDF, C_OTHER,
	C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER,
	C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER,
	C_OTHER, C_OTHER, C_OTHER, C_LSQRB, C_BACKS, C_RSQRB, C_OTHER, C_OTHER,

	C_OTHER, C_a,     C_b,     C_c,     C_d,     C_e,     C_f,     C_OTHER,
	C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_l,     C_OTHER, C_n,     C_OTHER,
	C_OTHER, C_OTHER, C_r,     C_s,     C_t,     C_u,     C_OTHER, C_OTHER,
	C_OTHER, C_OTHER, C_OTHER, C_LCURB, C_OTHER, C_RCURB, C_OTHER, C_OTHER
};

/* only the first 36 ascii characters need an escape */
static char *character_escape[36] = {
	"\\u0000", "\\u0001", "\\u0002", "\\u0003", "\\u0004", "\\u0005", "\\u0006", "\\u0007", /*  0-7  */
	"\\b"    ,     "\\t",     "\\n", "\\u000b",     "\\f",     "\\r", "\\u000e", "\\u000f", /*  8-f  */
	"\\u0010", "\\u0011", "\\u0012", "\\u0013", "\\u0014", "\\u0015", "\\u0016", "\\u0017", /* 10-17 */
	"\\u0018", "\\u0019", "\\u001a", "\\u001b", "\\u001c", "\\u001d", "\\u001e", "\\u001f", /* 18-1f */
	" "      , "!"      , "\\\""   , "#",
};

/* define all states and actions that will be taken on each transition.
 *
 * states are defined first because of the fact they are use as index in the
 * transitions table. they usually contains either a number or a prefix _ 
 * for simple state like string, object, value ...
 *
 * actions are defined starting from 0x80. state error is defined as 0xff
 */

enum states {
	STATE_GO, /* start  */
	STATE_OK, /* ok     */
	STATE__O, /* object */
	STATE__K, /* key    */
	STATE_CO, /* colon  */
	STATE__V, /* value  */
	STATE__A, /* array  */
	STATE__S, /* string */
	STATE_E0, /* escape */
	STATE_U1, STATE_U2, STATE_U3, STATE_U4, /* unicode states */
	STATE_M0, STATE_Z0, STATE_I0, /* number states */
	STATE_R1, STATE_R2, /* real states (after-dot digits) */
	STATE_X1, STATE_X2, STATE_X3, /* exponant states */
	STATE_T1, STATE_T2, STATE_T3, /* true constant states */
	STATE_F1, STATE_F2, STATE_F3, STATE_F4, /* false constant states */
	STATE_N1, STATE_N2, STATE_N3, /* null constant states */
	STATE_C1, STATE_C2, STATE_C3, /* C-comment states */
	STATE_Y1, /* YAML-comment state */
	STATE_D1, STATE_D2, /* multi unicode states */
};

/* the following are actions that need to be taken */
enum actions {
	STATE_KS = 0x80, /* key separator */
	STATE_SP, /* comma separator */
	STATE_AB, /* array begin */
	STATE_AE, /* array ending */
	STATE_OB, /* object begin */
	STATE_OE, /* object end */
	STATE_CB, /* C-comment begin */
	STATE_YB, /* YAML-comment begin */
	STATE_CE, /* YAML/C comment end */
	STATE_FA, /* false */
	STATE_TR, /* true */
	STATE_NU, /* null */
	STATE_DE, /* double detected by exponent */
	STATE_DF, /* double detected by . */
	STATE_SE, /* string end */
	STATE_MX, /* integer detected by minus */
	STATE_ZX, /* integer detected by zero */
	STATE_IX, /* integer detected by 1-9 */
	STATE_UC, /* Unicode character read */
};

/* error state */
#define STATE___ 	0xff

#define NR_STATES 	(STATE_D2 + 1)
#define NR_CLASSES	(C_HASH + 1)

#define IS_STATE_ACTION(s) ((s) & 0x80)
#define S(x) STATE_##x
#define PT_(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,a1,b1,c1,d1,e1,f1,g1,h1)	\
	{ S(a),S(b),S(c),S(d),S(e),S(f),S(g),S(h),S(i),S(j),S(k),S(l),S(m),S(n),		\
	  S(o),S(p),S(q),S(r),S(s),S(t),S(u),S(v),S(w),S(x),S(y),S(z),S(a1),S(b1),		\
	  S(c1),S(d1),S(e1),S(f1),S(g1),S(h1) }

/* map from the (previous state+new character class) to the next parser transition */
static const uint8_t state_transition_table[NR_STATES][NR_CLASSES] = {
/*             white                                                                            ABCDF  other    */
/*         sp nl |  {  }  [  ]  :  ,  "  \  /  +  -  .  0  19 a  b  c  d  e  f  l  n  r  s  t  u  |  E  |  *  # */
/*GO*/ PT_(GO,GO,GO,OB,__,AB,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,YB),
/*OK*/ PT_(OK,OK,OK,__,OE,__,AE,__,SP,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,YB),
/*_O*/ PT_(_O,_O,_O,__,OE,__,__,__,__,_S,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,YB),
/*_K*/ PT_(_K,_K,_K,__,__,__,__,__,__,_S,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,YB),
/*CO*/ PT_(CO,CO,CO,__,__,__,__,KS,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,YB),
/*_V*/ PT_(_V,_V,_V,OB,__,AB,__,__,__,_S,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,F1,__,N1,__,__,T1,__,__,__,__,__,YB),
/*_A*/ PT_(_A,_A,_A,OB,__,AB,AE,__,__,_S,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,F1,__,N1,__,__,T1,__,__,__,__,__,YB),
/****************************************************************************************************************/
/*_S*/ PT_(_S,__,__,_S,_S,_S,_S,_S,_S,SE,E0,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S,_S),
/*E0*/ PT_(__,__,__,__,__,__,__,__,__,_S,_S,_S,__,__,__,__,__,__,_S,__,__,__,_S,__,_S,_S,__,_S,U1,__,__,__,__,__),
/*U1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U2,U2,U2,U2,U2,U2,U2,U2,__,__,__,__,__,__,U2,U2,__,__,__),
/*U2*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U3,U3,U3,U3,U3,U3,U3,U3,__,__,__,__,__,__,U3,U3,__,__,__),
/*U3*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U4,U4,U4,U4,U4,U4,U4,U4,__,__,__,__,__,__,U4,U4,__,__,__),
/*U4*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,UC,UC,UC,UC,UC,UC,UC,UC,__,__,__,__,__,__,UC,UC,__,__,__),
/****************************************************************************************************************/
/*M0*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,Z0,I0,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/*Z0*/ PT_(OK,OK,OK,__,OE,__,AE,__,SP,__,__,CB,__,__,DF,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,YB),
/*I0*/ PT_(OK,OK,OK,__,OE,__,AE,__,SP,__,__,CB,__,__,DF,I0,I0,__,__,__,__,DE,__,__,__,__,__,__,__,__,DE,__,__,YB),
/*R1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,R2,R2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/*R2*/ PT_(OK,OK,OK,__,OE,__,AE,__,SP,__,__,CB,__,__,__,R2,R2,__,__,__,__,X1,__,__,__,__,__,__,__,__,X1,__,__,YB),
/*X1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,X2,X2,__,X3,X3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/*X2*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,X3,X3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/*X3*/ PT_(OK,OK,OK,__,OE,__,AE,__,SP,__,__,__,__,__,__,X3,X3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/****************************************************************************************************************/
/*T1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T2,__,__,__,__,__,__,__,__),
/*T2*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T3,__,__,__,__,__),
/*T3*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,TR,__,__,__,__,__,__,__,__,__,__,__,__),
/*F1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/*F2*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F3,__,__,__,__,__,__,__,__,__,__),
/*F3*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F4,__,__,__,__,__,__,__),
/*F4*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,FA,__,__,__,__,__,__,__,__,__,__,__,__),
/*N1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N2,__,__,__,__,__),
/*N2*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N3,__,__,__,__,__,__,__,__,__,__),
/*N3*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,NU,__,__,__,__,__,__,__,__,__,__),
/****************************************************************************************************************/
/*C1*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,C2,__),
/*C2*/ PT_(C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3,C2),
/*C3*/ PT_(C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,CE,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3,C2),
/*Y1*/ PT_(Y1,CE,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1,Y1),
/*D1*/ PT_(__,__,__,__,__,__,__,__,__,__,D2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__),
/*D2*/ PT_(__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U1,__,__,__,__,__),
};
#undef S
#undef PT_

/* map from (previous state+new character class) to the buffer policy. ignore=0/append=1/escape=2 */
static const uint8_t buffer_policy_table[NR_STATES][NR_CLASSES] = {
/*          white                                                                            ABCDF  other     */
/*      sp nl  |  {  }  [  ]  :  ,  "  \  /  +  -  .  0  19 a  b  c  d  e  f  l  n  r  s  t  u  |  E  |  *  # */
/*GO*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*OK*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*_O*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*_K*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*CO*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*_V*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*_A*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/**************************************************************************************************************/
/*_S*/ { 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
/*E0*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, 0, 2, 2, 0, 2, 0, 0, 0, 0, 0, 0 },
/*U1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
/*U2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
/*U3*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
/*U4*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
/**************************************************************************************************************/
/*M0*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*Z0*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*I0*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 },
/*R1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*R2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 },
/*X1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*X2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*X3*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/**************************************************************************************************************/
/*T1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*T2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*T3*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*F1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*F2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*F3*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*F4*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*N1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*N2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*N3*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/**************************************************************************************************************/
/*C1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*C2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*C3*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*Y1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*D1*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
/*D2*/ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

#define MODE_ARRAY 0
#define MODE_OBJECT 1

static inline void *memory_realloc(void *(*realloc_fct)(void *, size_t), void *ptr, size_t size)
{
	return (realloc_fct) ? realloc_fct(ptr, size) : realloc(ptr, size);
}

static inline void *memory_calloc(void *(*calloc_fct)(size_t, size_t), size_t nmemb, size_t size)
{
	return (calloc_fct) ? calloc_fct(nmemb, size) : calloc(nmemb, size);
}

#define parser_calloc(parser, n, s) memory_calloc(parser->config.user_calloc, n, s)
#define parser_realloc(parser, n, s) memory_realloc(parser->config.user_realloc, n, s)

static int state_grow(json_parser *parser)
{
	uint32_t newsize = parser->stack_size * 2;
	void *ptr;

	if (parser->config.max_nesting != 0)
		return JSON_ERROR_NESTING_LIMIT;

	ptr = parser_realloc(parser, parser->stack, newsize * sizeof(uint8_t));
	if (!ptr)
		return JSON_ERROR_NO_MEMORY;
	parser->stack = ptr;
	parser->stack_size = newsize;
	return 0;
}

static int state_push(json_parser *parser, int mode)
{
	if (parser->stack_offset >= parser->stack_size) {
		int ret = state_grow(parser);
		if (ret)
			return ret;
	}
	parser->stack[parser->stack_offset++] = mode;
	return 0;
}

static int state_pop(json_parser *parser, int mode)
{
	if (parser->stack_offset == 0)
		return JSON_ERROR_POP_EMPTY;
	parser->stack_offset--;
	if (parser->stack[parser->stack_offset] != mode)
		return JSON_ERROR_POP_UNEXPECTED_MODE;
	return 0;
}

static int buffer_grow(json_parser *parser)
{
	uint32_t newsize;
	void *ptr;
	int max = parser->config.max_data;

	if (max > 0 && parser->buffer_size == max)
		return JSON_ERROR_DATA_LIMIT;
	newsize = parser->buffer_size * 2;
	if (max > 0 && newsize > (uint32_t)(max))
		newsize = max;

	ptr = parser_realloc(parser, parser->buffer, newsize * sizeof(char));
	if (!ptr)
		return JSON_ERROR_NO_MEMORY;
	parser->buffer = ptr;
	parser->buffer_size = newsize;
	return 0;
}

static int buffer_push(json_parser *parser, unsigned char c)
{
	int ret;

	if (parser->buffer_offset + 1 >= parser->buffer_size) {
		ret = buffer_grow(parser);
		if (ret)
			return ret;
	}
	parser->buffer[parser->buffer_offset++] = c;
	return 0;
}

static int do_callback_withbuf(json_parser *parser, int type)
{
	if (!parser->callback)
		return 0;
	parser->buffer[parser->buffer_offset] = '\0';
	return (*parser->callback)(parser->userdata, type, parser->buffer, parser->buffer_offset);
}

static int do_callback(json_parser *parser, int type)
{
	if (!parser->callback)
		return 0;
	return (*parser->callback)(parser->userdata, type, NULL, 0);
}

static int do_buffer(json_parser *parser)
{
	int ret = 0;

	switch (parser->type) {
	case JSON_KEY: case JSON_STRING:
	case JSON_FLOAT: case JSON_INT:
	case JSON_NULL: case JSON_TRUE: case JSON_FALSE:
		ret = do_callback_withbuf(parser, parser->type);
		if (ret)
			return ret;
		break;
	default:
		break;
	}
	parser->buffer_offset = 0;
	return ret;
}

static const uint8_t hextable[] = {
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,255,255,255,255,255,255,
	255, 10, 11, 12, 13, 14, 15,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255, 10, 11, 12, 13, 14, 15,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

#define hex(c) (hextable[(uint8_t) c])

/* high surrogate range from d800 to dbff */
/* low surrogate range dc00 to dfff */
#define IS_HIGH_SURROGATE(uc) (((uc) & 0xfc00) == 0xd800)
#define IS_LOW_SURROGATE(uc)  (((uc) & 0xfc00) == 0xdc00)

/* transform an unicode [0-9A-Fa-f]{4} sequence into a proper value */
static int decode_unicode_char(json_parser *parser)
{
	uint32_t uval;
	char *b = parser->buffer;
	int offset = parser->buffer_offset;

	uval = (hex(b[offset - 4]) << 12) | (hex(b[offset - 3]) << 8)
	     | (hex(b[offset - 2]) << 4) | hex(b[offset - 1]);

	parser->buffer_offset -= 4;

	/* fast case */
	if (!parser->unicode_multi && uval < 0x80) {
		b[parser->buffer_offset++] = (char) uval;
		return 0;
	}

	if (parser->unicode_multi) {
		if (!IS_LOW_SURROGATE(uval))
			return JSON_ERROR_UNICODE_MISSING_LOW_SURROGATE;

		uval = 0x10000 + ((parser->unicode_multi & 0x3ff) << 10) + (uval & 0x3ff);
		b[parser->buffer_offset++] = (char) ((uval >> 18) | 0xf0);
		b[parser->buffer_offset++] = (char) (((uval >> 12) & 0x3f) | 0x80);
		b[parser->buffer_offset++] = (char) (((uval >> 6) & 0x3f) | 0x80);
		b[parser->buffer_offset++] = (char) ((uval & 0x3f) | 0x80);
		parser->unicode_multi = 0;
		return 0;
	}

	if (IS_LOW_SURROGATE(uval))
		return JSON_ERROR_UNICODE_UNEXPECTED_LOW_SURROGATE;
	if (IS_HIGH_SURROGATE(uval)) {
		parser->unicode_multi = uval;
		return 0;
	}

	if (uval < 0x800) {
		b[parser->buffer_offset++] = (char) ((uval >> 6) | 0xc0);
		b[parser->buffer_offset++] = (char) ((uval & 0x3f) | 0x80);
	} else {
		b[parser->buffer_offset++] = (char) ((uval >> 12) | 0xe0);
		b[parser->buffer_offset++] = (char) (((uval >> 6) & 0x3f) | 0x80);
		b[parser->buffer_offset++] = (char) (((uval >> 0) & 0x3f) | 0x80);
	}
	return 0;
}

static int buffer_push_escape(json_parser *parser, unsigned char next)
{
	char c = '\0';

	switch (next) {
	case 'b': c = '\b'; break;
	case 'f': c = '\f'; break;
	case 'n': c = '\n'; break;
	case 'r': c = '\r'; break;
	case 't': c = '\t'; break;
	case '"': c = '"'; break;
	case '/': c = '/'; break;
	case '\\': c = '\\'; break;
	}
	/* push the escaped character */
	return buffer_push(parser, c);
}

#define CHK(f) do { ret = f; if (ret) return ret; } while(0)

int act_uc(json_parser *parser)
{
	int ret;
	CHK(decode_unicode_char(parser));
	parser->state = (parser->unicode_multi) ? STATE_D1 : STATE__S;
	return 0;
}

int act_yb(json_parser *parser)
{
	if (!parser->config.allow_yaml_comments)
		return JSON_ERROR_COMMENT_NOT_ALLOWED;
	parser->save_state = parser->state;
	return 0;
}

int act_cb(json_parser *parser)
{
	if (!parser->config.allow_c_comments)
		return JSON_ERROR_COMMENT_NOT_ALLOWED;
	parser->save_state = parser->state;
	return 0;
}

int act_ce(json_parser *parser)
{
	parser->state = (parser->save_state > STATE__A) ? STATE_OK : parser->save_state;
	return 0;
}

int act_ob(json_parser *parser)
{
	int ret;
	CHK(do_callback(parser, JSON_OBJECT_BEGIN));
	CHK(state_push(parser, MODE_OBJECT));
	parser->expecting_key = 1;
	return 0;
}

int act_oe(json_parser *parser)
{
	int ret;
	CHK(do_callback(parser, JSON_OBJECT_END));
	CHK(state_pop(parser, MODE_OBJECT));
	parser->expecting_key = 0;
	return 0;
}

int act_ab(json_parser *parser)
{
	int ret;
	CHK(do_callback(parser, JSON_ARRAY_BEGIN));
	CHK(state_push(parser, MODE_ARRAY));
	return 0;
}
int act_ae(json_parser *parser)
{
	int ret;
	CHK(do_callback(parser, JSON_ARRAY_END));
	CHK(state_pop(parser, MODE_ARRAY));
	return 0;
}

int act_se(json_parser *parser)
{
	int ret;
	CHK(do_callback_withbuf(parser, (parser->expecting_key) ? JSON_KEY : JSON_STRING));
	parser->buffer_offset = 0;
	parser->state = (parser->expecting_key) ? STATE_CO : STATE_OK;
	parser->expecting_key = 0;
	return 0;
}

int act_sp(json_parser *parser)
{
	if (parser->stack_offset == 0)
		return JSON_ERROR_COMMA_OUT_OF_STRUCTURE;
	if (parser->stack[parser->stack_offset - 1] == MODE_OBJECT) {
		parser->expecting_key = 1;
		parser->state = STATE__K;
	} else
		parser->state = STATE__V;
	return 0;
}

struct action_descr
{
	int (*call)(json_parser *parser);
	uint8_t type;
	uint8_t state; /* 0 if we let the callback set the value it want */
	uint8_t dobuffer;
};

static struct action_descr actions_map[] = {
	{ NULL,   JSON_NONE,  STATE__V, 0 }, /* KS */
	{ act_sp, JSON_NONE,  0,        1 }, /* SP */
	{ act_ab, JSON_NONE,  STATE__A, 0 }, /* AB */
	{ act_ae, JSON_NONE,  STATE_OK, 1 }, /* AE */
	{ act_ob, JSON_NONE,  STATE__O, 0 }, /* OB */
	{ act_oe, JSON_NONE,  STATE_OK, 1 }, /* OE */
	{ act_cb, JSON_NONE,  STATE_C1, 1 }, /* CB */
	{ act_yb, JSON_NONE,  STATE_Y1, 1 }, /* YB */
	{ act_ce, JSON_NONE,  0,        0 }, /* CE */
	{ NULL,   JSON_FALSE, STATE_OK, 0 }, /* FA */
	{ NULL,   JSON_TRUE,  STATE_OK, 0 }, /* TR */
	{ NULL,   JSON_NULL,  STATE_OK, 0 }, /* NU */
	{ NULL,   JSON_FLOAT, STATE_X1, 0 }, /* DE */
	{ NULL,   JSON_FLOAT, STATE_R1, 0 }, /* DF */
	{ act_se, JSON_NONE,  0,        0 }, /* SE */
	{ NULL,   JSON_INT,   STATE_M0, 0 }, /* MX */
	{ NULL,   JSON_INT,   STATE_Z0, 0 }, /* ZX */
	{ NULL,   JSON_INT,   STATE_I0, 0 }, /* IX */
	{ act_uc, JSON_NONE,  0,        0 }, /* UC */
};

static int do_action(json_parser *parser, int next_state)
{
	struct action_descr *descr = &actions_map[next_state & ~0x80];

	if (descr->call) {
		int ret;
		if (descr->dobuffer)
			CHK(do_buffer(parser));
		CHK((descr->call)(parser));
	}
	if (descr->state)
		parser->state = descr->state;
	parser->type = descr->type;
	return 0;
}

/** json_parser_init initialize a parser structure taking a config,
 * a config and its userdata.
 * return JSON_ERROR_NO_MEMORY if memory allocation failed or SUCCESS.
 */
int json_parser_init(json_parser *parser, json_config *config,
                     json_parser_callback callback, void *userdata)
{
	memset(parser, 0, sizeof(*parser));

	if (config)
		memcpy(&parser->config, config, sizeof(json_config));
	parser->callback = callback;
	parser->userdata = userdata;

	/* initialise parsing stack and state */
	parser->stack_offset = 0;
	parser->state = STATE_GO;

	/* initialize the parse stack */
	parser->stack_size = (parser->config.max_nesting > 0)
		? parser->config.max_nesting
		: LIBJSON_DEFAULT_STACK_SIZE;

	parser->stack = parser_calloc(parser, parser->stack_size, sizeof(parser->stack[0]));
	if (!parser->stack)
		return JSON_ERROR_NO_MEMORY;

	/* initialize the parse buffer */
	parser->buffer_size = (parser->config.buffer_initial_size > 0)
		? parser->config.buffer_initial_size
		: LIBJSON_DEFAULT_BUFFER_SIZE;

	if (parser->config.max_data > 0 && parser->buffer_size > parser->config.max_data)
		parser->buffer_size = parser->config.max_data;

	parser->buffer = parser_calloc(parser, parser->buffer_size, sizeof(char));
	if (!parser->buffer) {
		free(parser->stack);
		return JSON_ERROR_NO_MEMORY;
	}
	return 0;
}

/** json_parser_free freed memory structure allocated by the parser */
int json_parser_free(json_parser *parser)
{
	if (!parser)
		return 0;
	free(parser->stack);
	free(parser->buffer);
	parser->stack = NULL;
	parser->buffer = NULL;
	return 0;
}

/** json_parser_is_done return 0 is the parser isn't in a finish state. !0 if it is */
int json_parser_is_done(json_parser *parser)
{
	/* need to compare the state to !GO to not accept empty document */
	return parser->stack_offset == 0 && parser->state != STATE_GO;
}

/** json_parser_string append a string s with a specific length to the parser
 * return 0 if everything went ok, a JSON_ERROR_* otherwise.
 * the user can supplied a valid processed pointer that will
 * be fill with the number of processed characters before returning */
int json_parser_string(json_parser *parser, const char *s,
                       uint32_t length, uint32_t *processed)
{
	int ret;
	int next_class, next_state;
	int buffer_policy;
	uint32_t i;

	ret = 0;
	for (i = 0; i < length; i++) {
		unsigned char ch = s[i];

		ret = 0;
		next_class = (ch >= 128) ? C_OTHER : character_class[ch];
		if (next_class == C_ERROR) {
			ret = JSON_ERROR_BAD_CHAR;
			break;
		}

		next_state = state_transition_table[parser->state][next_class];
		buffer_policy = buffer_policy_table[parser->state][next_class];
		TRACING("addchar %d (current-state=%d, next-state=%d, buf-policy=%d)\n",
			ch, parser->state, next_state, buffer_policy);
		if (next_state == STATE___) {
			ret = JSON_ERROR_UNEXPECTED_CHAR;
			break;
		}

		/* add char to buffer */
		if (buffer_policy) {
			ret = (buffer_policy == 2)
				? buffer_push_escape(parser, ch)
				: buffer_push(parser, ch);
			if (ret)
				break;
		}

		/* move to the next level */
		if (IS_STATE_ACTION(next_state))
			ret = do_action(parser, next_state);
		else
			parser->state = next_state;
		if (ret)
			break;
	}
	if (processed)
		*processed = i;
	return ret;
}

/** json_parser_char append one single char to the parser
 * return 0 if everything went ok, a JSON_ERROR_* otherwise */
int json_parser_char(json_parser *parser, unsigned char ch)
{
	return json_parser_string(parser, (char *) &ch, 1, NULL);
}

/** json_print_init initialize a printer context. always succeed */
int json_print_init(json_printer *printer, json_printer_callback callback, void *userdata)
{
	memset(printer, '\0', sizeof(*printer));
	printer->callback = callback;
	printer->userdata = userdata;

	printer->indentstr = "\t";
	printer->indentlevel = 0;
	printer->enter_object = 1;
	printer->first = 1;
	return 0;
}

/** json_print_free free a printer a context
 * doesn't do anything now, but in future print_init could allocate memory */
int json_print_free(json_printer *printer)
{
	return 0;
}

/* escape a C string to be a JSON valid string on the wire.
 * XXX: it doesn't do unicode verification. yet?. */
static int print_string(json_printer *printer, const char *data, uint32_t length)
{
	uint32_t i;

	printer->callback(printer->userdata, "\"", 1);
	for (i = 0; i < length; i++) {
		unsigned char c = data[i];
		if (c < 36) {
			char *esc = character_escape[c];
			printer->callback(printer->userdata, esc, strlen(esc));
		} else if (c == '\\') {
			printer->callback(printer->userdata, "\\\\", 2);
		} else
			printer->callback(printer->userdata, data + i, 1);
	}
	printer->callback(printer->userdata, "\"", 1);
	return 0;
}

static int print_indent(json_printer *printer)
{
	int i;
	printer->callback(printer->userdata, "\n", 1);
	for (i = 0; i < printer->indentlevel; i++)
		printer->callback(printer->userdata, printer->indentstr, strlen(printer->indentstr));
	return 0;
}

int json_print_mode(json_printer *printer, int type, const char *data, uint32_t length, int pretty)
{
	int enterobj = printer->enter_object;

	if (!enterobj && !printer->afterkey && (type != JSON_ARRAY_END && type != JSON_OBJECT_END)) {
		printer->callback(printer->userdata, ",", 1);
		if (pretty) print_indent(printer);
	}

	if (pretty && (enterobj && !printer->first && (type != JSON_ARRAY_END && type != JSON_OBJECT_END))) {
		print_indent(printer);
	}

	printer->first = 0;
	printer->enter_object = 0;
	printer->afterkey = 0;
	switch (type) {
	case JSON_ARRAY_BEGIN:
		printer->callback(printer->userdata, "[", 1);
		printer->indentlevel++;
		printer->enter_object = 1;
		break;
	case JSON_OBJECT_BEGIN:
		printer->callback(printer->userdata, "{", 1);
		printer->indentlevel++;
		printer->enter_object = 1;
		break;
	case JSON_ARRAY_END:
	case JSON_OBJECT_END:
		printer->indentlevel--;
		if (pretty && !enterobj) print_indent(printer);
		printer->callback(printer->userdata, (type == JSON_OBJECT_END) ? "}" : "]", 1);
		break;
	case JSON_INT: printer->callback(printer->userdata, data, length); break;
	case JSON_FLOAT: printer->callback(printer->userdata, data, length); break;
	case JSON_NULL: printer->callback(printer->userdata, "null", 4); break;
	case JSON_TRUE: printer->callback(printer->userdata, "true", 4); break;
	case JSON_FALSE: printer->callback(printer->userdata, "false", 5); break;
	case JSON_KEY:
		print_string(printer, data, length);
		printer->callback(printer->userdata, ": ", (pretty) ? 2 : 1);
		printer->afterkey = 1;
		break;
	case JSON_STRING:
		print_string(printer, data, length);
		break;
	default:
		break;
	}

	return 0;
}

/** json_print_pretty pretty print the passed argument (type/data/length). */
int json_print_pretty(json_printer *printer, int type, const char *data, uint32_t length)
{
	return json_print_mode(printer, type, data, length, 1);
}

/** json_print_raw prints without eye candy the passed argument (type/data/length). */
int json_print_raw(json_printer *printer, int type, const char *data, uint32_t length)
{
	return json_print_mode(printer, type, data, length, 0);
}

/** json_print_args takes multiple types and pass them to the printer function */
int json_print_args(json_printer *printer,
                    int (*f)(json_printer *, int, const char *, uint32_t),
                    ...)
{
	va_list ap;
	char *data;
	uint32_t length;
	int type, ret;

	ret = 0;
	va_start(ap, f);
	while ((type = va_arg(ap, int)) != -1) {
		switch (type) {
		case JSON_ARRAY_BEGIN:
		case JSON_ARRAY_END:
		case JSON_OBJECT_BEGIN:
		case JSON_OBJECT_END:
		case JSON_NULL:
		case JSON_TRUE:
		case JSON_FALSE:
			ret = (*f)(printer, type, NULL, 0);
			break;
		case JSON_INT:
		case JSON_FLOAT:
		case JSON_KEY:
		case JSON_STRING:
			data = va_arg(ap, char *);
			length = va_arg(ap, uint32_t);
			if (length == -1)
				length = strlen(data);
			ret = (*f)(printer, type, data, length);
			break;
		}
		if (ret)
			break;
	}
	va_end(ap);
	return ret;
}

static int dom_push(struct json_parser_dom *ctx, void *val)
{
	if (ctx->stack_offset == ctx->stack_size) {
		void *ptr;
		uint32_t newsize = ctx->stack_size * 2;
		ptr = memory_realloc(ctx->user_realloc, ctx->stack, newsize);
		if (!ptr)
			return JSON_ERROR_NO_MEMORY;
		ctx->stack = ptr;
		ctx->stack_size = newsize;
	}
	ctx->stack[ctx->stack_offset].val = val;
	ctx->stack[ctx->stack_offset].key = NULL;
	ctx->stack[ctx->stack_offset].key_length = 0;
	ctx->stack_offset++;
	return 0;
}

static int dom_pop(struct json_parser_dom *ctx, void **val)
{
	ctx->stack_offset--;
	*val = ctx->stack[ctx->stack_offset].val;
	return 0;
}

int json_parser_dom_init(json_parser_dom *dom,
                         json_parser_dom_create_structure create_structure,
                         json_parser_dom_create_data create_data,
                         json_parser_dom_append append)
{
	memset(dom, 0, sizeof(*dom));
	dom->stack_size = 1024;
	dom->stack_offset = 0;
	dom->stack = memory_calloc(dom->user_calloc, dom->stack_size, sizeof(*(dom->stack)));
	if (!dom->stack)
		return JSON_ERROR_NO_MEMORY;
	dom->append = append;
	dom->create_structure = create_structure;
	dom->create_data = create_data;
	return 0;
}

int json_parser_dom_free(json_parser_dom *dom)
{
	free(dom->stack);
	return 0;
}

int json_parser_dom_callback(void *userdata, int type, const char *data, uint32_t length)
{
	struct json_parser_dom *ctx = userdata;
	void *v;
	struct stack_elem *stack = NULL;

	switch (type) {
	case JSON_ARRAY_BEGIN:
	case JSON_OBJECT_BEGIN:
		v = ctx->create_structure(ctx->stack_offset, type == JSON_OBJECT_BEGIN);
		if (!v)
			return JSON_ERROR_CALLBACK;
		dom_push(ctx, v);
		break;
	case JSON_OBJECT_END:
	case JSON_ARRAY_END:
		dom_pop(ctx, &v);
		if (ctx->stack_offset > 0) {
			stack = &(ctx->stack[ctx->stack_offset - 1]);
			ctx->append(stack->val, stack->key, stack->key_length, v);
			free(stack->key);
		} else
			ctx->root_structure = v;
		break;
	case JSON_KEY:
		stack = &(ctx->stack[ctx->stack_offset - 1]);
		stack->key = memory_calloc(ctx->user_calloc, length + 1, sizeof(char));
		stack->key_length = length;
		if (!stack->key)
			return JSON_ERROR_NO_MEMORY;
		memcpy(stack->key, data, length);
		break;
	case JSON_STRING:
	case JSON_INT:
	case JSON_FLOAT:
	case JSON_NULL:
	case JSON_TRUE:
	case JSON_FALSE:
		stack = &(ctx->stack[ctx->stack_offset - 1]);
		v = ctx->create_data(type, data, length);
		ctx->append(stack->val, stack->key, stack->key_length, v);
		free(stack->key);
		break;
	}
	return 0;
}
