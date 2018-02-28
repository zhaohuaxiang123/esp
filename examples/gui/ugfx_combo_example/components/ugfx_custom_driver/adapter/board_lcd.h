/*
 * This file is subject to the terms of the GFX License. If a copy of
 * the license was not distributed with this file, you can obtain one at:
 *
 *              http://ugfx.org/license.html
 */

#include "sdkconfig.h"
#include "ugfx_driver_config.h"

#if UGFX_LCD_DRIVER_API_MODE
#include "gos_freertos_priv.h"
#include "gfx.h"

#define GDISP_DRIVER_VMT			GDISPVMT_ILI9341
#include "gdisp_lld_config.h"
#include "src/gdisp/gdisp_driver.h"
#include "lcd_adapter.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/
#ifndef GDISP_SCREEN_HEIGHT
  #define GDISP_SCREEN_HEIGHT     UGFX_DRIVER_SCREEN_HEIGHT
#endif
#ifndef GDISP_SCREEN_WIDTH
    #define GDISP_SCREEN_WIDTH        UGFX_DRIVER_SCREEN_WIDTH
#endif
#ifndef GDISP_INITIAL_CONTRAST
	#define GDISP_INITIAL_CONTRAST	100
#endif
#ifndef GDISP_INITIAL_BACKLIGHT
	#define GDISP_INITIAL_BACKLIGHT	100
#endif

#define LCD_CASET   0x2A
#define LCD_RASET   0x2B
#define LCD_RAMWR   0x2C
#define LCD_MADCTL  0x36

// Static functions
static void init_board(GDisplay *g)
{
    board_lcd_init();
}

static void post_init_board(GDisplay *g)
{
}

static void setpin_reset(GDisplay *g, bool_t state)
{
}

static void acquire_bus(GDisplay *g)
{
}

static void release_bus(GDisplay *g)
{
}

static void acquire_sem()
{
}

static void release_sem()
{
}

static void write_cmd(GDisplay *g, uint8_t cmd)
{
    board_lcd_write_cmd(cmd);
}

static void write_data(GDisplay *g, uint16_t data)
{
    board_lcd_write_data(data);
}

static void write_data_byte(GDisplay *g, uint8_t data)
{
    board_lcd_write_data_byte(data);
}

static void write_data_byte_repeat(GDisplay *g, uint16_t data, int point_num)
{
    board_lcd_write_data_byte_repeat(data, point_num);
}

static void write_cmddata(GDisplay *g, uint8_t cmd, uint32_t data)
{
    board_lcd_write_cmddata(cmd, data);
}

static void write_datas(GDisplay *g, uint8_t* data, uint16_t length)
{
    board_lcd_write_datas(data, length);
}

static void set_backlight(GDisplay *g, uint16_t data)
{
}

static void set_viewport(GDisplay *g)
{
    write_cmddata(g, LCD_CASET, MAKEWORD( ((g->p.x) >> 8), (g->p.x) & 0xFF, ((g->p.x+g->p.cx-1) >> 8), (g->p.x+g->p.cx-1) & 0xFF));
    write_cmddata(g, LCD_RASET, MAKEWORD( ((g->p.y) >> 8), (g->p.y) & 0xFF, ((g->p.y+g->p.cy-1) >> 8), (g->p.y+g->p.cy-1) & 0xFF));
    write_cmd (g, LCD_RAMWR);
}

#endif
