/*
GAME_ENGINE_STUB.C

symbols in this file:
000A4710 0010:
	_code_000a4710 (0000)
000A4720 0010:
	_code_000a4720 (0000)
000A4730 0010:
	_code_000a4730 (0000)
000A4740 0010:
	_code_000a4740 (0000)
000A4750 0010:
	_code_000a4750 (0000)
000A4760 0010:
	_code_000a4760 (0000)
000A4770 0010:
	_code_000a4770 (0000)
000A4780 0010:
	_code_000a4780 (0000)
000A4790 0010:
	_code_000a4790 (0000)
000A47A0 0010:
	_code_000a47a0 (0000)
000A47B0 0010:
	_code_000a47b0 (0000)
000A47C0 0010:
	_code_000a47c0 (0000)
000A47D0 0010:
	_code_000a47d0 (0000)
000A47E0 0010:
	_code_000a47e0 (0000)
000A47F0 0010:
	_code_000a47f0 (0000)
0025C210 0005:
	??_C@_04GGADAGKI@stub?$AA@ (0000)
002DE6F8 0088:
	_stub_engine (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"

#include "game/game_engine.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes */

void code_000a4710(void);
boolean code_000a4720(void);
void code_000a4730(void);
void code_000a4740(
	long player_index);
void code_000a4750(void);
void code_000a4760(void);
void code_000a4770(
	long statistic);
void code_000a4780(
	void *message);
void code_000a4790(
	void *message);
void code_000a47a0(void);
void code_000a47b0(void);
void code_000a47c0(void);
boolean code_000a47d0(
	long unit_index,
	long weapon_index);
void code_000a47e0(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type);
void code_000a47f0(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire);

/* ---------- globals */

struct game_engine stub_engine =
{
	"stub",
	game_engine_stub,
	code_000a4710,
	code_000a4720,
	code_000a4730,
	code_000a4740,
	code_000a4750,
	code_000a4760,
	code_000a4770,
	code_000a4780,
	code_000a4790,
	code_000a47a0,
	code_000a47b0,
	NULL,
	NULL,
	NULL,
	NULL,
	code_000a47c0,
	NULL,
	NULL,
	NULL,
	NULL,
	code_000a47d0,
	code_000a47e0,
	code_000a47f0,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

/* ---------- public code */

void code_000a4710(void)
{
}

boolean code_000a4720(void)
{
	return TRUE;
}

void code_000a4730(void)
{
}

void code_000a4740(
	long player_index)
{
	(void)player_index;
}

void code_000a4750(void)
{
}

void code_000a4760(void)
{
}

void code_000a4770(
	long statistic)
{
	(void)statistic;
}

void code_000a4780(
	void *message)
{
	(void)message;
}

void code_000a4790(
	void *message)
{
	(void)message;
}

void code_000a47a0(void)
{
}

void code_000a47b0(void)
{
}

void code_000a47c0(void)
{
}

boolean code_000a47d0(
	long unit_index,
	long weapon_index)
{
	(void)unit_index;
	(void)weapon_index;

	return TRUE;
}

void code_000a47e0(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type)
{
	(void)damaging_player_index;
	(void)dead_player_index;
	(void)damage_type;
}

void code_000a47f0(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire)
{
	(void)killing_player_index;
	(void)killing_object_index;
	(void)dead_player_index;
	(void)friendly_fire;
}

/* ---------- private code */
