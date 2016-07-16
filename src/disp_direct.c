

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "font_direct.h"
#include "disp_direct.h"
#include "compiler.h"
#include "consts.h"
#include "asm.h"

#define MEM(x) (*(volatile uint32_t *)(x))
#define UYVY_PACK(u,y1,v,y2) ((u) & 0xFF) | (((y1) & 0xFF) << 8) | (((v) & 0xFF) << 16) | (((y2) & 0xFF) << 24);
 
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))

#define ABS(a) ({ __typeof__ (a) _a = (a); _a > 0 ? _a : -_a; })

static uint8_t *disp_framebuf = (uint8_t *)0x44000000;
static uint8_t *disp_yuvbuf = (uint8_t *)0x44800000;

static int disp_yres = 480;
static int disp_xres = 720;
static int disp_bpp  = 4;

/* most cameras use YUV422, but some old models (e.g. 5D2) use YUV411 */
static enum { YUV422, YUV411 } yuv_mode;

static void disp_set_palette()
{
    // transparent
    // 1 - red
    // 2 - green
    // 3 - blue
    // 4 - cyan
    // 5 - magenta
    // 6 - yellow
    // 7 - orange
    // 8 - transparent black
    // 9 - black
    // A - gray 1
    // B - gray 2
    // C - gray 3
    // D - gray 4
    // E - gray 5
    // F - white

    uint32_t palette_pb[16] = {0x00fc0000, 0x0346de7f, 0x036dcba1, 0x031a66ea, 0x03a42280, 0x03604377, 0x03cf9a16, 0x0393b94b, 0x00000000, 0x03000000, 0x03190000, 0x033a0000, 0x03750000, 0x039c0000, 0x03c30000, 0x03eb0000};

    if (disp_bpp == 4)
    {
        for(uint32_t i = 0; i < 16; i++)
        {
            MEM(0xC0F14080 + i*4) = palette_pb[i];
        }
        MEM(0xC0F14078) = 1;
    }
    else if (disp_bpp == 8)
    {
        /* DIGIC 6 */
        static uint32_t __attribute__((aligned(16))) palette[16];
        
        for(uint32_t i = 0; i < 16; i++)
        {
            palette[i] = (palette_pb[i] << 8) | 0xFF;
            uint8_t* ovuy = (uint8_t*) &palette[i];
            ovuy[1] += 128; ovuy[2] += 128;
        }
        
        sync_caches();
        
        MEM(0xD20139A8) = (uint32_t) palette >> 4;
        MEM(0xD20139A0) = 1;
    }
}

static uint32_t rgb2yuv422(int R, int G, int B)
{
    int Y = COERCE(((217) * R + (732) * G + (73) * B) / 1024, 0, 255);
    int U = COERCE(((-117) * R + (-394) * G + (512) * B) / 1024, -128, 127);
    int V = COERCE(((512) * R + (-465) * G + (-46) * B) / 1024, -128, 127);
    return UYVY_PACK(U,Y,V,Y);
}

/* low resolution, only good for smooth gradients */
static uint32_t rgb2yuv411(int R, int G, int B, uint32_t addr)
{
    int Y = COERCE(((217) * R + (732) * G + (73) * B) / 1024, 0, 255);
    int U = COERCE(((-117) * R + (-394) * G + (512) * B) / 1024, -128, 127);
    int V = COERCE(((512) * R + (-465) * G + (-46) * B) / 1024, -128, 127);

    // 4 6  8 A  0 2 
    // uYvY yYuY vYyY
    addr = addr & ~3; // multiple of 4
        
    // multiples of 12, offset 0: vYyY u
    // multiples of 12, offset 4: uYvY
    // multiples of 12, offset 8: yYuY v

    switch ((addr/4) % 3)
    {
        case 0:
            return UYVY_PACK(V,Y,Y,Y);
        case 1:
            return UYVY_PACK(U,Y,V,Y);
        case 2:
            return UYVY_PACK(Y,Y,U,Y);
    }
    
    /* unreachable */
    return 0;
}

void disp_set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint32_t pixnum = ((y * disp_xres) + x);
    
    switch (disp_bpp)
    {
        case 8:
            disp_framebuf[pixnum] = color;
            break;

        case 4:
            disp_framebuf[pixnum/2] = (x & 1)
                ? (disp_framebuf[pixnum/2] & 0x0F) | ((color & 0x0F)<<4)
                : (disp_framebuf[pixnum/2] & 0xF0) |  (color & 0x0F);
            break;
    }
}

void disp_set_rgb_pixel(uint32_t x, uint32_t y, uint32_t R, uint32_t G, uint32_t B)
{
    /* get linear pixel number */
    uint32_t pixnum = ((y * disp_xres) + x);
    
    /* will update 32 bytes at a time */
    /* not full resolution, but simpler, and enough for smooth gradients */

    if (yuv_mode == YUV411)
    {
        /* 12 bytes per 8 pixels */
        uint32_t *ptr = (uint32_t *)&disp_yuvbuf[pixnum * 12 / 8];  
        *ptr = rgb2yuv411(R, G, B, (uint32_t)ptr);
    }
    else
    {
        /* two bytes per pixel */
        uint32_t *ptr = (uint32_t *)&disp_yuvbuf[pixnum * 2];
        *ptr = rgb2yuv422(R, G, B);
    }
}

void disp_fill(uint32_t color)
{
    /* build a 32 bit word */
    uint32_t val = color;
    if (disp_bpp == 4)
    {
        val |= val << 4;
    }
    val |= val << 8;
    val |= val << 16;
    
    for(int ypos = 0; ypos < disp_yres; ypos++)
    {
        /* we are writing 4 or 8 pixels at once with a 32 bit word */
        for(int xpos = 0; xpos < disp_xres; xpos += 32 / disp_bpp)
        {
            /* get linear pixel number */
            uint32_t pixnum = ((ypos * disp_xres) + xpos);
            /* two pixels per byte */
            uint32_t *ptr = (uint32_t *)&disp_framebuf[pixnum * disp_bpp / 8];
            
            *ptr = val;
        }
    }
}

void disp_fill_yuv_gradient()
{
    /* use signed int here, because uint32_t doesn't like "-" operator */
    for(int ypos = 0; ypos < disp_yres; ypos++)
    {
        /* we are writing 2 pixels at once with a 32 bit word */
        for(int xpos = 0; xpos < disp_xres; xpos += 2)
        {
            /* ok that is making things slow.... */
            /* but we love funny patterns :) */
            disp_set_rgb_pixel(xpos, ypos, xpos/3, ypos/3, ABS(xpos-ypos)/3);
        }
    }
}

void disp_progress(uint32_t progress)
{
    uint32_t width = 480;
    uint32_t height = 20;
    uint32_t paint = progress * width / 255;
    
    for(uint32_t ypos = (disp_yres - height) / 2; ypos < (disp_yres + height) / 2; ypos++)
    {
        for(uint32_t xofs = 0; xofs < width; xofs++)
        {
            uint32_t xpos = (disp_xres - width) / 2 + xofs;
            
            if(xofs >= paint)
            {
                disp_set_pixel(xpos, ypos, COLOR_BLACK);
            }
            else
            {
                disp_set_pixel(xpos, ypos, COLOR_RED);
            }
        }
    }
    
    /* print progress in percent */
    char text[32];
    
    snprintf(text, 32, "%d%%", progress * 100 / 255);
    uint32_t x = disp_xres / 2 - 28;
    uint32_t y = (disp_yres - height) / 2 + 2;
    font_draw(&x, &y, COLOR_WHITE, 2, text);
}

void disp_init_dummy (uint32_t buffer)
{
}

void* disp_init_autodetect()
{
    /* Called right before printing the following strings:
     * "Other models\n"                     (5D2, 5D3, 60D, 500D, 70D, 7D)
     * "File(*.fir) not found\n"            (5D2, 5D3, 60D, 500D, 70D)
     * "sum check error or code modify\n"   (5D2, 60D, 500D, 7D)
     * "sum check error\n"                  (5D3, 70D)
     * "CF Read error\n"                    (5D2, 60D, 500D, 7D)
     * ...
     */
    
    uint32_t a = find_func_called_before_string_ref("Other models\n");
    uint32_t b = find_func_called_before_string_ref("File(*.fir) not found\n");
    uint32_t c = find_func_called_before_string_ref("sum check error or code modify\n");
    
    if (a == b)
    {
        /* I think this is what we are looking for :) */
        return (void*) a;
    }
    
    if (a == c)
    {
        /* I think this is what we are looking for :) */
        return (void*) a;
    }
    
    return &disp_init_dummy;
}

extern uint32_t get_model_id();
extern uint32_t is_digic6();

void disp_init()
{
    if (is_digic6())
    {
        disp_bpp = 8;
    }

    uint32_t id = get_model_id();
    if (id == 0x218 || id == 0x261 || id == 0x250)
    {
        /* 5D2, 50D, 7D */
        yuv_mode = YUV411;
    }

    /* is this address valid for all cameras? */
    disp_framebuf = (uint8_t *)0x44000000;

    /* this should cover most (if not all) ML-supported cameras */
    /* and maybe most unsupported cameras as well :) */
    void (*fromutil_disp_init)(uint32_t) = disp_init_autodetect();

    /* this one initializes everyhting that is needed for display usage. PWM, PWR, GPIO, SIO and DISPLAY */
    fromutil_disp_init(0);

    /* we want our own palette */
    disp_set_palette();
    
    /* BMP foreground is transparent */
    disp_fill(COLOR_TRANSPARENT_BLACK);
    
    /* make a funny pattern in the YUV buffer*/
    disp_fill_yuv_gradient();
    
    if (disp_bpp == 8)
    {
        MEM(0xD2030108) = (uint32_t)disp_framebuf >> 8;
    }
    else
    {
        /* set frame buffer memory areas */
        MEM(0xC0F140D0) = (uint32_t)disp_framebuf & ~0x40000000;
        MEM(0xC0F140E0) = (uint32_t)disp_yuvbuf & ~0x40000000;
        
        /* trigger a display update */
        MEM(0xC0F14000) = 1;
    }
    
    /* from now on, everything you write on the display buffers
     * will appear on the screen without doing anything special */
}

uint32_t disp_direct_scroll_up(uint32_t height)
{
    uint32_t start = (720 * height) * disp_bpp / 8;
    uint32_t size = (720 * (480 - height)) * disp_bpp / 8;
    uint32_t color = COLOR_TRANSPARENT_BLACK;
    if (disp_bpp == 4)
    {
        color |= color << 4;
    }
    
    memcpy(disp_framebuf, &disp_framebuf[start], size);
    memset(&disp_framebuf[size], color, start);
    
    return height;
}
