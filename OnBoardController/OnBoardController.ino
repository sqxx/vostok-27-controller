#include <TimeLib.h>
#include <DS1307RTC.h>
#include <EEPROM.h>
#include <TroykaMQ.h>
#include <TroykaIMU.h>
#include <DHT.h>

#include "Hardware.h"
#include "Protocol.h"
#include "MathHelpers.h"

#define INIT_RELAY(pin) pinMode(pin, OUTPUT); \
                        STOP(pin);            \

#define SWITCH(pin, var) var = !var;                 \
                         if (var) RUN(pin);          \
                         else     STOP(pin);         \
                         value = var ? 0xFF : 0x00;  \


bool first_start = true;

MQ135 mq135(MQ135_PIN);
DHT   dht(DHT_PIN, DHT_TYPE);
Barometer bar;

bool pres_relief_valve_active = false;
bool pump_valve_active   = false;
bool prod_co2_active = false;

bool light_active = false;
bool auto_light_active = true;

uint32_t day_timestamp   = 0;
uint32_t night_timestamp = 0;


/* -- ИНИЦИАЛИЗАЦИЯ --- */

void setup()
{
  first_start = true;
  
  Serial.begin(9600);

  NOTIFY(_P_STARTUP);

  INIT_RELAY(PIN_PRES_RELIEF_VALVE)  // Сброс давления
  INIT_RELAY(PIN_LIGHT_CONTROL)      // Управление освещением
  INIT_RELAY(PIN_PUMP_VALVE)         // Раздув станции
  INIT_RELAY(PIN_HEAT_MODULE)        // Обогреватель
  INIT_RELAY(PIN_CO2_NUTRALIZATION)  // Нейтрализатор CO2
  INIT_RELAY(PIN_PROD_CO2)           // Генератор CO2    

  // Блокировка блока реле
  pinMode(PIN_RELAY_BLOCK, OUTPUT);
  digitalWrite(PIN_RELAY_BLOCK, LOW);

  // Инициализация датчиков
  mq135.heaterPwrHigh();
  dht.begin();
  bar.begin();

  setup_eeprom();
  setup_rtc();

  NOTIFY(_P_INIT_COMPLETE);
}

// Подгружает настройки из EEPROM памяти или инициализирует её
void setup_eeprom()
{
  uint32_t crc = 0x00;
  EEPROM.get(EEPROMA_STATE, crc);

  if (crc == EEPROMV_STATE_INITIALIZED)
  {
    EEPROM.get(EEPROMA_DAY_TIME, day_timestamp);
    EEPROM.get(EEPROMA_NIGHT_TIME, night_timestamp);
  }
  else
  {
    EEPROM.put(EEPROMA_DAY_TIME, 0x00);
    EEPROM.put(EEPROMA_NIGHT_TIME, 0x00);

    EEPROM.put(EEPROMA_STATE, EEPROMV_STATE_INITIALIZED);
  }
}

// Синхронизирует rtc модуль со временем на ПК
void setup_rtc()
{
  tmElements_t tm;

  if (!RTC.read(tm))
  {
    int Hour, Min, Sec;

    sscanf(__TIME__, "%d:%d:%d", &Hour, &Min, &Sec);

    tm.Hour   = Hour;
    tm.Minute = Min;
    tm.Second = Sec;
    tm.Day    = 1;
    tm.Month  = 1;
    tm.Year   = 1970;
  }
}


/* -- ЖИЗНЕННЫЙ ЦИКЛ --- */

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

  // Проверка доступности данных
  if (Serial.available() < PACKAGE_SIZE) return;

  // Снимаем блокировку блока реле
  if (first_start)
  {
    digitalWrite(PIN_RELAY_BLOCK, HIGH);
    first_start = false;
  }
  
  // Читаем пакет
  memset(package, 0, PACKAGE_SIZE);
  Serial.readBytes(package, PACKAGE_SIZE);

  // Проверка маркеров пакета (см. документацию)
  if (PACKAGE_MARKERS_NOT_VALID(package))
  {
    handle_bad_package(package);
    return;
  }

  // Проверяем контрольную сумму
  package_crc = extract_crc(package);
  calc_crc    = calculate_crc(package);

  if (package_crc != calc_crc)
  {
    handle_bad_crc(package, package_crc);
    return;
  }

  // Обрабатываем запрос
  handle_request(package);
}

// Управление освещением на станции
void handle_light()
{
  if (!auto_light_active) return;

  tmElements_t tm;
  RTC.read(tm);

  int ts = (tm.Hour * 60 * 60) + (tm.Minute * 60);

  if (ts >= day_timestamp &&
      ts < night_timestamp)
  {
    if (!light_active) return;
    
    digitalWrite(PIN_LIGHT, LOW);
    light_active = false;
  }
  else
  {
    if (light_active) return
    
    digitalWrite(PIN_LIGHT, HIGH);
    light_active = true;
  }
}

/* Запускает калибровку MQ-135
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
