/*
    main include file
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


#ifndef _GT3B_INCLUDED
#define _GT3B_INCLUDED



#include "stm8.h"
#include "task.h"


// frequency of crystal in kHz
#define KHZ  ((u16)18432)
// development board has another crystal
//#define KHZ  ((u16)16000)



/* PPM */
#define MAX_CHANNELS 8
extern u8 channels;
extern void ppm_set_channels(u8 n);
extern void ppm_set_value(u8 channel, u16 microsec);
extern void ppm_calc_sync(void);


/* INPUT - keys, steering, throttle */


/* buzzer */


/* timer */
extern u16 time_sec;
extern u8  time_5ms;


#endif
