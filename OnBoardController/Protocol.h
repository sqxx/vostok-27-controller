/* Protocol.h
   Макросы и определения для взаимодействия с внешним миром
   
   Исходный код станции Восток-27
*/

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* --- КОНСТАНТЫ --- */

#define PACKAGE_SIZE 10
#define PACKAGE_TYPE uint8_t

#define START_MAGIC  0xF4
#define END_CR  0x0D
#define END_LF  0x0A

#define _P_STARTUP          0x01
#define _P_INIT_COMPLETE    0x02
#define _P_NOT_READY        0x03

#define _P_LOW_VOLTAGE      0x0A  // Низкое напряжение на солнечных панелях
#define _P_LOW_PRESSURE     0x0B  // Пониженное давление
#define _P_STATION_IS_OPEN  0x0C  // Люк открыт

#define _P_REQ_CO2   0xA1  // CO2 (ppm)
#define _P_REQ_HUM   0xA2  // Влажность (%)
#define _P_REQ_TEMP  0xA3  // Температура (C)
#define _P_REQ_PRES  0xA4  // Давление (mbar)
#define _P_REQ_BAT_VOLTAGE   0xA5  // Напряжение АКБ
#define _P_REQ_ENERGY_USAGE  0xA6  // Потребление станции
#define _P_REQ_ENERGY_GEN    0xA7  // Генерация энергии

#define _P_CODE_SUCCESS 0x00
#define _P_CODE_FAILURE 0xFF

#define _P_SYSTEM_ENABLED  0x00
#define _P_SYSTEM_DISABLED 0xFF

#define _P_SWITCH_PRES_RELIEF_VALVE  0xB1  // Переключить клапан сброса давления
#define _P_STATUS_PRES_RELIEF_VALVE  0xB2  // Состояние клапана сброса давления
#define _P_SWITCH_PUMP_VALVE  0xB3  // Переключить систему подачи кислорода
#define _P_STATUS_PUMP_VALVE  0xB4  // Состояние системы подачи кислорода  
#define _P_SWITCH_PROD_CO2    0xB5  // Переключить выработку углекислого газа
#define _P_STATUS_PROD_CO2    0xB6  // Состояние системы выработки углекислого газ
#define _P_SWITCH_AUTO_LIGHT  0xB7  // Переключить автоматическое управление освещением
#define _P_STATUS_AUTO_LIGHT  0xB8  // Состояние автоматического переключения освещением

#define _P_SET_LIGHT  0xC1
#define _P_GET_LIGHT  0xC2

#define _P_SET_TIME   0xD1
#define _P_GET_TIME   0xD4

#define _P_SET_DAY_TIME    0xD2
#define _P_GET_DAY_TIME    0xD5

#define _P_SET_NIGHT_TIME  0xD3
#define _P_GET_NIGHT_TIME  0xD6

#define _P_SERIAL_RESET   0xEA  // Отчистка всего буфера Serial

// PE - Protocol Exception
#define _PE_PACKAGE_ERR 0xE1  // Некорректный пакет
#define _PE_PACKAGE_CRC 0xE2  // Неверная контрольная сумма
#define _PE_UNKNOWN_CMD 0xE3  // Неизвестная команда

#define _PE_PACKAGE_ERR_CRLF  0xDA
#define _PE_PACKAGE_ERR_MAGIC 0xF4


/* --- МАКРОСЫ --- */

#define PACKAGE_MARKERS_VALID(package) \
    package[0]                != START_MAGIC || \
    package[PACKAGE_SIZE - 2] != END_LF      || \
    package[PACKAGE_SIZE - 1] != END_CR         \

#define NOTIFY(cmd) (send_package(cmd, 0))

#endif
