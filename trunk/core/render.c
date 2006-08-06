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
 * render.c
 *
 * Rendering engine. Responsible for both Real3D graphics emulation and
 * for drawing the tile layers with correct priorities.
 *
 * NOTES:
 * ------
 *
 * - The "hardware coordinate system" is the default Model 3 coordinate system
 *   before any matrices are applied. In it, +X is to the right, +Y is down,
 *   and +Z is further into the screen.
 */

#include "model3.h"

extern BOOL render_scene(void);

#define LOG_MODEL_ADDR  0   // logs model addresses to models.log

/******************************************************************/
/* Useful Macros                                                  */
/******************************************************************/

/*
 * Single-Precision Floating Point
 */

#define GET_FLOAT(ptr)              (*((float *) ptr))

/*
 * Trigonometry
 */

#define PI                          3.14159265358979323846264338327
#define CONVERT_TO_DEGREES(a)       (((a) * 180.0) / PI)

/******************************************************************/
/* Private Data                                                   */
/******************************************************************/

/*
 * Memory Regions
 *
 * These will be passed to us before rendering begins.
 */

static UINT8    *culling_ram_8e;    // pointer to Real3D culling RAM
static UINT8    *culling_ram_8c;    // pointer to Real3D culling RAM
static UINT8    *polygon_ram;       // pointer to Real3D polygon RAM
static UINT8    *texture_ram;       // pointer to Real3D texture RAM
static UINT8    *vrom;              // pointer to VROM

/*
 * Matrix Base
 */

static float    *matrix_base;       // current scene's matrix table
static float	*lod_base;			// current scene's LOD table

/*
 * Matrix Conversion Tables
 *
 * These tables map Model 3 matrix indices to column-major form.
 */

static INT      normal_matrix[4*4] =    // normal matrices
                    {
                        3*4+0,  3*4+1,   3*4+2,
                        0*4+0,  1*4+0,   2*4+0,
                        0*4+1,  1*4+1,   2*4+1,
                        0*4+2,  1*4+2,   2*4+2
					};
static INT      coord_matrix[4*4] =     // coordinate system matrix
                    {
                        3*4+2,  3*4+0,   3*4+1,
                        0*4+2,  1*4+2,   2*4+2,
                        0*4+0,  1*4+0,   2*4+0,
                        0*4+1,  1*4+1,   2*4+1
                    };

/******************************************************************/
/* Function Prototypes                                            */
/******************************************************************/

static void draw_block(UINT32 *);

/******************************************************************/
/* TEMPORARY Model Tracking Code                                  */
/*                                                                */
/* This code builds a list of VROM model addresses as they are    */
/* referenced.                                                    */
/******************************************************************/

#if LOG_MODEL_ADDR

static struct model_addr
{
    UINT32              addr;       // model address in VROM
    INT                 num_polys;  // number of polygons in model
    struct model_addr   *next;      // next in list
} *model_addr_list = NULL;

static INT count_polys(UINT32 *mdl)
{
    UINT32  link_data, mask;
    INT     num_polys = 0, num_verts, i, stop;

    do
    {
        stop = BSWAP32(mdl[1]) & 4;     // get stop bit
        link_data = BSWAP32(mdl[0]);    // link data

        /*
         * Count how many vertices the polygon has by subtracting the number
         * of vertices used from the previous polygon from 4 (quad) or 3 (tri)
         */

        num_verts = (link_data & 0x40) ? 4 : 3;
        mask = 8;
        for (i = 0; i < 4 && num_verts; i++)
        {
            if ((link_data & mask))
                --num_verts;
            mask >>= 1;
        }

        /*
         * Advance to next polygon
         */

        mdl += 7 + num_verts * 4;
        ++num_polys;    // increment count
    } while (!stop);

    return num_polys;
}

static void record_model(UINT32 addr)
{
    struct model_addr   *l;

    /*
     * Search for this entry -- if already in the list, exit
    for (l = model_addr_list; l != NULL; l = l->next)
    {
        if (addr == l->addr)
            return;
    }

    /*
     * Add new entry
     */

    l = malloc(sizeof(struct model_addr));
    if (l == NULL)
        error("out of memory in record_model()");
    l->addr = addr;
    l->num_polys = count_polys((UINT32 *) &vrom[addr * 4]);
    l->next = model_addr_list;
    model_addr_list = l;
}

#endif

/******************************************************************/
/* Real3D Address Translation                                     */
/******************************************************************/

/*
 * translate_scene_graph_address():
 *
 * Returns a pointer to scene graph (culling RAM) memory given a scene graph
 * address (only lower 24 bits are relevant.)
 */

static UINT32 *translate_scene_graph_address(UINT32 addr)
{
    addr &= 0x00FFFFFF;         // only lower 24 bits matter
    if ((addr & 0x00800000))    // 8E culling RAM
    {
        if (addr >= 0x00840000)
            error("translate_scene_graph_address(): addr = %08X", addr);
        return (UINT32 *) &culling_ram_8e[(addr & 0x0003FFFF) * 4];
    }
    else                        // 8C culling RAM
    {
        //if (addr >= 0x00100000)
        //    error("translate_scene_graph_address(): addr = %08X", addr);
        return (UINT32 *) &culling_ram_8c[(addr & 0x000FFFFF) * 4];
    }
}

/*
 * draw_model():
 *
 * Translates the model address and draws the model (accounting for
 * endianness.)
 */

static void draw_model(UINT32 addr)
{
    addr &= 0x00FFFFFF;     // only lower 24 bits matter
    if (addr > 0x00100000)  // VROM 
    {
#if LOG_MODEL_ADDR
        record_model(addr); // TEMP: this tracks model addresses in VROM
#endif
		osd_renderer_draw_model((UINT32 *) &vrom[(addr & 0x00FFFFFF) * 4], addr, 0);
    }
    else                    // polygon RAM (may actually be 4MB)
	{
		osd_renderer_draw_model((UINT32 *) &polygon_ram[(addr & 0x000FFFFF) * 4], addr, 1);
	}
}

/******************************************************************/
/* Matrices                                                       */
/*                                                                */
/* Model 3 matrices are 4x3 in size (they lack the translational  */
/* component.) They are layed out like this:                      */
/*                                                                */
/*      03 04 05 00                                               */
/*      06 07 08 01                                               */
/*      09 10 11 02                                               */
/*                                                                */
/* Matrix #0 is for coordinate system selection, it is layed out  */
/* as:                                                            */
/*                                                                */
/*      06 07 08 01                                               */
/*      09 10 11 02                                               */
/*      03 04 05 00                                               */
/*                                                                */
/* By default, the Model 3 appears to have a form of left-handed  */
/* coordinate system where positive Z means further away from the */
/* camera and the positive Y axis points downward along the       */
/* screen.                                                        */
/******************************************************************/

/*
 * get_matrix():
 *
 * Reads a Model 3 matrix and converts it to column-major 4x4 form using the
 * supplied conversion table.
 */

static void get_matrix(MATRIX dest, INT convert[3*4], UINT num)
{
    INT m = num * 12, i;

    for (i = 0; i < 3*4; i++)   // fetch Model 3 4x3 matrix
        dest[convert[i]] = matrix_base[m + i];
    dest[0*4+3] = 0.0;          // fill in translation component to make 4x4
    dest[1*4+3] = 0.0;
    dest[2*4+3] = 0.0;
    dest[3*4+3] = 1.0;
}


/******************************************************************/
/* Scene Graph Traversal                                          */
/******************************************************************/

int list_depth = 0;

/*
 * draw_list():
 *
 * Processes a list backwards. Each list element references a block.
 */

static void draw_list(UINT32 *list)
{
    UINT32  *list_ptr;
    UINT32  addr;
    
    list_ptr = list;

	if (list_depth > 2)
		return;

	list_depth++;

    /*
     * Go to end of list
     */

    while (1)
    {
        addr = *list_ptr;
        if ((addr & 0x02000000))    // last pointer in list
        {
        	//--list_ptr;
            break;
		}            
        if (addr == 0 || addr == 0x800800)  // safeguard in case memory hasn't been set up
        {
            --list_ptr;
            break;
        }
        ++list_ptr;
    }

    while (list_ptr >= list)
    {
        addr = *list_ptr;
        if ((addr & 0x00FFFFFF) != 0x00FFFFFF)
	    	draw_block(translate_scene_graph_address(addr));		
        --list_ptr; // next element
    }

	list_depth--;
}

/*
 * draw_pointer_list():
 *
 * Draws a 4-element pointer list for model LOD selection.
 */

static void draw_pointer_list(UINT lod_num, UINT32* list)
{
	float *lod_control = (float *)((UINT32)lod_base + lod_num * 8);

	/*
	 * Perform the actual LOD calculation, select the LOD model
	 * to draw -- and perform additional LOD blending.
	 */

	#if 0
	printf(	"LOD control = %f, %f\n"
			"              %f, %f\n"
			"              %f, %f\n"
			"              %f, %f\n",
			lod_control[0], lod_control[1],
			lod_control[2], lod_control[3],
			lod_control[4], lod_control[5],
			lod_control[6], lod_control[7]
	);
	#endif

	if(1)
        draw_model( list[0] );
	else if(0)
        draw_model( list[1] );
	else if(0)
        draw_model( list[2] );
	else
        draw_model( list[3] );
}

/*
 * draw_block():
 *
 * Traverses a 10- (or 8-) word block (culling node.) The blocks have this
 * format:
 *
 *  0: ID code (upper 22 bits) and control bits 
 *  1: Scaling? Not present in Step 1.0
 *  2: Flags? Not present in Step 1.0
 *  3: Lower 12 bits are matrix select. Upper bits contain flags.
 *  4: X translation (floating point)
 *  5: Y translation
 *  6: Z translation
 *  7: Pointer to model data, list, or pointer list 
 *  8: Pointer to next block
 *  9: Pair of 16-bit values, related to rendering order
 *
 * Lower 16 bits of word 2 control "texture offset": -P-X XXXX X--Y YYYY, same
 * as in polygon headers. The purpose of this is unknown.
 */

static void draw_block(UINT32 *block)
{
    MATRIX  m;
    UINT32  addr;
    INT     offset;
    INT     txoffs_x, txoffs_y, txoffs_page;

    if (m3_config.step == 0x10) // Step 1.0 blocks are only 8 words
        offset = 2;
    else
        offset = 0;

    addr = block[7 - offset];

    txoffs_x = ((block[2] >> 7) & 0x3F) * 32;
    txoffs_y = (block[2] & 0x1F) * 32;
    txoffs_page = !!(block[2] & 0x4000);

    /*
     * Apply matrix and translation
     */

    get_matrix(m, normal_matrix, block[3 - offset] & 0xFFF);
    osd_renderer_push_matrix();

    if ((block[0] & 0x10))  // this bit appears to control translation
	{
        osd_renderer_translate_matrix(GET_FLOAT(&block[4 - offset]), GET_FLOAT(&block[5 - offset]), GET_FLOAT(&block[6 - offset]));
	}
	else
	{
		if ((block[3 - offset] & 0xFFF) != 0)
			osd_renderer_multiply_matrix(m);
	}

    /*
     * Bit 0x08 of word 0 indicates a pointer list
     */

    if ((block[0] & 0x08))   
		draw_pointer_list((block[3 - offset] >> 12) & 127, translate_scene_graph_address(addr));
    else
    {
        if (addr != 0x0FFFFFFF && addr != 0x01000000 && addr != 0x00800800 && addr != 0)  // valid?
        {
            switch ((addr >> 24) & 0xFF)    // decide what address points to
            {
            case 0x00:  // block
                draw_block(translate_scene_graph_address(addr));
                break;
            case 0x01:  // model
            case 0x03:  // model (Scud Race, model in VROM)
                draw_model(addr);
                break;
            case 0x04:  // list
                draw_list(translate_scene_graph_address(addr));
                break;
            default:
                break;
            }
        }
    }

    osd_renderer_pop_matrix();

    /*
     * NOTE: This second pointer needs to be investigated further.
     */

	addr = block[8 - offset];
	if (addr != 0x01000000 && addr != 0x00800800 && addr != 0)  // valid?
		draw_block(translate_scene_graph_address(addr));
}

/*
 * get_viewport_data():
 *
 * Sets up a VIEWPORT structure. Is passed a pointer to the main node.
 */

static void get_viewport_data(VIEWPORT *vp, UINT32 *node)
{
    vp->x       = (node[0x1A] & 0xFFFF) >> 4;   // position is in 12.4 format
    vp->y       = (node[0x1A] >> 16) >> 4;
    vp->width   = (node[0x14] & 0xFFFF) >> 2;   // size is in 14.2 format
    vp->height  = (node[0x14] >> 16) >> 2;

    vp->left    = asin(GET_FLOAT(&node[0x0C]));
    vp->right   = asin(GET_FLOAT(&node[0x10]));
    vp->up      = asin(GET_FLOAT(&node[0x0E]));
    vp->down    = asin(GET_FLOAT(&node[0x12]));

    vp->left    = CONVERT_TO_DEGREES(vp->left);
    vp->right   = CONVERT_TO_DEGREES(vp->right);
    vp->up      = CONVERT_TO_DEGREES(vp->up);
    vp->down    = CONVERT_TO_DEGREES(vp->down);
}

/*
 * get_light_data():
 *
 * Sets up a LIGHT structure based on ambient sun light. The sun vector
 * indicates the direction of the parallel sun light. It is specified in the
 * hardware coordinate system and must be applied before any matrices.
 */

static void get_light_data(LIGHT* l, UINT32* node)
{
	memset( l, 0, sizeof(LIGHT) );
    l->u = GET_FLOAT(&node[5]);    // it seems X needs to be inverted
    l->v = GET_FLOAT(&node[6]);
    l->w = GET_FLOAT(&node[4]);

	l->diffuse_intensity = GET_FLOAT(&node[7]);
	l->ambient_intensity = (UINT8)((node[0x24] >> 8) & 0xFF) / 256.0f;

    LOG("model3.log", "sun light = (%f,%f,%f),%f,(%02X=%f)\n", l->u, l->v, l->w, l->diffuse_intensity, ((node[0x24] >> 8) & 0xFF), l->ambient_intensity);

	l->color = 0xFFFFFFFF;


}

/*
 * draw_viewport():
 *
 * Traverses the main (scene descriptor) nodes and draws the ones with the
 * given viewport priority.
 */

static void draw_viewport(UINT pri, UINT32 addr)
{
    MATRIX      m;
    VIEWPORT    vp;
	LIGHT		sun;
    UINT32      *node;
    UINT32      next_addr, ptr;

    node = translate_scene_graph_address(addr);

    /*
     * Recurse until the last node has been reached
     */

    next_addr = node[1];
    if (next_addr == 0) // culling RAM probably hasn't been set up yet...
        return;
    if (next_addr != 0x01000000)
        draw_viewport(pri, next_addr);

    /*
     * Draw this node if the priority matches
     */

    if (pri == ((node[0] >> 3) & 3))
    {
        /*
         * Set up viewport and matrix table base
         */

        get_viewport_data(&vp, node);
        osd_renderer_set_viewport(&vp);
        matrix_base = (float *) translate_scene_graph_address(node[0x16]);
		lod_base = (float *) translate_scene_graph_address(node[0x17]);

        /*
         * Lighting -- seems to work nice if applied before coordinate system
         * selection but I haven't done a thorough check.
         */

		get_light_data(&sun, node);
		sun.type = LIGHT_PARALLEL;
		osd_renderer_set_light( 0, &sun );

        /*
         * Set coordinate system (matrix 0)
         */

        get_matrix(m, coord_matrix, 0);
        osd_renderer_set_coordinate_system(m);

        /*
         * Process a block or list. So far, no other possibilities have been
         * seen here...
         */

        ptr = node[2];

        switch ((ptr >> 24) & 0xFF)
        {
        case 0x00:  // block
            draw_block(translate_scene_graph_address(node[2]));
            break;
        //case 0x04:  // list
        //    draw_list(translate_scene_graph_address(node[2]));
        //    break;
        default:    // unknown
            break;
        }
    }
}


/******************************************************************/
/* Frame Update                                                   */
/******************************************************************/

/*
 * do_3d():
 *
 * Draw the complete Real3D frame.
 */

static void do_3d(void)
{
    INT i;
	
	
    for (i = 0; i < 4; i++)
    {
        osd_renderer_clear(0, 1);   // clear Z-buffer
		osd_renderer_begin_3d_scene();
        draw_viewport(i, 0x00800000);
		osd_renderer_end_3d_scene();
		
        LOG("model3.log", "GOT HERE\n");
    }
}

/*
 * void set_color_offset(UINT32 reg);
 *
 * Sets color offset to the value specified by reg, and updates the
 * renderer status.
 */

static void set_color_offset(UINT32 reg)
{
	FLOAT32 r, g, b;

	/*
	 * Color offset is specified as a triplet of signed 8-bit values, one
	 * for each color component. 0 is the default value, it doesn't modify
	 * the output color. It's equal to disabling the color offset (though
	 * some bit in the tilegen ports could be used for this).
	 * 0x80 (-128) is the minimum value, and corresponds to complete black.
	 * 0x7F (+127) is the maximum value, and corresponds to complete white.
	 */

	r = (FLOAT32)((INT32)((INT8)(((INT32)reg >>  8) & 0xFF))) / 128.0f;
	g = (FLOAT32)((INT32)((INT8)(((INT32)reg >> 16) & 0xFF))) / 128.0f;
	b = (FLOAT32)((INT32)((INT8)(((INT32)reg >> 24) & 0xFF))) / 128.0f;

//	osd_renderer_set_color_offset(r != 0.0f && g != 0.0f && b != 0.0f, r, g, b);
}

/*
 * void render_frame(void);
 *
 * Draws the entire frame (all 3D and 2D graphics) and blits it.
 */

void render_frame(void)
{
	UINT32 renderer_features;
	int i, j;
	UINT32 color_offset;
    LOG("model3.log", "RENDER START\n");

    tilegen_update();

	renderer_features = osd_renderer_get_features();

	if (renderer_features & RENDERER_FEATURE_PRIORITY)
	{
		// this is codepath for new-style renderers that support priorities
		{
			UINT32 *priority = tilegen_get_priority_buffer();
		
			for (i=0; i < 4; i++)
			{
				int pitch;
				UINT8 *buffer;
				osd_renderer_get_priority_buffer(i, &buffer, &pitch);

				for (j=0; j < 512; j++)
				{
					if (tilegen_is_priority_enabled())
					{
						buffer[j*pitch] = (priority[j] >> ((3-i) * 8)) & 0xff;
					}
					else
					{
						if (i < 2)	buffer[j*pitch] = 0xff;
						else		buffer[j*pitch] = 0x00;
					}
				}

				osd_renderer_free_priority_buffer(i);
			}
		}

		osd_renderer_clear(1, 1);   // clear both the frame and Z-buffer

		//    set_color_offset(tilegen_read_32(0x44));
	
		/*if (tilegen_is_layer_enabled(3))
		{
			UINT32 scroll = tilegen_get_layer_scroll_pos(3);
			color_offset = tilegen_get_layer_color_offset(3);
			osd_renderer_draw_layer(3, color_offset,  scroll & 0xffff, scroll >> 16);
		}
		if (tilegen_is_layer_enabled(2))
		{
			UINT32 scroll = tilegen_get_layer_scroll_pos(2);
			color_offset = tilegen_get_layer_color_offset(2);
			osd_renderer_draw_layer(2, color_offset, scroll & 0xffff, scroll >> 16);
		}*/

		for (i=3; i >= 0; i--)
		{
			if (tilegen_is_layer_enabled(i))
			{
				UINT32 scroll = tilegen_get_layer_scroll_pos(i);
				color_offset = tilegen_get_layer_color_offset(i);
				osd_renderer_draw_layer(i, color_offset, scroll & 0xffff, scroll >> 16, FALSE);
			}
		}

		do_3d();

		for (i=3; i >= 0; i--)
		{
			if (tilegen_is_layer_enabled(i))
			{
				UINT32 scroll = tilegen_get_layer_scroll_pos(i);
				color_offset = tilegen_get_layer_color_offset(i);
				osd_renderer_draw_layer(i, color_offset, scroll & 0xffff, scroll >> 16, TRUE);
			}
		}
	}
	else
	{
		// this codepath is for the old-style renderers
		osd_renderer_clear(1, 1);   // clear both the frame and Z-buffer
		
		if (tilegen_is_layer_enabled(3))
		{
			UINT32 scroll = tilegen_get_layer_scroll_pos(3);
			color_offset = tilegen_get_layer_color_offset(3);
			osd_renderer_draw_layer(3, color_offset, scroll & 0xffff, scroll >> 16, TRUE);
		}
		if (tilegen_is_layer_enabled(2))
		{
			UINT32 scroll = tilegen_get_layer_scroll_pos(2);
			color_offset = tilegen_get_layer_color_offset(2);
			osd_renderer_draw_layer(2, color_offset, scroll & 0xffff, scroll >> 16, TRUE);
		}

		do_3d();

		if (tilegen_is_layer_enabled(1))
		{
			UINT32 scroll = tilegen_get_layer_scroll_pos(1);
			color_offset = tilegen_get_layer_color_offset(1);
			osd_renderer_draw_layer(1, color_offset, scroll & 0xffff, scroll >> 16, TRUE);
		}
		if (tilegen_is_layer_enabled(0))
		{
			UINT32 scroll = tilegen_get_layer_scroll_pos(0);
			color_offset = tilegen_get_layer_color_offset(0);
			osd_renderer_draw_layer(0, color_offset, scroll & 0xffff, scroll >> 16, TRUE);
		}
	}

	/*{
		double time = (double)(counter_end - counter_start) / (double)counter_frequency;
		printf("Rendering time: %f ms\n", time*1000.0);
	}*/

//    set_color_offset(tilegen_read_32(0x40));

 //   osd_renderer_draw_layer(1);

	/*if (tilegen_is_layer_enabled(1))
	{
		UINT32 scroll = tilegen_get_layer_scroll_pos(1);
		color_offset = tilegen_get_layer_color_offset(1);
		osd_renderer_draw_layer(1, color_offset, scroll & 0xffff, scroll >> 16);
	}
	if (tilegen_is_layer_enabled(0))
	{
		UINT32 scroll = tilegen_get_layer_scroll_pos(0);
		color_offset = tilegen_get_layer_color_offset(0);
		osd_renderer_draw_layer(0, color_offset, scroll & 0xffff, scroll >> 16);
	}*/

	LOG("model3.log", "RENDER END\n");    
}


/******************************************************************/
/* Initialization and Shutdown                                    */
/******************************************************************/

/*
 * void render_init(UINT8 *culling_ram_8e_ptr, UINT8 *culling_ram_8c_ptr,
 *                  UINT8 *polygon_ram_ptr, UINT8 *texture_ram_ptr,
 *                  UINT8 *vrom_ptr);
 *
 * Initializes the renderer by receiving the Real3D memory regions. Passes
 * the memory to the OSD renderer.
 *
 * Parameters:
 *      culling_ram_8e_ptr = Pointer to Real3D culling RAM at 0x8E000000.
 *      culling_ram_8c_ptr = Pointer to Real3D culling RAM at 0x8C000000.
 *      polygon_ram_ptr    = Pointer to Real3D polygon RAM.
 *      texture_ram_ptr    = Pointer to Real3D texture RAM.
 *      vrom_ptr           = Pointer to VROM.
 */

void render_init(UINT8 *culling_ram_8e_ptr, UINT8 *culling_ram_8c_ptr,
                 UINT8 *polygon_ram_ptr, UINT8 *texture_ram_ptr,
                 UINT8 *vrom_ptr)
{
    culling_ram_8e = culling_ram_8e_ptr;
    culling_ram_8c = culling_ram_8c_ptr;
    polygon_ram = polygon_ram_ptr;
    texture_ram = texture_ram_ptr;
    vrom = vrom_ptr;
}

/*
 * void render_shutdown(void);
 *
 * Shuts down the rendering engine.
 */

void render_shutdown(void)
{
#if LOG_MODEL_ADDR
    // TEMP code to print out model list

    FILE                *fp;
    struct model_addr   *l, *next;

    fp = fopen("models.log", "w");
    if (fp == NULL)
    {
        printf("failed to write models.log\n");
        return;
    }

    l = model_addr_list;
    while (l != NULL)
    {
        fprintf(fp, "addr = %08X\tnum_polys = %d\n", l->addr, l->num_polys);
        next = l->next;
        free(l);
        l = next;
    }

    fclose(fp);
#endif
}
