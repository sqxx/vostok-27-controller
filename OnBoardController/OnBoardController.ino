/* -- Библиотеки --- */

#include <Wire.h>
#include <TimeLib.h>
#include <DS1307RTC.h>
#include <EEPROM.h>
#include <TroykaMQ.h>
#include <TroykaIMU.h>
#include <DHT.h>


/* -- МАКРОСЫ --- */

/*
   Arduino некоректно округляет float до int, не учитывая значение дробной части.
     По правилам, если дробная часть больше или равна 0.5,
     то округляется вверх.
*/
#define FLOAT_TO_INT(x) (((int) x) + ((((int)(x * 100)) % 100) >= 50 ? 1 : 0))

#define IS_IN_RANGE(x, l, h) (x >= l && x <= h)

#define RUN(pin)  (digitalWrite(pin, LOW))
#define STOP(pin) (digitalWrite(pin, HIGH))

#define NOTIFY(cmd) (send_package(cmd, 0))

/* -- НАСТРОЙКИ --- */

#define MQ135_PIN A0
#define MQ135_CALLIBRATION_DATA 77

#define DHT_PIN  3
#define DHT_TYPE DHT11

#define PIN_SOLAR_PANEL_LEFT  A1
#define PIN_SOLAR_PANEL_RIGHT A2

#define PIN_PROD_CO2   31
#define PIN_PUMP_VALVE 32
#define PIN_PRES_RELIEF_VALVE 33

#define PIN_LIGHT 34

// MA - Memory Address
#define _MA_STATE 0x00
#define _MA_STATE_INITIALIZED 0xAABBCCDD
#define _MA_DAY_TIME   (_MA_STATE + sizeof(uint32_t))
#define _MA_NIGHT_TIME (_MA_DAY_TIME + sizeof(uint32_t))


/* -- ПРОТОКОЛ --- */

#define PACKAGE_SIZE 10
#define PACKAGE uint8_t package[]

#define START_MAGIC 0xF4
#define END_CR 0x0A
#define END_LF 0x0D

#define _P_STARTUP       0x01
#define _P_INIT_COMPLETE 0x02
#define _P_NOT_READY     0x03

#define _P_REQ_CO2  0xA1  // CO2 (ppm)
#define _P_REQ_HUM  0xA2  // Влажность (%)
#define _P_REQ_TEMP 0xA3  // Температура (C)
#define _P_REQ_PRES 0xA4  // Давление (mbar)
#define _P_REQ_SOLAR_PANELS_EF 0xA5  /* 0-100%,
                                        выработка солнечных панелей в проценте 
                                        от максимальных 5V */

#define _P_CODE_SUCCESS 0x00
#define _P_CODE_FAILURE 0xFF

#define _P_SYSTEM_ENABLED  0x00
#define _P_SYSTEM_DISABLED 0xFF

#define _P_SWITCH_PRES_RELIEF_VALVE  0xB1  // Переключить клапан сброса давления
#define _P_STATUS_PRES_RELIEF_VALVE  0xB2  // Состояние клапана сброса давления
#define _P_SWITCH_PUMP_VALVE 0xB3  // Переключить систему подачи кислорода
#define _P_STATUS_PUMP_VALVE 0xB4  // Состояние системы подачи кислорода  
#define _P_SWITCH_PROD_CO2   0xB5  // Переключить выработку углекислого газа
#define _P_STATUS_PROD_CO2   0xB6  // Состояние системы выработки углекислого газ

#define _P_SET_TIME       0xD1
#define _P_SET_DAY_TIME   0xD2
#define _P_SET_NIGHT_TIME 0xD3
#define _P_GET_TIME       0xD4
#define _P_GET_DAY_TIME   0xD5
#define _P_GET_NIGHT_TIME 0xD6

#define _P_SERIAL_RESET   0xEA  // Отчистка всего буфера Serial

// PE - Protocol Exception
#define _PE_GAS_LEAK    0xE1  // Утечка газа
#define _PE_PACKAGE_ERR 0xE3  // Неверный пакет
#define _PE_PACKAGE_CRC 0xE4  // Неверная контрольная сумма
#define _PE_UNKNOWN_CMD 0xE5  // Неизвестная команда

#define _PE_PACKAGE_ERR_CRLF  0xDA
#define _PE_PACKAGE_ERR_MAGIC 0xF4


/* -- ОБЪЕКТЫ --- */

MQ135 mq135(MQ135_PIN);
DHT   dht(DHT_PIN, DHT_TYPE);
Barometer bar;

bool pres_relief_valve_active = false;
bool pump_valve_active   = false;
bool prod_co2_active = false;

uint32_t day_timestamp   = 0;
uint32_t night_timestamp = 0;

bool light_active = false;


/* -- MAIN & LOOP --- */

void setup()
{
  Serial.begin(9600);

  NOTIFY(_P_STARTUP);

  pinMode(PIN_PROD_CO2,   OUTPUT);
  pinMode(PIN_PUMP_VALVE, OUTPUT);
  pinMode(PIN_PRES_RELIEF_VALVE, OUTPUT);

  STOP(PIN_PROD_CO2);
  STOP(PIN_PUMP_VALVE);
  STOP(PIN_PRES_RELIEF_VALVE);

  mq135.heaterPwrHigh();
  dht.begin();
  bar.begin();

  init_eeprom();
  init_time();

  NOTIFY(_P_INIT_COMPLETE);
}

void loop()
{
  uint8_t  package[PACKAGE_SIZE];
  uint16_t package_crc = 0;
  uint16_t calc_crc    = 0;

  // Управление освещением
  handle_light();

  // Завершение настройки датчиков
  if (!setup_mq135())
  {
    NOTIFY(_P_NOT_READY);
    return;
  }

  // Проверяем доступность данных
  if (Serial.available() < PACKAGE_SIZE) return;

  // Читаем пакет
  memset(package, 0, PACKAGE_SIZE);
  Serial.readBytes(package, PACKAGE_SIZE);

  // Проверка маркеров пакета (см. документацию)
  if (package[0] !=  START_MAGIC ||
      package[PACKAGE_SIZE - 2] != END_CR ||
      package[PACKAGE_SIZE - 1] != END_LF)
  {
    handle_bad_package(package);
    return;
  }

  // Проверяем контрольную сумму
  package_crc = extract_crc(package);
  calc_crc    = calculate_crc(package);

  if (package_crc != calc_crc)
  {
    Serial.println("CRC: " + String(package_crc, HEX));
    handle_bad_crc(package, package_crc);
    return;
  }

  // Обрабатываем запрос
  handle_request(package);
}


/* -- ЖЕЛЕЗО -- */

/*
   Запускает прогрев MQ-135

   Возвращает true, если прогрев уже прошёл
*/
bool setup_mq135()
{
  if (mq135.heatingCompleted() && !mq135.isCalibrated())
  {
    mq135.calibrate(MQ135_CALLIBRATION_DATA);
    return false;
  }
  else
  {
    return true;
  }
}

/*
   Управление освещением в зависимости от времени
*/
void handle_light()
{
  tmElements_t tm;
  RTC.read(tm);

  int ts = (tm.Hour * 60 * 60) + (tm.Minute * 60) + tm.Second;

  if (ts >= day_timestamp &&
      ts < night_timestamp)
  {
    if (light_active)
    {
      digitalWrite(PIN_LIGHT, LOW);
      light_active = false;
    }
  }
  else
  {
    if (!light_active)
    {
      digitalWrite(PIN_LIGHT, HIGH);
      light_active = true;
    }
  }
}


/* -- ЛОГИКА --- */

/*
   Подгружает настройки из EEPROM памяти или инициализирует её
*/
void init_eeprom()
{
  uint32_t crc = 0x00;
  EEPROM.get(_MA_STATE, crc);

  if (crc == _MA_STATE_INITIALIZED)
  {
    EEPROM.get(_MA_DAY_TIME, day_timestamp);
    EEPROM.get(_MA_NIGHT_TIME, night_timestamp);
  }
  else
  {
    EEPROM.put(_MA_DAY_TIME, 0x00);
    EEPROM.put(_MA_NIGHT_TIME, 0x00);

    EEPROM.put(_MA_STATE, _MA_STATE_INITIALIZED);
  }
}

/*
   Синхронизирует rtc модуль со временем на ПК
*/
void init_time()
{
  tmElements_t tm;

  if (!RTC.read(tm))
  {
    int Hour, Min, Sec;

    sscanf(__TIME__, "%d:%d:%d", &Hour, &Min, &Sec);

    tm.Hour = Hour;
    tm.Minute = Min;
    tm.Second = Sec;
    tm.Day   = 1;
    tm.Month = 1;
    tm.Year  = 1970;
  }
}

/*
   Отчищает весь буфер Serial
*/
void serial_reset()
{
  while (Serial.available())
  {
    Serial.read();
  }
}


/* -- ПРОТОКОЛ --- */

/*
   Формирует и отправляет пакет по Serial
*/
void send_package(uint8_t cmd, uint32_t value)
{
  uint8_t  package[PACKAGE_SIZE] = {START_MAGIC, cmd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x0D};
  uint32_t crc = 0;

  package[2] = (value & 0xFF000000) >> 24;
  package[3] = (value & 0x00FF0000) >> 16;
  package[4] = (value & 0x0000FF00) >> 8;
  package[5] = (value & 0x000000FF);

  crc = calculate_crc(package);

  package[6] = (crc & 0xFF00) >> 8;
  package[7] = (crc & 0x00FF);

  Serial.write(package, PACKAGE_SIZE);
}

/*
   Подсчитывает контрольную сумму для переданного пакета
*/
uint32_t calculate_crc(PACKAGE)
{
  uint32_t crc = 0;

  for (uint32_t i = 1; i < (PACKAGE_SIZE - 4); i++)
  {
    crc += package[i];
  }

  return crc;
}

/*
   Извлекает контрольную сумму из переданного пакета
*/
uint16_t extract_crc(PACKAGE)
{
  byte high_byte = (uint16_t) package[PACKAGE_SIZE - 4];
  byte low_byte  = package[PACKAGE_SIZE - 3];

  return (high_byte << 8) | low_byte;
}


/* -- ОБРАБОТКА ИСКЛЮЧЕНИЙ -- */

void handle_bad_package(PACKAGE)
{
  serial_reset();
  if (package[0] !=  START_MAGIC)
  {
    send_package(_PE_PACKAGE_ERR, _PE_PACKAGE_ERR_MAGIC);
  }
  else if (
    package[PACKAGE_SIZE - 2] != END_CR ||
    package[PACKAGE_SIZE - 1] != END_LF)
  {
    send_package(_PE_PACKAGE_ERR, _PE_PACKAGE_ERR_CRLF);
  }
}

void handle_bad_crc(PACKAGE, uint16_t crc)
{
  serial_reset();
  send_package(_PE_PACKAGE_CRC, crc);
}


/* -- ОБРАБОТКА ЗАПРОСОВ -- */

void handle_request(PACKAGE)
{
  uint8_t cmd = package[1];

  if (cmd == _P_SERIAL_RESET)
  {
    serial_reset();
  }
  else if (IS_IN_RANGE(cmd, 0xA0, 0xAF))
  {
    handle_data_request(package);
  }
  else if (IS_IN_RANGE(cmd, 0xB0, 0xBF))
  {
    handle_commands_request(package);
  }
  else if (IS_IN_RANGE(cmd, 0xD0, 0xDF))
  {
    handle_commands_request(package);
  }
  else
  {
    send_package(_PE_UNKNOWN_CMD, 0);
  }
}

void handle_data_request(PACKAGE)
{
  uint8_t cmd = package[1];
  uint32_t value = 0;

  if (cmd == _P_REQ_CO2)
  {
    value = mq135.readCO2();
  }
  else if (cmd == _P_REQ_HUM)
  {
    value = FLOAT_TO_INT(dht.readHumidity());
  }
  else if (cmd == _P_REQ_TEMP)
  {
    float temp_lps331 = bar.readTemperatureC();
    float temp_dht = dht.readTemperature();
    float temp_average = (temp_lps331 + temp_dht) / 2.0;

    value = FLOAT_TO_INT(temp_average);
  }
  else if (cmd == _P_REQ_PRES)
  {
    value = FLOAT_TO_INT(bar.readPressureMillibars());
  }
  else if (cmd == _P_REQ_SOLAR_PANELS_EF)
  {
    //uint16_t leftP = analogRead(PIN_SOLAR_PANEL_LEFT) * 100.0 / 1023.0;
    //uint16_t rightP = analogRead(PIN_SOLAR_PANEL_RIGHT) * 100.0 / 1023.0;
    value = 0; //FLOAT_TO_INT(((leftP + rightP) / 2.0));
  }
  else
  {
    cmd = _PE_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}

void handle_commands_request(PACKAGE)
{
  uint8_t cmd = package[1];
  uint32_t value = 0;

  if (cmd == _P_SWITCH_PRES_RELIEF_VALVE)
  {
    pres_relief_valve_active = !pres_relief_valve_active;

    digitalWrite(PIN_PRES_RELIEF_VALVE, pres_relief_valve_active ? LOW : HIGH);
    value = pres_relief_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_STATUS_PRES_RELIEF_VALVE)
  {
    value = pres_relief_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_PUMP_VALVE)
  {
    pump_valve_active = !pump_valve_active;

    digitalWrite(PIN_PUMP_VALVE, pump_valve_active ? LOW : HIGH);
    value = pump_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_STATUS_PUMP_VALVE)
  {
    value = pump_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_PROD_CO2)
  {
    prod_co2_active = !prod_co2_active;

    digitalWrite(PIN_PROD_CO2, prod_co2_active ? HIGH : LOW);
    value = prod_co2_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_STATUS_PROD_CO2)
  {
    value = prod_co2_active ? 0xFF : 0x00;
  }
  else
  {
    cmd = _PE_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}

void handle_time_request(PACKAGE)
{
  uint8_t cmd = package[1];
  uint32_t value = 0;

  uint32_t given_value = (package[2] << 0) &
                         (package[3] << 8) &
                         (package[4] << 16) &
                         (package[5] << 24);

  if (cmd == _P_SET_TIME)
  {
    tmElements_t tm;

    tm.Second = given_value % 60;
    tm.Minute = given_value / 60 % 60;
    tm.Hour   = given_value / 3600 % 24;
    tm.Day   = 1;
    tm.Month = 1;
    tm.Year  = 1970;

    RTC.write(tm);
  }
  else if (cmd == _P_SET_DAY_TIME)
  {
    day_timestamp = given_value;
    EEPROM.put(_MA_DAY_TIME, day_timestamp);
  }
  else if (cmd == _P_SET_NIGHT_TIME)
  {
    night_timestamp = given_value;
    EEPROM.put(_MA_NIGHT_TIME, night_timestamp);
  }
  else if (cmd == _P_GET_TIME)
  {
    tmElements_t tm;
    RTC.read(tm);

    value = (tm.Hour * 60 * 60) + (tm.Minute * 60) + tm.Second;
  }
  else if (cmd == _P_GET_DAY_TIME)
  {
    value = day_timestamp;
  }
  else if (cmd == _P_GET_NIGHT_TIME)
  {
    value = night_timestamp;
  }
  else
  {
    cmd = _PE_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}
