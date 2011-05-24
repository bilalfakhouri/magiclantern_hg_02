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
//#include "lua.h"

int config_autosave = 1;
#define CONFIG_AUTOSAVE_FLAG_FILE "B:/AUTOSAVE.NEG"

//////////////////////////////////////////////////////////
// debug manager enable/disable
//////////////////////////////////////////////////////////

CONFIG_INT("dm.enable", dm_enable, 0);

void dm_update()
{
	if (dm_enable) dmstart();
	else dmstop();
}

static void
dm_display(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	int mode = *(int*) priv;
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Debug logging  : %s",
		dm_enable ? "ON, Q=dump" : "OFF,Q=dump"
	);
}

static void dm_toggle(void* priv)
{
	dm_enable = !dm_enable;
	dm_update();
}
//////////////////////////////////////////////////////////

extern void bootdisk_disable();


void take_screenshot( void * priv )
{
	call( "dispcheck" );
	silent_pic_take_lv_dbg();
}

int draw_prop = 0;

static void
draw_prop_select( void * priv )
{
	draw_prop = !draw_prop;
}

static int dbg_propn = 0;
static void 
draw_prop_reset( void * priv )
{
	dbg_propn = 0;
}

int mem_spy = 0;
int mem_spy_start = 0; // start from here
int mem_spy_bool = 0;           // only display booleans (0,1,-1)
int mem_spy_fixed_addresses = 0; // only look from a list of fixed addresses
const int mem_spy_addresses[] = {};//0xc0000044, 0xc0000048, 0xc0000057, 0xc00011cf, 0xc02000a8, 0xc02000ac, 0xc0201004, 0xc0201010, 0xc0201100, 0xc0201104, 0xc0201200, 0xc0203000, 0xc020301c, 0xc0203028, 0xc0203030, 0xc0203034, 0xc020303c, 0xc0203044, 0xc0203048, 0xc0210200, 0xc0210208, 0xc022001c, 0xc0220028, 0xc0220034, 0xc0220070, 0xc02200a4, 0xc02200d0, 0xc02200d4, 0xc02200d8, 0xc02200e8, 0xc02200ec, 0xc0220100, 0xc0220104, 0xc022010c, 0xc0220118, 0xc0220130, 0xc0220134, 0xc0220138, 0xc0222000, 0xc0222004, 0xc0222008, 0xc022200c, 0xc0223000, 0xc0223010, 0xc0223060, 0xc0223064, 0xc0223068, 0xc0224100, 0xc0224104, 0xc022d000, 0xc022d02c, 0xc022d074, 0xc022d1ec, 0xc022d1f0, 0xc022d1f4, 0xc022d1f8, 0xc022d1fc, 0xc022dd14, 0xc022f000, 0xc022f004, 0xc022f200, 0xc022f210, 0xc022f214, 0xc022f340, 0xc022f344, 0xc022f430, 0xc022f434, 0xc0238060, 0xc0238064, 0xc0238080, 0xc0238084, 0xc0238098, 0xc0242010, 0xc0300000, 0xc0300100, 0xc0300104, 0xc0300108, 0xc0300204, 0xc0400004, 0xc0400008, 0xc0400018, 0xc040002c, 0xc0400080, 0xc0400084, 0xc040008c, 0xc04000b4, 0xc04000c0, 0xc04000c4, 0xc04000cc, 0xc0410000, 0xc0410008, 0xc0500080, 0xc0500088, 0xc0500090, 0xc0500094, 0xc05000a0, 0xc05000a8, 0xc05000b0, 0xc05000b4, 0xc05000c0, 0xc05000c4, 0xc05000c8, 0xc05000cc, 0xc05000d0, 0xc05000d4, 0xc05000d8, 0xc0520000, 0xc0520004, 0xc0520008, 0xc052000c, 0xc0520014, 0xc0520018, 0xc0720000, 0xc0720004, 0xc0720008, 0xc072000c, 0xc0720014, 0xc0720024, 0xc07200ec, 0xc07200f0, 0xc0720100, 0xc0720104, 0xc0720108, 0xc072010c, 0xc0720110, 0xc0720114, 0xc0720118, 0xc072011c, 0xc07201c8, 0xc0720200, 0xc0720204, 0xc0720208, 0xc072020c, 0xc0720210, 0xc0800008, 0xc0800014, 0xc0800018, 0xc0820000, 0xc0820304, 0xc0820308, 0xc082030c, 0xc0820310, 0xc0820318, 0xc0920000, 0xc0920004, 0xc0920008, 0xc092000c, 0xc0920010, 0xc0920100, 0xc0920118, 0xc092011c, 0xc0920120, 0xc0920124, 0xc0920204, 0xc0920208, 0xc092020c, 0xc0920210, 0xc0920220, 0xc0920224, 0xc0920238, 0xc0920320, 0xc0920344, 0xc0920348, 0xc0920354, 0xc0920358, 0xc0a00000, 0xc0a00008, 0xc0a0000c, 0xc0a00014, 0xc0a00018, 0xc0a0001c, 0xc0a00020, 0xc0a00024, 0xc0a00044, 0xc0a10008 };
int mem_spy_len = 0x10000/4;    // look at ### int32's; use only when mem_spy_fixed_addresses = 0
//~ int mem_spy_len = COUNT(mem_spy_addresses); // use this when mem_spy_fixed_addresses = 1

int mem_spy_count_lo = 5; // how many times is a value allowed to change
int mem_spy_count_hi = 0; // (limits)
int mem_spy_freq_lo = 5; 
int mem_spy_freq_hi = 100;  // or check frequecy between 2 limits (0 = disable)
int mem_spy_value_lo = 0x40000000;
int mem_spy_value_hi = 0;  // or look for a specific range of values (0 = disable)

static void
mem_spy_select( void * priv )
{
	mem_spy = !mem_spy;
}

void card_led_on() { AJ_guess_LED_ON(1); }
void card_led_off() { AJ_guess_LED_OFF(1); }
void card_led_blink(int times, int delay_on, int delay_off)
{
	int i;
	for (i = 0; i < times; i++)
	{
		card_led_on();
		msleep(delay_on);
		card_led_off();
		msleep(delay_off);
	}
}

int config_ok = 0;

static void
save_config( void * priv )
{
	config_save_file( "B:/magic.cfg" );
}
static void
delete_config( void * priv )
{
	FIO_RemoveFile( "B:/magic.cfg" );
	if (config_autosave) config_autosave_toggle(0);
}

static void
config_autosave_display(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Config AutoSave: %s", 
		config_autosave ? "ON" : "OFF"
	);
}

void
config_autosave_toggle(void* priv)
{
	config_flag_file_setting_save(CONFIG_AUTOSAVE_FLAG_FILE, !!config_autosave);
	msleep(50);
	config_autosave = !config_flag_file_setting_load(CONFIG_AUTOSAVE_FLAG_FILE);
}


static int vmax(int* x, int n)
{
	int i; 
	int m = -100000;
	for (i = 0; i < n; i++)
		if (x[i] > m)
			m = x[i];
	return m;
}

void xx_test(void* priv)
{
	cbr_init();
	bitrate_set();
	while (!get_halfshutter_pressed()) msleep(100);
}

void toggle_mirror_display()
{
	//~ zebra_pause();
	if (lv_drawn()) msleep(200); // redrawing screen while zebra is active seems to cause trouble
	static int i = 0;
	if (i) MirrorDisplay();
	else NormalDisplay();
	i = !i;
	msleep(200);
	//~ zebra_resume();
}

void fake_simple_button(int bgmt_code)
{
	struct event e = {
		.type = 0,
		.param = bgmt_code, 
		.obj = 0,
		.arg = 0,
	};
	GUI_CONTROL(&e);
}

static int* dbg_memmirror = 0;
static int* dbg_memchanges = 0;

static int dbg_memspy_get_addr(int i)
{
	if (mem_spy_fixed_addresses)
		return mem_spy_addresses[i];
	else
		return mem_spy_start + i*4;
}

// for debugging purpises only
int _t = 0;
int _get_timestamp(struct tm * t)
{
	return t->tm_sec + t->tm_min * 60 + t->tm_hour * 3600 + t->tm_mday * 3600 * 24;
}
void _tic()
{
	struct tm now;
	LoadCalendarFromRTC(&now);
	_t = _get_timestamp(&now);
}
int _toc()
{
	struct tm now;
	LoadCalendarFromRTC(&now);
	return _get_timestamp(&now) - _t;
}

static void dbg_memspy_init() // initial state of the analyzed memory
{
	bmp_printf(FONT_MED, 10,10, "memspy init @ %x ... (+%x) ... %x", mem_spy_start, mem_spy_len, mem_spy_start + mem_spy_len * 4);
	//~ msleep(2000);
	//mem_spy_len is number of int32's
	if (!dbg_memmirror) dbg_memmirror = AllocateMemory(mem_spy_len*4 + 100); // local copy of mem area analyzed
	if (!dbg_memmirror) return;
	if (!dbg_memchanges) dbg_memchanges = AllocateMemory(mem_spy_len*4 + 100); // local copy of mem area analyzed
	if (!dbg_memchanges) return;
	int i;
	//~ bmp_printf(FONT_MED, 10,10, "memspy alloc");
	int crc = 0;
	for (i = 0; i < mem_spy_len; i++)
	{
		uint32_t addr = dbg_memspy_get_addr(i);
		dbg_memmirror[i] = *(int*)(addr);
		dbg_memchanges[i] = 0;
		crc += dbg_memmirror[i];
		//~ bmp_printf(FONT_MED, 10,10, "memspy: %8x => %8x ", addr, dbg_memmirror[i]);
		//~ msleep(1000);
	}
	bmp_printf(FONT_MED, 10,10, "memspy OK: %x", crc);
	_tic();
}
static void dbg_memspy_update()
{
	static int init_done = 0;
	if (!init_done) dbg_memspy_init();
	init_done = 1;

	if (!dbg_memmirror) return;
	if (!dbg_memchanges) return;

	int elapsed_time = _toc();
	bmp_printf(FONT_MED, 50, 400, "%d ", elapsed_time);

	int i;
	int k=0;
	for (i = 0; i < mem_spy_len; i++)
	{
		uint32_t fnt = FONT_SMALL;
		uint32_t addr = dbg_memspy_get_addr(i);
		int oldval = dbg_memmirror[i];
		int newval = *(int*)(addr);
		if (oldval != newval)
		{
			//~ bmp_printf(FONT_MED, 10,460, "memspy: %8x: %8x => %8x", addr, oldval, newval);
			dbg_memmirror[i] = newval;
			if (dbg_memchanges[i] < 1000000) dbg_memchanges[i]++;
			fnt = FONT(FONT_SMALL, 5, COLOR_BG);
		}
		//~ else continue;

		if (mem_spy_bool && newval != 0 && newval != 1 && newval != -1) continue;

		if (mem_spy_value_lo && newval < mem_spy_value_lo) continue;
		if (mem_spy_value_hi && newval > mem_spy_value_hi) continue;
		
		if (mem_spy_count_lo && dbg_memchanges[i] < mem_spy_count_lo) continue;
		if (mem_spy_count_hi && dbg_memchanges[i] > mem_spy_count_hi) continue;
		
		int freq = dbg_memchanges[i] / elapsed_time;
		if (mem_spy_freq_lo && freq < mem_spy_freq_lo) continue;
		if (mem_spy_freq_hi && freq > mem_spy_freq_hi) continue;

		int x = 10 + 8 * 22 * (k % 4);
		int y = 10 + 12 * (k / 4);
		bmp_printf(fnt, x, y, "%8x:%2d:%8x", addr, freq, newval);
		k = (k + 1) % 120;
	}

	for (i = 0; i < 10; i++)
	{
		int x = 10 + 8 * 22 * (k % 4);
		int y = 10 + 12 * (k / 4);
		bmp_printf(FONT_SMALL, x, y, "                    ");
		k = (k + 1) % 120;
	}
}

void display_info()
{
	bmp_printf(FONT_MED, 400, 0, "Shutter Count: %d", shutter_count);
	bmp_printf(FONT_MED, 400, 20, "CMOS Temperat: %d", efic_temp);
	bmp_printf(FONT_MED, 400, 40, "Lens: %s          ", lens_info.name);
}

void display_shortcut_key_hints_lv()
{
	static int old_mode = 0;
	int mode = 0;
	if (!zebra_should_run()) return;
	if (shooting_mode == SHOOTMODE_MOVIE && FLASH_BTN_MOVIE_MODE) mode = 1;
	else if (get_lcd_sensor_shortcuts() && display_sensor && DISPLAY_SENSOR_POWERED) mode = 2;
	else if (is_follow_focus_active() && !is_manual_focus() && (!display_sensor || !get_lcd_sensor_shortcuts())) mode = 3;
	if (mode == 0 && old_mode == 0) return;

	int mz = (mode == 2 && get_zoom_overlay_z() && lv_dispsize == 1);
	
	if (mode == 1)
	{
		bmp_printf(FONT_MED, 360 - 100 - font_med.width*2, 240 - font_med.height/2, "ISO-");
		bmp_printf(FONT_MED, 360 + 100 - font_med.width*2, 240 - font_med.height/2, "ISO+");
		bmp_printf(FONT_MED, 360 - font_med.width*2, 240 - 100 - font_med.height/2, "Vol+");
		bmp_printf(FONT_MED, 360 - font_med.width*2, 240 + 100 - font_med.height/2, "Vol-");
	}
	else if (mode == 2)
	{
		bmp_printf(FONT_MED, 360 - 100 - font_med.width*2, 240 - font_med.height/2, "Kel-");
		bmp_printf(FONT_MED, 360 + 100 - font_med.width*2, 240 - font_med.height/2, "Kel+");
		bmp_printf(FONT_MED, 360 - font_med.width*2, 240 - 100 - font_med.height/2, "LCD+");
		bmp_printf(FONT_MED, 360 - font_med.width*2, 240 + 100 - font_med.height/2, "LCD-");
	}
	else if (mode == 3)
	{
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 - 100 - font_med.width*2, 240 - font_med.height/2, "FF+ ");
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 + 100 - font_med.width*2, 240 - font_med.height/2, "FF- ");
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 - font_med.width*2, 240 - 100 - font_med.height/2, "FF++");
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 - font_med.width*2, 240 + 100 - font_med.height/2, "FF--");
	}
	else
	{
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 - 100 - font_med.width*2, 240 - font_med.height/2, "    ");
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 + 100 - font_med.width*2, 240 - font_med.height/2, "    ");
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 - font_med.width*2, 240 - 100 - font_med.height/2, "    ");
		bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 - font_med.width*2, 240 + 100 - font_med.height/2, "    ");
	}

	if (mz) bmp_printf(FONT_MED, 360 + 100, 240 - 150, "Magic Zoom");
	else bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 360 + 100, 240 - 150, "          ");

	old_mode = mode;
	
	if (!should_draw_zoom_overlay())
		crop_set_dirty(20);
}

void display_clock()
{
	int bg = bmp_getpixel(15, 430);

	struct tm now;
	LoadCalendarFromRTC( &now );
	if (lv_drawn())
	{
		uint32_t fnt = FONT(FONT_MED, COLOR_WHITE, TOPBAR_BGCOLOR);
		bmp_printf(fnt, 0, 0, "%02d:%02d", now.tm_hour, now.tm_min);
	}
	else
	{
		uint32_t fnt = FONT(FONT_LARGE, COLOR_FG_NONLV, bg);
		bmp_printf(fnt, 400, 410, "%02d:%02d", now.tm_hour, now.tm_min);
	}
}

PROP_INT(PROP_APERTURE, aper1);
PROP_INT(PROP_APERTURE2, aper2);
PROP_INT(PROP_APERTURE3, aper3);

struct rolling_pitching 
{
	uint8_t status;
	uint8_t cameraposture;
	int8_t roll_sensor1;
	int8_t roll_sensor2;
	int8_t pitch_sensor1;
	int8_t pitch_sensor2;
};
struct rolling_pitching level_data;

PROP_HANDLER(PROP_ROLLING_PITCHING_LEVEL)
{
	memcpy(&level_data, buf, 6);
	return prop_cleanup(token, property);
}

static void dbg_draw_props(int changed);
static unsigned dbg_last_changed_propindex = 0;
int screenshot_sec = 0;
static void
debug_loop_task( void ) // screenshot, draw_prop
{
	msleep(500);
	int k;
	for (k = 0; ; k++)
	{
		msleep(10);
		if (gui_state == GUISTATE_MENUDISP)
		{
			display_info();
		}
		
		//~ bmp_printf(FONT_MED, 50, 50, "%2x %2x %2x %2x %2x  ", level_data.status, level_data.cameraposture, level_data.roll_sensor1, level_data.roll_sensor2, level_data.roll_sensor1 + level_data.roll_sensor2);
		//~ struct tm now;
		//~ LoadCalendarFromRTC(&now);
		//~ bmp_hexdump(FONT_SMALL, 0, 20, 0x14c00, 32*5);
		//~ bmp_hexdump(FONT_SMALL, 0, 200, 0x26B8, 32*5);
		
		//~ if (recording == 2)
			//~ bmp_printf(FONT_MED, 0, 0, "frame=%d bytes=%8x", MVR_FRAME_NUMBER, MVR_BYTES_WRITTEN);
		//~ bmp_hexdump(FONT_SMALL, 0, 20, 0x1E774, 32*10);
		//~ bmp_printf(FONT_MED, 0, 0, "%x  ", *(int*)131030);
		//~ DEBUG("MovRecState: %d", MOV_REC_CURRENT_STATE);
		
		if (!lv_drawn() && gui_state == GUISTATE_IDLE && !gui_menu_shown() && /*!big_clock &&*/ bmp_getpixel(2,10) != 2) BMP_SEM
		(
			display_clock();
			display_shooting_info();
		)
		
		if (lv_drawn() && !gui_menu_shown()) BMP_SEM
		(
			display_shooting_info_lv();
			if (shooting_mode == SHOOTMODE_MOVIE && !ae_mode_movie && !gui_menu_shown()) 
				bmp_printf(FONT(FONT_MED, COLOR_WHITE, 0), 50, 50, "!!! Auto exposure !!!\nSet 'Movie exposure: Manual' in Canon menu");
			display_shortcut_key_hints_lv();
		)
		
		if (screenshot_sec)
		{
			if (screenshot_sec >= 5) BMP_SEM( bmp_printf( FONT_SMALL, 0, 0, "Screenshot in %d seconds ", screenshot_sec); )
			if (screenshot_sec == 4) redraw();
			screenshot_sec--;
			msleep( 1000 );
			if (!screenshot_sec)
				take_screenshot(0);
		}
		
		if (BTN_METERING_PRESSED_IN_LV)
		{
			toggle_disp_mode();
			while (BTN_METERING_PRESSED_IN_LV) msleep(100);
		}
		
		if (draw_prop)
		{
			dbg_draw_props(dbg_last_changed_propindex);
		}
		else if (mem_spy)
		{
			dbg_memspy_update();
		}
		
		msleep(10);
	}
}

static void screenshot_start(void)
{
	screenshot_sec = 10;
}

void toggle_draw_event( void * priv );

static void
spy_print(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Spy %s/%s/%s (s/d/q)",
		draw_prop ? "PROP" : "prop",
		get_draw_event() ? "EVT" : "evt", 
		mem_spy ? "MEM" : "mem"
	);
}


struct menu_entry debug_menus[] = {
	{
		.priv		= "Draw palette",
		.select		= bmp_draw_palette,
		.display	= menu_print,
		.help = "Display a test pattern to see the color palette."
	},
	{
		.priv		= "Screenshot (10 s)",
		.select		= screenshot_start,
		.select_auto = take_screenshot,
		.display	= menu_print,
		.help = "Take a screenshot after 10 seconds [SET] or right now [Q]."
	},
	{
		.select = dm_toggle, 
		.select_auto		= (void*) dumpf,
		.display	= dm_display,
		.help = "While ON, debug messages are saved. [Q] => LOGnnn.LOG."
	},
	{
		.select		= draw_prop_select,
		.select_reverse = toggle_draw_event,
		.select_auto = mem_spy_select,
		.display	= spy_print,
		.help = "Spy properties / events / memory addresses which change."
	},
	{
		.priv		= "Don't click me!",
		.select		= xx_test,
		.display	= menu_print,
		.help = "The camera may turn into a 1D Mark V or it may explode."
	}
/*	{
		.select = focus_test,
		.display = focus_print,
		.select_reverse = focus_en_bump,
		.select_auto = focus_mod_bump
	},
	{
		.priv = "CBR test", 
		.select = cbr_set,
		.display = menu_print,
	}*/

#if 0
	{
		.priv		= "Enable full HD",
		.select		= enable_full_hd,
		.display	= display_full_hd,
	},
	{
		.display	= mvr_time_const_display,
		.select		= mvr_time_const_select,
	},
#endif
/*	{
		.priv		= "Clear config",
		.select		= clear_config,
		.display	= menu_print,
	}, */
};

static struct menu_entry cfg_menus[] = {
	{
		.display	= config_autosave_display,
		.select		= config_autosave_toggle,
		.help = "If enabled, ML settings are saved automatically at shutdown."
	},
	{
		.priv = "Save config now",
		.display	= menu_print,
		.select		= save_config,
		.help = "Save ML settings to MAGIC.CFG"
	},
	{
		.priv = "Delete config file",
		.display	= menu_print,
		.select		= delete_config,
		.help = "Use this to restore ML default settings. Restart needed."
	},
};




static void * debug_token;

static void
debug_token_handler(
	void *			token,
	void *			arg1,
	void *			arg2,
	void *			arg3
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
		unsigned x = 80;
		unsigned y = 15 + i * font_small.height;
		unsigned property = dbg_props[i];
		unsigned len = dbg_props_len[i];
		unsigned fnt = FONT_SMALL;
		if (i == changed) fnt = FONT(FONT_SMALL, 5, COLOR_BG);
		bmp_printf(fnt, x, y,
			"%08x %04x: %8lx %8lx %8lx %8lx %8lx %8lx",
			property,
			len,
			len > 0x00 ? dbg_props_a[i] : 0,
			len > 0x04 ? dbg_props_b[i] : 0,
			len > 0x08 ? dbg_props_c[i] : 0,
			len > 0x0c ? dbg_props_d[i] : 0,
			len > 0x10 ? dbg_props_e[i] : 0,
			len > 0x14 ? dbg_props_f[i] : 0
		);
	}
}


static void *
debug_property_handler(
	unsigned		property,
	void *			UNUSED( priv ),
	void *			buf,
	unsigned		len
)
{
	const uint32_t * const addr = buf;

	/*console_printf("Prop %08x: %2x: %08x %08x %08x %08x\n",
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
	return prop_cleanup( debug_token, property );
}



#define num_properties 4096
unsigned* property_list = 0;


void
debug_init( void )
{
	draw_prop = 0;

#if CONFIG_DEBUGMSG
	if (!property_list) property_list = AllocateMemory(num_properties * sizeof(unsigned));
	if (!property_list) return;
	unsigned i, j, k;
	unsigned actual_num_properties = 0;
	
	unsigned is[] = {0x80, 0xe, 0x5, 0x4, 0x2, 0x1, 0x0};
	for( i=0 ; i<COUNT(is) ; i++ )
	{
		for( j=0 ; j<=0x8 ; j++ )
		{
			for( k=0 ; k<0x40 ; k++ )
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
		(void*) 0xdeadbeef,
		debug_token_handler
	);
#endif

	menu_add( "Debug", debug_menus, COUNT(debug_menus) );
    menu_add( "Config", cfg_menus, COUNT(cfg_menus) );
}

CONFIG_INT( "debug.timed-dump",		timed_dump, 0 );

//~ CONFIG_INT( "debug.dump_prop", dump_prop, 0 );
//~ CONFIG_INT( "debug.dumpaddr", dump_addr, 0 );
//~ CONFIG_INT( "debug.dumplen", dump_len, 0 );

CONFIG_INT( "magic.disable_bootdiskf",	disable_bootdiskf, 0 );

struct bmp_file_t * logo = -1;
void load_logo()
{
	if (logo == -1) 
		logo = bmp_load("B:/logo.bmp",0);
}
void show_logo()
{
	load_logo();
	if (logo > 0)
	{
		bmp_draw(logo, 360 - logo->width/2, 240 - logo->height/2, 0, 0);
	}
	else
	{
		bmp_printf( FONT(FONT_LARGE, COLOR_WHITE, COLOR_BLACK), 200, 100,
			"Magic Lantern\n"
			"...loading...\n"
		);
	}
}

void
debug_init_stuff( void )
{
	//~ set_pic_quality(PICQ_RAW);
	config_autosave = !config_flag_file_setting_load(CONFIG_AUTOSAVE_FLAG_FILE);
	config_ok = 1;
	
	dm_update();
	restore_kelvin_wb();
	
	/*
	DEBUG();
	dm_set_store_level( DM_DISP, 7 );
	dm_set_store_level( DM_LVFD, 7 );
	dm_set_store_level( DM_LVCFG, 7 );
	dm_set_store_level( DM_LVCDEV, 7 );
	dm_set_store_level( DM_LV, 7 );
	dm_set_store_level( DM_RSC, 7 );
	dm_set_store_level( DM_MAC, 7 );
	dm_set_store_level( DM_CRP, 7 );
	dm_set_store_level( DM_SETPROP, 7 );
	dm_set_store_level( DM_PRP, 7 );
	dm_set_store_level( DM_PROPAD, 7 );
	dm_set_store_level( DM_INTCOM, 7 );
	dm_set_store_level( DM_WINSYS, 7 );
	dm_set_store_level( DM_CTRLSRV, 7 );
	dm_set_store_level( DM_GUI, 7);
	dm_set_store_level( DM_GUI_M, 7);
	dm_set_store_level( DM_GUI_E, 7);
	dm_set_store_level( DM_BIND, 7);
	dm_set_store_level( DM_DISP, 7);
	DEBUG();*/

	//~ int i;
	//~ for (i = 0; i < 256; i++)
		//~ dm_set_store_level( i, 15);
	
	//msleep(1000);
	//bmp_draw_palette();
	//dispcheck();


	unsigned x=10;
	unsigned y=32;

	if (disable_bootdiskf!=0) {
	  bmp_printf( FONT_SMALL, x, y, "**disable_bootdiskf**%s","" );
	  bootdisk_disable();
	}

	if( timed_dump == 0 )
		goto end;

	int sec = timed_dump;

	DebugMsg( DM_MAGIC, 3, "%s: Will do debug dump in %d sec",
		__func__,
		sec
	);

	while( sec-- )
	{
		//~ bmp_printf( FONT_SMALL, 600, 400, "dump %2d", sec );
		msleep( 1000 );
	}

	DebugMsg( DM_MAGIC, 3, "%s: calling dumpf", __func__ );
	dumpf();

end:
	return;
}


//~ TASK_CREATE( "dump_task", dump_task, 0, 0x1e, 0x1000 );
TASK_CREATE( "debug_loop_task", debug_loop_task, 0, 0x1e, 0x1000 );

//~ CONFIG_INT( "debug.timed-start",	timed_start, 0 );
/*
static void
movie_start( void )
{
	int sec = timed_start;
	if( sec == 0 )
		return;

	const int x = 320;
	const int y = 150;

	while( --sec > 0 )
	{
		msleep( 1000 );
		bmp_printf(
			FONT(
				FONT_LARGE,
				sec > 4 ? COLOR_WHITE : COLOR_RED,
				0
			),
			x, y,
			"T-%d",
			sec
		);
	}

	bmp_printf( FONT(FONT_LARGE,COLOR_WHITE,0), x, y, "GO!" );

	call( "MovieStart" );

	msleep( 1000 );

	bmp_printf( FONT(FONT_LARGE,COLOR_WHITE,0), x, y, "   " );
}*/

//~ TASK_CREATE( "movie_start", movie_start, 0, 0x1f, 0x1000 );


PROP_HANDLER(PROP_TERMINATE_SHUT_REQ)
{
	//bmp_printf(FONT_MED, 0, 0, "SHUT REQ %d ", buf[0]);
	if (config_autosave && buf[0] == 0 && config_ok) save_config(0);
	return prop_cleanup(token, property);
}

/*
PROP_HANDLER(PROP_APERTURE)
{
	static int old = 0;
	
	if (old && lv_drawn())
	{
		if (display_sensor)
		{
			if (buf[0] != old)
			{
				int newiso = COERCE(lens_info.raw_iso + buf[0] - old, codes_iso[1], codes_iso[COUNT(codes_iso)-1]);
				lens_set_rawiso(newiso);
			}
		}
	}

	old = buf[0];

	return prop_cleanup(token, property);
}*/

/*
PROP_HANDLER(PROP_SHUTTER)
{
	if (lv_drawn() && shooting_mode == SHOOTMODE_MOVIE)
	{
		static volatile int old = 0;
		
		if (old)
		{
			if (buf[0] != old)
			{
				//~ int newiso = COERCE(lens_info.raw_iso + buf[0] - old, codes_iso[1], codes_iso[COUNT(codes_iso)-1]);
				//~ lens_set_rawiso(newiso);
				buf[0] = old;
			}
		}
		old = buf[0];
	}
	return prop_cleanup(token, property);
}*/
