
/* Simple module to force either 480p or 1080i output by patching ROM instructions.
 * This method is more reliable compared to prop_request_change in FEATURE_FORCE_HDMI_VGA.
 * FEATURE_FORCE_HDMI_VGA doesn't work on some models:
 * https://www.magiclantern.fm/forum/index.php?topic=26108.0                               */

#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <config.h>
#include <patch.h>

static int is_digic4 = 0;
static int is_digic5 = 0;

static CONFIG_INT("hdmi.patch.enabled", hdmi_patch_enabled, 0);
static CONFIG_INT("hdmi.output_resolution", output_resolution, 0);

static int Output_480p_patched  = 0;
static int Output_1080i_patched = 0;
static uint32_t Output_480p_Force_Address  = 0;
static uint32_t Output_1080i_Force_Address = 0;

/* These appear to hold the selected HDMI configuration before applying it
 * we can detect LCD, 480p or 1080i outputs
 *
 * This LOG from 700D when connecting HDMI to 480p ouput:
 *DisplayMgr:ff330104:88:16: [EDID] dwVideoCode = 2
 *DisplayMgr:ff330118:88:16: [EDID] dwHsize = 720
 *DisplayMgr:ff33012c:88:16: [EDID] dwVsize = 480
 *DisplayMgr:ff330148:88:16: [EDID] ScaningMode = EDID_NON_INTERLACE(p)
 *DisplayMgr:ff330194:88:16: [EDID] VerticalFreq = EDID_FREQ_60Hz
 *DisplayMgr:ff3301b0:88:16: [EDID] AspectRatio = EDID_ASPECT_4x3
 *DisplayMgr:ff3301cc:88:16: [EDID] AudioMode = EDID_AUDIO_LINEAR_PCM
 *DisplayMgr:ff331580:88:16: [EDID] ColorMode = EDID_COLOR_RGB */
const struct EDID_HDMI_INFO
{
    uint32_t dwVideoCode;   /* 0 LCD, 2 480p, 5 1080i */
    uint32_t dwHsize;       /* LCD = 0, 480p = 720, 1080i = 1920 */
    uint32_t dwVsize;       /* LCD = 0, 480p = 480, 1080i = 1080 */
    uint32_t ScaningMode;   /* 0 = EDID_NON_INTERLACE(p), 1 = EDID_INTERLACE(i) */
    uint32_t VerticalFreq;
    uint32_t AspectRatio;   /* 0 = EDID_ASPECT_4x3, 1 = EDID_ASPECT_16x9 */
    uint32_t AudioMode;
    uint32_t ColorMode;
} * EDID_HDMI_INFO = 0;

void patch_HDMI_output()
{
    if (output_resolution == 0) // 480p
    {
        if (!Output_480p_patched)
        {
            patch_instruction(Output_480p_Force_Address, 0xe3500005, 0xe3500002,"480p");
            Output_480p_patched = 1;
        }
    }

    if (output_resolution == 1) // 1080i
    {
        if (!Output_1080i_patched)
        {
            patch_instruction(Output_1080i_Force_Address, 0xe3a00002, 0xe3a00005,"1080i");
            Output_1080i_patched = 1;
        }
    }
}

void unpatch_HDMI_output()
{
    if (Output_1080i_patched)
    {
        unpatch_memory(Output_1080i_Force_Address);
        Output_1080i_patched = 0;
    }
    if (Output_480p_patched)
    {
        unpatch_memory(Output_480p_Force_Address);
        Output_480p_patched = 0;
    }
}

static void hdmi_output_toggle(void* priv, int sign)
{
    if (hdmi_patch_enabled) 
    {
        hdmi_patch_enabled = 0;
        unpatch_HDMI_output();
    }
    else
    {
        hdmi_patch_enabled = 1;
        patch_HDMI_output();
    }
}

static void hdmi_output_resolution_toggle(void* priv, int sign)
{
    if (hdmi_patch_enabled) 
    {
        if      (output_resolution == 0) output_resolution = 1;
        else if (output_resolution == 1) output_resolution = 0;
        unpatch_HDMI_output();
        patch_HDMI_output();
    }
}

static MENU_UPDATE_FUNC(hdmi_update)
{
    if (EDID_HDMI_INFO->dwVideoCode == 0) // LCD, HDMI isn't connected
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "HDMI isn't connected.");
    }

    if (EDID_HDMI_INFO->dwVideoCode != 0) // Not LCD, HDMI is connected
    {
        if ((Output_480p_patched  && EDID_HDMI_INFO->dwVideoCode != 2) ||
            (Output_1080i_patched && EDID_HDMI_INFO->dwVideoCode != 5)  )
        {
            MENU_SET_WARNING(MENU_WARN_ADVICE, "Reconnect HDMI cable or restart camera to apply output setting.");
        }
    }
}

static MENU_UPDATE_FUNC(output_resolution_update)
{
    if (EDID_HDMI_INFO->dwVideoCode != 0) // Not LCD, HDMI is connected
    {
        if ((Output_480p_patched  && EDID_HDMI_INFO->dwVideoCode != 2) ||
            (Output_1080i_patched && EDID_HDMI_INFO->dwVideoCode != 5)  )
        {
            MENU_SET_WARNING(MENU_WARN_ADVICE, "Reconnect HDMI cable or restart camera to apply output setting.");
        }
    }
}

static struct menu_entry hdmi_out_menu[] =
{
    {
        .name       = "HDMI output",
        .select     = hdmi_output_toggle,
        .update     = hdmi_update,
        .max        = 1,
        .priv       = &hdmi_patch_enabled,
        .help       = "Change HDMI output settings.",
        .children =  (struct menu_entry[]) {
            {
                .name       = "Output resolution",
                .select     = hdmi_output_resolution_toggle,
                .update     = output_resolution_update,
                .choices    = CHOICES("480p", "1080i"),
                .max        = 1,
                .priv       = &output_resolution,
                .help       = "Select an output resolution for HDMI displays.",
                .help2      = "480p: 720x480 output.\n"
                              "1080i: 1920x1080i output.",
            },
            MENU_EOL,
        },
    },
};

static unsigned int hdmi_out_init()
{    
    if (is_camera("700D", "1.1.5"))
    {
        Output_480p_Force_Address = 0xFF33154C;
        Output_1080i_Force_Address = 0xFF33157C;
        EDID_HDMI_INFO = (void *) 0x648B0;
        is_digic5 = 1;
    }
    
    else if (is_camera("650D", "1.0.4"))
    {
        Output_480p_Force_Address = 0xFF32EB28;
        Output_1080i_Force_Address = 0xFF32EB58;
        EDID_HDMI_INFO = (void *) 0x63F7C;
        is_digic5 = 1;
    }
    
    else if (is_camera("600D", "1.0.2"))
    {
        Output_480p_Force_Address = 0xFF1EE158;
        Output_1080i_Force_Address = 0xFF1EE188;
        EDID_HDMI_INFO = (void *) 0x2C4C0; 
        is_digic4 = 1;
    }

    else if (is_camera("550D", "1.0.9"))
    {
        Output_480p_Force_Address = 0xFF1CDF8C;
        Output_1080i_Force_Address = 0xFF1CDFBC;
        EDID_HDMI_INFO = (void *) 0x3BC60; 
        is_digic4 = 1;
    }

    else if (is_camera("100D", "1.0.1"))
    {
        Output_480p_Force_Address = 0xFF324798;
        Output_1080i_Force_Address = 0xFF3247C8;
        EDID_HDMI_INFO = (void *) 0xA3C0C;
        is_digic5 = 1;
    }
    
    else if (is_camera("EOSM", "2.0.2"))
    {
        Output_480p_Force_Address = 0xFF33298C;
        Output_1080i_Force_Address = 0xFF3329BC;
        EDID_HDMI_INFO = (void *) 0x821CC;
        is_digic5 = 1;
    }
    
    else if (is_camera("6D", "1.1.6"))
    {
        Output_480p_Force_Address = 0xFF32340C;
        Output_1080i_Force_Address = 0xFF32343C;
        EDID_HDMI_INFO = (void *) 0xAECA4;
        is_digic5 = 1;
    }
    
    else if (is_camera("5D2", "2.1.2"))
    {
        Output_480p_Force_Address = 0xFF1B1500;
        Output_1080i_Force_Address = 0xFF1B1530;
        EDID_HDMI_INFO = (void *) 0x34384;
        is_digic4 = 1;
    }
    
    else if (is_camera("5D3", "1.1.3"))
    {
        Output_480p_Force_Address = 0xFF2F8750;
        Output_1080i_Force_Address = 0xFF2F8780;
        EDID_HDMI_INFO = (void *) 0x5198C; 
        is_digic5 = 1;
    }

    else if (is_camera("60D", "1.1.1"))
    {
        Output_480p_Force_Address = 0xFF1D1CE8;
        Output_1080i_Force_Address = 0xFF1D1D18;
        EDID_HDMI_INFO = (void *) 0x49D08;
        is_digic4 = 1;
    }

    else if (is_camera("70D", "1.1.2"))
    {
        Output_480p_Force_Address = 0xFF337AD0;
        Output_1080i_Force_Address = 0xFF337B00;
        EDID_HDMI_INFO = (void *) 0xD2264; 
        is_digic5 = 1;
    }

    if (Output_480p_Force_Address || Output_1080i_Force_Address)
    {
        menu_add("Display", hdmi_out_menu, COUNT(hdmi_out_menu));
    }
    else
    {
        hdmi_patch_enabled = 0;
        return 1;
    }

    /* patch on startup if "HDMI output" was enabled */
    if (hdmi_patch_enabled)
    {
        if (!Output_480p_patched || !Output_1080i_patched)
        {
            unpatch_HDMI_output();
            patch_HDMI_output();
        }
    }

    return 0;
}

static unsigned int hdmi_out_deinit()
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(hdmi_out_init)
    MODULE_DEINIT(hdmi_out_init)
MODULE_INFO_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(hdmi_patch_enabled)
    MODULE_CONFIG(output_resolution)
MODULE_CONFIGS_END()