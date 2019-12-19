/* Protocol.ino
   Функции для реализации протокола Восток-27
   
   Исходный код станции Восток-27
*/

#include "Protocol.h"
#include "Hardware.h"


/* --- ОСНОВНАЯ ЧАСТЬ --- */

void clear_broken_packages()
{
  while (Serial.read() != END_LF);
}

// Отправляет пакет по Serial
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

// Подсчитывает контрольную сумму для переданного пакета
uint32_t calculate_crc(PACKAGE_TYPE package[])
{
  uint32_t crc = 0;

  for (uint32_t i = 1; i < (PACKAGE_SIZE - 4); i++)
  {
    crc += package[i];
  }

  return crc;
}

// Извлекает контрольную сумму из переданного пакета
uint16_t extract_crc(PACKAGE_TYPE package[])
{
  byte high_byte = (uint16_t) package[PACKAGE_SIZE - 4];
  byte low_byte  = package[PACKAGE_SIZE - 3];

  return (high_byte << 8) | low_byte;
}


/* --- ОБРАБОТКА ИСКЛЮЧЕНИЙ --- */

void handle_bad_package(PACKAGE_TYPE package[])
{
  clear_broken_packages();
  
  if (package[0] != START_MAGIC)
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

void handle_bad_crc(PACKAGE_TYPE package[], uint16_t crc)
{
  clear_broken_packages();
  send_package(_PE_PACKAGE_CRC, crc);
}


/* --- ОБРАБОТКА ЗАПРОСОВ --- */

void handle_request(PACKAGE_TYPE package[])
{
  uint8_t cmd = package[1];

  if (IS_IN_RANGE_IN(cmd, 0xA0, 0xAF))
  {
    handle_data_request(package);
  }
  else if (IS_IN_RANGE_IN(cmd, 0xB0, 0xBF))
  {
    handle_commands_request(package);
  }
  else if (IS_IN_RANGE_IN(cmd, 0xD0, 0xDF))
  {
    handle_commands_request(package);
  }
  else
  {
    send_package(_PE_UNKNOWN_CMD, 0);
  }
}

void handle_data_request(PACKAGE_TYPE package[])
{
  uint8_t  cmd   = package[1];
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
  else if (cmd == _P_REQ_BAT_VOLTAGE)
  {
    value = map(analogRead(PIN_BATTERY_VOLTAGE), 0, 1023, 0, 12);
  }
  else if (cmd == _P_REQ_ENERGY_USAGE)
  {
    value = map(analogRead(PIN_ENERGY_USAGE), 0, 1023, 0, 100);
  }
  else if (cmd == _P_REQ_ENERGY_GEN)
  {
    uint16_t r = analogRead(PIN_SOLAR_PANEL_RIGHT);
    uint16_t l = analogRead(PIN_SOLAR_PANEL_LEFT);
    value = map((r + l) / 2, 0, 1023, 0, 100);
  }
  else
  {
    cmd = _PE_UNKNOWN_CMD;
    value = 0;
  }

  send_package(cmd, value);
}

void handle_commands_request(PACKAGE_TYPE package[])
{
  uint8_t  cmd   = package[1];
  uint32_t value = 0;

  if (cmd == _P_SWITCH_PRES_RELIEF_VALVE)
  {
    SWITCH(PIN_PRES_RELIEF_VALVE, pres_relief_valve_active);
  }
  else if (cmd == _P_STATUS_PRES_RELIEF_VALVE)
  {
    value = pres_relief_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_PUMP_VALVE)
  {
    SWITCH(PIN_PUMP_VALVE, pump_valve_active);
  }
  else if (cmd == _P_STATUS_PUMP_VALVE)
  {
    value = pump_valve_active ? 0xFF : 0x00;
  }
  else if (cmd == _P_SWITCH_PROD_CO2)
  {
    SWITCH(PIN_PROD_CO2, prod_co2_active);
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

void handle_time_request(PACKAGE_TYPE package[])
{
  uint8_t  cmd   = package[1];
  uint32_t value = 0;

  uint32_t given_value = (package[2] << 0)  &
                         (package[3] << 8)  &
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
    EEPROM.put(EEPROMA_DAY_TIME, day_timestamp);
  }
  else if (cmd == _P_SET_NIGHT_TIME)
  {
    night_timestamp = given_value;
    EEPROM.put(EEPROMA_NIGHT_TIME, night_timestamp);
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
