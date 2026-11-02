#include "ssd1306.h"
#include "stm32f4xx_ll_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>  // For memcpy

#if defined(SSD1306_USE_I2C)

void ssd1306_Reset(void) {
    /* for I2C - do nothing */
}

// Send a byte to the command register
Error_I2C ssd1306_WriteCommand(uint8_t byte) {

  return  I2C_LL_Master_Transmit_SSD1306( SSD1306_I2C_PORT, SSD1306_I2C_ADDR,  0x00,  &byte,  1);
    //  HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, SSD1306_I2C_ADDR, 0x00, 1, &byte, 1, HAL_MAX_DELAY);
}

// Send data
Error_I2C ssd1306_WriteData(uint8_t* buffer, size_t buff_size) {

  return  I2C_LL_Master_Transmit_SSD1306( SSD1306_I2C_PORT, SSD1306_I2C_ADDR,  0x40,  buffer,  buff_size);
    // HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, SSD1306_I2C_ADDR, 0x40, 1, buffer, buff_size, HAL_MAX_DELAY);
}

#elif defined(SSD1306_USE_SPI)

void ssd1306_Reset(void) {
    // CS = High (not selected)
    HAL_GPIO_WritePin(SSD1306_CS_Port, SSD1306_CS_Pin, GPIO_PIN_SET);

    // Reset the OLED
    HAL_GPIO_WritePin(SSD1306_Reset_Port, SSD1306_Reset_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(SSD1306_Reset_Port, SSD1306_Reset_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

// Send a byte to the command register
void ssd1306_WriteCommand(uint8_t byte) {
    HAL_GPIO_WritePin(SSD1306_CS_Port, SSD1306_CS_Pin, GPIO_PIN_RESET); // select OLED
    HAL_GPIO_WritePin(SSD1306_DC_Port, SSD1306_DC_Pin, GPIO_PIN_RESET); // command
    HAL_SPI_Transmit(&SSD1306_SPI_PORT, (uint8_t *) &byte, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SSD1306_CS_Port, SSD1306_CS_Pin, GPIO_PIN_SET); // un-select OLED
}

// Send data
void ssd1306_WriteData(uint8_t* buffer, size_t buff_size) {
    HAL_GPIO_WritePin(SSD1306_CS_Port, SSD1306_CS_Pin, GPIO_PIN_RESET); // select OLED
    HAL_GPIO_WritePin(SSD1306_DC_Port, SSD1306_DC_Pin, GPIO_PIN_SET); // data
    HAL_SPI_Transmit(&SSD1306_SPI_PORT, buffer, buff_size, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SSD1306_CS_Port, SSD1306_CS_Pin, GPIO_PIN_SET); // un-select OLED
}

#else
#error "You should define SSD1306_USE_SPI or SSD1306_USE_I2C macro"
#endif


// Screenbuffer
static uint8_t SSD1306_Buffer[SSD1306_BUFFER_SIZE];

// Screen object
static SSD1306_t SSD1306;

/* Fills the Screenbuffer with values from a given buffer of a fixed length */
SSD1306_Error_t ssd1306_FillBuffer(uint8_t* buf, uint32_t len) {
    SSD1306_Error_t ret = SSD1306_ERR;
    if (len <= SSD1306_BUFFER_SIZE) {
        memcpy(SSD1306_Buffer,buf,len);
        ret = SSD1306_OK;
    }
    return ret;
}

/* Initialize the oled screen */
Error_I2C ssd1306_Init(void) {
    Error_I2C status = SUCCESS_I2C;

    // 1. Reset OLED (GPIO операция, считаем что она всегда успешна)
    ssd1306_Reset();

    // Wait for the screen to boot
//LL_mDelay(100);
    DelayMs(100);
    // 2. Init OLED sequence
    // Используем такой паттерн: если функция вернула ошибку, сразу выходим.
    
    if ((status = ssd1306_SetDisplayOn(0)) != SUCCESS_I2C) return status;

    if ((status = ssd1306_WriteCommand(0x20)) != SUCCESS_I2C) return status; // Set Memory Addressing Mode
    if ((status = ssd1306_WriteCommand(0x00)) != SUCCESS_I2C) return status; // 00b,Horizontal Addressing Mode

    if ((status = ssd1306_WriteCommand(0xB0)) != SUCCESS_I2C) return status; // Set Page Start Address

#ifdef SSD1306_MIRROR_VERT
    if ((status = ssd1306_WriteCommand(0xC0)) != SUCCESS_I2C) return status; // Mirror vertically
#else
    if ((status = ssd1306_WriteCommand(0xC8)) != SUCCESS_I2C) return status; // Set COM Output Scan Direction
#endif

    if ((status = ssd1306_WriteCommand(0x00)) != SUCCESS_I2C) return status; // set low column address
    if ((status = ssd1306_WriteCommand(0x10)) != SUCCESS_I2C) return status; // set high column address

    if ((status = ssd1306_WriteCommand(0x40)) != SUCCESS_I2C) return status; // set start line address

    if ((status = ssd1306_SetContrast(0xFF)) != SUCCESS_I2C) return status;

#ifdef SSD1306_MIRROR_HORIZ
    if ((status = ssd1306_WriteCommand(0xA0)) != SUCCESS_I2C) return status; // Mirror horizontally
#else
    if ((status = ssd1306_WriteCommand(0xA1)) != SUCCESS_I2C) return status; // set segment re-map
#endif

#ifdef SSD1306_INVERSE_COLOR
    if ((status = ssd1306_WriteCommand(0xA7)) != SUCCESS_I2C) return status; // inverse color
#else
    if ((status = ssd1306_WriteCommand(0xA6)) != SUCCESS_I2C) return status; // normal color
#endif

// Set multiplex ratio
#if (SSD1306_HEIGHT == 128)
    if ((status = ssd1306_WriteCommand(0xFF)) != SUCCESS_I2C) return status;
#else
    if ((status = ssd1306_WriteCommand(0xA8)) != SUCCESS_I2C) return status; // 1 to 64
#endif

#if (SSD1306_HEIGHT == 32)
    if ((status = ssd1306_WriteCommand(0x1F)) != SUCCESS_I2C) return status;
#elif (SSD1306_HEIGHT == 64)
    if ((status = ssd1306_WriteCommand(0x3F)) != SUCCESS_I2C) return status;
#elif (SSD1306_HEIGHT == 128)
    if ((status = ssd1306_WriteCommand(0x3F)) != SUCCESS_I2C) return status;
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif

    if ((status = ssd1306_WriteCommand(0xA4)) != SUCCESS_I2C) return status; // Output follows RAM content

    if ((status = ssd1306_WriteCommand(0xD3)) != SUCCESS_I2C) return status; // set display offset
    if ((status = ssd1306_WriteCommand(0x00)) != SUCCESS_I2C) return status; // not offset

    if ((status = ssd1306_WriteCommand(0xD5)) != SUCCESS_I2C) return status; // set display clock divide ratio
    if ((status = ssd1306_WriteCommand(0xF0)) != SUCCESS_I2C) return status; // set divide ratio

    if ((status = ssd1306_WriteCommand(0xD9)) != SUCCESS_I2C) return status; // set pre-charge period
    if ((status = ssd1306_WriteCommand(0x22)) != SUCCESS_I2C) return status;

    if ((status = ssd1306_WriteCommand(0xDA)) != SUCCESS_I2C) return status; // set com pins hardware config
#if (SSD1306_HEIGHT == 32)
    if ((status = ssd1306_WriteCommand(0x02)) != SUCCESS_I2C) return status;
#elif (SSD1306_HEIGHT == 64)
    if ((status = ssd1306_WriteCommand(0x12)) != SUCCESS_I2C) return status;
#elif (SSD1306_HEIGHT == 128)
    if ((status = ssd1306_WriteCommand(0x12)) != SUCCESS_I2C) return status;
#else
#error "Only 32, 64, or 128 lines of height are supported!"
#endif

    if ((status = ssd1306_WriteCommand(0xDB)) != SUCCESS_I2C) return status; // set vcomh
    if ((status = ssd1306_WriteCommand(0x20)) != SUCCESS_I2C) return status; // 0.77xVcc

    if ((status = ssd1306_WriteCommand(0x8D)) != SUCCESS_I2C) return status; // set DC-DC enable
    if ((status = ssd1306_WriteCommand(0x14)) != SUCCESS_I2C) return status;
    
    // Включаем панель
    if ((status = ssd1306_SetDisplayOn(1)) != SUCCESS_I2C) return status;

    // 3. Очистка и обновление
    // Заполняем буфер (это безопасно, просто память)
    ssd1306_Fill(Black);
    
    // Отправляем буфер на экран (вот тут может быть ошибка)
    if ((status = ssd1306_UpdateScreen()) != SUCCESS_I2C) return status;
    
    // 4. Если мы дошли сюда - значит экран жив и ответил на все команды
    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;
    SSD1306.Initialized = 1;
    
    return SUCCESS_I2C;
}

/* Fill the whole screen with the given color */
void ssd1306_Fill(SSD1306_COLOR color) {
    memset(SSD1306_Buffer, (color == Black) ? 0x00 : 0xFF, sizeof(SSD1306_Buffer));
}

/* Write the screenbuffer with changed to the screen */
Error_I2C ssd1306_UpdateScreen(void) {
    // Write data to each page of RAM. Number of pages
    // depends on the screen height:
    //
    //  * 32px   ==  4 pages
    //  * 64px   ==  8 pages
    //  * 128px  ==  16 pages
    Error_I2C status;

    for(uint8_t i = 0; i < SSD1306_HEIGHT/8; i++) {
        
        // 1. Попытка отправить команду. Если ошибка - сразу выходим.
        status = ssd1306_WriteCommand(0xB0 + i); 
        if (status != SUCCESS_I2C) return status;

        status = ssd1306_WriteCommand(0x00 + SSD1306_X_OFFSET_LOWER);
        if (status != SUCCESS_I2C) return status;

        status = ssd1306_WriteCommand(0x10 + SSD1306_X_OFFSET_UPPER);
        if (status != SUCCESS_I2C) return status;

        // 2. Попытка отправить данные.
        status = ssd1306_WriteData(&SSD1306_Buffer[SSD1306_WIDTH*i], SSD1306_WIDTH);
        if (status != SUCCESS_I2C) return status;
    }

    // Если мы дошли сюда, значит весь цикл прошел без ошибок
    return SUCCESS_I2C;
}

/*
 * Draw one pixel in the screenbuffer
 * X => X Coordinate
 * Y => Y Coordinate
 * color => Pixel color
 */
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color) {
    if(x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        // Don't write outside the buffer
        return;
    }
   
    // Draw in the right color
    if(color == White) {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= 1 << (y % 8);
    } else { 
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

/*
 * Draw 1 char to the screen buffer
 * ch       => char om weg te schrijven
 * Font     => Font waarmee we gaan schrijven
 * color    => Black or White
 */
char ssd1306_WriteChar(char ch, SSD1306_Font_t Font, SSD1306_COLOR color) {
    uint32_t i, b, j;
    
    // Check if character is valid
    if (ch < 32 || ch > 126)
        return 0;
    
    // Char width is not equal to font width for proportional font
    const uint8_t char_width = Font.char_width ? Font.char_width[ch-32] : Font.width;
    // Check remaining space on current line
    if (SSD1306_WIDTH < (SSD1306.CurrentX + char_width) ||
        SSD1306_HEIGHT < (SSD1306.CurrentY + Font.height))
    {
        // Not enough space on current line
        return 0;
    }
    
    // Use the font to write
    for(i = 0; i < Font.height; i++) {
        b = Font.data[(ch - 32) * Font.height + i];
        for(j = 0; j < char_width; j++) {
            if((b << j) & 0x8000)  {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR) color);
            } else {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR)!color);
            }
        }
    }
    
    // The current space is now taken
    SSD1306.CurrentX += char_width;
    
    // Return written char for validation
    return ch;
}

/* Write full string to screenbuffer */
char ssd1306_WriteString(char* str, SSD1306_Font_t Font, SSD1306_COLOR color) {
    while (*str) {
        if (ssd1306_WriteChar(*str, Font, color) != *str) {
            // Char could not be written
            return *str;
        }
        str++;
    }
    
    // Everything ok
    return *str;
}

/* Position the cursor */
void ssd1306_SetCursor(uint8_t x, uint8_t y) {
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

/* Draw line by Bresenhem's algorithm */
void ssd1306_Line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color) {
    int32_t deltaX = abs(x2 - x1);
    int32_t deltaY = abs(y2 - y1);
    int32_t signX = ((x1 < x2) ? 1 : -1);
    int32_t signY = ((y1 < y2) ? 1 : -1);
    int32_t error = deltaX - deltaY;
    int32_t error2;
    
    ssd1306_DrawPixel(x2, y2, color);

    while((x1 != x2) || (y1 != y2)) {
        ssd1306_DrawPixel(x1, y1, color);
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
    return;
}

/* Draw polyline */
void ssd1306_Polyline(const SSD1306_VERTEX *par_vertex, uint16_t par_size, SSD1306_COLOR color) {
    uint16_t i;
    if(par_vertex == NULL) {
        return;
    }

    for(i = 1; i < par_size; i++) {
        ssd1306_Line(par_vertex[i - 1].x, par_vertex[i - 1].y, par_vertex[i].x, par_vertex[i].y, color);
    }

    return;
}

/* Convert Degrees to Radians */
static float ssd1306_DegToRad(float par_deg) {
    return par_deg * (3.14f / 180.0f);
}

/* Normalize degree to [0;360] */
static uint16_t ssd1306_NormalizeTo0_360(uint16_t par_deg) {
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
 * DrawArc. Draw angle is beginning from 4 quart of trigonometric circle (3pi/2)
 * start_angle in degree
 * sweep in degree
 */
void ssd1306_DrawArc(uint8_t x, uint8_t y, uint8_t radius, uint16_t start_angle, uint16_t sweep, SSD1306_COLOR color) {
    static const uint8_t CIRCLE_APPROXIMATION_SEGMENTS = 36;
    float approx_degree;
    uint32_t approx_segments;
    uint8_t xp1,xp2;
    uint8_t yp1,yp2;
    uint32_t count;
    uint32_t loc_sweep;
    float rad;
    
    loc_sweep = ssd1306_NormalizeTo0_360(sweep);
    
    count = (ssd1306_NormalizeTo0_360(start_angle) * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_segments = (loc_sweep * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_degree = loc_sweep / (float)approx_segments;
    while(count < approx_segments)
    {
        rad = ssd1306_DegToRad(count*approx_degree);
        xp1 = x + (int8_t)(sinf(rad)*radius);
        yp1 = y + (int8_t)(cosf(rad)*radius);    
        count++;
        if(count != approx_segments) {
            rad = ssd1306_DegToRad(count*approx_degree);
        } else {
            rad = ssd1306_DegToRad(loc_sweep);
        }
        xp2 = x + (int8_t)(sinf(rad)*radius);
        yp2 = y + (int8_t)(cosf(rad)*radius);    
        ssd1306_Line(xp1,yp1,xp2,yp2,color);
    }
    
    return;
}

/*
 * Draw arc with radius line
 * Angle is beginning from 4 quart of trigonometric circle (3pi/2)
 * start_angle: start angle in degree
 * sweep: finish angle in degree
 */
void ssd1306_DrawArcWithRadiusLine(uint8_t x, uint8_t y, uint8_t radius, uint16_t start_angle, uint16_t sweep, SSD1306_COLOR color) {
    const uint32_t CIRCLE_APPROXIMATION_SEGMENTS = 36;
    float approx_degree;
    uint32_t approx_segments;
    uint8_t xp1;
    uint8_t xp2 = 0;
    uint8_t yp1;
    uint8_t yp2 = 0;
    uint32_t count;
    uint32_t loc_sweep;
    float rad;
    
    loc_sweep = ssd1306_NormalizeTo0_360(sweep);
    
    count = (ssd1306_NormalizeTo0_360(start_angle) * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_segments = (loc_sweep * CIRCLE_APPROXIMATION_SEGMENTS) / 360;
    approx_degree = loc_sweep / (float)approx_segments;

    rad = ssd1306_DegToRad(count*approx_degree);
    uint8_t first_point_x = x + (int8_t)(sinf(rad)*radius);
    uint8_t first_point_y = y + (int8_t)(cosf(rad)*radius);   
    while (count < approx_segments) {
        rad = ssd1306_DegToRad(count*approx_degree);
        xp1 = x + (int8_t)(sinf(rad)*radius);
        yp1 = y + (int8_t)(cosf(rad)*radius);    
        count++;
        if (count != approx_segments) {
            rad = ssd1306_DegToRad(count*approx_degree);
        } else {
            rad = ssd1306_DegToRad(loc_sweep);
        }
        xp2 = x + (int8_t)(sinf(rad)*radius);
        yp2 = y + (int8_t)(cosf(rad)*radius);    
        ssd1306_Line(xp1,yp1,xp2,yp2,color);
    }
    
    // Radius line
    ssd1306_Line(x,y,first_point_x,first_point_y,color);
    ssd1306_Line(x,y,xp2,yp2,color);
    return;
}

/* Draw circle by Bresenhem's algorithm */
void ssd1306_DrawCircle(uint8_t par_x,uint8_t par_y,uint8_t par_r,SSD1306_COLOR par_color) {
    int32_t x = -par_r;
    int32_t y = 0;
    int32_t err = 2 - 2 * par_r;
    int32_t e2;

    if (par_x >= SSD1306_WIDTH || par_y >= SSD1306_HEIGHT) {
        return;
    }

    do {
        ssd1306_DrawPixel(par_x - x, par_y + y, par_color);
        ssd1306_DrawPixel(par_x + x, par_y + y, par_color);
        ssd1306_DrawPixel(par_x + x, par_y - y, par_color);
        ssd1306_DrawPixel(par_x - x, par_y - y, par_color);
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

    return;
}

/* Draw filled circle. Pixel positions calculated using Bresenham's algorithm */
void ssd1306_FillCircle(uint8_t par_x,uint8_t par_y,uint8_t par_r,SSD1306_COLOR par_color) {
    int32_t x = -par_r;
    int32_t y = 0;
    int32_t err = 2 - 2 * par_r;
    int32_t e2;

    if (par_x >= SSD1306_WIDTH || par_y >= SSD1306_HEIGHT) {
        return;
    }

    do {
        for (uint8_t _y = (par_y + y); _y >= (par_y - y); _y--) {
            for (uint8_t _x = (par_x - x); _x >= (par_x + x); _x--) {
                ssd1306_DrawPixel(_x, _y, par_color);
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

    return;
}

/* Draw a rectangle */
void ssd1306_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color) {
    ssd1306_Line(x1,y1,x2,y1,color);
    ssd1306_Line(x2,y1,x2,y2,color);
    ssd1306_Line(x2,y2,x1,y2,color);
    ssd1306_Line(x1,y2,x1,y1,color);

    return;
}

/* Draw a filled rectangle */
void ssd1306_FillRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, SSD1306_COLOR color) {
    uint8_t x_start = ((x1<=x2) ? x1 : x2);
    uint8_t x_end   = ((x1<=x2) ? x2 : x1);
    uint8_t y_start = ((y1<=y2) ? y1 : y2);
    uint8_t y_end   = ((y1<=y2) ? y2 : y1);

    for (uint8_t y= y_start; (y<= y_end)&&(y<SSD1306_HEIGHT); y++) {
        for (uint8_t x= x_start; (x<= x_end)&&(x<SSD1306_WIDTH); x++) {
            ssd1306_DrawPixel(x, y, color);
        }
    }
    return;
}

SSD1306_Error_t ssd1306_InvertRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
  if ((x2 >= SSD1306_WIDTH) || (y2 >= SSD1306_HEIGHT)) {
    return SSD1306_ERR;
  }
  if ((x1 > x2) || (y1 > y2)) {
    return SSD1306_ERR;
  }
  uint32_t i;
  if ((y1 / 8) != (y2 / 8)) {
    /* if rectangle doesn't lie on one 8px row */
    for (uint32_t x = x1; x <= x2; x++) {
      i = x + (y1 / 8) * SSD1306_WIDTH;
      SSD1306_Buffer[i] ^= 0xFF << (y1 % 8);
      i += SSD1306_WIDTH;
      for (; i < x + (y2 / 8) * SSD1306_WIDTH; i += SSD1306_WIDTH) {
        SSD1306_Buffer[i] ^= 0xFF;
      }
      SSD1306_Buffer[i] ^= 0xFF >> (7 - (y2 % 8));
    }
  } else {
    /* if rectangle lies on one 8px row */
    const uint8_t mask = (0xFF << (y1 % 8)) & (0xFF >> (7 - (y2 % 8)));
    for (i = x1 + (y1 / 8) * SSD1306_WIDTH;
         i <= (uint32_t)x2 + (y2 / 8) * SSD1306_WIDTH; i++) {
      SSD1306_Buffer[i] ^= mask;
    }
  }
  return SSD1306_OK;
}

/* Draw a bitmap */
void ssd1306_DrawBitmap(uint8_t x, uint8_t y, const unsigned char* bitmap, uint8_t w, uint8_t h, SSD1306_COLOR color) {
    int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
    uint8_t byte = 0;

    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }

    for (uint8_t j = 0; j < h; j++, y++) {
        for (uint8_t i = 0; i < w; i++) {
            if (i & 7) {
                byte <<= 1;
            } else {
                byte = (*(const unsigned char *)(&bitmap[j * byteWidth + i / 8]));
            }

            if (byte & 0x80) {
                ssd1306_DrawPixel(x + i, y, color);
            }
        }
    }
    return;
}

Error_I2C ssd1306_SetContrast(const uint8_t value) {
    Error_I2C status;
    
    status = ssd1306_WriteCommand(0x81); // Команда установки контраста
    if (status != SUCCESS_I2C) return status;

    return ssd1306_WriteCommand(value);  // Значение контраста
}
Error_I2C ssd1306_SetDisplayOn(const uint8_t on) {
    uint8_t value;
    if (on) {
        value = 0xAF; // Display on
        SSD1306.DisplayOn = 1;
    } else {
        value = 0xAE; // Display off
        SSD1306.DisplayOn = 0;
    }
    return ssd1306_WriteCommand(value);
}

uint8_t ssd1306_GetDisplayOn() {
    return SSD1306.DisplayOn;
}

// x, y: Координаты НИЖНЕГО ЛЕВОГО угла столбика (откуда он растет)
// width: Ширина столбика в пикселях
// max_height: Максимальная высота столбика (когда value = 1000)
// value: Текущее значение (0 - 1000)
void DrawVerticalBar(uint8_t x, uint8_t y, uint8_t width, uint8_t max_height, int16_t value) {
    // 1. Ограничиваем значение (чтобы не вылезти за пределы)
    if (value < 0) value = 0;
    if (value > 1000) value = 1000;

    // 2. Считаем высоту заполненной части (пропорция)
    // Формула: (текущее * макс_высота) / макс_значение
    uint8_t bar_height = (value * max_height) / 1000;

    // 3. Рисуем рамку (контур) для всего столбика (от 0 до 100%)
    // Верхняя точка рамки = y - max_height
    ssd1306_DrawRectangle(x, y - max_height, x + width, y, White);

    // 4. Рисуем заполненную часть (сам столбик)
    if (bar_height > 0) {
        // Координата верха заполненной части:
        // (y - 1) - это низ (внутри рамки)
        // отнимаем высоту, чтобы подняться вверх
        uint8_t filled_top_y = y - bar_height; 
        
        // Используем x+1 и width-1, чтобы не стереть рамку
        ssd1306_FillRectangle(x + 1, filled_top_y, x + width - 1, y - 1, White);
    }
}