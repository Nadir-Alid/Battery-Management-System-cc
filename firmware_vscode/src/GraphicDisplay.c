/*
 * GraphicDisplay.c
 *
 *  Created on: Jun 28, 2024
 *      Author: pablo-jean
 */

#include "GraphicDisplay.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>

/*
 * Privates
 */

/*
 * Extern
 */

/*
 * Display Drivers
 */
/* Adapters keep function pointer types compatible with the generic API. */
static uint8_t st7565_pixel(void *handle, uint32_t x, uint32_t y, uint8_t color) {
    return ST7565_Write(handle, x, y, color != 0);
}
static uint8_t st7565_fill(void *handle, uint8_t color) {
    return ST7565_Fill(handle, color);
}
static uint8_t st7565_refresh(void *handle) {
    return ST7565_Refresh(handle);
}
static uint8_t st7565_contrast(void *handle, uint8_t value) {
    return ST7565_SetContrast(handle, value);
}
gd_driver_t _Gd_Driver_ST7565Attr = {
    .fxnSetPixelColor = st7565_pixel,
    .fxnFillFrameBuffer = st7565_fill,
    .fxnRefreshDisp = st7565_refresh,
    .fxnSetContrast = st7565_contrast
};
gd_driver_t *Gd_Driver_ST7565 = &_Gd_Driver_ST7565Attr;

/* Macros */

/* Display Functions */

static gd_error_e _drv_fill_frame_buffer(gd_t *Gd, gd_color_e color) {
    return Gd->disp->fxnFillFrameBuffer(Gd->disp->pHandle, (uint8_t)color) == 0 ? GD_OK : GD_FAIL;
}

static gd_error_e _drv_set_pixel(gd_t *Gd, uint32_t x, uint32_t y, gd_color_e color) {
    return Gd->disp->fxnSetPixelColor(Gd->disp->pHandle, x, y, (uint8_t)color) == 0 ? GD_OK : GD_FAIL;
}

static gd_error_e _drv_set_refresh(gd_t *Gd) {
    return Gd->disp->fxnRefreshDisp(Gd->disp->pHandle) == 0 ? GD_OK : GD_FAIL;
}

static gd_error_e _drv_set_constrat(gd_t *Gd, uint8_t value) {
    if (Gd->disp->fxnSetContrast == NULL) return GD_FAIL;
    return Gd->disp->fxnSetContrast(Gd->disp->pHandle, value) == 0 ? GD_OK : GD_FAIL;
}

static gd_error_e _drv_set_on(gd_t *Gd, bool on) {
    if (Gd->disp->fxnSetOn == NULL) return GD_FAIL;
    return Gd->disp->fxnSetOn(Gd->disp->pHandle, on) == 0 ? GD_OK : GD_FAIL;
}

/* Mutex Functions */
static void _mtx_lock(gd_t *Gd){
	if (Gd->fxn.mtxLock != NULL){
		Gd->fxn.mtxLock();
	}
}

static void _mtx_unlock(gd_t *Gd){
	if (Gd->fxn.mtxUnlock != NULL){
		Gd->fxn.mtxUnlock();
	}
}

/* Convert Degrees to Radians */
static float _DegToRad(float par_deg) {
	return par_deg * (3.14159265358979323846f / 180.0f);
}

/* Normalize degree to [0;360] */
static uint16_t _NormalizeTo0_360(uint16_t par_deg) {
	uint16_t loc_angle;
	if(par_deg <= 360) {
		loc_angle = par_deg;
	} else {
		loc_angle = par_deg % 360;
		loc_angle = (loc_angle ? loc_angle : 360);
	}
	return loc_angle;
}


/*
 * Publics
 */

/* Initialize the oled screen */
gd_error_e GD_Init(gd_t *Gd, gd_params_t *params) {
	if (Gd == NULL || params == NULL || params->u32Width == 0 || params->u32Height == 0 ||
        params->DisplayHandle == NULL || params->DisplayDriver == NULL ||
        params->DisplayDriver->fxnSetPixelColor == NULL ||
        params->DisplayDriver->fxnFillFrameBuffer == NULL ||
        params->DisplayDriver->fxnRefreshDisp == NULL ||
        (params->mtxLock == NULL) != (params->mtxUnlock == NULL)) return GD_FAIL;
    // Assert if some parameter is invalid
	assert(Gd != NULL);
	assert(params != NULL);
	assert(params->u32Width != 0);
	assert(params->u32Height != 0);
	assert(params->DisplayHandle != NULL);
	assert(params->DisplayDriver != NULL);

	// Allocate buffer if doesn`t exitsts
	Gd->u32Height = params->u32Height;
	Gd->u32Width = params->u32Width;
	Gd->driver_instance = *params->DisplayDriver;
    Gd->disp = &Gd->driver_instance;
    Gd->fxn.mtxLock = params->mtxLock;
    Gd->fxn.mtxUnlock = params->mtxUnlock;
	Gd->disp->pHandle = params->DisplayHandle;
	Gd->_intern.u32BufferLen = Gd->u32Height * Gd->u32Width / 8;

	// Set default values for screen object
	Gd->_intern.u32CurrX = 0;
	Gd->_intern.u32CurrY = 0;
	Gd->_intern.bInitialized = true;
    Gd->_intern.bDisplayOn = true;

	// Clear screen
	if (GD_Fill(Gd, GD_BLACK) != GD_OK) return GD_FAIL;

	return GD_UpdateScreen(Gd);
}

/* Fill the whole screen with the given color */
gd_error_e GD_Fill(gd_t *Gd, gd_color_e color) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	_mtx_lock(Gd);
	gd_error_e result = _drv_fill_frame_buffer(Gd, color);
	_mtx_unlock(Gd);

	return result;
}

/* Write the screenbuffer with changed to the screen */
gd_error_e GD_UpdateScreen(gd_t *Gd) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	_mtx_lock(Gd);
	gd_error_e result = _drv_set_refresh(Gd);
	_mtx_unlock(Gd);

	return result;
}

/*
 * Draw one pixel in the screenbuffer
 * X => X Coordinate
 * Y => Y Coordinate
 * color => Pixel color
 */
gd_error_e GD_DrawPixel(gd_t *Gd, uint32_t x, uint32_t y, gd_color_e color) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	if(x >= Gd->u32Width || y >= Gd->u32Height) {
		// Don't write outside the buffer
		return GD_FAIL;
	}

	_mtx_lock(Gd);
	// Draw in the right color
	gd_error_e result = _drv_set_pixel(Gd, x, y, color);
//	if(color == GD_WHITE) {
//		Gd->_intern.pu8FrameBuffer[x + (y / 8) * Gd->u32Width] |= 1 << (y % 8);
//	} else {
//		Gd->_intern.pu8FrameBuffer[x + (y / 8) * Gd->u32Width] &= ~(1 << (y % 8));
//	}
	_mtx_unlock(Gd);

	return result;
}

/*
 * Draw 1 char to the screen buffer
 * ch       => char om weg te schrijven
 * Font     => Font waarmee we gaan schrijven
 * color    => Black or White
 */
gd_error_e GD_WriteChar(gd_t *Gd, char ch, const gd_font_t *Font, gd_color_e color) {
	uint32_t i, b, j;
	gd_color_e pxColor;

	assert(Gd != NULL);
	assert(Font != NULL);
	assert(Gd->_intern.bInitialized == true);

	// Check if character is valid
	if (ch < 32 || ch > 126)
		return GD_FAIL;

	// Check remaining space on current line
	if (Gd->u32Width < (Gd->_intern.u32CurrX + Font->width) ||
			Gd->u32Height < (Gd->_intern.u32CurrY + Font->height))
	{
		// Not enough space on current line
		return GD_FAIL;
	}

	// Use the font to write
	for(i = 0; i < Font->height; i++) {
		b = Font->data[(ch - 32) * Font->height + i];
		for(j = 0; j < Font->width; j++) {
			if((b << j) & 0x8000)  {
				pxColor = color;
			} else {
				pxColor = (gd_color_e)!color;
			}
			GD_DrawPixel(Gd,
					Gd->_intern.u32CurrX + j,
					Gd->_intern.u32CurrY + i,
					pxColor);
		}
	}

	// The current space is now taken
	Gd->_intern.u32CurrX += Font->char_width ? Font->char_width[ch - 32] : Font->width;

	// Return written OK
	return GD_OK;
}

/* Write full string to screenbuffer */
gd_error_e GD_WriteString(gd_t *Gd, const char* str, const gd_font_t *Font, gd_color_e color) {
	assert(Gd != NULL);
	assert(Font != NULL);
	assert(Gd->_intern.bInitialized == true);

	while (*str) {
		if (GD_WriteChar(Gd, *str, Font, color) != GD_OK) return GD_FAIL;
		str++;
	}

	// Everything ok
	return GD_OK;
}

/* Position the cursor */
gd_error_e GD_SetCursor(gd_t *Gd, uint32_t x, uint32_t y) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	if(x >= Gd->u32Width || y >= Gd->u32Height) {
		// Don't write outside the buffer
		return GD_FAIL;
	}
	_mtx_lock(Gd);
	Gd->_intern.u32CurrX = x;
	Gd->_intern.u32CurrY = y;
	_mtx_unlock(Gd);

	return GD_OK;
}

/* Draw line by Bresenhem's algorithm */
gd_error_e GD_Line(gd_t *Gd, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, gd_color_e color) {
	int32_t deltaX = (int32_t)abs((int)(x2 - x1));
	int32_t deltaY = (int32_t)abs((int)(y2 - y1));
	int32_t signX = ((x1 < x2) ? 1 : -1);
	int32_t signY = ((y1 < y2) ? 1 : -1);
	int32_t error = deltaX - deltaY;
	int32_t error2;

	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	GD_DrawPixel(Gd, x2, y2, color);
	while((x1 != x2) || (y1 != y2)) {
		GD_DrawPixel(Gd, x1, y1, color);
		error2 = error * 2;
		if(error2 > -deltaY) {
			error -= deltaY;
			x1 += signX;
		}

		if(error2 < deltaX) {
			error += deltaX;
			y1 += signY;
		}
	}

	return GD_OK;
}

/* Draw polyline */
gd_error_e GD_Polyline(gd_t*Gd, gd_vertex_t *par_vertex, uint16_t par_size, gd_color_e color) {
	uint16_t i;

	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);
	assert(par_vertex != NULL);

	for(i = 1; i < par_size; i++) {
		GD_Line(Gd, par_vertex[i - 1].x,
				par_vertex[i - 1].y,
				par_vertex[i].x,
				par_vertex[i].y,
				color);
	}

	return GD_OK;
}

/*
 * DrawArc. Draw angle is beginning from 4 quart of trigonometric circle (3pi/2)
 * start_angle in degree
 * sweep in degree
 */
/* Signed coordinates allow arcs to cross display edges without unsigned wrap. */
static void arc_line(gd_t *gd, int32_t x1, int32_t y1, int32_t x2, int32_t y2, gd_color_e color) {
    int32_t dx = abs(x2 - x1), dy = -abs(y2 - y1);
    int32_t sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x1 >= 0 && y1 >= 0 && (uint32_t)x1 < gd->u32Width && (uint32_t)y1 < gd->u32Height)
            GD_DrawPixel(gd, (uint32_t)x1, (uint32_t)y1, color);
        if (x1 == x2 && y1 == y2) break;
        int32_t twice = 2 * error;
        if (twice >= dy) { error += dy; x1 += sx; }
        if (twice <= dx) { error += dx; y1 += sy; }
    }
}

static gd_error_e draw_arc(gd_t *gd, uint32_t x, uint32_t y, uint32_t radius,
                          uint16_t start, uint16_t sweep, gd_color_e color, bool radii) {
    if (gd == NULL || !gd->_intern.bInitialized || x >= gd->u32Width || y >= gd->u32Height ||
        radius > INT16_MAX) return GD_FAIL;
    uint32_t angle = _NormalizeTo0_360(sweep);
    if (angle == 0) return GD_OK;
    uint32_t segments = (angle + 9U) / 10U;
    float first = _DegToRad(start % 360U);
    int32_t previous_x = (int32_t)x + (int32_t)(sinf(first) * radius);
    int32_t previous_y = (int32_t)y + (int32_t)(cosf(first) * radius);
    if (radii) arc_line(gd, x, y, previous_x, previous_y, color);
    for (uint32_t i = 1; i <= segments; ++i) {
        float rad = _DegToRad((float)(start % 360U) + (float)angle * i / segments);
        int32_t next_x = (int32_t)x + (int32_t)(sinf(rad) * radius);
        int32_t next_y = (int32_t)y + (int32_t)(cosf(rad) * radius);
        arc_line(gd, previous_x, previous_y, next_x, next_y, color);
        previous_x = next_x;
        previous_y = next_y;
    }
    if (radii) arc_line(gd, x, y, previous_x, previous_y, color);
    return GD_OK;
}

gd_error_e GD_DrawArc(gd_t *Gd, uint32_t x, uint32_t y, uint32_t radius, uint16_t start_angle, uint16_t sweep, gd_color_e color) {
    return draw_arc(Gd, x, y, radius, start_angle, sweep, color, false);
}

/*
 * Draw arc with radius line
 * Angle is beginning from 4 quart of trigonometric circle (3pi/2)
 * start_angle: start angle in degree
 * sweep: finish angle in degree
 */
gd_error_e GD_DrawArcWithRadiusLine(gd_t *Gd, uint32_t x, uint32_t y, uint32_t radius, uint16_t start_angle, uint16_t sweep, gd_color_e color) {
    return draw_arc(Gd, x, y, radius, start_angle, sweep, color, true);
}

/* Draw circle by Bresenhem's algorithm */
gd_error_e GD_DrawCircle(gd_t *Gd, uint32_t par_x, uint32_t par_y, uint32_t par_r, gd_color_e par_color) {
	int32_t x = -par_r;
	int32_t y = 0;
	int32_t err = 2 - 2 * par_r;
	int32_t e2;

	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	do {
		GD_DrawPixel(Gd, par_x - x, par_y + y, par_color);
		GD_DrawPixel(Gd, par_x + x, par_y + y, par_color);
		GD_DrawPixel(Gd, par_x + x, par_y - y, par_color);
		GD_DrawPixel(Gd, par_x - x, par_y - y, par_color);
		e2 = err;

		if (e2 <= y) {
			y++;
			err = err + (y * 2 + 1);
			if(-x == y && e2 <= x) {
				e2 = 0;
			}
		}

		if (e2 > x) {
			x++;
			err = err + (x * 2 + 1);
		}
	} while (x <= 0);

	return GD_OK;
}

/* Draw filled circle. Pixel positions calculated using Bresenham's algorithm */
gd_error_e GD_FillCircle(gd_t *Gd, uint32_t par_x, uint32_t par_y, uint32_t par_r, gd_color_e par_color) {
	int32_t x = -par_r;
	int32_t y = 0;
	int32_t err = 2 - 2 * par_r;
	int32_t e2;

	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	do {
		for (int64_t _y = (int64_t)par_y + y; _y >= (int64_t)par_y - y; _y--) {
			for (int64_t _x = (int64_t)par_x - x; _x >= (int64_t)par_x + x; _x--) {
				if (_x >= 0 && _y >= 0 && _x < Gd->u32Width && _y < Gd->u32Height)
                    GD_DrawPixel(Gd, (uint32_t)_x, (uint32_t)_y, par_color);
			}
		}

		e2 = err;
		if (e2 <= y) {
			y++;
			err = err + (y * 2 + 1);
			if (-x == y && e2 <= x) {
				e2 = 0;
			}
		}

		if (e2 > x) {
			x++;
			err = err + (x * 2 + 1);
		}
	} while (x <= 0);

	return GD_OK;
}

/* Draw a rectangle */
gd_error_e GD_DrawRectangle(gd_t *Gd, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, gd_color_e color) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	GD_Line(Gd, x1, y1, x2, y1, color);
	GD_Line(Gd, x2, y1, x2, y2, color);
	GD_Line(Gd, x2, y2, x1, y2, color);
	GD_Line(Gd, x1, y2, x1, y1, color);

	return GD_OK;
}

/* Draw a filled rectangle */
gd_error_e GD_FillRectangle(gd_t *Gd, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, gd_color_e color) {
	uint32_t x_start = ((x1<=x2) ? x1 : x2);
	uint32_t x_end   = ((x1<=x2) ? x2 : x1);
	uint32_t y_start = ((y1<=y2) ? y1 : y2);
	uint32_t y_end   = ((y1<=y2) ? y2 : y1);

	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	for (uint32_t y = y_start; (y <= y_end)&&(y < Gd->u32Height); y++) {
		for (uint32_t x = x_start; (x <= x_end)&&(x < Gd->u32Width); x++) {
			GD_DrawPixel(Gd, x, y, color);
		}
	}

	return GD_OK;
}

/* Draw a bitmap */
/* You can use the https://github.com/faytor/open_lcd_assistant assistant to create you bitmaps */
gd_error_e GD_DrawBitmap(gd_t *Gd, uint32_t x, uint32_t y, const unsigned char* bitmap, uint32_t w, uint32_t h, gd_color_e color) {
	int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
	uint32_t byte = 0;

	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);
	assert(bitmap != NULL);

	if ((x >= Gd->u32Width) || (y >= Gd->u32Height)) {
		return GD_FAIL;
	}

	for (uint32_t j = 0; j < h; j++, y++) {
		for (uint32_t i = 0; i < w; i++) {
			if (i & 7) {
				byte >>= 1;
			} else {
				byte = (*(const unsigned char *)(&bitmap[j * byteWidth + i / 8]));
			}

			if (byte & 0x1) {
				GD_DrawPixel(Gd, x + i, y, color);
			}
			else{
				GD_DrawPixel(Gd, x + i, y, !color);
			}
		}
	}

	return GD_OK;
}

gd_error_e GD_SetContrast(gd_t *Gd, uint8_t value) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	return _drv_set_constrat(Gd, value);
}

gd_error_e GD_SetDisplayOn(gd_t *Gd, bool on) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	gd_error_e result = _drv_set_on(Gd, on);
    if (result == GD_OK) Gd->_intern.bDisplayOn = on;
    return result;
}

bool GD_GetDisplayOn(gd_t *Gd) {
	assert(Gd != NULL);
	assert(Gd->_intern.bInitialized == true);

	return Gd->_intern.bDisplayOn;
}
