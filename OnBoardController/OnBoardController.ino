/*
   Программа для управления станцией Восток-27

   Автор: Клачков sqxx Валерий
   Дата: хх.хх.2019

   Для обмена данными с внешним миром используется специфичный протокол,
     описанный в сопроводительном документе
*/

/* -- Библиотеки --- */

#include <Wire.h>
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


/* -- НАСТРОЙКИ --- */

#define MQ135_PIN A0
#define DHT_PIN   4

#define MQ135_CALLIBRATION_DATA 77

#define DHT_TYPE DHT11


/* -- ПРОТОКОЛ --- */

#define PACKAGE_SIZE 5
#define MAGIC_BYTE 0xF4

#define PROTOCOL_STARTUP   0x01
#define PROTOCOL_INIT_COMPLETE 0x02
#define PROTOCOL_NOT_READY 0x03

#define PROTOCOL_REQ_CO2  0xA1  // CO2 (ppm)
#define PROTOCOL_REQ_HUM  0xA2  // Влажность (%)
#define PROTOCOL_REQ_TEMP 0xA3  // Температура (C)
#define PROTOCOL_REQ_PRES 0xA4  // Давление (mbar)


/* -- ОБЪЕКТЫ --- */

MQ135 mq135(MQ135_PIN);
DHT dht(DHT_PIN, DHT_TYPE);
Barometer bar;


/* -- MAIN & LOOP --- */

void setup()
{
  Serial.begin(9600);

  send_package(PROTOCOL_STARTUP, 0);

  pinMode(13, OUTPUT);
  
  mq135.heaterPwrHigh();
  dht.begin();
  bar.begin();

  send_package(PROTOCOL_INIT_COMPLETE, 0);
}

void loop()
{
  byte package[PACKAGE_SIZE];
  byte package_crc = 0;

  // Ожидаем, пока прогреется датчик и калибруем
  if (mq135.heatingCompleted() && !mq135.isCalibrated())
  {
    send_package(PROTOCOL_NOT_READY, 0);

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

  package_crc = calculate_crc(package);

  // Проверяем контрольную сумму
  if (package[PACKAGE_SIZE - 1] != package_crc)
  {
    Serial.println("CRC error. Give " + String(package[PACKAGE_SIZE - 1]) + ", expected " + String(package_crc));
    return;
  }

  // Обрабатываем запрос
  handle_request(package);
}


/* -- ЛОГИКА --- */

byte send_package(byte cmd, int value)
{
  byte package[PACKAGE_SIZE + 2] = {MAGIC_BYTE, cmd, 0x00, 0x00, 0x00, 0x0A, 0x0D};

  package[2] = value & 0xFF;
  package[3] = value & 0xFF00;

  package[PACKAGE_SIZE - 1] = calculate_crc(package);

  Serial.write(package, PACKAGE_SIZE + 2);
}

byte calculate_crc(byte package[])
{
  byte crc = 0;

  for (int i = 1; i < (PACKAGE_SIZE - 1); i++)
  {
    crc += package[i];
  }

  return crc;
}

void handle_request(byte package[])
{
  byte cmd = package[1];
  int value = 0;

  if (cmd == PROTOCOL_REQ_CO2)
  {
    value = mq135.readCO2();
  }
  else if (cmd == PROTOCOL_REQ_HUM)
  {
    value = FLOAT_TO_INT(dht.readHumidity());
  }
  else if (cmd == PROTOCOL_REQ_TEMP)
  {
    float temp_lps331 = bar.readTemperatureC();
    float temp_dht = dht.readTemperature();
    float temp_average = (temp_lps331 + temp_dht) / 2.0;
    
    value = FLOAT_TO_INT(temp_average);
  }
  else if (cmd == PROTOCOL_REQ_PRES)
  {
    value = FLOAT_TO_INT(bar.readPressureMillibars());
  }

  send_package(cmd, value);
}
