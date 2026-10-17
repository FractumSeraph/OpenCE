/*
CE_HUD.H

What of the Halo PC maps' HUD (ce_hud.c) the mods use: a walk of every
placement of a HUD definition, the same placements a Custom Edition map's
HUD has halved where Halo PC halves them (port/linux/game/mod_bitmaps.c
scales those that draw a mod's larger bitmaps).
*/

#ifndef __CE_HUD_H
#define __CE_HUD_H
#pragma once

#ifdef HALO_CUSTOM_EDITION

/* ---------- structures */

/* `placement` is called for each placement of a HUD definition: of an
element drawing the bitmap the tag reference at `bitmap` names (NULL: none
of its own), or (`items`) of an item of a crosshair or overlay, all of which
draw the bitmap its crosshair or overlay names */
struct ce_hud_walk
{
	void (*placement)(void *context, byte *placement, byte const *bitmap, boolean items);
	void *context;
};

/* ---------- prototypes/CE_HUD.C */

/* Walks every placement of the HUD definition `definition` of the loaded
map's tags (a unit, weapon or grenade HUD interface, 'unhi', 'wphi', 'grhi',
or the HUD globals, 'hudg'; any other group has none walked): of an Xbox
map's or a Halo PC map's, which lie alike. The numbers' are not walked:
their digits are drawn as the digits tag says. */
void ce_hud_placements_walk(
	void *definition,
	unsigned long group_tag,
	struct ce_hud_walk const *walk);

#endif

#endif
