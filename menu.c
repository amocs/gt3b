/*
    menu - handle showing to display, menus
    Copyright (C) 2011 Pavel Semerad

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include <string.h>
#include "menu.h"
#include "config.h"
#include "calc.h"
#include "timer.h"
#include "ppm.h"
#include "lcd.h"
#include "buzzer.h"
#include "input.h"




#define TRIM_MAX  99
#define SUBTRIM_MAX 99





// variables to be used in CALC task
_Bool ch3_state;		// state of channel 3 button





// flags for wakeup after each ADC measure
_Bool menu_wants_adc;
// battery low flag
_Bool menu_battery_low;
// raw battery ADC value for check to battery low
u16 battery_low_raw;








// ****************** UTILITY FUNCTIONS ******************************

// load model config from eeprom and set model settings
static void load_model(void) {
    config_model_read();

    ppm_set_channels(cm.channels);
    ch3_state = 0;
}


// apply global setting to libraries
void apply_global_config(void) {
    button_autorepeat(cg.autorepeat);
    backlight_set_default(cg.backlight_time);
    backlight_on();
    // compute raw value for battery low voltage
    battery_low_raw = (u16)(((u32)cg.battery_calib * cg.battery_low + 50) / 100);
}


// show model number, extra function to handle more than 10 models
static void show_model_number(u8 model) {
    lcd_7seg((u8)(model % 10));
    if (model >= 40) {
	// too much, set blinking arrows
	lcd_segment(LS_SYM_RIGHT, LS_ON);
	lcd_segment(LS_SYM_LEFT,  LS_ON);
	lcd_segment_blink(LS_SYM_RIGHT, LB_SPC);
	lcd_segment_blink(LS_SYM_LEFT,  LB_SPC);
    }
    else {
	lcd_segment(LS_SYM_RIGHT, (u8)((u8)(model / 10) & 1));
	lcd_segment(LS_SYM_LEFT, (u8)((u8)(model / 20) & 1));
    }
}


// menu stop - checks low battery
static void menu_stop(void) {
    static _Bool battery_low_on;
    stop();
    if (battery_low_on == menu_battery_low)  return;  // no change

    // battery low status changed
    if (menu_battery_low) {
	// battery low firstly
	battery_low_on = 1;
	lcd_segment(LS_SYM_LOWPWR, LS_ON);
	lcd_segment_blink(LS_SYM_LOWPWR, LB_SPC);
	buzzer_on(40, 160, BUZZER_MAX);
    }
    else {
	// battery low now OK
	battery_low_on = 0;
	lcd_segment(LS_SYM_LOWPWR, LS_OFF);
	buzzer_off();
    }
    lcd_update();
}





// show main screen (model number and name/battery/...)
#define MS_NAME		0
#define MS_BATTERY	1
#define MS_MAX		2
static void main_screen(u8 item) {
    lcd_segment(LS_SYM_MODELNO, LS_ON);
    if (ch3_state)
	lcd_segment_blink(LS_SYM_MODELNO, LB_SPC);
    lcd_segment(LS_SYM_CHANNEL, LS_OFF);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
    show_model_number(cg.model);

    menu_wants_adc = 0;

    // chars is item dependent
    if (item == MS_NAME) {
	// model name
	lcd_segment(LS_SYM_DOT, LS_OFF);
	lcd_segment(LS_SYM_VOLTS, LS_OFF);
	lcd_chars(cm.name);
    }
    else if (item == MS_BATTERY) {
	static u16 bat_val = 0;
	static u16 bat_time = 0;

	// battery voltage
	lcd_segment(LS_SYM_DOT, LS_ON);
	lcd_segment(LS_SYM_VOLTS, LS_ON);
	// calculate voltage from current raw value and calib value
	if (time_sec >= bat_time) {
	    bat_time = time_sec + 2;
	    bat_val = (u16)(((u32)adc_battery * 100 + 300) / cg.battery_calib);
	}
	lcd_char_num3(bat_val);
	menu_wants_adc = 1;
    }
    lcd_update();
}




// common menu processing, selecting channel and then changing values
static _Bool menu_adc_direction;
static void menu_set_adc_direction(u8 channel) {
    u16 adc, calm;

    // for channel 2 use throttle, for others steering
    if (channel == 2) {
	adc = adc_throttle_ovs;
	calm = cg.calib_throttle_mid;
    }
    else {
	adc = adc_steering_ovs;
	calm = cg.calib_steering_mid;
    }
    // if over threshold to one side, set menu_adc_direction
    if (adc < ((calm - 40) << ADC_OVS_SHIFT)) {
	if (menu_adc_direction) {
	    menu_adc_direction = 0;
	    lcd_segment(LS_SYM_LEFT, LS_ON);
	    lcd_segment(LS_SYM_RIGHT, LS_OFF);
	}
    }
    else if (adc > ((calm + 40) << ADC_OVS_SHIFT)) {
	if (!menu_adc_direction) {
	    menu_adc_direction = 1;
	    lcd_segment(LS_SYM_LEFT, LS_OFF);
	    lcd_segment(LS_SYM_RIGHT, LS_ON);
	}
    }
}
static _Bool menu_set_adc(u8 channel, u8 use_adc) {
    if ((u8)(use_adc & (1 << (channel - 1)))) {
	// use ADC
	if (menu_adc_direction) {
	    lcd_segment(LS_SYM_LEFT, LS_OFF);
	    lcd_segment(LS_SYM_RIGHT, LS_ON);
	}
	else {
	    lcd_segment(LS_SYM_LEFT, LS_ON);
	    lcd_segment(LS_SYM_RIGHT, LS_OFF);
	}
	menu_wants_adc = 1;
	menu_set_adc_direction(channel);
	return 1;
    }
    else {
	// don't use ADC
	lcd_segment(LS_SYM_LEFT,    LS_OFF);
	lcd_segment(LS_SYM_RIGHT,   LS_OFF);
	menu_wants_adc = 0;
	return 0;
    }
}
static void menu_channel(u8 end_channel, u8 use_adc, void (*subfunc)(u8, u8)) {
    u8 channel = 1;
    _Bool chan_val = 0;			// now in channel
    _Bool adc_active;
    u8 last_direction;

    // show CHANNEL
    lcd_segment(LS_SYM_MODELNO, LS_OFF);
    lcd_segment(LS_SYM_CHANNEL, LS_ON);

    // show channel number and possible direction
    menu_adc_direction = 0;
    lcd_7seg(channel);
    lcd_set_blink(L7SEG, LB_SPC);
    adc_active = menu_set_adc(channel, use_adc);
    subfunc((u8)(channel - 1), 0);	// show current value
    lcd_update();

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_BACK | BTN_ENTER))  break;

	last_direction = menu_adc_direction;
	if (adc_active)  menu_set_adc_direction(channel);

	if (btn(BTN_ROT_ALL)) {
	    if (chan_val) {
		// change value
		subfunc((u8)(channel - 1), 1);
		lcd_chars_blink(LB_SPC);
	    }
	    else {
		// change channel number
		if (btn(BTN_ROT_L)) {
		    if (!--channel)  channel = end_channel;
		}
		else {
		    if (++channel > end_channel)  channel = 1;
		}
		lcd_7seg(channel);
		lcd_set_blink(L7SEG, LB_SPC);
		adc_active = menu_set_adc(channel, use_adc);
		subfunc((u8)(channel - 1), 0);
	    }
	    lcd_update();
	    last_direction = menu_adc_direction;  // was already showed
	}

	else if (btn(BTN_END)) {
	    // switch channel/value
	    if (chan_val) {
		// switch to channel number
		lcd_set_blink(L7SEG, LB_SPC);
		lcd_chars_blink(LB_OFF);
		chan_val = 0;
	    }
	    else {
		// switch to value
		lcd_set_blink(L7SEG, LB_OFF);
		lcd_chars_blink(LB_SPC);
		chan_val = 1;
	    }
	}

	if (last_direction != menu_adc_direction) {
	    // show other dir value
	    subfunc((u8)(channel - 1), 0);
	    lcd_update();
	    if (chan_val) {
		lcd_chars_blink(LB_SPC);
	    }
	}
    }

    menu_wants_adc = 0;
    key_beep();
    config_model_save();
}


// change value 
s16 menu_change_val(s16 val, s16 min, s16 max, u8 amount_fast, u8 rotate) {
    u8 amount = 1;

    if (btn(BTN_ROT_L)) {
	// left
	if (btnl(BTN_ROT_L))  amount = amount_fast;
	val -= amount;
	if (val < min)
	    if (rotate)	 val = max;
	    else         val = min;
    }
    else {
	// right
	if (btnl(BTN_ROT_R))  amount = amount_fast;
	val += amount;
	if (val > max)
	    if (rotate)  val = min;
	    else         val = max;
    }
    return val;
}


// temporary show trim/dualrate value
// if another key pressed, return
static void trim_dualrate(u8 menu, u8 channel, s8 *val, u16 btn_l, u16 btn_r,
                          s8 min, s8 max, u8 step, u8 *labels) {
    u16 to_time;

    // show MENU and CHANNEL
    lcd_segment(menu, LS_ON);
    lcd_segment(LS_SYM_MODELNO, LS_OFF);
    lcd_segment(LS_SYM_DOT, LS_OFF);
    lcd_segment(LS_SYM_VOLTS, LS_OFF);
    lcd_segment(LS_SYM_CHANNEL, LS_ON);
    lcd_7seg(channel);

    while (1) {
	// check value left/right
	if (btnl_all(btn_l | btn_r)) {
	    // reset to 0
	    key_beep();
	    *val = 0;
	    btnr(btn_l | btn_r);
	}
	else if (btn(btn_l | btn_r)) {
	    key_beep();
	    if (btn(btn_l)) {
		*val -= step;
		if (*val < min)  *val = min;
	    }
	    else {
		*val += step;
		if (*val > max)  *val = max;
	    }
	    btnr_nolong(btn_l | btn_r);  // waiting for possible 0-set
	}

	// show current value
	if (labels)  lcd_char_num2_lbl(*val, labels);
	else         lcd_char_num3(*val);
	lcd_update();

	// if another button was pressed, leave this screen
	if (buttons)  break;

	// sleep 5s, and if no button was pressed during, end this screen
	to_time = time_sec + 5;
	while (time_sec < to_time && !buttons)
	    delay_menu((to_time - time_sec) * 200);

	if (!buttons)  break;
    }

    btnr(btn_l | btn_r);  // reset also long values

    // set selected MENU off
    lcd_segment(menu, LS_OFF);

    // save model config
    config_model_save();
}







// *************************** MODEL MENUS *******************************

// select model/save model as (to selected model position)
static void menu_model(u8 saveas) {
    s8 model = (s8)cg.model;
    u8 amount;

    if (saveas)  lcd_set_blink(LMENU, LB_SPC);
    lcd_set_blink(L7SEG, LB_SPC);

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_ENTER | BTN_BACK))  break;
	if (btn(BTN_ROT_ALL)) {
	    amount = 1;
	    if (btn(BTN_ROT_L)) {
		if (btnl(BTN_ROT_L))  amount = 2;
		model -= amount;
		if (model < 0)  model += CONFIG_MODEL_MAX;
	    }
	    else {
		if (btnl(BTN_ROT_R))  amount = 2;
		model += amount;
		if (model >= CONFIG_MODEL_MAX)  model -= CONFIG_MODEL_MAX;
	    }
	    show_model_number(model);
	    lcd_set_blink(L7SEG, LB_SPC);
	    lcd_chars(config_model_name(model));
	    lcd_update();
	}
    }

    key_beep();
    // if new model choosed, save it
    if ((u8)model != cg.model) {
	cg.model = (u8)model;
	config_global_save();
	if (saveas) {
	    // save to new model position
	    config_model_save();
	}
	else {
	    // load selected model
	    load_model();
	    awake(CALC);	// must be awaked to do first PPM calc
	}
    }
    if (saveas)  lcd_set_blink(LMENU, LB_OFF);
}


// change model name
static void menu_name(void) {
    u8 pos = LCHR1;
    u8 letter = cm.name[0];

    lcd_set_blink(LCHR1, LB_SPC);

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_ENTER | BTN_BACK))  break;
	if (btn(BTN_END)) {
	    key_beep();
	    // to next char
	    lcd_set_blink(pos, LB_OFF);
	    if (++pos > LCHR3)  pos = LCHR1;
	    lcd_set_blink(pos, LB_SPC);
	    lcd_update();
	    letter = cm.name[pos];
	}
	else if (btn(BTN_ROT_ALL)) {
	    // change letter
	    if (btn(BTN_ROT_L)) {
		// lower
		if (letter == '0')      letter = 'Z';
		else if (letter == 'A')	letter = '9';
		else                    letter--;
	    }
	    else {
		// upper
		if (letter == '9')      letter = 'A';
		else if (letter == 'Z')	letter = '0';
		else                    letter++;
	    }
	    cm.name[pos] = letter;
	    lcd_char(pos, letter);
	    lcd_set_blink(pos, LB_SPC);
	    lcd_update();
	}
    }

    key_beep();
    config_model_save();
}


// reset model to default values
static void menu_reset_model(void) {
    config_model_set_default();
    config_model_save();
}


// set reverse
void sf_reverse(u8 channel, u8 change) {
    u8 bit = (u8)(1 << channel);
    if (change)  cm.reverse ^= bit;
    if (cm.reverse & bit)  lcd_chars("REV");
    else                   lcd_chars("NOR");
}
@inline static void menu_reverse(void) {
    menu_channel(MAX_CHANNELS, 0, sf_reverse);
}


// set endpoints
void sf_endpoint(u8 channel, u8 change) {
    u8 *addr = &cm.endpoint[channel][menu_adc_direction];
    if (change)  *addr = (u8)menu_change_val(*addr, 0, cg.endpoint_max, 5, 0);
    lcd_char_num3(*addr);
}
static void menu_endpoint(void) {
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(MAX_CHANNELS, 0xff, sf_endpoint);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
}


// set trims
static void sf_trim(u8 channel, u8 change) {
    s8 *addr = &cm.trim[channel];
    if (change)  *addr = (s8)menu_change_val(*addr, -TRIM_MAX, TRIM_MAX, 5, 0);
    if (channel == 0)  lcd_char_num2_lbl(*addr, "LNR");
    else               lcd_char_num2_lbl(*addr, "FNB");
}
@inline static void menu_trim(void) {
    menu_channel(2, 0, sf_trim);
}


// set subtrims
static void sf_subtrim(u8 channel, u8 change) {
    s8 *addr = &cm.subtrim[channel];
    if (change)
	*addr = (s8)menu_change_val(*addr, -SUBTRIM_MAX, SUBTRIM_MAX, 5, 0);
    lcd_char_num2(*addr);
}
static void menu_subtrim(void) {
    lcd_set_blink(LMENU, LB_SPC);
    menu_channel(MAX_CHANNELS, 0, sf_subtrim);
    lcd_set_blink(LMENU, LB_OFF);
}


// set dualrate
static void sf_dualrate(u8 channel, u8 change) {
    u8 *addr = &cm.dualrate[channel];
    if (channel == 1 && menu_adc_direction)  addr = &cm.dualrate[2];
    if (change)  *addr = (u8)menu_change_val(*addr, 0, 100, 5, 0);
    lcd_char_num3(*addr);
}
static void menu_dualrate(void) {
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(2, 0x2, sf_dualrate);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
}


// set expos
static void sf_expo(u8 channel, u8 change) {
    s8 *addr = &cm.expo[channel];
    if (channel == 1 && menu_adc_direction)  addr = &cm.expo[2];
    if (change)  *addr = (s8)menu_change_val(*addr, -99, 99, 5, 0);
    lcd_char_num2(*addr);
}
static void menu_expo(void) {
    lcd_segment(LS_SYM_PERCENT, LS_ON);
    menu_channel(2, 0x2, sf_expo);
    lcd_segment(LS_SYM_PERCENT, LS_OFF);
}


// set abs: OFF, SLO(6), NOR(4), FAS(3)
// pulses between full brake and 1/2 brake and only when enought brake applied
static const u8 *abs_labels[] = {
    "OFF", "SLO", "NOR", "FAS"
};
#define ABS_LABEL_SIZE  (sizeof(abs_labels) / sizeof(u8 *))
static void menu_abs(void) {
    lcd_segment(LS_SYM_MODELNO, LS_OFF);
    lcd_segment(LS_SYM_LEFT, LS_OFF);
    lcd_segment(LS_SYM_RIGHT, LS_OFF);
    lcd_segment(LS_SYM_CHANNEL, LS_ON);

    lcd_7seg(2);
    lcd_chars(abs_labels[cm.abs_type]);
    lcd_chars_blink(LB_SPC);
    lcd_update();

    while (1) {
	btnra();
	menu_stop();

	if (btn(BTN_BACK | BTN_ENTER))  break;

	if (btn(BTN_ROT_ALL)) {
	    cm.abs_type = (u8)menu_change_val(cm.abs_type, 0, ABS_LABEL_SIZE-1,
	                                      1, 1);
	    lcd_chars(abs_labels[cm.abs_type]);
	    lcd_chars_blink(LB_SPC);
	    lcd_update();
	}
    }

    key_beep();
    config_model_save();
}




// choose from menu items
static void select_menu(void) {
    u8 menu = LM_MODEL;
    lcd_menu(menu);
    main_screen(MS_NAME);	// show model number and name

    while (1) {
	btnra();
	menu_stop();

	// Back key
	if (btn(BTN_BACK))  break;

	// Enter key
	if (btn(BTN_ENTER)) {
	    key_beep();
	    if (menu == LM_MODEL)	menu_model((u8)(btnl(BTN_ENTER) ? 1 : 0));
	    else if (menu == LM_NAME) {
		if (btnl(BTN_ENTER))	menu_reset_model();
		else			menu_name();
	    }
	    else if (menu == LM_REV)	menu_reverse();
	    else if (menu == LM_EPO)	menu_endpoint();
	    else if (menu == LM_TRIM) {
		if (btnl(BTN_ENTER))	menu_subtrim();
		else			menu_trim();
	    }
	    else if (menu == LM_DR)	menu_dualrate();
	    else if (menu == LM_EXP)	menu_expo();
	    else 			menu_abs();
	    main_screen(MS_NAME);	// show model number and name
	    if (btn(BTN_BACK))  break;	// and exit when BACK
	}

	// rotate keys
	else if (btn(BTN_ROT_ALL)) {
	    if (btn(BTN_ROT_R)) {
		menu >>= 1;
		if (!menu)  menu = LM_MODEL;
	    }
	    else {
		menu <<= 1;
		if (!menu)  menu = LM_ABS;
	    }
	    lcd_menu(menu);
	    lcd_update();
	}
    }

    key_beep();
    lcd_menu(0);
}








// ****************** MAIN LOOP and init *********************************

// main menu loop, shows main screen and handle keys
static void menu_loop(void) {
    u8 item = MS_NAME;

    lcd_clear();

    while (1) {
	main_screen(item);
	btnra();
	menu_stop();

	// don't wanted in submenus, will be set back in main_screen()
	menu_wants_adc = 0;

    check_keys:
	// Enter long key
	if (btnl(BTN_ENTER)) {
	    if (adc_steering_ovs > (CALIB_ST_MID_HIGH << ADC_OVS_SHIFT))
		menu_calibrate();
	    else if (adc_steering_ovs < (CALIB_ST_LOW_MID << ADC_OVS_SHIFT))
		menu_key_test();
	    else menu_global_setup();
	}

	// Enter key
	else if (btn(BTN_ENTER)) {
	    key_beep();
	    select_menu();
	}

	// trims
	else if (btn(BTN_TRIM_LEFT | BTN_TRIM_RIGHT)) {
	    trim_dualrate(LS_MENU_TRIM, 1, &cm.trim[0],
	                  BTN_TRIM_LEFT, BTN_TRIM_RIGHT,
	                  -TRIM_MAX, TRIM_MAX, cg.trim_step, "LNR");
	    goto check_keys;
	}
	else if (btn(BTN_TRIM_FWD | BTN_TRIM_BCK)) {
	    trim_dualrate(LS_MENU_TRIM, 2, &cm.trim[1],
	                  BTN_TRIM_FWD, BTN_TRIM_BCK,
	                  -TRIM_MAX, TRIM_MAX, cg.trim_step, "FNB");
	    goto check_keys;
	}

	// dualrate
	else if (btn(BTN_DR_L | BTN_DR_R)) {
	    trim_dualrate(LS_MENU_DR, 1, (s8 *)&cm.dualrate[0],
	                  BTN_DR_L, BTN_DR_R,
			  0, 100, 1, 0);
	    goto check_keys;
	}
	

	// channel 3 button
	else if (!cg.ch3_momentary && btn(BTN_CH3)) {
	    key_beep();
	    ch3_state = ~ch3_state;
	}

	// rotate encoder - change model name/battery/...
	else if (btn(BTN_ROT_ALL)) {
	    if (btn(BTN_ROT_L)) {
		if (item)  item--;
		else       item = MS_MAX - 1;
	    }
	    else {
		if (++item >= MS_MAX)  item = 0;
	    }
	}
    }
}



// read config from eeprom, initialize and call menu loop
void menu_init(void) {
    // variables

    // read global config from eeprom, if calibrate values changed,
    //   call calibrate
    if (config_global_read())
	menu_calibrate();
    apply_global_config();

    // read model config from eeprom
    load_model();

    // and main loop
    menu_loop();
}

