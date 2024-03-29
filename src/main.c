#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include "macros.h"
#include "config.h"
#include "lcd.h"
#include "ds18b20.h"

// EEPROM
typedef struct {
	double set_temp;
	uint8_t is_set;
} MY_EEPROM_DATA;

MY_EEPROM_DATA EEMEM my_eeprom_addr;
MY_EEPROM_DATA my_eeprom_data;

// Flags
volatile uint8_t event_flags;
uint8_t status_flags;

// Timers
volatile uint8_t lock_temp_timer;
volatile uint8_t eeprom_update_timer;
volatile uint8_t display_off_timer;

unsigned long int ticker;
double temp;
double prev_temp;
char item_buf[8];
char line_buffer[24];

void init_global_vars() {
	// Flags
	event_flags = 0;
	status_flags = 0;

	// Timers
	lock_temp_timer = 0;
	eeprom_update_timer = 0;
	display_off_timer = DISPLAY_OFF_SEC;

	ticker = 0L;
}

// Timer0
void timer0_init(void) {
	TCCR0 |= (1<<CS02) | (1<<CS00);
	TCCR0 &= ~(1<<CS01);
	
	// Set timer0 initial value
	TCNT0 = TIMER0_INIT_VAL;
	
	// Interrupt enable for timer0
	TIMSK |= (1 << TOIE0);
}

// Each 32 ms
ISR(TIMER0_OVF_vect) {
	// Hint: Every 160 * 32 ms = 5120 ms = 5.12 sec
	TCNT0 = TIMER0_INIT_VAL;
}
// /Timer0

// Timer1
void timer1_init(void) {
	TCCR1B |= (1<<CS12);
	
	// Set timer0 initial value
	TCNT1 = TIMER1_INIT_VAL;
	
	// Interrupt enable for timer1
	TIMSK |= (1 << TOIE1);
}

// Each ~1 sec
ISR(TIMER1_OVF_vect) {
	static uint8_t timer_read_sensor = 0;

	++timer_read_sensor;
	if (timer_read_sensor == TEMP_UPDATE_INTERVAL_SEC) {
		bit_set(event_flags, EVENT_SENSOR_START_CONV);
	} else if (timer_read_sensor > (TEMP_UPDATE_INTERVAL_SEC + 1)) {
		bit_set(event_flags, EVENT_SENSOR_READ_TEMP);
		timer_read_sensor = 0;
	}

	if (eeprom_update_timer) {
		--eeprom_update_timer;
		if (!eeprom_update_timer) {
			bit_set(event_flags, EVENT_UPDATE_EEPROM);
		}
	}
	
	if (display_off_timer) {
		--display_off_timer;
		if (!display_off_timer) {
			bit_set(event_flags, EVENT_DISPLAY_OFF);
		}
	}
	
	if (lock_temp_timer) {
		lock_temp_timer--;
	}
	
	bit_set(event_flags, EVENT_ONCE_IN_SEC);
	
	TCNT1 = TIMER1_INIT_VAL;
}
// /Timer1

void ports_init() {
	// Buttons
	BTN_DDR &= ~((1 << BTN_UP_PIN) | (1 << BTN_DOWN_PIN) | (1 << BTN_DISPLAY_PIN));
	BTN_PORT |= (1 << BTN_UP_PIN) | (1 << BTN_DOWN_PIN) | (1 << BTN_DISPLAY_PIN);
	
	// Relays
	RELAY_DDR |= (1 << RELAY_HEAT_PIN) | (1 << RELAY_COOL_PIN) | (1 << RELAY_BEEP_PIN);
}

uint8_t btn_up_pressed(void) {
	return !bit_test(BTN_PIN, BTN_UP_PIN);
}

uint8_t btn_down_pressed(void) {
	return !bit_test(BTN_PIN, BTN_DOWN_PIN);
}

uint8_t btn_display_pressed(void) {
	return !bit_test(BTN_PIN, BTN_DISPLAY_PIN);
}

void switch_beep(uint8_t on) {
	if (on) {
		bit_set(RELAY_PORT, RELAY_BEEP_PIN);
	} else {
		bit_clear(RELAY_PORT, RELAY_BEEP_PIN);
	}
}

void render_cur_temp(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	dtostrf(temp, 2, 2, item_buf);
	sprintf(line_buffer, "%s°C  ", item_buf);
	lcd_charMode(DOUBLESIZE);
	lcd_gotoxy(5,0);
	lcd_puts(line_buffer);
}

void render_set_temp(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	dtostrf(my_eeprom_data.set_temp, 2, 2, item_buf);
	sprintf(line_buffer, "<%s°C>     ", item_buf);
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,3);
	lcd_puts(line_buffer);
}

void render_mode(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,4);
	if (bit_test(RELAY_PORT, RELAY_HEAT_PIN)) {
		lcd_puts_p(PSTR("<heat>"));
	} else if (bit_test(RELAY_PORT, RELAY_COOL_PIN)) {
		lcd_puts_p(PSTR("<cool>"));
	} else {
		lcd_puts_p(PSTR("<off> "));
	}
}

void render_diff_temp(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	dtostrf(temp > my_eeprom_data.set_temp ? temp - my_eeprom_data.set_temp : my_eeprom_data.set_temp - temp, 2, 2, item_buf);
	sprintf(line_buffer, "<%s°C>     ", item_buf);
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,5);
	lcd_puts(line_buffer);
}

void render_temp_change(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,6);
	if (temp > prev_temp) {
		lcd_puts_p(PSTR("<up>  "));
	} else if (temp < prev_temp) {
		lcd_puts_p(PSTR("<down>"));
	} else {
		lcd_puts_p(PSTR("<none>"));
	}
}

void render_status(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,7);
	
	sprintf(line_buffer, "<%lu>", ticker);
	lcd_puts(line_buffer);
}

void render_loading(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	lcd_charMode(DOUBLESIZE);
	lcd_home();
	lcd_puts_p(PSTR("Loading..."));
}

void render_static(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}
	
	lcd_charMode(DOUBLESIZE);
	lcd_home();
	lcd_puts_p(PSTR("T: "));
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(0,3);
	lcd_puts_p(PSTR("Set to "));
	lcd_gotoxy(0,4);
	lcd_puts_p(PSTR("Mode ")); // heat, off, cool
	lcd_gotoxy(0,5);
	lcd_puts_p(PSTR("Diff "));
	lcd_gotoxy(0,6);
	lcd_puts_p(PSTR("Change <none>"));
	lcd_gotoxy(0,7);
	lcd_puts_p(PSTR("Status "));
}

void render_values(void) {
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		return;
	}

	render_cur_temp();
	render_set_temp();
	render_mode();
	render_diff_temp();
	render_temp_change();
	render_status();
}

void display_on_if(uint8_t init_render) {
	// On if off
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		lcd_init(LCD_DISP_ON);
		bit_set(status_flags, STATUS_DISPLAY_ON);
		
		if (init_render) {
			render_static();
			render_values();
		}
	}
}

void display_off_if(void) {
	// Off if on
	if (bit_test(status_flags, STATUS_DISPLAY_ON)) {
		lcd_init(LCD_DISP_OFF);
		bit_clear(status_flags, STATUS_DISPLAY_ON);
	}
}

void system_setup() {
	init_global_vars();
	ports_init();
	timer0_init();
	timer1_init();
	sei();
	
	switch_beep(1);
	_delay_ms(100);
	switch_beep(0);
}

int main(void) {
	system_setup();
	
	display_on_if(0);
	
	render_loading();
	
	eeprom_read_block(&my_eeprom_data, &my_eeprom_addr, sizeof(MY_EEPROM_DATA));
	if (my_eeprom_data.is_set != 28) {
		my_eeprom_data.set_temp = 22;
		my_eeprom_data.is_set = 28;
		
		eeprom_update_timer = EEPROM_UPDATE_DELAY;
	}

	// Get first sample of temp from sensor while "Loading..." is rendered on display
	ds18b20_start_conv();
	ds18b20_wait_until_conv_ready();
	temp = ds18b20_read_temp();
	prev_temp = temp;
	
	// Render initial text (static)
	render_static();
	render_values();
	
	while(1) {
		// Event: display off
		if (bit_test(event_flags, EVENT_DISPLAY_OFF)) {
			display_off_if();
			bit_clear(event_flags, EVENT_DISPLAY_OFF);
		}
		// /Event: display off
		
		// Event: display on
		if (bit_test(event_flags, EVENT_DISPLAY_ON)) {
			display_on_if(1);
			bit_clear(event_flags, EVENT_DISPLAY_ON);
		}
		// /Event: display on
		
		// Event: start temp sensor conv
		if (bit_test(event_flags, EVENT_SENSOR_START_CONV)) {
			ds18b20_start_conv();
			bit_clear(event_flags, EVENT_SENSOR_START_CONV);
		}
		// /Event: start temp sensor conv

		// Event: Read temp sensor
		if (bit_test(event_flags, EVENT_SENSOR_READ_TEMP)) {
			temp = ds18b20_read_temp();
			
			render_temp_change();
			
			prev_temp = temp;
			
			render_cur_temp();
			render_diff_temp();
			
			bit_clear(event_flags, EVENT_SENSOR_READ_TEMP);
		}
		// /Event: Read temp sensor
		
		// Btn logic
		if (btn_up_pressed()) {
			my_eeprom_data.set_temp += TEMP_BTN_STEP;
			
			if (my_eeprom_data.set_temp > 60.0) {
				my_eeprom_data.set_temp = 60.0;
			}
			
			eeprom_update_timer = EEPROM_UPDATE_DELAY;
			display_off_timer = DISPLAY_OFF_SEC;
			
			bit_set(event_flags, EVENT_DISPLAY_ON);
			
			render_set_temp();
			render_diff_temp();
		} else if (btn_down_pressed()) {
			my_eeprom_data.set_temp -= TEMP_BTN_STEP;
			
			if (my_eeprom_data.set_temp < -30.0) {
				my_eeprom_data.set_temp = -30.0;
			}
			
			eeprom_update_timer = EEPROM_UPDATE_DELAY;
			display_off_timer = DISPLAY_OFF_SEC;
			
			bit_set(event_flags, EVENT_DISPLAY_ON);
			
			render_set_temp();
			render_diff_temp();
		} else if (btn_display_pressed()) {
			bit_set(event_flags, EVENT_DISPLAY_ON);
			display_off_timer = DISPLAY_OFF_SEC;
		}
		// /Btn logic
		
		// Relay logic
		if (!bit_test(RELAY_PORT, RELAY_HEAT_PIN)) { // Heat is off
			if (temp < (my_eeprom_data.set_temp - TEMP_HYSTERESIS)) {
				if (!lock_temp_timer) {
					bit_set(RELAY_PORT, RELAY_HEAT_PIN); // Turn heat on
					render_mode();
				}
			}
		} else { // Heat is on
			if (temp >= my_eeprom_data.set_temp) {
				bit_clear(RELAY_PORT, RELAY_HEAT_PIN); // Turn heat off
				render_mode();
				lock_temp_timer = TEMP_LOCK_SEC;
			}
		}
		
		if (!bit_test(RELAY_PORT, RELAY_COOL_PIN)) { // Cool is off
			if (temp > my_eeprom_data.set_temp + TEMP_HYSTERESIS) {
				if (!lock_temp_timer) {
					bit_set(RELAY_PORT, RELAY_COOL_PIN); // Turn cool on
					render_mode();
				}
			}
		} else { // Cool is on
			if (temp <= my_eeprom_data.set_temp) {
				bit_clear(RELAY_PORT, RELAY_COOL_PIN); // Turn cool off
				render_mode();
				lock_temp_timer = TEMP_LOCK_SEC;
			}
		}
		// /Relay logic
		
		// Event: update EEPROM
		if (bit_test(event_flags, EVENT_UPDATE_EEPROM)) {
			eeprom_write_block(&my_eeprom_data, &my_eeprom_addr, sizeof(MY_EEPROM_DATA));
			bit_clear(event_flags, EVENT_UPDATE_EEPROM);
		}
		// /Event: update EEPROM
		
		// Event: once in sec
		if (bit_test(event_flags, EVENT_ONCE_IN_SEC)) {
			ticker++;
			render_status();
			bit_clear(event_flags, EVENT_ONCE_IN_SEC);
		}
		// /Event: once in sec
		
		_delay_ms(25);
	}
}

