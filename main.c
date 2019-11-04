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

// TODO:
// Check display on/off logic
// Check display rendering
// One setup func instead 3 func
// Check EEPROM save address (eeprom special name)
// Check btn up/down logic
// Add display on btn (maybe off too)
// Add hardware ON led (low loght)

// EEPROM
typedef struct {
	double set_temp;
	uint8_t is_set;
} MY_EEPROM_DATA;

MY_EEPROM_DATA my_eeprom_data = {0.0, 0};

// Flags
uint8_t event_flags = 0;
uint8_t status_flags = 0;

// Timers
uint8_t lock_temp_timer = 0;
uint8_t eeprom_update_timer = 0;
uint8_t display_off_timer = DISPLAY_OFF_SEC;

double temp;
double prev_temp;
char item_buf[8];
char line_buffer[24];

// Timer0
void timer0_init(void) {
	TCCR0 |= (1<<CS02) |  (1<<CS00);
	TCCR0 &= ~(1<<CS01);
	
	// Set timer0 initial value
	TCNT0 = TIMER0_INIT_VAL;
	
	// Interrupt enable for timer0
	TIMSK |= (1 << TOIE0);
}

// Each 32 ms
ISR(TIMER0_OVF_vect) {
	static uint8_t timer_read_sensor = 0;

	++timer_read_sensor;
	if (timer_read_sensor >= 160) { // Every 160 * 32 ms = 5120 ms = 5.12 sec
		bit_set(event_flags, EVENT_READ_SENSOR);
		timer_read_sensor = 0;
	}

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
	
	TCNT1 = TIMER1_INIT_VAL;
}
// /Timer1

void ports_init() {
	// Buttons
	BTN_DDR &= ~((1 << BTN_UP_PIN) | (1 << BTN_DOWN_PIN));
	BTN_PORT |= (1 << BTN_UP_PIN) | (1 << BTN_DOWN_PIN);
	
	// Relays
	RELAY_DDR |= (1 << RELAY_HEAT_PIN) | (1 << RELAY_COOL_PIN);
}

uint8_t btn_up_pressed(void) {
	return !bit_test(BTN_PIN, BTN_UP_PIN);
}

uint8_t btn_down_pressed(void) {
	return !bit_test(BTN_PIN, BTN_DOWN_PIN);
}

void render_set_temp(void) {
	dtostrf(my_eeprom_data.set_temp, 2, 2, item_buf);
	sprintf(line_buffer, "<%s?C>     ", item_buf);
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,3);
	lcd_puts(line_buffer);
}

void render_cur_temp(void) {
	dtostrf(temp, 2, 2, item_buf);
	sprintf(line_buffer, "%s?C  ", item_buf);
	lcd_charMode(DOUBLESIZE);
	lcd_gotoxy(5,0);
	lcd_puts(line_buffer);
}

void render_diff_temp(void) {
	dtostrf(temp > my_eeprom_data.set_temp ? temp - my_eeprom_data.set_temp : my_eeprom_data.set_temp - temp, 2, 2, item_buf);
	sprintf(line_buffer, "<%s?C>     ", item_buf);
	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(7,5);
	lcd_puts(line_buffer);
}

void render_temp_change(void) {
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

void render_mode(void) {
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

void render_loading(void) {
	lcd_charMode(DOUBLESIZE);
	lcd_home();
	lcd_puts_p(PSTR("Loading..."));
}

void render_init(void) {
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
	lcd_puts_p(PSTR("Change <up>"));
	lcd_gotoxy(0,7);
	lcd_puts_p(PSTR("Uptime <5m>"));
}

void display_on_if(void) {
	// On if off
	if (!bit_test(status_flags, STATUS_DISPLAY_ON)) {
		lcd_init(LCD_DISP_ON);
		bit_set(status_flags, STATUS_DISPLAY_ON);
		render_init();
		render_cur_temp();
		render_set_temp();
		render_diff_temp();
		render_mode();
	}
}

void display_off_if(void) {
	// Off if on
	if (bit_test(status_flags, STATUS_DISPLAY_ON)) {
		lcd_init(LCD_DISP_OFF);
		bit_clear(status_flags, STATUS_DISPLAY_ON);
	}
}

int main(void)
{
	ports_init();
	timer0_init();
	timer1_init();
	
	lcd_init(LCD_DISP_ON);
	bit_set(status_flags, STATUS_DISPLAY_ON);
	render_loading();
	
	eeprom_read_block(&my_eeprom_data, 0, sizeof(MY_EEPROM_DATA));
	if (my_eeprom_data.is_set != 28) {
		my_eeprom_data.set_temp = 22;
		my_eeprom_data.is_set = 28;
		
		eeprom_update_timer = EEPROM_UPDATE_DELAY;
	}

	// Get first sample of temp from sensor while "Loading..." is rendered on display
	temp = ds18b20_gettemp();
	prev_temp = temp;
	
	// Render initial text (static)
	render_init();
	
	render_cur_temp();
	render_set_temp();
	render_diff_temp();
	render_mode();
	
	while(1) {
		// Event: display off
		if (bit_test(event_flags, EVENT_DISPLAY_OFF)) {
			display_off_if();
			bit_clear(event_flags, EVENT_DISPLAY_OFF);
		}
		// /Event: display off
		
		// Event: display on
		if (bit_test(event_flags, EVENT_DISPLAY_ON)) {
			display_on_if();
			bit_clear(event_flags, EVENT_DISPLAY_ON);
		}
		// /Event: display on
		
		// Call temp sensor
		if (bit_test(event_flags, EVENT_READ_SENSOR)) {
			temp = ds18b20_gettemp();
			
			render_temp_change();
			
			prev_temp = temp;
			
			render_cur_temp();
			render_diff_temp();
			
			bit_clear(event_flags, EVENT_READ_SENSOR);
		}
		// /Call temp sensor
		
		// Set temp logic
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
		}
		// /Set temp logic
		
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
			eeprom_write_block(&my_eeprom_data, 0, sizeof(MY_EEPROM_DATA));
			bit_clear(event_flags, EVENT_UPDATE_EEPROM);
		}
		// /Event: update EEPROM
		
		_delay_ms(25);
	}
}

