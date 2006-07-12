/*
 * Sega Model 3 Emulator
 * Copyright (C) 2003 Bart Trzynadlowski, Ville Linde, Stefano Teso
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License Version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program (license.txt); if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * osd_common/osd_common.h
 *
 * Header file which defines the OSD interface. This is included last in
 * model3.h because it relies on data structures defined there.
 */

#ifndef INCLUDED_OSD_COMMON_OSD_H
#define INCLUDED_OSD_COMMON_OSD_H

/******************************************************************/
/* OSD Includes                                                   */
/******************************************************************/

#include "disasm.h" // didn't know where to put it, so I stuck it here ;)

/******************************************************************/
/* OSD Data Structures                                            */
/******************************************************************/

/*
 * OSD_CONTROLS Structure
 *
 * Holds the current state of the controls. Filled by the input code and used
 * by the control emulation code.
 */

typedef struct
{
    /*
     * Common to all games
     */

    UINT8   system_controls[2]; // maps directly to Fx040004 banks 0 and 1
    UINT8   game_controls[2];   // map directly to Fx040008 and Fx04000C

    /*
     * For games with guns
     *
     * The gun positions are reported in screen coordinates. The emulator will
     * make the appropriate adjustments. Gun coordinates should range from
     * (0,0), the upper-left corner, to (495,383), the lower-right corner.
     */

    UINT    gun_x[2], gun_y[2]; // gun positions for players 1 (0) and 2 (1)
    BOOL    gun_acquired[2];    // gun acquired status for players 1 and 2
                                // 0 = acquired, 1 = lost

	// Analog controls
	int		analog_axis[8];
} OSD_CONTROLS;

/******************************************************************/
/* Executable memory allocation                                   */
/******************************************************************/

extern void *malloc_exec(int length);
extern void free_exec(void *ptr);

/******************************************************************/
/* OSD GUI                                                        */
/******************************************************************/

extern void osd_message();
extern void osd_error();

/******************************************************************/
/* Renderer                                                       */
/******************************************************************/

extern void osd_renderer_invalidate_textures(UINT x, UINT y, UINT w, UINT h, UINT8 *texture_sheet);
extern void osd_renderer_draw_model(UINT32 *, UINT32, BOOL);
extern void osd_renderer_multiply_matrix(MATRIX);
extern void osd_renderer_translate_matrix(float, float, float);
extern void osd_renderer_push_matrix(void);
extern void osd_renderer_pop_matrix(void);
extern void osd_renderer_set_light(INT, LIGHT *);
extern void osd_renderer_set_viewport(const VIEWPORT *);
extern void osd_renderer_set_coordinate_system(const MATRIX);
extern void osd_renderer_clear(BOOL, BOOL);
extern void osd_renderer_set_color_offset(BOOL, FLOAT32, FLOAT32, FLOAT32);
extern void osd_renderer_draw_layer(int layer, UINT32 color_offset);
extern void osd_renderer_get_layer_buffer(int layer_num, UINT8 **buffer, int *pitch);
extern void osd_renderer_free_layer_buffer(UINT);
extern void osd_renderer_get_palette_buffer(UINT32 **, int *, int *);
extern void osd_renderer_free_palette_buffer(void);
extern void osd_renderer_blit(void);
extern void osd_renderer_begin_3d_scene(void);
extern void osd_renderer_end_3d_scene(void);
extern void osd_renderer_draw_text(int x, int y, const char* string, DWORD color, BOOL shadow);
                                    
                                    

/******************************************************************/
/* Sound Output                                                   */
/******************************************************************/

/******************************************************************/
/* Input                                                           */
/******************************************************************/

extern OSD_CONTROLS * osd_input_update_controls(void);
extern BOOL osd_input_init(void);
extern void osd_input_shutdown(void);

#endif  // INCLUDED_OSD_COMMON_OSD_H

