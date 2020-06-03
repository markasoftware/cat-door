/**
 * Copyright (c) Mark Polyakov 2020
 * Released under the GNU GPL v3
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/atomic.h>

// at prescaler /1024. About 2 seconds.
#define ACTIVE_TIMER_COMPARE 0x0800;
// at prescaler /1024. About 67 seconds. TODO: look into increasing this.
#define SLEEP_TIMER_COMPARE 0xffff;

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
volatile unsigned char last_PINA;

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

static void set_inner_active_timer() {
	;
}

static void set_inner_sleep_timer() {
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
	ATOMIC_BLOCK(ATOMIC_FORCEON) {
		set_flash();
		state = OUTER_UP;
		reset_then_set(PIN_OUTER_DOWN_O, PIN_OUTER_UP_O);
		_delay_ms(250);
	}
}

// idle -> outer close
static void begin_outer_close() {
	ATOMIC_BLOCK(ATOMIC_FORCEON) {
		set_flash();
		state = OUTER_DOWN;
		reset_then_set(PIN_OUTER_UP_O, PIN_OUTER_DOWN_O);
		_delay_ms(250);
	}
}

static void begin_idle() {
	clear_flash();
	TCNT1 = 0x0000;
	OCR1A = SLEEP_TIMER_COMPARE;
	state = IDLE;
	PORTA &= ~(
		(1<<PIN_INNER_UP_O) |
		(1<<PIN_INNER_DOWN_O) |
		(1<<PIN_OUTER_UP_O) |
		(1<<PIN_OUTER_DOWN_O));
}

// if the state of the outer door differs from what the user wants, start moving
// it.
static void move_outer() {
	if (sw_outer_open()) {
		if (!(PINB & (1<<PIN_SENS_OPEN_I))) {
			begin_outer_open();
		} else {
			begin_idle();
		}
	} else {
		if (!(PINB & (1<<PIN_SENS_CLOSED_I))) {
			begin_outer_close();
		} else {
			begin_idle();
		}
	}
}

static void move_inner() {
	TCNT1 = 0x0000;
	OCR1A = ACTIVE_TIMER_COMPARE;
	if (sw_inner_open()) {
		begin_inner_open();
	} else {
		begin_inner_close();
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
	TIMSK0 = (1<<TOIE0);

	// enable pin change interrupts
	PCMSK0 =
		(1<<PIN_SW_OPEN_I) |
		(1<<PIN_SW_CLOSED_I) |
		(1<<PIN_SENS_OPEN_I) |
		(1<<PIN_SENS_CLOSED_I);
	GIMSK = (1<<PCIE0);
	last_PINA = PINA;

	// Timer 1 controls the inner door. It always has prescaler 1024 and
	// resets on compare. The compare value will be set to the correct value in move_inner(); we set it here just so that the interrupt 
	TCCR1B = (1<<WGM12) | (1<<CS12) | (1<<CS10);
	TIMSK1 = (1<<OCIE1A);

	// As soon as interrupts enable, we should get a timer1 interrupt (it
	// equals zero, the initial compare register). This will start the inner
	// door moving, as we want.
	sei();

	// TODO: sleep
	while (1);
}

// Pin change
ISR(PCINT0_vect) {
	// it's important not to `delay` to debounce here, because bouncing
	// could cause many interrupts, which will pile up to a long delay.

	// We don't know which pin triggered the interrupt!
	unsigned char changed_pins = last_PINA ^ PINA;

	if (state == OUTER_DOWN &&
	    changed_pins & PIN_SENS_OPEN_I &&
	    (PINA & PIN_SENS_OPEN_I) == 0) {
		//rewind();
	}

	// We're interested in a switch change only if it was from open <-> unopen
	if (changed_pins & PIN_SW_OPEN_I) {
		move_inner();
	}

	// unconditionally check the outer door, because it can't do any harm!
	move_outer();
}

// Inner door raise complete OR inner door periodic
ISR(TIM1_COMPA_vect) {
	if (state == IDLE) {
		move_inner();
	} else {
		begin_idle();
		move_outer();
	}
}
