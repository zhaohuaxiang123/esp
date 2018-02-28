/*
 * This file is subject to the terms of the GFX License. If a copy of
 * the license was not distributed with this file, you can obtain one at:
 *
 *              http://ugfx.org/license.html
 */

#ifndef _LCD_GDISP_LLD_CONFIG_H_
#define _LCD_GDISP_LLD_CONFIG_H_
#include "sdkconfig.h"
#include "ugfx_driver_config.h"

#if UGFX_LCD_DRIVER_API_MODE
/*===========================================================================*/
/* Driver hardware support.                                                  */
/*===========================================================================*/
#define GDISP_HARDWARE_STREAM_WRITE		TRUE
#define GDISP_HARDWARE_CONTROL			TRUE
#define GDISP_HARDWARE_FILLS			TRUE
#define GDISP_LLD_PIXELFORMAT			GDISP_PIXELFORMAT_RGB565

#endif  /* UGFX_LCD_DRIVER_API_MODE */

#endif	/* _GDISP_LLD_CONFIG_H */
