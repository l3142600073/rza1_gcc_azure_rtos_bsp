#include "mcu_board_select.h"
#if (TARGET_GUI_BPP == TARGET_GUI_RGB888)
#include    <stdio.h>
#include    <string.h>

#include    "r_typedefs.h"

#include 	"lcd_panel.h"
#include 	"r_vdc_portsetting.h"
#include 	"r_rvapi_header.h"
#include 	"r_display_init.h"

#include	"r_os_abstraction_api.h"

#include    "gx_api.h"
#include    "gx_display.h"

#define     GRAPHICS_CHANNEL            (VDC_CHANNEL_0)

#define DATA_SIZE_PER_PIC      (4u)


#define FRAMEBUFFER_WIDTH	LCD_CH0_DISP_HW
#define FRAMEBUFFER_HEIGHT	LCD_CH0_DISP_VW
#define FRAMEBUFFER_STRIDE  (((FRAMEBUFFER_WIDTH * DATA_SIZE_PER_PIC) + 31u) & ~31u)

#define DISPLAY_XRES FRAMEBUFFER_WIDTH
#define DISPLAY_YRES FRAMEBUFFER_HEIGHT

#define RZ_SCREEN_HANDLE                0x525A0000

GX_COLOR frame_buffer[2][FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH] __attribute__ ((section(".VRAM_SECTION0")));

static UCHAR draw_buffer_index = 0;
static UCHAR visible_buffer_index = 1;
static UCHAR buffer_refresh_request = 0;
static UCHAR buffer_refresh_done = 0;

static TX_SEMAPHORE            semaphore_vdc_vsync;

static void vdc_vsync_callback(vdc_int_type_t int_type) {

	if (buffer_refresh_request) {

		buffer_refresh_request = 0;
		buffer_refresh_done = 1;
		R_RVAPI_GraphChangeSurfaceVDC(VDC_CHANNEL_0, VDC_LAYER_ID_2_RD, (void*)frame_buffer[visible_buffer_index]);

	}
	else if ( buffer_refresh_done ) {
		tx_semaphore_put ( &semaphore_vdc_vsync );
		buffer_refresh_done = 0;
	}
}

static void CopyCanvasToBackBuffer24xrgb(GX_CANVAS *canvas, GX_RECTANGLE *copy)
{
    GX_RECTANGLE display_size;
    GX_RECTANGLE copy_clip;
    ULONG *pPutRow;
    ULONG *pGetRow;
    ULONG *pGet;
    ULONG *pPut;
    int row;
    int col;
    int copy_width;
    int copy_height;

    gx_utility_rectangle_define(&display_size, 0, 0, DISPLAY_XRES - 1, DISPLAY_YRES - 1);
    copy_clip = *copy;

    gx_utility_rectangle_shift(&copy_clip, canvas->gx_canvas_display_offset_x, canvas->gx_canvas_display_offset_y);
    gx_utility_rectangle_overlap_detect(&copy_clip, &display_size, &copy_clip);
    copy_width = (copy_clip.gx_rectangle_right - copy_clip.gx_rectangle_left + 1);
    copy_height = copy_clip.gx_rectangle_bottom - copy_clip.gx_rectangle_top + 1;

    if (copy_width <= 0 ||
        copy_height <= 0)
    {
        return;
    }
    
    pGetRow =  (ULONG*)frame_buffer[visible_buffer_index]; // (USHORT*)canvas -> gx_canvas_memory;
    pGetRow += copy->gx_rectangle_top * canvas->gx_canvas_x_resolution;
    pGetRow += copy->gx_rectangle_left;

    pPutRow = (ULONG*)frame_buffer[draw_buffer_index];
    pPutRow += (canvas ->gx_canvas_display_offset_y + copy->gx_rectangle_top) * DISPLAY_XRES;
    pPutRow += (canvas ->gx_canvas_display_offset_x + copy->gx_rectangle_left);


    for (row = 0; row < copy_height; row++)
    {
        pGet = pGetRow;
        pPut = pPutRow;
        
        for (col = 0; col < copy_width; col++)
        {
            *pPut++ = *pGet++;
        }
        pGetRow += canvas->gx_canvas_x_resolution;
        pPutRow += DISPLAY_XRES;
    }
    

}

static void rz_24xrgb_buffer_toggle(GX_CANVAS *canvas, GX_RECTANGLE *dirty)
{
    GX_RECTANGLE Limit;
    GX_RECTANGLE Copy;
        
    gx_utility_rectangle_define(&Limit, 0, 0,
        canvas->gx_canvas_x_resolution -1,
        canvas->gx_canvas_y_resolution -1);
    
    // Swap Buffers
    visible_buffer_index = draw_buffer_index;
    draw_buffer_index ^= 1;

    canvas -> gx_canvas_memory = frame_buffer[draw_buffer_index];

    buffer_refresh_request = 1;

    tx_semaphore_get ( &semaphore_vdc_vsync, TX_WAIT_FOREVER);

	if (gx_utility_rectangle_overlap_detect(&Limit, &canvas->gx_canvas_dirty_area, &Copy))
	{
		/* copy our canvas to the back buffer */
        CopyCanvasToBackBuffer24xrgb(canvas, &Copy);
	}



}

static void ConfigureGUIXDisplayHardware24xrgb(GX_DISPLAY *display)
{
	vdc_error_t error;
	vdc_channel_t vdc_ch = GRAPHICS_CHANNEL;

	/***********************************************************************/
	/* display init (VDC output setting) */
	/***********************************************************************/
	{
		error = r_display_init (vdc_ch);
	}

	R_RVAPI_InterruptEnableVDC( vdc_ch, VDC_INT_TYPE_S0_LO_VSYNC, 0, vdc_vsync_callback);
	/***********************************************************************/
	/* Graphic Layer 2 CLUT8 */
	/***********************************************************************/
	if (error == VDC_OK)
	{
		gr_surface_disp_config_t gr_disp_cnf;

		/* buffer clear */
		// Set frame buffer to black
        memset((void*)frame_buffer[0], 0x00, FRAMEBUFFER_STRIDE * FRAMEBUFFER_HEIGHT);
        memset((void*)frame_buffer[1], 0x00, FRAMEBUFFER_STRIDE * FRAMEBUFFER_HEIGHT);

        gr_disp_cnf.layer_id         = VDC_LAYER_ID_2_RD;
        gr_disp_cnf.disp_area.hs_rel = 0;
        gr_disp_cnf.disp_area.hw_rel = FRAMEBUFFER_WIDTH;
        gr_disp_cnf.disp_area.vs_rel = 0;
        gr_disp_cnf.disp_area.vw_rel = FRAMEBUFFER_HEIGHT;
		gr_disp_cnf.fb_buff          = frame_buffer[visible_buffer_index];
        gr_disp_cnf.fb_stride        = FRAMEBUFFER_STRIDE;
        gr_disp_cnf.read_format      = VDC_GR_FORMAT_RGB888;
        gr_disp_cnf.read_ycc_swap    = VDC_GR_YCCSWAP_CBY0CRY1;
        gr_disp_cnf.read_swap        = VDC_WR_RD_WRSWA_32BIT;
        gr_disp_cnf.disp_mode        = VDC_DISPSEL_CURRENT;

        error = R_RVAPI_GraphCreateSurfaceVDC(vdc_ch, &gr_disp_cnf);
    }

    /* Image Quality Adjustment */
    if (VDC_OK == error)
    {
        error = r_image_quality_adjustment(vdc_ch);
    }

	/* Enable signal output */
	if (VDC_OK == error)
	{
		/* Wait for register update */
		R_OS_TaskSleep(5);

		R_RVAPI_DispPortSettingVDC(vdc_ch, &VDC_LcdPortSetting);
	}

}

UINT rz_graphics_driver_setup_24xrgb(GX_DISPLAY *display)
{
    _gx_display_driver_24xrgb_setup(display, GX_NULL, rz_24xrgb_buffer_toggle);
    display -> gx_display_handle =                       RZ_SCREEN_HANDLE;
    tx_semaphore_create( &semaphore_vdc_vsync, "Swap buffer", 0);
    ConfigureGUIXDisplayHardware24xrgb(display);
    return GX_SUCCESS;
}
#endif
