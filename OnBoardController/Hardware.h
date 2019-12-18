
/* Harware.h
   Макросы и определения для железной части
   
   Исходный код станции Восток-27
*/

/* --- КОНСТАНТЫ --- */

#ifndef HARDWARE_H
#define HARDWARE_H

#define MQ135_PIN A0
#define MQ135_CALLIBRATION_DATA 77

#define DHT_PIN  4
#define DHT_TYPE DHT11

#define PIN_SOLAR_PANEL_LEFT   A2  // Напряжение левой панели
#define PIN_SOLAR_PANEL_RIGHT  A1  // Напряжение правой панели
#define PIN_BATTERY_VOLTAGE    A3  // Напряжение АКБ
#define PIN_ENERGY_USAGE       A4  // Ток потребления модулем

#define PIN_PRES_RELIEF_VALVE  10  // Сброс давления
#define PIN_LIGHT_CONTROL      9   // Управление освещением
#define PIN_PUMP_VALVE         7   // Раздув станции
#define PIN_HEAT_MODULE        5   // Обогреватель
#define PIN_CO2_NUTRALIZATION  6   // Нейтрализатор CO2
#define PIN_PROD_CO2           2   // Генератор CO2

#define PIN_RELAY_BLOCK  14  // Разрешение работы блока реле

#define PIN_LIGHT 11

// Адреса в памяти для хранения настроек
#define EEPROMA_STATE      0x00
#define EEPROMA_DAY_TIME   (EEPROMA_STATE    + sizeof(uint32_t))
#define EEPROMA_NIGHT_TIME (EEPROMA_DAY_TIME + sizeof(uint32_t))

// Ключевое значение, для проверки инициализации EEPROM памяти
#define EEPROMV_STATE_INITIALIZED 0xAABBCCDD


/* --- МАКРОСЫ --- */

#define RUN(pin)  (digitalWrite(pin, LOW))
#define STOP(pin) (digitalWrite(pin, HIGH))

#endif
