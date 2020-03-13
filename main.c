/**
 * Copyright (c) Mark Polyakov 2020
 * Released under the GNU GPL v3
 */

#include <avr/io.h>
#include <avr/interrupt.h>

enum pins {
	PIN_INNER_UP_O = 0,
	PIN_INNER_DOWN_O,
	PIN_OUTER_UP_O,
	PIN_OUTER_DOWN_O,
	PIN_SW_OPEN_I,
	PIN_SW_CLOSED_I,
	PIN_SENS_OPEN_I,
	PIN_SENS_CLOSED_I,
};
// flashground is always on Port B, pin 2 -- because of timer overrides.

enum state {
	IDLE,
	INNER_UP,
	INNER_DOWN,
	OUTER_UP,
	OUTER_DOWN,
};

volatile unsigned char ticks = 0;
volatile unsigned char state = IDLE;

// Determine whether the user wants the inner and outer doors to be open
#define sw_inner_open() (PINA & (1<<PIN_SW_OPEN_I))
#define sw_outer_open() (!(PINA & (1<<PIN_SW_CLOSED_I)))

static void reset_then_set(unsigned char reset_pin, unsigned char set_pin) {
	PORTA &= ~(1<<reset_pin);
	PORTA |= (1<<set_pin);
}

static void set_flash() {
	// Start timer
	TCCR0B = (1<<CS02) | (1<<CS00);
}

static void clear_flash() {
	TCCR0B = 0x00;
}

static void set_timer(unsigned char t) {
}

static void clear_timer() {
}

// idle -> inner open
static void begin_inner_open() {
	state = INNER_UP;
	reset_then_set(PIN_INNER_DOWN_O, PIN_INNER_UP_O);
}

// idle -> inner close
static void begin_inner_close() {
	state = INNER_DOWN;
	reset_then_set(PIN_INNER_UP_O, PIN_INNER_DOWN_O);
}

// idle -> outer open
static void begin_outer_open() {
	state = OUTER_UP;
	reset_then_set(PIN_OUTER_DOWN_O, PIN_OUTER_UP_O);
}

// idle -> outer close
static void begin_outer_close() {
	state = OUTER_DOWN;
	reset_then_set(PIN_OUTER_UP_O, PIN_OUTER_DOWN_O);
}

static void begin_idle() {
	state = IDLE;
	PORTA &= ~(
		(1<<PIN_INNER_UP_O) |
		(1<<PIN_INNER_DOWN_O) |
		(1<<PIN_OUTER_UP_O) |
		(1<<PIN_OUTER_DOWN_O));
}

// if the state of the outer door differs from what the user wants, start moving
// it.
static void check_outer() {
	if (sw_outer_open()) {
		if (PINB & (1<<PIN_SENS_OPEN_I)) {
			begin_outer_open();
		} else {
			begin_idle();
		}
	} else {
		if (PINB & (1<<PIN_SENS_CLOSED_I)) {
			begin_outer_close();
		} else {
			begin_idle();
		}
	}
}

int main() {
	// Pull high on the door sensors.
	PORTA = (1<<PIN_SENS_OPEN_I) | (1<<PIN_SENS_CLOSED_I);
	// All port A pins are output. Low defaults are OK: Everything off,
	// light solid on.
	DDRA =
		(1<<PIN_INNER_UP_O) |
		(1<<PIN_INNER_DOWN_O) |
		(1<<PIN_OUTER_UP_O) |
		(1<<PIN_OUTER_DOWN_O);

	// Toggle OC0A pin on timer compare match. This is flash ground. The
	// compare register (OCR0A) is 0x00 by default, which will match
	// sometimes!
	TCCR0A = (1<<COM0A0);

	// Timer 1 controls the inner door: It represents both

	// enable pin change interrupts
	PCMSK0 =
		(1<<PIN_SW_OPEN_I) |
		(1<<PIN_SW_CLOSED_I) |
		(1<<PIN_SENS_OPEN_I) |
		(1<<PIN_SENS_CLOSED_I);
	GIMSK = (1<<PCIE0);
	sei();

	// Always start by activating the inner door; its position on power-on
	// is unknown.
	if (sw_inner_open()) {
		begin_inner_open();
	} else {
		begin_inner_close();
	}

	// TODO: sleep
	while (1);
}

ISR(TIM1_OVF_vect) {
	// if not idle, then we must be moving inner door.
	if (state == IDLE) {
		if (sw_inner_open()) {
			begin_inner_open();
		} else {
			begin_inner_close();
		}
	} else {
		begin_idle();
		check_outer();
	} 
}
