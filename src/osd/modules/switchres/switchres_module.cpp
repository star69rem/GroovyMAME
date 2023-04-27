/**************************************************************

   switchres_module.cpp - Switchres MAME module

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

// MAME headers
#include "emu.h"
#include "rendlay.h"
#include "render.h"
#include "../frontend/mame/mameopts.h"

// MAMEOS headers
#if defined(OSD_WINDOWS)
#include "winmain.h"
#elif defined(OSD_SDL)
#include "osdsdl.h"
#endif

#include "modules/osdwindow.h"
#include "modules/monitor/monitor_module.h"
#include <switchres/switchres.h>
#include "switchres_module.h"

#include <cstdarg>
#include <locale>

#define OPTION_PRIORITY_SWITCHRES OPTION_PRIORITY_MAME_INI + 1

//============================================================
//  logging wrappers
//============================================================

static void sr_printf_verbose(const char *format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	osd_vprintf_verbose(util::make_format_argument_pack(std::forward<char*>(buffer)));
	va_end(args);
}

static void sr_printf_info(const char *format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	osd_vprintf_info(util::make_format_argument_pack(std::forward<char*>(buffer)));
	va_end(args);
}

static void sr_printf_error(const char *format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	osd_vprintf_error(util::make_format_argument_pack(std::forward<char*>(buffer)));
	va_end(args);
}

//============================================================
//  switchres_module::init
//============================================================

void switchres_module::init(running_machine &machine)
{
	setlocale(LC_NUMERIC, "C");

	m_machine = &machine;
	m_switchres = new switchres_manager;

	// Set logging functions
	switchres().set_log_verbose_fn((void *)sr_printf_verbose);
	switchres().set_log_info_fn((void *)sr_printf_info);
	switchres().set_log_error_fn((void *)sr_printf_error);

	if (machine.options().verbose()) switchres().set_log_level(3);
}

//============================================================
//  switchres_module::exit
//============================================================

void switchres_module::exit()
{
	osd_printf_verbose("Switchres: exit\n");
	if (m_switchres) delete m_switchres;
	m_switchres = 0;
}

//============================================================
//  switchres_module::add_display
//============================================================

display_manager* switchres_module::add_display(int index, osd_monitor_info *monitor, osd_window_config *config)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	m_priority = OPTION_PRIORITY_DEFAULT;

	display_manager* df = switchres().display_factory();

	// Fill in SR's settings with MAME's options

	df->set_monitor(options.monitor());
	df->set_modeline(options.modeline());
	for (int i = 0; i < MAX_RANGES; i++) df->set_crt_range(i, options.crt_range(i));
	df->set_lcd_range(options.lcd_range());
	df->set_modeline_generation(options.modeline_generation());
	df->set_lock_unsupported_modes(options.lock_unsupported_modes());
	df->set_lock_system_modes(options.lock_system_modes());
	df->set_refresh_dont_care(options.refresh_dont_care());

	df->set_interlace(options.interlace());
	df->set_doublescan(options.doublescan());
	df->set_dotclock_min(options.dotclock_min());
	df->set_refresh_tolerance(options.sync_refresh_tolerance());
	df->set_super_width(options.super_width());
	df->set_h_size(options.h_size());
	df->set_h_shift(options.h_shift());
	df->set_v_shift(options.v_shift());
	df->set_v_shift_correct(options.v_shift_correct());
	df->set_pixel_precision(options.pixel_precision());
	df->set_interlace_force_even(options.interlace_force_even());

	df->set_api(options.switchres_backend());
	df->set_screen_compositing(options.screen_compositing());
	df->set_screen_reordering(options.screen_reordering());
	df->set_allow_hardware_refresh(options.allow_hw_refresh());

	modeline user_mode = {};
	user_mode.width = config->width;
	user_mode.height = config->height;
	user_mode.refresh = config->refresh;
	df->set_user_mode(&user_mode);

	// If allowed, try to parse switchres.ini, and raise our priority if found
	if (options.switchres_ini() && m_switchres->parse_config("switchres.ini"))
		m_priority = OPTION_PRIORITY_SWITCHRES;

	// Add a new display manager. This also parses its display#.ini
	display_manager *display = switchres().add_display(0);

	// If we found a display#.ini, raise our priority
	if (display->has_ini())
		m_priority = OPTION_PRIORITY_SWITCHRES;

	// Always override SR's display option with MAME's -screen option
	display->set_screen(monitor->devicename().c_str());

	// Finally, override SR's settings with MAME's options of higher priority
	if (options.get_entry(OSDOPTION_MONITOR)->priority() > m_priority) display->set_monitor(options.monitor());
	if (options.get_entry(OSDOPTION_MODELINE)->priority() > m_priority) display->set_modeline(options.modeline());
	for (int i = 0; i < MAX_RANGES; i++) if (options.get_entry(string_format("%s%d", OSDOPTION_CRT_RANGE, i).c_str())->priority() > m_priority) display->set_crt_range(i, options.crt_range(i));
	if (options.get_entry(OSDOPTION_LCD_RANGE)->priority() > m_priority) display->set_lcd_range(options.lcd_range());
	if (options.get_entry(OSDOPTION_MODELINE_GENERATION)->priority() > m_priority) display->set_modeline_generation(options.modeline_generation());
	if (options.get_entry(OSDOPTION_LOCK_UNSUPPORTED_MODES)->priority() > m_priority) display->set_lock_unsupported_modes(options.lock_unsupported_modes());
	if (options.get_entry(OSDOPTION_LOCK_SYSTEM_MODES)->priority() > m_priority) display->set_lock_system_modes(options.lock_system_modes());
	if (options.get_entry(OSDOPTION_REFRESH_DONT_CARE)->priority() > m_priority) display->set_refresh_dont_care(options.refresh_dont_care());

	if (options.get_entry(OSDOPTION_INTERLACE)->priority() > m_priority) display->set_interlace(options.interlace());
	if (options.get_entry(OSDOPTION_DOUBLESCAN)->priority() > m_priority) display->set_doublescan(options.doublescan());
	if (options.get_entry(OSDOPTION_DOTCLOCK_MIN)->priority() > m_priority) display->set_dotclock_min(options.dotclock_min());
	if (options.get_entry(OSDOPTION_SYNC_REFRESH_TOLERANCE)->priority() > m_priority) display->set_refresh_tolerance(options.sync_refresh_tolerance());
	if (options.get_entry(OSDOPTION_SUPER_WIDTH)->priority() > m_priority) display->set_super_width(options.super_width());
	if (options.get_entry(OSDOPTION_H_SIZE)->priority() > m_priority) display->set_h_size(options.h_size());
	if (options.get_entry(OSDOPTION_H_SHIFT)->priority() > m_priority) display->set_h_shift(options.h_shift());
	if (options.get_entry(OSDOPTION_V_SHIFT)->priority() > m_priority) display->set_v_shift(options.v_shift());
	if (options.get_entry(OSDOPTION_V_SHIFT_CORRECT)->priority() > m_priority) display->set_v_shift_correct(options.v_shift_correct());
	if (options.get_entry(OSDOPTION_PIXEL_PRECISION)->priority() > m_priority) display->set_pixel_precision(options.pixel_precision());
	if (options.get_entry(OSDOPTION_INTERLACE_FORCE_EVEN)->priority() > m_priority) display->set_interlace_force_even(options.interlace_force_even());

	if (options.get_entry(OSDOPTION_SWITCHRES_BACKEND)->priority() > m_priority) display->set_api(options.switchres_backend());
	if (options.get_entry(OSDOPTION_SCREEN_COMPOSITING)->priority() > m_priority) display->set_screen_compositing(options.screen_compositing());
	if (options.get_entry(OSDOPTION_SCREEN_REORDERING)->priority() > m_priority) display->set_screen_reordering(options.screen_reordering());
	if (options.get_entry(OSDOPTION_ALLOW_HW_REFRESH)->priority() > m_priority) display->set_allow_hardware_refresh(options.allow_hw_refresh());

	if ((options.get_entry(OSDOPTION_RESOLUTION)->priority() > m_priority) ||
		(options.get_entry(string_format("%s%d", OSDOPTION_RESOLUTION, index).c_str())->priority() > m_priority))
	{
		user_mode.width = config->width;
		user_mode.height = config->height;
		user_mode.refresh = config->refresh;
		display->set_user_mode(&user_mode);
	}

	// Parse options now
	display->parse_options();

	m_num_screens ++;
	return display;
}

//============================================================
//  switchres_module::init_display
//============================================================

bool switchres_module::init_display(int index, osd_monitor_info *monitor, osd_window_config *config, render_target *target, void* pf_data)
{
	display_manager *display = switchres().display(index);

	if (!display)
		return false;

	// Initialize the display manager
	if (!display->init(pf_data))
		return false;

	display->set_monitor_aspect(display->desktop_is_rotated()? 1.0f / monitor->aspect() : monitor->aspect());
	get_game_info(display, target);

	osd_printf_verbose("Switchres: get_mode(%d) %d %d %f %f\n", index, width(index), height(index), refresh(index), display->monitor_aspect());
	display->get_mode(width(index), height(index), refresh(index), rotation(index)? SR_MODE_ROTATED : 0);
	if (display->got_mode()) set_mode(index, monitor, target, config);

	return true;
}

//============================================================
//  switchres_module::delete_display
//============================================================

void switchres_module::delete_display(int index)
{
	if (switchres().displays[index])
	{
		delete switchres().displays[index];
		switchres().displays[index] = nullptr;
	}
}

//============================================================
//  switchres_module::get_game_info
//============================================================

void switchres_module::get_game_info(display_manager* display, render_target *target)
{
	bool rotation = effective_orientation(display, target);
	set_rotation(display->index(), rotation);

	int minwidth, minheight;
	target->compute_minimum_size(minwidth, minheight);

	if (rotation ^ display->desktop_is_rotated()) std::swap(minwidth, minheight);
	set_width(display->index(), minwidth);
	set_height(display->index(), minheight);

	// determine the refresh rate of the primary screen
	const screen_device *primary_screen = screen_device_enumerator(machine().root_device()).first();
	if (primary_screen != nullptr) set_refresh(display->index(), primary_screen->frame_number() == 0? ATTOSECONDS_TO_HZ(primary_screen->refresh_attoseconds()) : primary_screen->frame_period().as_hz());
}

//============================================================
//  switchres_module::effective_orientation
//============================================================

bool switchres_module::effective_orientation(display_manager* display, render_target *target)
{

	bool target_is_rotated = (target->orientation() & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY? true:false;
	bool game_is_rotated = (machine().system().flags & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY;

	return target_is_rotated ^ game_is_rotated ^ display->desktop_is_rotated();
}

//============================================================
//  switchres_module::check_resolution_change
//============================================================

bool switchres_module::check_resolution_change(int i, osd_monitor_info *monitor, render_target *target, osd_window_config *config)
{
	display_manager *display = switchres().display(i);

	int old_width = width(i);
	int old_height = height(i);
	double old_refresh = refresh(i);
	bool old_rotation = rotation(i);

	get_game_info(display, target);

	if (old_width != width(i) || old_height != height(i) || old_refresh != refresh(i) || old_rotation != rotation(i))
	{
		osd_printf_verbose("Switchres: Resolution change from %dx%d@%f %s to %dx%d@%f %s\n",
			old_width, old_height, old_refresh, old_rotation?"rotated":"normal", width(i), height(i), refresh(i), rotation(i)?"rotated":"normal");

		display->get_mode(width(i), height(i), refresh(i), rotation(i)? SR_MODE_ROTATED : 0);

		if (display->got_mode())
		{
			if (display->is_switching_required())
			{
				set_mode(i, monitor, target, config);
				return true;
			}

			set_options(display, target);
		}
	}

	return false;
}

//============================================================
//  switchres_module::set_mode
//============================================================

bool switchres_module::set_mode(int i, osd_monitor_info *monitor, render_target *target, osd_window_config *config)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	display_manager *display = switchres().display(i);

	if (display->got_mode())
	{
		if (display->is_mode_updated()) display->update_mode(display->selected_mode());

		else if (display->is_mode_new()) display->add_mode(display->selected_mode());

		config->width = display->width();
		config->height = display->height();
		config->refresh = display->refresh();

		if (options.mode_setting())
		{
			display->set_mode(display->selected_mode());
			monitor->refresh();
			monitor->update_resolution(display->width(), display->height());
		}

		set_options(display, target);

		return true;
	}

	return false;
}

//============================================================
//  switchres_module::check_geometry_change
//============================================================

bool switchres_module::check_geometry_change(int i)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	display_manager *display = switchres().display(i);

	if (options.h_size() != display->h_size() || options.h_shift() != display->h_shift() || options.v_shift() != display->v_shift())
		return true;

	return false;
}

//============================================================
//  switchres_module::adjust_mode
//============================================================

bool switchres_module::adjust_mode(int i)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	display_manager *display = switchres().display(i);

	display->set_h_size(options.h_size());
	display->set_h_shift(options.h_shift());
	display->set_v_shift(options.v_shift());

	display->get_mode(width(i), height(i), refresh(i), 0);
	if (display->got_mode())
	{
		if (display->is_mode_updated()) display->update_mode(display->selected_mode());

		else if (display->is_mode_new()) display->add_mode(display->selected_mode());

		if (options.mode_setting())
			display->set_mode(display->selected_mode());

		options.set_value(OSDOPTION_H_SIZE, (float)display->h_size(), OPTION_PRIORITY_CMDLINE);
		options.set_value(OSDOPTION_H_SHIFT, display->h_shift(), OPTION_PRIORITY_CMDLINE);
		options.set_value(OSDOPTION_V_SHIFT, display->v_shift(), OPTION_PRIORITY_CMDLINE);
	}

	return true;
}

//============================================================
//  switchres_module::set_options
//============================================================

void switchres_module::set_options(display_manager* display, render_target *target)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	// Set scaling/stretching options

	if (options.autostretch())
	{
		bool is_super_resolution = !(display->is_stretched()) && (display->width() >= display->super_width());

		bool target_is_rotated = (target->orientation() & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY? true : false;
		float target_aspect = target->current_view().effective_aspect();
		if (target_is_rotated) target_aspect = 1.0f / target_aspect;
		bool force_aspect = (target_aspect != display->monitor_aspect());

		set_option(OPTION_KEEPASPECT, force_aspect);
		set_option(OPTION_UNEVENSTRETCH, display->is_stretched());
		set_option(OPTION_UNEVENSTRETCHX, is_super_resolution);

		// Update target if it's already initialized
		if (target)
		{
			target->set_keepaspect(options.keep_aspect());

			if (options.uneven_stretch())
				target->set_scale_mode(SCALE_FRACTIONAL);
			else if(options.uneven_stretch_x())
				target->set_scale_mode(SCALE_FRACTIONAL_X);
			else if(options.uneven_stretch_y())
				target->set_scale_mode(SCALE_FRACTIONAL_Y);
			else
				target->set_scale_mode(SCALE_INTEGER);
		}
	}

	// Set MAME OSD specific options

	// Vertical synchronization management (autosync)
	// Disable -syncrefresh if our vfreq is scaled or out of syncrefresh_tolerance
	if (options.autosync())
	{
		bool sync_refresh_effective = (options.black_frame_insertion() > 0) || !((display->is_refresh_off()) || display->v_scale() > 1);
	#if defined(OSD_WINDOWS)
		set_option(OSDOPTION_WAITVSYNC, true);
	#elif defined(OSD_SDL)
		set_option(OSDOPTION_WAITVSYNC, sync_refresh_effective);
	#endif
		set_option(OPTION_SYNCREFRESH, sync_refresh_effective);
	}

	// Set filter options
	if (options.autofilter())
		set_option(OSDOPTION_FILTER, (display->is_stretched() || display->is_interlaced()));

	#if defined(OSD_WINDOWS)
		downcast<windows_osd_interface &>(machine().osd()).extract_video_config();
	#elif defined(OSD_SDL)
		downcast<sdl_osd_interface &>(machine().osd()).extract_video_config();
	#endif
}

//============================================================
//  switchres_module::set_option - option setting wrapper
//============================================================

void switchres_module::set_option(const char *option_ID, bool state)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	//options.set_value(option_ID, state, OPTION_PRIORITY_SWITCHRES);
	options.set_value(option_ID, state, OPTION_PRIORITY_SWITCHRES);
	osd_printf_verbose("SwitchRes: Setting option -%s%s\n", options.bool_value(option_ID)?"":"no", option_ID);
}

//============================================================
//  switchres_module::mode_to_txt
//============================================================

const char *switchres_module::display_mode_to_txt(int i)
{
	if (!downcast<osd_options &>(machine().options()).switch_res())
		return "Switchres is disabled\n";

	display_manager *display = switchres().display(i);

	if (display == nullptr)
		sprintf(m_mode_txt, "SR(%d): no physical display\n", i);

	else if (display->got_mode())
		sprintf(m_mode_txt, "SR(%d): %d x %d%s%s %2.3f Hz %2.3f kHz\n",
				i, display->width(), display->height(), display->is_interlaced()?"i":"p", display->is_doublescanned()?"d":"", display->v_freq(), display->h_freq()/1000);
	else
		sprintf(m_mode_txt, "SR(%d): could not find a video mode\n", i);

	return m_mode_txt;
}
