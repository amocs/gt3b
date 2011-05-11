/*
    calc - calculate values of ppm signal
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



#include "calc.h"
#include "menu.h"
#include "ppm.h"



// CALC task
TASK(CALC, 256);
static void calc_loop(void);


// initialize CALC task
void calc_init(void) {
    build(CALC);
    activate(CALC, calc_loop);
    sleep(CALC);	// nothing to do yet
}




// calculate new PPM values from ADC and internal variables
// called for each PPM cycle
static void calc_loop(void) {

}
