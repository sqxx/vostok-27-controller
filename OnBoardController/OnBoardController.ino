/*
   Программа для управления станцией Восток-27

   Автор: Клачков sqxx Валерий
   Дата: хх.хх.2019

   Для обмена данными с внешним миром используется специфичный протокол,
     описанный в сопроводительном документе
*/

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

#define IN_RANGE(x, l, h) (x >= l && x <= h)

/* -- НАСТРОЙКИ --- */

#define MQ135_PIN A0
#define MQ135_CALLIBRATION_DATA 77

#define DHT_PIN  4
#define DHT_TYPE DHT11

#define PIN_SOLAR_PANEL_LEFT  A1
#define PIN_SOLAR_PANEL_RIGHT A2

#define PIN_PROD_CO2          0
#define PIN_OXYGEN_SUPPLY     0
#define PIN_PRES_RELIEF_VALVE 0

#define PIN_LIGHT             0

#define _MA_STATE  0x00
#define _MA_STATE_INITIALIZED 0xAABBCCDD
#define _MA_DAY_TIME   (_MA_STATE + sizeof(uint32_t))
#define _MA_NIGHT_TIME (_MA_DAY_TIME + sizeof(uint32_t))


/* -- ПРОТОКОЛ --- */

#define PACKAGE_SIZE 8
#define MAGIC_BYTE   0xF4

#define _P_STARTUP       0x01
#define _P_INIT_COMPLETE 0x02
#define _P_NOT_READY     0x03
#define _P_PRESSURE_LOSS 0x04
#define _P_UNKNOWN_CMD   0x0F

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
#define _P_SWITCH_OXYGEN_SUPPLY  0xB3  // Переключить систему подачи кислорода
#define _P_STATUS_OXYGEN_SUPPLY  0xB4  // Состояние системы подачи кислорода  
#define _P_SWITCH_PROD_CO2  0xB5  // Переключить выработку углекислого газа
#define _P_SWITCH_PROD_CO2  0xB6  // Состояние системы выработки углекислого газ

#define _P_SET_TIME       0xD1
#define _P_SET_DAY_TIME   0xD2
#define _P_SET_NIGHT_TIME 0xD3
#define _P_GET_TIME       0xD4
#define _P_GET_DAY_TIME   0xD5
#define _P_GET_NIGHT_TIME 0xD6


/* -- ОБЪЕКТЫ --- */

MQ135 mq135(MQ135_PIN);
DHT dht(DHT_PIN, DHT_TYPE);
Barometer bar;

bool prod_co2_active = false;
bool oxygen_supply_active = false;
bool pres_relief_valve_active = false;

uint32_t day_timestamp = 0;
uint32_t night_timestamp = 0;
bool light_active = false;

/* -- MAIN & LOOP --- */

void setup()
{
  Serial.begin(9600);

  send_package(_P_STARTUP, 0);

  pinMode(PIN_PROD_CO2, OUTPUT);
  pinMode(PIN_OXYGEN_SUPPLY, OUTPUT);
  pinMode(PIN_PRES_RELIEF_VALVE, OUTPUT);

  mq135.heaterPwrHigh();
  dht.begin();
  bar.begin();

  init_eeprom();
  init_time();

  send_package(_P_INIT_COMPLETE, 0);
}

void loop()
{
  handle_time();

  uint8_t package[PACKAGE_SIZE];

  uint16_t given_crc = 0;
  uint16_t calc_crc = 0;

  // Ожидаем, пока прогреется датчик и калибруем
  if (mq135.heatingCompleted() && !mq135.isCalibrated())
  {
    send_package(_P_NOT_READY, 0);

    mq135.calibrate(MQ135_CALLIBRATION_DATA);

    return;
  }

  // Проверяем доступность данных
  if (Serial.available() < PACKAGE_SIZE)
  {
    return;
  }

  // Читаем пакет
  memset(package, 0, PACKAGE_SIZE);
  Serial.readBytes(package, PACKAGE_SIZE);

  if (package[0] != MAGIC_BYTE)
  {
    Serial.println("Magic byte error.");
    return;
  }

  given_crc = (((unsigned int)package[PACKAGE_SIZE - 1]) << 8) | package[PACKAGE_SIZE - 2];
  calc_crc = calculate_crc(package);

  // Проверяем контрольную сумму
  if (given_crc != calc_crc)
  {
    Serial.println("CRC error. Give " + String(given_crc) + ", expected " + String(calc_crc));
    return;
  }

  // Обрабатываем запрос
  handle_request(package);
}


/* -- ЛОГИКА --- */

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

void handle_time()
{
  tmElements_t tm;
  RTC.read(tm);

  int ts = (tm.Hour * 60 * 60) + (tm.Minute * 60) + tm.Second;

  if (day_timestamp <= ts && ts <= night_timestamp)
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

void send_package(uint8_t cmd, uint32_t value)
{
  uint8_t package[PACKAGE_SIZE + 2] = {MAGIC_BYTE, cmd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x0D};
  uint16_t crc = 0;

  package[2] = (value & 0x000000FF);
  package[3] = (value & 0x0000FF00) >> 8;
  package[4] = (value & 0x00FF0000) >> 16;
  package[5] = (value & 0xFF000000) >> 24;

  crc = calculate_crc(package);

  package[6] = (value & 0x00FF);
  package[7] = (value & 0xFF00) >> 8;

  Serial.write(package, PACKAGE_SIZE + 2);
}

uint16_t calculate_crc(uint8_t package[])
{
  uint16_t crc = 0;

  for (uint32_t i = 1; i < (PACKAGE_SIZE - 2); i++)
  {
    crc += package[i];
  }

  return crc;
}

void handle_request(uint8_t package[])
{
  uint8_t cmd = package[1];

  if (IN_RANGE(cmd, 0xA0, 0xAF))
  {
    handle_data_request(package);
  }
  else if (IN_RANGE(cmd, 0xB0, 0xBF))
  {
    handle_commands_request(package);
  }
  else if (IN_RANGE(cmd, 0xD0, 0xDF))
  {
    handle_commands_request(package);
  }
  else
  {
    send_package(_P_UNKNOWN_CMD, 0);
  }
}

void handle_data_request(uint8_t package[])
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
    uint16_t leftP = analogRead(PIN_SOLAR_PANEL_LEFT) * 100.0 / 1023.0;
    uint16_t rightP = analogRead(PIN_SOLAR_PANEL_RIGHT) * 100.0 / 1023.0;
    value = FLOAT_TO_INT(((leftP + rightP) / 2.0));
  }
  else
  {
    cmd = _P_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}

void handle_commands_request(uint8_t package[])
{
  uint8_t cmd = package[1];
  uint32_t value = 0;

  if (cmd == _P_SWITCH_PRES_RELIEF_VALVE)
  {
    pres_relief_valve_active = !pres_relief_valve_active;

    digitalWrite(PIN_PRES_RELIEF_VALVE, pres_relief_valve_active ? HIGH : LOW);
    value = pres_relief_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_STATUS_PRES_RELIEF_VALVE)
  {
    value = pres_relief_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_OXYGEN_SUPPLY)
  {
    oxygen_supply_active = !oxygen_supply_active;

    digitalWrite(PIN_OXYGEN_SUPPLY, oxygen_supply_active ? HIGH : LOW);
    value = oxygen_supply_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_STATUS_OXYGEN_SUPPLY)
  {
    value = oxygen_supply_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_PROD_CO2)
  {
    prod_co2_active = !prod_co2_active;

    digitalWrite(PIN_PROD_CO2, prod_co2_active ? HIGH : LOW);
    value = prod_co2_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_PROD_CO2)
  {
    value = prod_co2_active ? 0xFF : 0x00;
  }
  else
  {
    cmd = _P_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}

void handle_time_request(uint8_t package[])
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
    cmd = _P_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}
