/** \file
 * Magic Lantern debugging and reverse engineering code
 */
#include "dryos.h"
#include "bmp.h"
#include "tasks.h"
#include "debug.h"
#include "menu.h"
#include "property.h"
#include "config.h"
#include "gui.h"
#include "lens.h"
#include "version.h"
#include "edmac.h"
#include "asm.h"
#include "beep.h"
#include "screenshot.h"
#include "console.h"
#include "zebra.h"
#include "shoot.h"
#include "cropmarks.h"
#include "fw-signature.h"
#include "lvinfo.h"
#include "timer.h"
#include "raw.h"

#ifdef CONFIG_DEBUG_INTERCEPT
#include "dm-spy.h"
#endif

#ifdef CONFIG_MODULES
#include "module.h"
#endif
//#include "lua.h"

#if defined(CONFIG_600D) && defined(CONFIG_AUDIO_600D_DEBUG)
void audio_reg_dump_once();
#endif

#if defined(CONFIG_EDMAC_MEMCPY)
#include "edmac-memcpy.h"
#endif

extern int config_autosave;
extern void config_autosave_toggle(void* unused, int delta);

static struct semaphore * beep_sem = 0;

static void debug_init_func()
{
    beep_sem = create_named_semaphore("beep_sem",1);
}
INIT_FUNC("debug", debug_init_func);

void NormalDisplay();
void MirrorDisplay();
static void HijackFormatDialogBox_main();
void debug_menu_init();
void display_on();
void display_off();


void fake_halfshutter_step();

#ifdef CONFIG_DEBUG_INTERCEPT
void j_debug_intercept() { msleep(1000); debug_intercept(); }
#endif

#if CONFIG_DEBUGMSG
static int draw_prop = 0;

static int dbg_propn = 0;
static void
draw_prop_reset( void * priv )
{
    dbg_propn = 0;
}
#endif

#if defined(CONFIG_7D) // pel: Checked. That's how it works in the 7D firmware
void _card_led_on()  //See sub_FF32B410 -> sub_FF0800A4
{
    *(volatile uint32_t*) (CARD_LED_ADDRESS) = 0x800c00;
    *(volatile uint32_t*) (CARD_LED_ADDRESS) = 0x138000;
}
void _card_led_off()  //See sub_FF32B424 -> sub_FF0800B8
{
    *(volatile uint32_t*) (CARD_LED_ADDRESS) = 0x800c00;
    *(volatile uint32_t*) (CARD_LED_ADDRESS) = 0x38400;
}
//TODO: Check if this is correct, because reboot.c said 0x838C00
#elif defined(CARD_LED_ADDRESS) && defined(LEDON) && defined(LEDOFF)
void _card_led_on()  { *(volatile uint32_t*) (CARD_LED_ADDRESS) = (LEDON); }
void _card_led_off() { *(volatile uint32_t*) (CARD_LED_ADDRESS) = (LEDOFF); }
#else
void _card_led_on()  { return; }
void _card_led_off() { return; }
#endif

void info_led_on()
{
    #ifdef CONFIG_VXWORKS
    LEDBLUE = LEDON;
    #elif defined(CONFIG_BLUE_LED)
    call("EdLedOn");
    #else
    _card_led_on();
    #endif
}
void info_led_off()
{
    #ifdef CONFIG_VXWORKS
    LEDBLUE = LEDOFF;
    #elif defined(CONFIG_BLUE_LED)
    call("EdLedOff");
    #else
    _card_led_off();
    #endif
}
void info_led_blink(int times, int delay_on, int delay_off)
{
    for (int i = 0; i < times; i++)
    {
        info_led_on();
        msleep(delay_on);
        info_led_off();
        msleep(delay_off);
    }
}

static void dump_rom_task(void* priv, int unused)
{
    msleep(200);
    FILE * f = NULL;

    f = FIO_CreateFile("ML/LOGS/ROM0.BIN");
    if (f)
    {
        bmp_printf(FONT_LARGE, 0, 60, "Writing ROM0");
        FIO_WriteFile(f, (void*) 0xF0000000, 0x01000000);
        FIO_CloseFile(f);
    }
    msleep(200);

    f = FIO_CreateFile("ML/LOGS/ROM1.BIN");
    if (f)
    {
        bmp_printf(FONT_LARGE, 0, 60, "Writing ROM1");
        FIO_WriteFile(f, (void*) 0xF8000000, 0x01000000);
        FIO_CloseFile(f);
    }
    msleep(200);

    dump_big_seg(4, "ML/LOGS/RAM4.BIN");
    dump_big_seg(5, "ML/LOGS/RAM5.BIN");

    /* you can't write the TCM directly, needs to be copied */
    void* buf = fio_malloc(0x1000);
    memcpy(buf, 0, 0x1000);
    dump_seg(buf, 0x1000, "ML/LOGS/TCM.BIN");
    free(buf);
}

static void dump_img_task(void* priv, int unused)
{
    for (int i = 5; i > 0; i--)
    {
        NotifyBox(1000, "Will dump VRAMs in %d s...", i);
        msleep(1000);
    }
    NotifyBox(5000, "Dumping VRAMs...");
    
    FILE * f = NULL;
    char pattern[0x80];
    char filename[0x80];
    
    char* video_mode = get_video_mode_name(0);
    char* display_device = get_display_device_name();
    

    int path_len = snprintf(pattern, sizeof(pattern), "%s/%s/%s/", CAMERA_MODEL, video_mode, display_device);
    
    /* make sure the VRAM parameters are updated */
    get_yuv422_vram();
    get_yuv422_hd_vram();

    snprintf(pattern + path_len, sizeof(pattern) - path_len, "LV-%%03d.422", 0);
    get_numbered_file_name(pattern, 999, filename, sizeof(filename));
    f = FIO_CreateFile(filename);
    if (f)
    {
        FIO_WriteFile(f, vram_lv.vram, vram_lv.height * vram_lv.pitch);
        FIO_CloseFile(f);
    }

    snprintf(pattern + path_len, sizeof(pattern) - path_len, "HD-%%03d.422", 0);
    get_numbered_file_name(pattern, 999, filename, sizeof(filename));
    f = FIO_CreateFile(filename);
    if (f)
    {
        FIO_WriteFile(f, vram_hd.vram, vram_hd.height * vram_hd.pitch);
        FIO_CloseFile(f);
    }

#ifdef CONFIG_RAW_LIVEVIEW
    snprintf(pattern + path_len, sizeof(pattern) - path_len, "RAW-%%03d.DNG", 0);
    get_numbered_file_name(pattern, 999, filename, sizeof(filename));
    
    if (lv) raw_lv_request();
    if (raw_update_params())
    {
        /* first frames right after enabling the raw buffer might be corrupted, figure out why */
        /* todo: fix it in the raw backend */
        wait_lv_frames(3);
        raw_set_dirty();
        raw_update_params();
        
        /* make a copy of the raw buffer, because it's being updated while we are saving it */
        void* buf = malloc(raw_info.frame_size);
        if (buf)
        {
            memcpy(buf, raw_info.buffer, raw_info.frame_size);
            struct raw_info local_raw_info = raw_info;
            local_raw_info.buffer = buf;
            save_dng(filename, &local_raw_info);
            free(buf);
        }
    }
    if (lv) raw_lv_release();
    
    if (!is_file(filename))
    {
        /* if we don't have any raw data, create an empty DNG just to keep file numbering consistent */
        f = FIO_CreateFile(filename);
        FIO_CloseFile(f);
    }
#endif

    /* create a log file with relevant settings */
    snprintf(pattern + path_len, sizeof(pattern) - path_len, "VRAM-%%03d.LOG", 0);
    get_numbered_file_name(pattern, 999, filename, sizeof(filename));
    f = FIO_CreateFile(filename);
    if (f)
    {
        my_fprintf(f, "display=%d (hdmi=%d code=%d rca=%d)\n", EXT_MONITOR_CONNECTED, ext_monitor_hdmi, hdmi_code, _ext_monitor_rca);
        my_fprintf(f, "lv=%d (zoom=%d dispmode=%d rec=%d)\n", lv, lv_dispsize, lv_disp_mode, RECORDING_H264);
        my_fprintf(f, "movie=%d (res=%d crop=%d fps=%d)\n", is_movie_mode(), video_mode_resolution, video_mode_crop, video_mode_fps);
        my_fprintf(f, "play=%d (ph=%d, mv=%d, qr=%d)\n", PLAY_MODE, is_pure_play_photo_mode(), is_pure_play_movie_mode(), QR_MODE);
        
        FIO_CloseFile(f);
    }

    NotifyBox(2000, "Done :)");
    beep();
}

#ifdef FEATURE_GUIMODE_TEST
// beware, might be dangerous, some gui modes will give errors
void guimode_test()
{
    msleep(1000);
    for (int i = 0; i < 99; i++)
    {
        // some GUI modes may lock-up the camera or reboot
        // if this is the case, the troublesome mode will be skipped at next reboot.
        char fn[50];
        snprintf(fn, sizeof(fn), "VRAM%d.BMP", i);

        if (FIO_GetFileSize_direct(fn) != 0xFFFFFFFF) // this gui mode was already tested?
            continue;

        NotifyBox(500, "Trying GUI mode %d...", i);
        dump_seg(0, 0, fn); // temporary flag to indicate that this GUI mode was tried (and probably found to be troublesome)
        msleep(200);

        SetGUIRequestMode(i);

        msleep(1000);
        FIO_RemoveFile(fn);

        take_screenshot(SCREENSHOT_FILENAME_AUTO, SCREENSHOT_BMP);

        // try to reset to initial gui mode
        SetGUIRequestMode(0);
        SetGUIRequestMode(1);
        SetGUIRequestMode(0);

        msleep(1000);
    }
}
#endif

//~ uncompressed video testing
#ifdef CONFIG_6D
FILE * movfile;
int record_uncomp = 0;
#endif

void bsod()
{
    do {
        gui_stop_menu();
        SetGUIRequestMode(1);
        msleep(1000);
    } while (CURRENT_GUI_MODE != 1);
    NotifyBoxHide();
    canon_gui_disable_front_buffer();
    gui_uilock(UILOCK_EVERYTHING);
    bmp_fill(COLOR_BLUE, 0, 0, 720, 480);
    int fnt = SHADOW_FONT(FONT_MONO_20);
    int h = 20;
    int y = 20;
    bmp_printf(fnt, 0, y+=h, "   A problem has been detected and Magic Lantern has been"   );
    bmp_printf(fnt, 0, y+=h, "   shut down to prevent damage to your camera."              );
    y += h;
    bmp_printf(fnt, 0, y+=h, "   If this is the first time you've seen this STOP error"    );
    bmp_printf(fnt, 0, y+=h, "   screen, restart your camera. If this screen appears"      );
    bmp_printf(fnt, 0, y+=h, "   again, follow these steps:"                               );
    y += h;
    bmp_printf(fnt, 0, y+=h, "   - Go to LiveView and enable DIGIC peaking.  "             );
    bmp_printf(fnt, 0, y+=h, "   - Take a photo of a calendar, focusing on today's date. " );
    bmp_printf(fnt, 0, y+=h, "   - Try pressing the magic button quickly enough. "         );
    y += h;
    bmp_printf(fnt, 0, y+=h, "   Technical information:");
    bmp_printf(fnt, 0, y+=h, "   *** STOP 0x000000aa (0x1000af22, 0xdeadbeef, 0xffff)"     );
    y += h;
    bmp_printf(fnt, 0, y+=h, "   Beginning dump of physical memory"                        );
    bmp_printf(fnt, 0, y+=h, "   Physical memory dump complete. Your camera is bricked."   );
    y += h;
    bmp_printf(fnt, 0, y+=h, "   Contact the Magic Lantern guys at www.magiclantern.fm"    );
    bmp_printf(fnt, 0, y+=h, "   for further assistance and information."                  );
}


struct TwoInTwoOutLosslessPath_args
{ 
  uint32_t * ResLockKey;
  uint32_t   ResLockKeySize;
  uint32_t * engio_cmd_1_prepare;
  uint32_t * engio_cmd_2_prepare;
  uint32_t * engio_cmd_pre_jpcore;
  uint32_t * engio_cmd_post_jpcore;
  uint32_t * engio_cmd_stop;
  uint32_t * engio_cmd_6_prepare;
  uint32_t * engio_cmd_7_prepare;
  uint32_t * engio_cmd_8_HIV_PPR;
  uint16_t * PPR_table;
  uint32_t * goes_to_obinteg_C0F0E000_size_0x188;
  uint32_t * goes_to_C0F21000_size_0x7E0;
  uint32_t * LuckyEnable_goes_to_C0F20000_size_0x1000;
  uint16_t   LuckyEnable;
  uint16_t   xRes;
  uint16_t   yRes;
  uint16_t   off_0x3E;
  uint32_t   SamplePrecision;
  uint32_t   off_0x44;
  uint32_t   Read1_EdmacChannel;
  uint32_t   Read1_EdmacStartFlags;
  uint32_t   Read1_EdmacFlags;
  uint32_t   Read1_EdmacConnection;
  void *     Read1_Address;
  uint32_t   Read1_Offset;
  uint32_t   Read2_EdmacChannel;
  uint32_t   Read2_EdmacStartFlags;
  uint32_t   Read2_EdmacFlags;
  uint32_t   Read2_EdmacConnection;
  void *     Read2_Address;
  uint32_t   Read2_Offset;
  uint32_t   Write1_EdmacChannel;
  uint32_t   Write1_EdmacStartFlags;
  uint32_t   Write1_EdmacFlags;
  uint32_t   Write1_EdmacConnection;
  struct memSuite * Write1_MemSuite;
  uint32_t   off_0x8C;
  uint32_t   Write2_EdmacChannel;
  uint32_t   Write2_EdmacStartFlags;
  uint32_t   Write2_EdmacFlags;
  uint32_t   Write2_EdmacConnection;
  void *     Write2_Address;
  uint32_t   Write2_Offset;
};

static struct semaphore * lossless_sem = 0;

static void LosslessCompleteCBR()
{
    DryosDebugMsg(0, 0, "LosslessCompleteCBR\n");
    give_semaphore(lossless_sem);
}

/* pack two 16-bit values into a 32-bit one */
#define PACK32(lo,hi) (((uint32_t)(lo) & 0xFFFF) | ((uint32_t)(hi) << 16))

static void run_test()
{
    /* ProcessTwoInTwoOutLosslessPath, 5D3 1.1.3 */
    /* LiveView test */
    int width = 2080;
    int height = 1080;

    console_hide();
    raw_lv_request();
    raw_update_params();
    msleep(2000);
    raw_lv_release();
    console_show();

    /* start logging */
    void debug_intercept();
    debug_intercept();
    info_led_on();

    if (!lossless_sem)
    {
        lossless_sem = create_named_semaphore(0, 0);
    }

    /* not sure what this does; it's also used with some serial flash routines */
    MEM(0xC022200C) = 0x12;

    /* prepare arguments */
    struct TwoInTwoOutLosslessPath_args * TTL_Args = (void*) 0x456F0;

    /* setup photo quality (valid values: 0=RAW, 1=MRAW, 2=SRAW, 14, 15) */
    void (*TTL_setup_PictureSize_Mem1ToRaw)(int unused, void* TTL_Args, int PictureSize) = (void*) 0xFF32330C;
    TTL_setup_PictureSize_Mem1ToRaw(0, TTL_Args, 0);
    TTL_Args->xRes = width;
    TTL_Args->yRes = height;

    /* Output channel 22 appears used in LiveView; use 17 instead */
    TTL_Args->Write1_EdmacChannel = 0x11;

    /* allocate memory (not sure how much is needed) */
    TTL_Args->Write1_MemSuite = shoot_malloc_suite_contig(16*1024*1024);
    TTL_Args->Write2_Address  = 0;

    /* must have a valid raw buffer in LiveView */
    TTL_Args->Read1_Address   = raw_info.buffer;

    if (!TTL_Args->Write1_MemSuite) return;
    if (!TTL_Args->Read1_Address) return;

    /* configure the processing modules */

    uint32_t resources[] = {
        0x10000,                        /* read channel 0x8 */
        edmac_channel_to_index(0x11),   /* write channel 0x11 */
        0x2002d,
        0x20016,
        0x50034,
        0x5002d,
        0x50010,
        0x90001,
        0x230000,
        0x160000,
        0x260000,
        0x260001,
        0x260002,
        0x260003,
    };

    /* create ResLock on first call */
    static void * TTL_ResLock = 0;
    if (!TTL_ResLock)
    {
        TTL_ResLock = CreateResLockEntry(resources, COUNT(resources));
    }

    void (*ProcessTwoInTwoOutLosslessPath_setup)(void*, void*) = (void*) 0xFF3D4680;
    ProcessTwoInTwoOutLosslessPath_setup(TTL_ResLock, TTL_Args);

    /* resolution is hardcoded in some places; patch them */
    EngDrvOut(0xC0F375B4, PACK32(width/2 - 1,   height - 1 ));  /* 0xF6D0B8F */
    EngDrvOut(0xC0F13068, PACK32(width   - 1,   height - 1 ));  /* 0xF6D171F */
    EngDrvOut(0xC0F12010,        width/2 - 1                );  /* 0xB8F     */
    EngDrvOut(0xC0F12014, PACK32(width/2 - 1,   height - 1 ));  /* 0xF6D0B8F */
    EngDrvOut(0xC0F1201C,        width/2/10-1               );  /* 0x127     */
    EngDrvOut(0xC0F12020, PACK32(width/2/10-1,  height/10-1));  /* 0x18A0127 */

    /* need to read the image data in 2 vertical slices (not sure why)
     * example: for 2040 x 1080 x 14bpp =>
     * (1820, skip 1820) x 1079, 1820, skip -3927560, (1820, skip 1820) x 1080
     */
    int slice_pitch = width/2 * 14/8;

    struct edmac_info read1_info = {
        .xa = slice_pitch,
        .xb = slice_pitch,
        .xn = 1,
        .yb = height - 1,
        .off1a = slice_pitch,
        .off1b = slice_pitch,
        .off2b = -2 * slice_pitch * (height - 1),
    };

    SetEDmac(TTL_Args->Read1_EdmacChannel, TTL_Args->Read1_Address, &read1_info, TTL_Args->Read1_EdmacFlags);

    void * Write1_Address = GetMemoryAddressOfMemoryChunk(GetFirstChunkFromSuite(TTL_Args->Write1_MemSuite));

    printf("%dx%d %d\n", TTL_Args->xRes, TTL_Args->yRes, TTL_Args->SamplePrecision);
    printf("WR1: %x EDMAC#%d<%d> (%x)\n", Write1_Address,       TTL_Args->Write1_EdmacChannel, TTL_Args->Write1_EdmacConnection, TTL_Args->Write1_MemSuite);
    printf("WR2: %x EDMAC#%d<%d>\n", TTL_Args->Write2_Address,  TTL_Args->Write2_EdmacChannel, TTL_Args->Write2_EdmacConnection);
    printf("RD1: %x EDMAC#%d<%d>\n", TTL_Args->Read1_Address,   TTL_Args->Read1_EdmacChannel,  TTL_Args->Read1_EdmacConnection);
    printf("RD2: %x EDMAC#%d<%d>\n", TTL_Args->Read2_Address,   TTL_Args->Read1_EdmacChannel,  TTL_Args->Read2_EdmacConnection);

    /* register our CBR, to be called when finished */
    void (*RegisterTwoInTwoOutLosslessPathCompleteCBR)(int, void*, void*) = (void*) 0xFF3D3774;
    RegisterTwoInTwoOutLosslessPathCompleteCBR(4, LosslessCompleteCBR, 0);
    
    /* this changes a few registers that appear to be bit fields */
    void (*TTL_set_some_flags)(int PictureType) = (void*) 0xFF32B418;
    TTL_set_some_flags(0x10000);

    /* time the operation */
    uint32_t t0 = get_us_clock_value();

    /* this starts the EDmac channels */
    void (*ProcessTwoInTwoOutLosslessPath_start)(void*) = (void*) 0xFF3D46F0;
    ProcessTwoInTwoOutLosslessPath_start(TTL_Args);

    /* wait until finished */
    take_semaphore(lossless_sem, 1000);
    uint32_t t1 = get_us_clock_value();

    /* this is called at the end, not sure what it does */
    MEM(0xC022200C) = 0x10;

    printf("Elapsed time: %d us\n", (int)(t1 - t0));

    /* stop processing; this will report output size */
    uint32_t output_size = 0;
    void (*TTL_Stop)(void*) = (void*) 0xFF3D4728;
    void (*TTL_ReleasePathResources)(void*, void*, void*) = (void*) 0xFF3D4760;
    TTL_Stop(TTL_Args);
    TTL_ReleasePathResources(TTL_ResLock, TTL_Args, &output_size);
    
    uint32_t uncompressed_size = width * height * 14/8;
    int ratio_x100 = output_size * 10000.0 / uncompressed_size;
    printf("Output size : %s (%s%d.%02d%%)\n", format_memory_size(output_size), FMT_FIXEDPOINT2(ratio_x100));

    /* save the output buffer */
    dump_seg(Write1_Address, output_size, "TTL_WR1.BIN");

    /* to decode, add this at the beginning of lossless_jpeg_load_raw in dcraw.c:
     *   ifp = fopen("TTL_WR1.BIN", "rb");
     * also, for 2080x1080, CR2 metadata must be patched as well:
     *   cr2_slice[1] = cr2_slice[2] = 2080/2;
     *   if (jh->wide == 2960) { jh->wide = 2080/2; jh->high = 1080; }
     */

    /* cleanup */
    shoot_free_suite(TTL_Args->Write1_MemSuite); TTL_Args->Write1_MemSuite = 0;

    /* save the log */
    info_led_off();
    debug_intercept();
}

static void unmount_sd_card()
{
    extern void FSUunMountDevice(int drive);
    
    msleep(1000);
    console_clear();
    console_show();
    
    /* call shutdown hooks that need to save configs */
    extern int module_shutdown();
    config_save_at_shutdown();
    module_shutdown();
    
    /* unmount the SD card */
    FSUunMountDevice(2);
    
    printf("Unmounted SD card.\n");
    printf("You may now copy files remotely on your wifi card.\n");
    printf("Press shutter halfway to reboot.\n");
    
    while (!get_halfshutter_pressed())
    {
        msleep(10);
    }

    int reboot = 0;
    prop_request_change(PROP_REBOOT, &reboot, 4);
}

#if CONFIG_DEBUGMSG
static void dbg_draw_props(int changed);
static unsigned dbg_last_changed_propindex = 0;

void
memfilt(void* m, void* M, int value)
{
    int k = 0;
    bmp_printf(FONT_SMALL, 0, 0, "%8x", value);
    for (void* i = m; i < M; i ++)
    {
        if ((*(uint8_t*)i) == value)
        {
            int x =  10 + 4 * 22 * (k % 8);
            int y =  10 + 12 * (k / 8);
            bmp_printf(FONT_SMALL, x, y, "%8x", i);
            k = (k + 1) % 240;
        }
    }
    int x =  10 + 4 * 22 * (k % 8);
    int y =  10 + 12 * (k / 8);
    bmp_printf(FONT_SMALL, x, y, "        ");
}
#endif

static int screenshot_sec = 0;


#ifdef CONFIG_HEXDUMP

CONFIG_INT("hexdump", hexdump_addr, 0x24298);

int hexdump_enabled = 0;

static MENU_UPDATE_FUNC (hexdump_print_value_hex)
{
    MENU_SET_VALUE("0x%x",
        MEMX(hexdump_addr)
    );
}

static MENU_UPDATE_FUNC (hexdump_print_value_int32)
{
    MENU_SET_VALUE(
        "%d",
        MEMX(hexdump_addr)
    );
}

static MENU_UPDATE_FUNC (hexdump_print_value_int16)
{
    int value = MEMX(hexdump_addr);
    MENU_SET_VALUE(
        "%d %d",
        value & 0xFFFF, (value>>16) & 0xFFFF
    );
}

static MENU_UPDATE_FUNC (hexdump_print_value_int8)
{
    int value = MEMX(hexdump_addr);
    MENU_SET_VALUE(
        "%d %d %d %d",
        (int8_t)( value      & 0xFF),
        (int8_t)((value>>8 ) & 0xFF),
        (int8_t)((value>>16) & 0xFF),
        (int8_t)((value>>24) & 0xFF)
    );
}

static MENU_UPDATE_FUNC (hexdump_print_value_str)
{
    if (hexdump_addr & 0xF0000000) return;
    MENU_SET_VALUE(
        "%s",
        (char*)hexdump_addr
    );
}

static void
hexdump_toggle_value_int32(void * priv, int delta)
{
    MEM(hexdump_addr) += delta;
}

static void
hexdump_toggle_value_int16(void * priv, int delta)
{
    (*(int16_t*)(hexdump_addr+2)) += delta;
}

int hexdump_prev = 0;
void hexdump_back(void* priv, int dir)
{
    hexdump_addr = hexdump_prev;
}
void hexdump_deref(void* priv, int dir)
{
    if (dir < 0) hexdump_back(priv, dir);
    hexdump_prev = hexdump_addr;
    hexdump_addr = MEMX(hexdump_addr);
}
#endif

static int crash_log_requested = 0;
void request_crash_log(int type)
{
    crash_log_requested = type;
}

static int core_dump_requested = 0;
static int core_dump_req_from = 0;
static int core_dump_req_size = 0;
void request_core_dump(int from, int size)
{
    core_dump_req_from = from;
    core_dump_req_size = size;
    core_dump_requested = 1;
}

extern int GetFreeMemForAllocateMemory();

#ifdef CONFIG_CRASH_LOG
static void save_crash_log()
{
    static char log_filename[100];

    int log_number = 0;
    for (log_number = 0; log_number < 100; log_number++)
    {
        snprintf(log_filename, sizeof(log_filename), crash_log_requested == 1 ? "CRASH%02d.LOG" : "ASSERT%02d.LOG", log_number);
        uint32_t size;
        if( FIO_GetFileSize( log_filename, &size ) != 0 ) break;
        if (size == 0) break;
    }

    FILE* f = FIO_CreateFile(log_filename);
    if (f)
    {
        my_fprintf(f, "%s\n\n", get_assert_msg());
        my_fprintf(f,
            "Magic Lantern version : %s\n"
            "Mercurial changeset   : %s\n"
            "Built on %s by %s.\n",
            build_version,
            build_id,
            build_date,
            build_user);

        int M = GetFreeMemForAllocateMemory();
        int m = MALLOC_FREE_MEMORY;
        my_fprintf(f,
            "Free Memory  : %dK + %dK\n",
            m/1024, M/1024
        );

        FIO_CloseFile(f);
    }

    msleep(1000);

    if (crash_log_requested == 1)
    {
        NotifyBox(5000, "Crash detected - log file saved.\n"
                        "Pls send CRASH%02d.LOG to ML devs.\n"
                        "\n"
                        "%s", log_number, get_assert_msg());
    }
    else
    {
        printf("%s\n", get_assert_msg());
        console_show();
    }

}

static void crash_log_step()
{
    static int dmlog_saved = 0;
    if (crash_log_requested)
    {
        //~ beep();
        save_crash_log();
        crash_log_requested = 0;
        msleep(2000);
    }

    if (core_dump_requested)
    {
        NotifyBox(100000, "Saving core dump, please wait...\n");
        dump_seg((void*)core_dump_req_from, core_dump_req_from + core_dump_req_size, "COREDUMP.DAT");
        NotifyBox(10000, "Pls send COREDUMP.DAT to ML devs.\n");
        core_dump_requested = 0;
    }

    //~ bmp_printf(FONT_MED, 100, 100, "%x ", get_current_dialog_handler());
    extern thunk ErrForCamera_handler;
    if (get_current_dialog_handler() == &ErrForCamera_handler)
    {
        if (!dmlog_saved)
        {
            beep();
            NotifyBox(10000, "Saving debug log...");
            call("dumpf");
        }
        dmlog_saved = 1;
    }
    else dmlog_saved = 0;
}
#endif

static void
debug_loop_task( void* unused ) // screenshot, draw_prop
{
    TASK_LOOP
    {
#ifdef CONFIG_HEXDUMP
        if (hexdump_enabled)
            bmp_hexdump(FONT_SMALL, 0, 480-120, (void*) hexdump_addr, 32*10);
#endif

        #ifdef FEATURE_SCREENSHOT
        if (screenshot_sec)
        {
            info_led_blink(1, 20, 1000-20-200);
            screenshot_sec--;
            if (!screenshot_sec)
                take_screenshot(SCREENSHOT_FILENAME_AUTO, SCREENSHOT_BMP | SCREENSHOT_YUV);
        }
        #endif

        #ifdef CONFIG_RESTORE_AFTER_FORMAT
        if (MENU_MODE)
        {
            HijackFormatDialogBox_main();
        }
        #endif

        #if CONFIG_DEBUGMSG
        if (draw_prop)
        {
            dbg_draw_props(dbg_last_changed_propindex);
            continue;
        }
        #endif

        #ifdef CONFIG_CRASH_LOG
        crash_log_step();
        #endif

        msleep(200);
    }
}

static void screenshot_start(void* priv, int delta)
{
    screenshot_sec = 10;
}

static int draw_event = 0;

#ifdef FEATURE_SHOW_IMAGE_BUFFERS_INFO
static MENU_UPDATE_FUNC(image_buf_display)
{
    MENU_SET_VALUE(
        "%dx%d, %dx%d",
        vram_lv.width, vram_lv.height,
        vram_hd.width, vram_hd.height
    );
}
#endif

#ifdef FEATURE_SHOW_SHUTTER_COUNT
static MENU_UPDATE_FUNC(shuttercount_display)
{
    MENU_SET_VALUE(
        "%dK = %d+%d",
        (shutter_count_plus_lv_actuations + 500) / 1000,
        shutter_count, shutter_count_plus_lv_actuations - shutter_count
    );

    if (shutter_count_plus_lv_actuations > CANON_SHUTTER_RATING*2)
    {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Lets break Guiness World Records (rated lifespan %d).", CANON_SHUTTER_RATING);
    }
    else if (shutter_count_plus_lv_actuations > CANON_SHUTTER_RATING)
    {
        MENU_SET_WARNING(MENU_WARN_INFO, "Lifespans are for wimps (rated lifespan %d).", CANON_SHUTTER_RATING);
    }
    else if (shutter_count_plus_lv_actuations > CANON_SHUTTER_RATING/2)
    {
        MENU_SET_WARNING(MENU_WARN_INFO, "I hope I get to rated lifespan (rated lifespan %d).", CANON_SHUTTER_RATING);
    }
    else
    {
        MENU_SET_WARNING(MENU_WARN_INFO, "You may get around %d.", CANON_SHUTTER_RATING);
    }
}
#endif

#ifdef FEATURE_SHOW_CMOS_TEMPERATURE
#ifdef EFIC_CELSIUS
#define FAHRENHEIT (EFIC_CELSIUS * 9 / 5 + 32)
static MENU_UPDATE_FUNC(efictemp_display)
{
    MENU_SET_VALUE(
        "%d C, %d F, %d raw",
        EFIC_CELSIUS, FAHRENHEIT, efic_temp
    );
}
#else
static MENU_UPDATE_FUNC(efictemp_display)
{
    MENU_SET_VALUE(
        "%d raw (help needed)",
        efic_temp
    );
}
#endif
#endif

#if 0 // CONFIG_5D2
static void ambient_display(
    void *            priv,
    int            x,
    int            y,
    int            selected
)
{
    extern int lightsensor_raw_value;
    int ev = gain_to_ev_scaled(lightsensor_raw_value, 10);
    bmp_printf(
        selected ? MENU_FONT_SEL : MENU_FONT,
        x, y,
        "Ambient light: %d.%d EV",
        ev/10, ev%10
    );
    menu_draw_icon(x, y, MNI_ON, 0);
}
#endif

#ifdef FEATURE_DEBUG_PROP_DISPLAY
static CONFIG_INT("prop.i", prop_i, 0);
static CONFIG_INT("prop.j", prop_j, 0);
static CONFIG_INT("prop.k", prop_k, 0);

static MENU_UPDATE_FUNC (prop_display)
{
    unsigned prop = (prop_i << 24) | (prop_j << 16) | (prop_k);
    int* data = 0;
    size_t len = 0;
    int err = prop_get_value(prop, (void **) &data, &len);
    MENU_SET_VALUE(
    "%8x: %d: %x %x %x %x\n"
        "'%s' ",
        prop,
        len,
        len > 0x00 ? data[0] : 0,
        len > 0x04 ? data[1] : 0,
        len > 0x08 ? data[2] : 0,
        len > 0x0c ? data[3] : 0,
        strlen((const char *) data) < 100 ? (const char *) data : ""
    );
}

void prop_dump()
{
    FILE* f = FIO_CreateFile("ML/LOGS/PROP.LOG");
    if (!f)
    {
        return;
    }

    FILE* g = FIO_CreateFile("ML/LOGS/PROP-STR.LOG");
    if (!g)
    {
        FIO_CloseFile(f);
        return;
    }

    unsigned i, j, k;

    for( i=0 ; i<256 ; i++ )
    {
        if (i > 0x10 && i != 0x80) continue;
        for( j=0 ; j<=0xA ; j++ )
        {
            for( k=0 ; k<0x50 ; k++ )
            {
                unsigned prop = 0
                    | (i << 24)
                    | (j << 16)
                    | (k <<  0);

                bmp_printf(FONT_LARGE, 0, 0, "PROP %x...", prop);
                int* data = 0;
                size_t len = 0;
                int err = prop_get_value(prop, (void **) &data, &len);
                if (!err)
                {
                    my_fprintf(f, "\nPROP %8x: %5d:", prop, len );
                    my_fprintf(g, "\nPROP %8x: %5d:", prop, len );
                    for (unsigned int i = 0; i < (MIN(len,40)+3)/4; i++)
                    {
                        my_fprintf(f, "%8x ", data[i]);
                    }
                    if (strlen((const char *) data) < 100) my_fprintf(g, "'%s'", data);
                }
            }
        }
    }
    FIO_CloseFile(f);
    FIO_CloseFile(g);
    beep();
    redraw();
}

static void prop_toggle_i(void* priv, int unused) {prop_i = prop_i < 5 ? prop_i + 1 : prop_i == 5 ? 0xE : prop_i == 0xE ? 0x80 : 0; }
static void prop_toggle_j(void* priv, int unused) {prop_j = MOD(prop_j + 1, 0x10); }
static void prop_toggle_k(void* priv, int dir) {if (dir < 0) prop_toggle_j(priv, dir); prop_k = MOD(prop_k + 1, 0x51); }
#endif

#ifdef CONFIG_KILL_FLICKER
void menu_kill_flicker()
{
    gui_stop_menu();
    canon_gui_disable_front_buffer();
}
#endif


extern void menu_open_submenu();
extern MENU_UPDATE_FUNC(tasks_print);
extern MENU_UPDATE_FUNC(batt_display);
extern MENU_SELECT_FUNC(tasks_toggle_flags);

extern int show_cpu_usage_flag;

static struct menu_entry debug_menus[] = {
    MENU_PLACEHOLDER("File Manager"),
#ifdef CONFIG_HEXDUMP
    {
        .name = "Memory Browser",
        .priv = &hexdump_enabled,
        .max = 1,
        .help = "Display memory contents in real-time (hexdump).",
        .children =  (struct menu_entry[]) {
            {
                .name = "HexDump",
                .priv = &hexdump_addr,
                .max = 0x20000000,
                .unit = UNIT_HEX,
                .icon_type = IT_PERCENT,
                .help = "Address to be analyzed. Press Q to select the digit to edit."
            },
            {
                .name = "Pointer dereference",
                .select = hexdump_deref,
                .help = "Changes address to *(int*)addr [SET] or goes back [PLAY]."
            },
            {
                .name = "Val hex32",
                .update = hexdump_print_value_hex,
                .select = hexdump_toggle_value_int32,
                .help = "Value as hex."
            },
            {
                .name = "Val int32",
                .update = hexdump_print_value_int32,
                .select = hexdump_toggle_value_int32,
                .help = "Value as int32."
            },
            {
                .name = "Val int16",
                .update = hexdump_print_value_int16,
                .select = hexdump_toggle_value_int16,
                .help = "Value as 2 x int16. Toggle: changes second value."
            },
            {
                .name = "Val int8",
                .update = hexdump_print_value_int8,
                .help = "Value as 4 x int8."
            },
            {
                .name = "Val string",
                .update = hexdump_print_value_str,
                .help = "Value as string."
            },
            MENU_EOL
        },
    },
#endif
    /*{
        .name        = "Flashlight",
        .select        = flashlight_lcd,
        .select_reverse = flashlight_frontled,
        .help = "Turn on the front LED [PLAY] or make display bright [SET]."
    },*/
    #ifdef FEATURE_SCREENSHOT
    {
        .name   = "Screenshot - 10s",
        .select = screenshot_start,
        .help   = "Screenshot after 10 seconds => VRAMx.PPM.",
        .help2  = "The screenshot will contain BMP and YUV overlays."
    },
    #endif
/*    {
        .name = "Menu screenshots",
        .select     = (void (*)(void*,int))run_in_separate_task,
        .priv = screenshots_for_menu,
        .help = "Take a screenshot for each ML menu.",
    }, */
#if CONFIG_DEBUGMSG
    #if 0
    {
        .name = "Draw palette",
        .select        = bmp_draw_palette,
        .help = "Display a test pattern to see the color palette."
    },
    #endif
    {
        .name = "Spy properties",
        .priv = &draw_prop,
        .max = 1,
        .help = "Show properties as they change."
    },
/*    {
        .name        = "Dialog test",
        .select        = dlg_test,
        .help = "Dialog templates (up/dn) and color palettes (left/right)"
    },*/
#endif
    {
        .name        = "Dump ROM and RAM",
        .priv        = dump_rom_task,
        .select      = run_in_separate_task,
        .help = "ROM0.BIN:F0000000, ROM1.BIN:F8000000, RAM4.BIN"
    },
    {
        .name        = "Dump image buffers",
        .priv        = dump_img_task,
        .select      = run_in_separate_task,
        .help = "Dump all image buffers (LV, HD, RAW) from current video mode."
    },
#ifdef FEATURE_UNMOUNT_SD_CARD
    {
        .name        = "Unmount SD card",
        .priv        = unmount_sd_card,
        .select      = run_in_separate_task,
        .help        = "Run before uploading files to a Wi-Fi card, to avoid data corruption.",
        .help2       = "No further writes will be performed on your card from the camera.",
    },
#endif
#ifdef FEATURE_DONT_CLICK_ME
    {
        .name        = "Don't click me!",
        .priv =         run_test,
        .select        = run_in_separate_task,
        .help = "The camera may turn into a 1DX or it may explode."
    },
#endif
#ifdef CONFIG_DEBUG_INTERCEPT
    {
        .name        = "DebugMsg Log",
        .priv        = j_debug_intercept,
        .select      = (void(*)(void*,int))run_in_separate_task,
        .help = "Log all DebugMsg calls (dm-spy)."
    },
#endif
#ifdef FEATURE_SHOW_TASKS
    {
        .name = "Show tasks",
        .select = menu_open_submenu,
        .help = "Displays the tasks started by Canon and Magic Lantern.",
        .children =  (struct menu_entry[]) {
            {
                .name = "Task list",
                .update = tasks_print,
                .select = tasks_toggle_flags,
                #ifdef CONFIG_VXWORKS
                .help = "Task info: name, priority, stack memory usage.",
                #else
                .help = "Task info: ID, name, priority, wait_id, mem, state.",
                #endif
            },
            MENU_EOL
        }
    },
#endif
#ifdef FEATURE_SHOW_CPU_USAGE
#ifdef CONFIG_TSKMON
    {
        .name = "Show CPU usage",
        .priv = &show_cpu_usage_flag,
        .max = 3,
        .choices = (const char *[]) {"OFF", "Percentage", "Busy tasks (ABS)", "Busy tasks (REL)"},
        .help = "Display total CPU usage (percentage).",
    },
#endif
#endif
#ifdef FEATURE_SHOW_GUI_EVENTS
    {
        .name = "Show GUI evts",
        .priv = &draw_event,
        .max = 2,
        .choices = (const char *[]) {"OFF", "ON", "ON + delay 300ms"},
        .help = "Display GUI events (button codes).",
    },
#endif
#ifdef FEATURE_GUIMODE_TEST
    {
        .name = "Test GUI modes (DANGEROUS!!!)",
        .select = run_in_separate_task,
        .priv = guimode_test,
        .help = "Cycle through all GUI modes and take screenshots.",
    },
#endif
    MENU_PLACEHOLDER("Free Memory"),
#ifdef FEATURE_SHOW_IMAGE_BUFFERS_INFO
    {
        .name = "Image buffers",
        .update = image_buf_display,
        .icon_type = IT_ALWAYS_ON,
        .help = "Display the image buffer sizes (LiveView and Craw).",
        //.essential = 0,
    },
#endif
#ifdef FEATURE_SHOW_SHUTTER_COUNT
    {
        .name = "Shutter Count",
        .update = shuttercount_display,
        .icon_type = IT_ALWAYS_ON,
        .help = "Number of pics taken + number of LiveView actuations",
        //.essential = FOR_MOVIE | FOR_PHOTO,
    },
#endif
#ifdef FEATURE_SHOW_CMOS_TEMPERATURE
    {
        .name = "Internal Temp",
        .update = efictemp_display,
        .icon_type = IT_ALWAYS_ON,
	 #ifdef EFIC_CELSIUS
        .help = "EFIC chip temperature (somewhere on the mainboard).",
	 #else
	.help = "EFIC chip temperature (raw values).",
	.help2 = "http://www.magiclantern.fm/forum/index.php?topic=9673.0",
	 #endif
        //.essential = FOR_MOVIE | FOR_PHOTO,
    },
#endif
    #if 0 // CONFIG_5D2
    {
        .name = "Ambient light",
        //~.display = ambient_display,
        .help = "Ambient light from the sensor under LCD, in raw units.",
        //.essential = FOR_MOVIE | FOR_PHOTO,
    },
    #endif
#ifdef CONFIG_BATTERY_INFO
    {
        .name = "Battery level",
        .update = batt_display,
        .help = "Battery remaining. Wait for 2% discharge before reading.",
        .icon_type = IT_ALWAYS_ON,
    },
#endif
#ifdef FEATURE_DEBUG_PROP_DISPLAY
    {
        .name = "PROP Display",
        .update = prop_display,
        .select = prop_toggle_k,
        // .select_reverse = prop_toggle_j,
        .select_Q = prop_toggle_i,
        .help = "Raw property display (read-only)",
    },
#endif
};

#if CONFIG_DEBUGMSG

static void * debug_token;

static void
debug_token_handler(
    void *            token,
    void *            arg1,
    void *            arg2,
    void *            arg3
)
{
    debug_token = token;
    DebugMsg( DM_MAGIC, 3, "token %08x arg=%08x %08x %08x",
        (unsigned) token,
        (unsigned) arg1,
        (unsigned) arg2,
        (unsigned) arg3
    );
}

//~ static int dbg_propn = 0;
#define MAXPROP 30
static unsigned dbg_props[MAXPROP] = {0};
static unsigned dbg_props_len[MAXPROP] = {0};
static unsigned dbg_props_a[MAXPROP] = {0};
static unsigned dbg_props_b[MAXPROP] = {0};
static unsigned dbg_props_c[MAXPROP] = {0};
static unsigned dbg_props_d[MAXPROP] = {0};
static unsigned dbg_props_e[MAXPROP] = {0};
static unsigned dbg_props_f[MAXPROP] = {0};
static void dbg_draw_props(int changed)
{
    dbg_last_changed_propindex = changed;
    int i;
    for (i = 0; i < dbg_propn; i++)
    {
    	int x =  80;
        unsigned property = dbg_props[i];
        unsigned len = dbg_props_len[i];
#ifdef CONFIG_VXWORKS
        uint32_t fnt = FONT_MONO_20;
        unsigned y =  15 + i * 20;
#else
        uint32_t fnt = FONT_MONO_12;
        int y =  15 + i * 12;
#endif
        if (i == changed) fnt = FONT(fnt, 5, COLOR_BG);
        char msg[100];
        snprintf(msg, sizeof(msg),
#ifdef CONFIG_VXWORKS
            "%08x %04x: %8lx %8lx %8lx %8lx",
#else
            "%08x %04x: %8lx %8lx %8lx %8lx %8lx %8lx",
#endif
            property,
            len,
            len > 0x00 ? dbg_props_a[i] : 0,
            len > 0x04 ? dbg_props_b[i] : 0,
            len > 0x08 ? dbg_props_c[i] : 0,
            len > 0x0c ? dbg_props_d[i] : 0
            #ifndef CONFIG_VXWORKS
           ,len > 0x10 ? dbg_props_e[i] : 0,
            len > 0x14 ? dbg_props_f[i] : 0
            #endif
        );
        bmp_puts(fnt, &x, &y, msg);
    }
}


static void *
debug_property_handler(
    unsigned        property,
    void *            UNUSED_ATTR( priv ),
    void *            buf,
    unsigned        len
)
{
    const uint32_t * const addr = buf;

    /*printf("Prop %08x: %2x: %08x %08x %08x %08x\n",
        property,
        len,
        len > 0x00 ? addr[0] : 0,
        len > 0x04 ? addr[1] : 0,
        len > 0x08 ? addr[2] : 0,
        len > 0x0c ? addr[3] : 0
    );*/

    if( !draw_prop )
        goto ack;

    // maybe the property is already in the array
    int i;
    for (i = 0; i < dbg_propn; i++)
    {
        if (dbg_props[i] == property)
        {
            dbg_props_len[i] = len;
            dbg_props_a[i] = addr[0];
            dbg_props_b[i] = addr[1];
            dbg_props_c[i] = addr[2];
            dbg_props_d[i] = addr[3];
            dbg_props_e[i] = addr[4];
            dbg_props_f[i] = addr[5];
            dbg_draw_props(i);
            goto ack; // return with cleanup
        }
    }
    // new property
    if (dbg_propn >= MAXPROP) dbg_propn = MAXPROP-1; // too much is bad :)
    dbg_props[dbg_propn] = property;
    dbg_props_len[dbg_propn] = len;
    dbg_props_a[dbg_propn] = addr[0];
    dbg_props_b[dbg_propn] = addr[1];
    dbg_props_c[dbg_propn] = addr[2];
    dbg_props_d[dbg_propn] = addr[3];
    dbg_props_e[dbg_propn] = addr[4];
    dbg_props_f[dbg_propn] = addr[5];
    dbg_propn++;
    dbg_draw_props(dbg_propn);

ack:
    return (void*)_prop_cleanup( debug_token, property );
}

#endif

#if defined(CONFIG_500D)
#define num_properties 2048
#elif defined(CONFIG_5DC)
#define num_properties 202
#else
#define num_properties 8192
#endif

void
debug_init( void )
{
#if CONFIG_DEBUGMSG
    draw_prop = 0;
    static unsigned* property_list = 0;
    if (!property_list) property_list = malloc(num_properties * sizeof(unsigned));
    if (!property_list) return;
    unsigned i, j, k;
    unsigned actual_num_properties = 0;

    unsigned is[] = {0x2, 0x80, 0xe, 0x5, 0x4, 0x1, 0x0};
    for( i=0 ; i<COUNT(is) ; i++ )
    {
        for( j=0 ; j<=0xA ; j++ )
        {
            for( k=0 ; k<0x50 ; k++ )
            {
                unsigned prop = 0
                    | (is[i] << 24)
                    | (j << 16)
                    | (k <<  0);

                property_list[ actual_num_properties++ ] = prop;

                if( actual_num_properties >= num_properties )
                    goto thats_all;
            }
        }
    }

thats_all:
    prop_register_slave(
        property_list,
        actual_num_properties,
        debug_property_handler,
        0,
        0
    );
#endif

}

CONFIG_INT( "debug.timed-dump",        timed_dump, 0 );

//~ CONFIG_INT( "debug.dump_prop", dump_prop, 0 );
//~ CONFIG_INT( "debug.dumpaddr", dump_addr, 0 );
//~ CONFIG_INT( "debug.dumplen", dump_len, 0 );

/*
struct bmp_file_t * logo = (void*) -1;
void load_logo()
{
    if (logo == (void*) -1)
        logo = bmp_load("ML/DOC/logo.bmp",0);
}
void show_logo()
{
    load_logo();
    if ((int)logo > 0)
    {
        kill_flicker(); msleep(100);
        bmp_draw_scaled_ex(logo, 360 - logo->width/2, 240 - logo->height/2, logo->width, logo->height, 0, 0);
    }
}*/

// initialization done AFTER reading the config file,
// but BEFORE starting ML tasks
void
debug_init_stuff( void )
{
    //~ set_pic_quality(PICQ_RAW);

    #ifdef CONFIG_WB_WORKAROUND
    if (is_movie_mode())
    {
        extern void restore_kelvin_wb(); /* movtweaks.c */
        restore_kelvin_wb();
    }
    #endif

    #ifdef CONFIG_5D3
    _card_tweaks();
    #endif
}

TASK_CREATE( "debug_task", debug_loop_task, 0, 0x1e, 0x2000 );


#ifdef CONFIG_INTERMEDIATE_ISO_INTERCEPT_SCROLLWHEEL
    #ifndef FEATURE_EXPO_ISO
    #error This requires FEATURE_EXPO_ISO.
    #endif

int iso_intercept = 1;

void iso_adj(int prev_iso, int sign)
{
    if (sign)
    {
        lens_info.raw_iso = prev_iso;
        iso_intercept = 0;
        iso_toggle(0, sign);
        if (lens_info.iso > 6400) lens_set_rawiso(0);
        iso_intercept = 1;
    }
}

int iso_adj_flag = 0;
int iso_adj_old = 0;
int iso_adj_sign = 0;

void iso_adj_task(void* unused)
{
    TASK_LOOP
    {
        msleep(20);
        if (iso_adj_flag)
        {
            iso_adj_flag = 0;
            iso_adj(iso_adj_old, iso_adj_sign);
            lens_display_set_dirty();
        }
    }
}

TASK_CREATE("iso_adj_task", iso_adj_task, 0, 0x1a, 0);

PROP_HANDLER(PROP_ISO)
{
    static unsigned int prev_iso = 0;
    if (!prev_iso) prev_iso = lens_info.raw_iso;

    if (iso_intercept && ISO_ADJUSTMENT_ACTIVE && lv && lv_disp_mode == 0 && is_movie_mode())
    {
        if ((prev_iso && buf[0] && prev_iso < buf[0]) || // 100 -> 200 => +
            (prev_iso >= 112 && buf[0] == 0)) // 3200+ -> auto => +
        {
            //~ bmp_printf(FONT_LARGE, 50, 50, "[%d] ISO+", k++);
            iso_adj_old = prev_iso;
            iso_adj_sign = 1;
            iso_adj_flag = 1;
        }
        else if ((prev_iso && buf[0] && prev_iso > buf[0]) || // 200 -> 100 => -
            (prev_iso <= 88 && buf[0] == 0)) // 400- -> auto => -
        {
            //~ bmp_printf(FONT_LARGE, 50, 50, "[%d] ISO-", k++);
            iso_adj_old = prev_iso;
            iso_adj_sign = -1;
            iso_adj_flag = 1;
        }
    }
    prev_iso = buf[0];
}

#endif

#ifdef CONFIG_RESTORE_AFTER_FORMAT

static int keep_ml_after_format = 1;

static void HijackFormatDialogBox()
{
    if (MEM(DIALOG_MnCardFormatBegin) == 0) return;
    struct gui_task * current = gui_task_list.current;
    struct dialog * dialog = current->priv;
    if (dialog && !streq(dialog->type, "DIALOG")) return;

    if (keep_ml_after_format)
        dialog_set_property_str(dialog, 4, "Format card, keep ML " FORMAT_BTN_NAME);
    else
        dialog_set_property_str(dialog, 4, "Format card, remove ML " FORMAT_BTN_NAME);
    dialog_redraw(dialog);
}

static void HijackCurrentDialogBox(int string_id, char* msg)
{
    struct gui_task * current = gui_task_list.current;
    struct dialog * dialog = current->priv;
    if (dialog && !streq(dialog->type, "DIALOG")) return;
    dialog_set_property_str(dialog, string_id, msg);
    dialog_redraw(dialog);
}

int handle_keep_ml_after_format_toggle(struct event * event)
{
    if (event->param == FORMAT_BTN && MENU_MODE && MEM(DIALOG_MnCardFormatBegin))
    {
        keep_ml_after_format = !keep_ml_after_format;
        fake_simple_button(MLEV_HIJACK_FORMAT_DIALOG_BOX);
        return 0;
    }
    
    return 1;
}

/**
 * for testing dialogs and string IDs
 */

static void HijackDialogBox()
{
    struct gui_task * current = gui_task_list.current;
    struct dialog * dialog = current->priv;
    if (dialog && !streq(dialog->type, "DIALOG")) return;
    int i;
    for (i = 0; i<255; i++) {
            char s[30];
            snprintf(s, sizeof(s), "%d", i);
            dialog_set_property_str(dialog, i, s);
    }
    dialog_redraw(dialog);
}

struct tmp_file {
    char name[50];
    void* buf;
    int size;
    int sig;
};

static struct tmp_file * tmp_files = 0;
static int tmp_file_index = 0;
static void* tmp_buffer = 0;
static void* tmp_buffer_ptr = 0;
#define TMP_MAX_BUF_SIZE 15000000

static int TmpMem_Init()
{
    ASSERT(!tmp_buffer);
    ASSERT(!tmp_files);
    static int retries = 0;
    tmp_file_index = 0;
    if (!tmp_files) tmp_files = malloc(200 * sizeof(struct tmp_file));
    if (!tmp_files)
    {
        retries++;
        HijackCurrentDialogBox(4,
            retries > 2 ? "Restart your camera (malloc error)." :
                          "Format: malloc error :("
            );
        beep();
        msleep(2000);
        return 0;
    }

    if (!tmp_buffer) tmp_buffer = (void*)fio_malloc(TMP_MAX_BUF_SIZE);
    if (!tmp_buffer)
    {
        retries++;
        HijackCurrentDialogBox(4,
            retries > 2 ? "Restart your camera (fio_malloc err)." :
                          "Format: fio_malloc error, retrying..."
        );
        beep();
        msleep(2000);
        free(tmp_files); tmp_files = 0;
        return 0;
    }

    retries = 0;
    tmp_buffer_ptr = tmp_buffer;

    return 1;
}

static void TmpMem_Done()
{
    free(tmp_files); tmp_files = 0;
    fio_free(tmp_buffer); tmp_buffer = 0;
}

static void TmpMem_UpdateSizeDisplay(int counting)
{
    int size = tmp_buffer_ptr - tmp_buffer;
    int size_mb = size * 10 / 1024 / 1024;

    char msg[100];
    snprintf(msg, sizeof(msg), "Format       (ML size: %s%d.%d MB%s)", counting ? "> " : "", size_mb/10, size_mb%10, counting ? "..." : "");
    HijackCurrentDialogBox(3, msg);
}

static void TmpMem_AddFile(char* filename)
{
    if (!tmp_buffer) return;
    if (!tmp_buffer_ptr) return;

    int filesize = FIO_GetFileSize_direct(filename);
    if (filesize == -1) return;
    if (tmp_file_index >= 200) return;
    if (tmp_buffer_ptr + filesize + 10 >= tmp_buffer + TMP_MAX_BUF_SIZE) return;
    
    /* don't add the same file twice */
    for (int i = 0; i < tmp_file_index; i++)
        if (streq(tmp_files[i].name, filename))
            return;

    read_file(filename, tmp_buffer_ptr, filesize);
    snprintf(tmp_files[tmp_file_index].name, 50, "%s", filename);
    tmp_files[tmp_file_index].buf = tmp_buffer_ptr;
    tmp_files[tmp_file_index].size = filesize;
    tmp_files[tmp_file_index].sig = compute_signature(tmp_buffer_ptr, filesize/4);
    tmp_file_index++;
    tmp_buffer_ptr += ALIGN32SUP(filesize);

    /* no not update on every file, else it takes too long (90% of time updating display) */
    static int aux = 0;
    if(should_run_polling_action(500, &aux))
    {
        char msg[100];

        snprintf(msg, sizeof(msg), "Reading %s...", filename, tmp_buffer_ptr);
        HijackCurrentDialogBox(4, msg);
        TmpMem_UpdateSizeDisplay(1);
    }
}

static void CopyMLDirectoryToRAM_BeforeFormat(char* dir, int (*is_valid_filename)(char*), int recursive_levels)
{
    struct fio_file file;
    struct fio_dirent * dirent = FIO_FindFirstEx( dir, &file );
    if( IS_ERROR(dirent) )
        return;

    do {
        if (file.name[0] == '.' || file.name[0] == '_') continue;
        if (file.mode & ATTR_DIRECTORY)
        {
            if (recursive_levels > 0)
            {
                char new_dir[0x80];
                snprintf(new_dir, sizeof(new_dir), "%s%s/", dir, file.name);
                CopyMLDirectoryToRAM_BeforeFormat(new_dir, is_valid_filename, recursive_levels-1);
            }
            continue; // is a directory
        }
        
        if (is_valid_filename && !is_valid_filename(file.name))
        {
            continue;
        }

        char fn[0x80];
        snprintf(fn, sizeof(fn), "%s%s", dir, file.name);
        TmpMem_AddFile(fn);

    } while( FIO_FindNextEx( dirent, &file ) == 0);
    FIO_FindClose(dirent);
}

static int is_valid_fir_filename(char* filename)
{
    int n = strlen(filename);
    if ((n > 4) && (streq(filename + n - 4, ".FIR") || streq(filename + n - 4, ".fir")))
        return 1;
    return 0;
}

static int is_valid_log_filename(char* filename)
{
    int n = strlen(filename);
    if ((n > 4) && (streq(filename + n - 4, ".LOG") || streq(filename + n - 4, ".log")))
        return 1;
    return 0;
}

static void CopyMLFilesToRAM_BeforeFormat()
{
    /* this is the most important file, read it first */
    TmpMem_AddFile("AUTOEXEC.BIN");
    
    /* some important subdirectories from ML/ */
    CopyMLDirectoryToRAM_BeforeFormat("ML/FONTS/", 0, 0);
    CopyMLDirectoryToRAM_BeforeFormat("ML/MODULES/", 0, 0);
    CopyMLDirectoryToRAM_BeforeFormat("ML/SETTINGS/", 0, 1);

    /* FIR files from root dir */
    CopyMLDirectoryToRAM_BeforeFormat("", is_valid_fir_filename, 0);
    
    /* everything else from ML dir */
    CopyMLDirectoryToRAM_BeforeFormat("ML/", 0, 2);
    
    /* and, if we still have free space, also keep the LOG files from root dir */
    CopyMLDirectoryToRAM_BeforeFormat("", is_valid_log_filename, 0);
    
    /* restore Toshiba FlashAir files, if any */
    /* (normally, formatting this card from camera disables wifi operation) */
    /* (not sure which of those are strictly needed) */
    CopyMLDirectoryToRAM_BeforeFormat("B:/SD_WLAN/", 0, 0);
    CopyMLDirectoryToRAM_BeforeFormat("B:/GUPIXINF/", 0, 1);
    TmpMem_AddFile("B:/DCIM/100__TSB/FA000001.JPG");

    TmpMem_UpdateSizeDisplay(0);
}

// check if autoexec.bin is present on the card
static int check_autoexec()
{
    return is_file("AUTOEXEC.BIN");
}

static void CopyMLFilesBack_AfterFormat()
{
    int i;
    char msg[100];
    int aux = 0;
    for (i = 0; i < tmp_file_index; i++)
    {
        if(should_run_polling_action(500, &aux))
        {
            snprintf(msg, sizeof(msg), "Restoring %s...", tmp_files[i].name);
            HijackCurrentDialogBox(FORMAT_STR_LOC, msg);
        }
        dump_seg(tmp_files[i].buf, tmp_files[i].size, tmp_files[i].name);
        int sig = compute_signature(tmp_files[i].buf, tmp_files[i].size/4);
        if (sig != tmp_files[i].sig)
        {
            snprintf(msg, sizeof(msg), "Could not restore %s :(", tmp_files[i].name);
            HijackCurrentDialogBox(FORMAT_STR_LOC, msg);
            msleep(2000);
            FIO_RemoveFile(tmp_files[i].name);
            if (i <= 1) return;
            //else: if it copies AUTOEXEC.BIN and fonts, ignore the error, it's safe to run
        }
    }

    /* make sure we don't enable bootflag when there is no autoexec.bin (anymore) */
    if(check_autoexec())
    {
        HijackCurrentDialogBox(FORMAT_STR_LOC, "Writing bootflags...");
        
        extern int bootflag_write_bootblock(void);
        if (!bootflag_write_bootblock())
        {
            beep_times(3);
            NotifyBox(5000, "Bootflags not written, use EosCard");
        }
    }

    HijackCurrentDialogBox(FORMAT_STR_LOC, "Magic Lantern restored :)");
    msleep(2000);
}

static void restart_after_format()
{
    /* restart the camera after formatting */
    HijackCurrentDialogBox(FORMAT_STR_LOC, "Restarting camera...");
    msleep(1000);
    
    int reboot = 0;
    prop_request_change(PROP_REBOOT, &reboot, 4);
}

static void HijackFormatDialogBox_main()
{
    if (!MENU_MODE) return;
    if (MEM(DIALOG_MnCardFormatBegin) == 0) return;
    // at this point, Format dialog box is active
    
    #ifdef CONFIG_DUAL_SLOT
    int ml_on_cf = (get_ml_card()->drive_letter[0] == 'A');
    if (ml_on_cf != FORMATTING_CF_CARD)
    {
        /* we are not formatting the ML card, no need to restore anything */
        return;
    }
    #endif

    // make sure we have something to restore :)
    if (!check_autoexec()) return;

    gui_uilock(UILOCK_EVERYTHING);
    
    while (!TmpMem_Init())  /* may fail because of not enough memory */
        msleep(100);

    // before user attempts to do something, copy ML files to RAM
    CopyMLFilesToRAM_BeforeFormat();
    gui_uilock(UILOCK_NONE);

    // all files copied, we can change the message in the format box and let the user know what's going on
    fake_simple_button(MLEV_HIJACK_FORMAT_DIALOG_BOX);

    // waiting to exit the format dialog somehow
    while (MEM(DIALOG_MnCardFormatBegin))
        msleep(200);

    // and maybe to finish formatting the card
    while (MEM(DIALOG_MnCardFormatExecute))
        msleep(50);

    // card was formatted (autoexec no longer there) => restore ML
    if (keep_ml_after_format && !check_autoexec())
    {
        gui_uilock(UILOCK_EVERYTHING);
        CopyMLFilesBack_AfterFormat();
        TmpMem_Done();
        restart_after_format();
        /* needed? */
        gui_uilock(UILOCK_NONE);
    }
    else
    {
        TmpMem_Done();
    }
}
#endif

void debug_menu_init()
{
    #ifdef FEATURE_LV_DISPLAY_PRESETS
    extern struct menu_entry livev_cfg_menus[];
    menu_add( "Prefs", livev_cfg_menus,  1);
    #endif

    crop_factor_menu_init();
    customize_menu_init();
    menu_add( "Debug", debug_menus, COUNT(debug_menus) );
    
    #ifdef FEATURE_SHOW_FREE_MEMORY
    mem_menu_init();
    #endif
    
    movie_tweak_menu_init();
}

void spy_event(struct event * event)
{
    if (draw_event)
    {
        static int kev = 0;
        static int y = 250;
        kev++;
        bmp_printf(FONT_MONO_20, 0, y, "Ev%d: p=%8x *o=%8x/%8x/%8x a=%8x\n                                                           ",
            kev,
            event->param,
            event->obj ? ((int)event->obj & 0xf0000000 ? (int)event->obj : *(int*)(event->obj)) : 0,
            event->obj ? ((int)event->obj & 0xf0000000 ? (int)event->obj : *(int*)(event->obj + 4)) : 0,
            event->obj ? ((int)event->obj & 0xf0000000 ? (int)event->obj : *(int*)(event->obj + 8)) : 0,
            event->arg);
        y += 20;
        if (y > 350) y = 250;
        if (draw_event == 2) msleep(300);
    }
}

#ifdef CONFIG_5DC
static int halfshutter_pressed;
bool get_halfshutter_pressed() { return halfshutter_pressed; }
#else
bool get_halfshutter_pressed() { return HALFSHUTTER_PRESSED && !dofpreview; }
#endif

static int zoom_in_pressed = 0;
static int zoom_out_pressed = 0;
int get_zoom_out_pressed() { return zoom_out_pressed; }

int handle_buttons_being_held(struct event * event)
{
    // keep track of buttons being pressed
    #ifdef CONFIG_5DC
    if (event->param == BGMT_PRESS_HALFSHUTTER) halfshutter_pressed = 1;
    if (event->param == BGMT_UNPRESS_HALFSHUTTER) halfshutter_pressed = 0;
    #endif
    #ifdef BGMT_UNPRESS_ZOOM_IN
    if (event->param == BGMT_PRESS_ZOOM_IN) {zoom_in_pressed = 1; zoom_out_pressed = 0; }
    if (event->param == BGMT_UNPRESS_ZOOM_IN) {zoom_in_pressed = 0; zoom_out_pressed = 0; }
    #endif
    #ifdef BGMT_PRESS_ZOOM_OUT
    if (event->param == BGMT_PRESS_ZOOM_OUT) { zoom_out_pressed = 1; zoom_in_pressed = 0; }
    if (event->param == BGMT_UNPRESS_ZOOM_OUT) { zoom_out_pressed = 0; zoom_in_pressed = 0; }
    #endif
    
    (void)zoom_in_pressed; /* silence warning */

    return 1;
}

// those functions seem not to be thread safe
// calling them from gui_main_task seems to sync them with other Canon calls properly
int handle_tricky_canon_calls(struct event * event)
{
    // fake ML events are always negative numbers
    if (event->param >= 0) return 1;

    //~ static int k; k++;
    //~ bmp_printf(FONT_LARGE, 50, 50, "[%d] tricky call: %d ", k, event->param); msleep(1000);

    switch (event->param)
    {
        #ifdef CONFIG_RESTORE_AFTER_FORMAT
        case MLEV_HIJACK_FORMAT_DIALOG_BOX:
            HijackFormatDialogBox();
            break;
        #endif
        case MLEV_TURN_ON_DISPLAY:
            if (!DISPLAY_IS_ON) call("TurnOnDisplay");
            break;
        case MLEV_TURN_OFF_DISPLAY:
            if (DISPLAY_IS_ON) call("TurnOffDisplay");
            break;
        /*case MLEV_ChangeHDMIOutputSizeToVGA:
            ChangeHDMIOutputSizeToVGA();
            break;*/
        case MLEV_LCD_SENSOR_START:
            #ifdef CONFIG_LCD_SENSOR
            DispSensorStart();
            #endif
            break;
        case MLEV_REDRAW:
            _redraw_do();   /* todo: move in gui-common.c */
            break;
    }
    
    return 1;
}

// engio functions may fail and lock the camera
void EngDrvOut(uint32_t reg, uint32_t value)
{
    if (ml_shutdown_requested) return;
    if (!(MEM(0xC0400008) & 0x2)) return; // this routine requires LCLK enabled
    _EngDrvOut(reg, value);
}

void engio_write(uint32_t* reg_list)
{
    if (!(MEM(0xC0400008) & 0x2)) return; // this routine requires LCLK enabled
    _engio_write(reg_list);
}
