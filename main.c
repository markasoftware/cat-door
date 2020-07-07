#define F_CPU 128000L

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <stdbool.h>

enum action {
	ACT_IDLE,
	// direction for these two is determined by switch position
	ACT_INNER,
	ACT_OUTER,

	// "close" or "open" is sw position when rewind began
	ACT_REWIND_CLOSE,
	ACT_REWIND_OPEN,

	ACT_COOLDOWN,
};

enum pins {
	PIN_INNER_UP_O,
	PIN_INNER_DOWN_O,
	PIN_OUTER_UP_O,
	PIN_OUTER_DOWN_O,
	PIN_SW_OPEN_I,
	PIN_SW_CLOSED_I,
	PIN_SENS_OPEN_I,
	PIN_SENS_CLOSED_I,
};

struct state {
	unsigned char action : 3;
	unsigned char sw_inner_open : 1;
	unsigned char sw_outer_closed : 1;
	unsigned char sens_open : 1;
	unsigned char sens_closed : 1;
	unsigned char inner_done : 1;
};

volatile struct state state = { 0 };

/////////////////////////////////
// pin related functions

static void pin_idle() {
	PORTA &= ~(
		(1<<PIN_INNER_UP_O) |
		(1<<PIN_INNER_DOWN_O) |
		(1<<PIN_OUTER_UP_O) |
		(1<<PIN_OUTER_DOWN_O));
}

// physically move the outer motor up
static void pin_outer_up() {
	pin_idle();
	PORTA |= (1<<PIN_OUTER_UP_O);
}

static void pin_outer_down() {
	pin_idle();
	PORTA |= (1<<PIN_OUTER_DOWN_O);
}

static void pin_inner_up() {
	pin_idle();
	PORTA |= (1<<PIN_INNER_UP_O);
}

static void pin_inner_down() {
	pin_idle();
	PORTA |= (1<<PIN_INNER_DOWN_O);
}

// update switch and sensor info in state using pin information.
static void read_pins() {
	// avoid races
	unsigned char pina = PINA;

	bool new_sw_inner_open = !!(pina & (1<<PIN_SW_OPEN_I));

	// if inner door changed
	if (new_sw_inner_open != state.sw_inner_open) {
		state.inner_done = false;
	}

	state.sw_inner_open = new_sw_inner_open;
	state.sw_outer_closed = !!(pina & (1<<PIN_SW_CLOSED_I));
	// active low (pulled up)
	state.sens_open = !(pina & (1<<PIN_SENS_OPEN_I));
	state.sens_closed = !(pina & (1<<PIN_SENS_CLOSED_I));
}

///////////////////////////
// random helpers

static bool outer_done_p() {
	return (state.sw_outer_closed && state.sens_closed) ||
		(!state.sw_outer_closed && state.sens_open);
}

static void ensure_flshgnd() {
	TCCR0B = (1<<CS02);
	TCCR0A = (1<<COM0A0);
}

static void clear_flshgnd() {
	// stop the counter
	TCCR0B = 0;
	// the next time flshgnd is enabled, we want it to go black immediately.
	if (PINB & (1<<PINB2)) {
		TCNT0 = 2;
	} else {
		TCNT0 = 0;
	}
	// finally, disconnect the OCR
	TCCR0A = 0;
}

////////////////////////////
// state transitions

static void action_idle() {
	clear_flshgnd();
	state.action = ACT_IDLE;
	pin_idle();
	// four and a bit minutes
	OCR1B = TCNT1 + 0x8000;
}

static void action_inner() {
	ensure_flshgnd();
	state.action = ACT_INNER;
	if (state.sw_inner_open) {
		pin_inner_up();
	} else {
		pin_inner_down();
	}
	// two seconds
	OCR1B = TCNT1 + 0x0100;
}

static void action_outer() {
	ensure_flshgnd();
	state.action = ACT_OUTER;
	if (state.sw_outer_closed) {
		pin_outer_down();
	} else {
		pin_outer_up();
	}
	// eight seconds, then rewind
	OCR1B = TCNT1 + 0x0400;
}

static void auto_action_motion() {
	if (state.inner_done) {
		if (outer_done_p()) {
			action_idle();
		} else {
			action_outer();
		}
	} else {
		action_inner();
	}
}

static void action_rewind_close() {
	ensure_flshgnd();
	state.action = ACT_REWIND_CLOSE;
	// should already be set, but for consistency...
	pin_outer_up();
	OCR1B = TCNT1 + 0x0800;
}

static void action_rewind_open() {
	ensure_flshgnd();
	state.action = ACT_REWIND_OPEN;
	pin_outer_down();
	OCR1B = TCNT1 + 0x0800;
}

static void action_cooldown() {
	ensure_flshgnd();
	state.action = ACT_COOLDOWN;
	pin_idle();
	// about a minute
	OCR1B = TCNT1 + 0x2000;
}

///////////////////////////////
// act and act-timer

// perform action and update state accordingly.
static void act() {
	switch (state.action) {
	case ACT_IDLE:
		auto_action_motion();
		break;
	case ACT_INNER:
		// no interrupting inner motor, let timer handle it.
		break;
	case ACT_OUTER:
		// avoid interruptions, for good measure.
		if (state.sens_open || state.sens_closed) {
			auto_action_motion();
		}
		break;
	case ACT_REWIND_CLOSE:
		if (state.sens_closed) {
			// rewind finished gracefully
			auto_action_motion();
		} else if (state.sens_open) {
			// got stuck on the way down during rewind, so it came
			// back up the other (normal spooling) side. Assume a
			// serious blockage.
			action_cooldown();
		}
		break;
	case ACT_REWIND_OPEN:
		if (state.sens_closed || state.sens_open) {
			// rewind finished OR the door got caught while
			// rewinding (never reached the bottom) and is now open
			// but spooled the wrong direction, which will be
			// corrected when the door is next opened.

			// This could cause a loop of activating the motor in
			// case of a serious blockage.
			auto_action_motion();
		}
		break;
	case ACT_COOLDOWN:
		// do nothing, we're waiting for the timer.
		break;
	}
}

// perform action, knowing that the timer compare just fired
static void act_timer() {
	switch (state.action) {
	case ACT_IDLE:
		// Inner door periodic
		state.inner_done = false;
		auto_action_motion();
		// special override
		clear_flshgnd();
		break;
	case ACT_INNER:
		// it's only done if the direction was not changed while it was
		// in motion.
		if (!!(PORTA & (1<<PIN_INNER_UP_O)) == state.sw_inner_open) {
			state.inner_done = true;
		}
		auto_action_motion();
		break;
	case ACT_OUTER:
		// Start rewind
		if (state.sw_outer_closed) {
			action_rewind_close();
		} else {
			action_rewind_open();
		}
		break;

	case ACT_REWIND_OPEN:
	case ACT_REWIND_CLOSE:
		// rewind took too long
		action_cooldown();
		break;
	case ACT_COOLDOWN:
		// cooldown complete
		auto_action_motion();
		break;
	}
}

/////////////////////////////////
// main and interrupts

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
	DDRB = (1<<PINB2); // flshgnd

	// 8-bit timer compare
	OCR0A = 1;

	// enable pin change interrupts
	PCMSK0 =
		(1<<PIN_SW_OPEN_I) |
		(1<<PIN_SW_CLOSED_I) |
		(1<<PIN_SENS_OPEN_I) |
		(1<<PIN_SENS_CLOSED_I);
	GIMSK = (1<<PCIE0);
	_delay_ms(5);

	// timer 1 runs continuously, used for debouncing and inner door timing.
	TCCR1B = (1<<CS12) | (1<<CS10);
	// just make sure it doesn't fire immediately. The first act() should
	// set it properly (door moves immediately).
	OCR1B = 255;
	TIMSK1 = (1<<OCIE1B);

	read_pins();
	act();

	sei();

	while (true) sleep_mode(); // default sleep is idle, allows timers
	// set_sleep_mode can be used for power down, adc sleep, etc
}

// debouncing -- don't read pins until a few tens of milliseconds after the last
// pin change occurred.
ISR(PCINT0_vect) {
	OCR1A = TCNT1 + 4;
	// enable the interrupt
	TIMSK1 = (1<<OCIE1A) | (1<<OCIE1B);
}

// debouncing timer
ISR(TIM1_COMPA_vect) {
	// disable debounce interrupt
	TIMSK1 = (1<<OCIE1B);
	read_pins();
	act();
}

// longer timers
ISR(TIM1_COMPB_vect) {
	act_timer();
}
