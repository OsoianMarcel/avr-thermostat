#ifndef CONFIG_H_
#define CONFIG_H_

#define TEMP_HYSTERESIS 0.25
#define TEMP_BTN_STEP 0.05
#define TEMP_LOCK_SEC 3
#define TEMP_UPDATE_INTERVAL_SEC 10
// #define TEMP_STORE_VALUES 3

#define RELAY_DDR DDRD
#define RELAY_PORT PORTD
#define RELAY_PIN PIND
#define RELAY_HEAT_PIN PD6
#define RELAY_COOL_PIN PD7
#define RELAY_BEEP_PIN PD5

#define BTN_DDR DDRC
#define BTN_PORT PORTC
#define BTN_PIN PINC
#define BTN_UP_PIN PC1
#define BTN_DOWN_PIN PC2
#define BTN_DISPLAY_PIN PC0

#define TIMER0_INIT_VAL 6
#define TIMER1_INIT_VAL 34286

#define DISPLAY_OFF_SEC 30

#define EEPROM_UPDATE_DELAY 5

#define EVENT_NONE 0
#define EVENT_UPDATE_EEPROM 1
#define EVENT_DISPLAY_OFF 2
#define EVENT_DISPLAY_ON 3
#define EVENT_SENSOR_START_CONV 4
#define EVENT_SENSOR_READ_TEMP 5
#define EVENT_ONCE_IN_SEC 6

#define STATUS_DISPLAY_ON 0

#endif /* CONFIG_H_ */