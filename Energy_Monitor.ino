#include <Arduino.h>          // Стандартная библиотека Arduino — базовая функциональность платформы
#include <PZEM004Tv30.h>      // Библиотека для работы с энергометром PZEM‑004T версии 3.0
#include <SoftwareSerial.h>   // Библиотека для создания программного последовательного порта
#include <PubSubClient.h>     // Библиотека MQTT‑клиента для Arduino
#include <ESP8266WiFi.h>      // Работа с Wi‑Fi на ESP8266: подключение, статус, IP и т.д.
#include <ESP8266mDNS.h>      // Поддержка mDNS: позволяет обращаться к устройству по имени (например, PZEM004MQTT.local)
#include <WiFiUdp.h>          // Поддержка UDP: нужна для некоторых сетевых функций и OTA
#include <ArduinoOTA.h>       // Поддержка OTA (Over‑The‑Air): обновление прошивки по Wi‑Fi без кабеля

// ===== КОНФИГУРАЦИЯ СИСТЕМЫ =====
#define PZEM_RX_PIN D4           // Пин RX для SoftwareSerial (приём данных от PZEM)
#define PZEM_TX_PIN D3           // Пин TX для SoftwareSerial (передача данных к PZEM)
#define WIFI_SSID "SSID"         // SSID Wi‑Fi сети для подключения
#define WIFI_PASS "PASSWORD"     // Пароль Wi‑Fi сети

const char* ota_hostname = "PZEM2MQTT";           // Имя хоста для OTA и MQTT
const char* mqtt_host = "10.10.10.10";            // IP-адрес MQTT-сервера
const int mqtt_port = 1883;                       // Порт MQTT-сервера (стандартный)
const char* mqtt_user = "UserMQTT";               // Логин для аутентификации в MQTT
const char* mqtt_pass = "PassMQTT";               // Пароль для MQTT (символ " экранирован)
const String mqtt_base_topic = "homeassistant";   // Базовый топпик для публикаций в MQTT
// =============================

// Клиент для работы с Wi‑Fi
WiFiClient espClient;
// Клиент для работы с MQTT
PubSubClient mqttClient(espClient);

// Инициализация SoftwareSerial для связи с устройствами PZEM
SoftwareSerial pzemSerial(PZEM_RX_PIN, PZEM_TX_PIN);
// Объекты для трёх устройств PZEM с уникальными адресами (0x01, 0x02, 0x03)
PZEM004Tv30 pzem1(pzemSerial, 0x01);

// Структура для хранения данных, получаемых от устройства PZEM
struct PZEMData {
  float voltage;    // Напряжение, В
  float current;    // Ток, А
  float power;      // Мощность, Вт
  float energy;     // Энергия, кВт·ч
  float frequency;  // Частота, Гц
  float pf;         // Коэффициент мощности (power factor, безразмерная величина)
};

// Экземпляры структуры для трёх устройств
PZEMData data1;

unsigned long lastMsg = 0;                       // Переменная для хранения времени (в миллисекундах) последней отправки сообщения MQTT
String mqttChipID;                               // Переменная для хранения Chip ID в строковом формате
const int MAX_ERROR_COUNT = 3;                   // Максимальное количество ошибок перед перезагрузкой
int errorCount = 0;                              // Счётчик ошибок

void setup() {
  Serial.begin(115200);  // Инициализация Serial‑порта для отладки (скорость 115200 бод)
  Serial.println();      // выводим пустую строку для разделения сообщений

  // Устанавливаем режим работы Wi‑Fi как станция (STA)
  WiFi.mode(WIFI_STA);
  // Устанавливаем имя хоста: важно для mDNS и корректной работы OTA
  WiFi.hostname(ota_hostname);
  // Начинаем подключение к указанной Wi‑Fi сети
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  // Ожидаем установления соединения с Wi‑Fi
  // Каждые 500 мс выводим точку для индикации процесса
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Настройка событий OTA (обновление прошивки по Wi‑Fi)
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";      // Обновление прошивки (скетча)
    } else {
      type = "filesystem";  // Обновление файловой системы (FS)
    }
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");  // Событие: обновление завершено
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Показываем прогресс в процентах
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    // Обработка ошибок OTA: выводим код и описание
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");      // Ошибка аутентификации (неверный пароль)
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");      // Не удалось начать обновление
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");    // Ошибка соединения
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");    // Ошибка приёма данных
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");        // Ошибка завершения обновления
    }
  });
  
  // Устанавливаем пароль для OTA: теперь при обновлении потребуется ввести этот пароль
  ArduinoOTA.setPassword((const char*)"Password");

  // Запускаем OTA‑сервер: теперь устройство готово принимать обновления по Wi‑Fi
  ArduinoOTA.begin(ota_hostname);
  
  // После успешного подключения выводим информацию о подключении 
  Serial.println();
  Serial.print("Успешное подключение к сети Wi-Fi: ");
  Serial.println(WiFi.localIP()); // Выводим локальный IP‑адрес устройства

  // Получаем уникальный идентификатор микроконтроллера (Chip ID) как 32‑битное число
  uint32_t chipId = ESP.getChipId();
  
  // Преобразуем в строку шестнадцатеричного представления (без префикса "0x")
  mqttChipID = String(chipId, HEX);

  // Настройка MQTT‑клиента: задаём сервер и порт
  mqttClient.setServer(mqtt_host, mqtt_port);
  
  // Увеличиваем размер буфера MQTT‑клиента до 512 байт
  // Это позволяет обрабатывать более длинные сообщения
  mqttClient.setBufferSize(512);
  
  // Подключаемся к MQTT‑серверу
  connectToMQTT();

  // Публикуем конфигурационные сообщения (MQTT Discovery) для Home Assistant
  // Эти сообщения позволяют автоматически обнаружить устройства в системе
  // Публикуем для каждого из трёх устройств PZEM с уникальные именами на основе Chip ID
  publishDiscovery(mqttChipID + "_pzem1", 0x01);

  // Даём системе время стабилизироваться перед первым опросом PZEM
  Serial.println("Ожидание 5 секунд перед первым чтением данных с PZEM...");
  delay(5000);
}

void loop() {
  // Обязательно вызываем в loop(): обрабатывает входящие OTA‑запросы
  ArduinoOTA.handle();
  
  // Проверяем, подключён ли MQTT‑клиент
  if (!mqttClient.connected()) {
    // Если подключение потеряно — выводим сообщение и пытаемся переподключиться
    Serial.println("MQTT отключен, повторное подключение...");
    connectToMQTT();
  }

  // Обрабатываем MQTT‑события (подписка, публикации и т. д.)
  mqttClient.loop();

  // Получаем текущее время в миллисекундах с момента запуска программы
  unsigned long now = millis();
  
  // Проверяем, прошло ли более 2000 мс с момента последней публикации данных
  if (now - lastMsg > 2000) {
    // Обновляем метку времени последней публикации
    lastMsg = now;

    // Считываем данные с первого устройства PZEM
    data1 = readPZEM(pzem1, "PZEM1");
    // Публикуем полученные данные в MQTT c уникальным именем "mqttChipID_pzem1"
    publishData(mqttChipID + "_pzem1", data1);
  }
}

// Функция чтения данных с устройства PZEM с валидацией полученных значений
PZEMData readPZEM(PZEM004Tv30 &pzem, const String &label) {
  PZEMData d;

  // Добавляем небольшую задержку перед чтением данных
  delay(10);

  // Считываем параметры с устройства PZEM
  d.voltage = pzem.voltage();
  d.current = pzem.current();
  d.power = pzem.power();
  d.energy = pzem.energy();
  d.frequency = pzem.frequency();
  d.pf = pzem.pf();

  // Флаги валидности данных
  bool isValid = true;
  String invalidReasons = "";

  // Проверка на невалидные значения (NaN)
  if (isnan(d.voltage)) {
    invalidReasons += "NaN voltage, ";
    isValid = false;
  }
  if (isnan(d.current)) {
    invalidReasons += "NaN current, ";
    isValid = false;
  }
  if (isnan(d.power)) {
    invalidReasons += "NaN power, ";
    isValid = false;
  }
  if (isnan(d.energy)) {
    invalidReasons += "NaN energy, ";
    isValid = false;
  }
  if (isnan(d.frequency)) {
    invalidReasons += "NaN frequency, ";
    isValid = false;
  }
  if (isnan(d.pf)) {
    invalidReasons += "NaN power factor, ";
    isValid = false;
  }

  // Логические проверки границ значений
  if (d.voltage < 100 || d.voltage > 260) {  // Напряжение: 100–260 В
    invalidReasons += "voltage вне диапазона (" + String(d.voltage) + " V), ";
    isValid = false;
  }
  if (d.current < 0 || d.current > 100) {  // Ток: 0–100 А
    invalidReasons += "current вне диапазона (" + String(d.current) + " A), ";
    isValid = false;
  }
  if (d.power < 0 || d.power > 25000) {  // Мощность: 0–25 кВт
    invalidReasons += "power вне диапазона (" + String(d.power) + " W), ";
    isValid = false;
  }
  if (d.energy < 0 || d.energy > 100000) {  // Энергия: 0–100 000 kWh
    invalidReasons += "energy вне диапазона (" + String(d.energy) + " kWh), ";
    isValid = false;
  }
  if (d.frequency < 45 || d.frequency > 55) {  // Частота: 45–55 Гц
    invalidReasons += "frequency вне диапазона (" + String(d.frequency) + " Hz), ";
    isValid = false;
  }
  if (d.pf < 0 || d.pf > 1) {  // Коэффициент мощности: 0–1
    invalidReasons += "power factor вне диапазона (" + String(d.pf) + "), ";
    isValid = false;
  }

  if (!isValid) {
    errorCount++;
    Serial.println("Обнаружены невалидные данные от " + label + ": " + invalidReasons);
    Serial.println("Счётчик ошибок: " + String(errorCount) + "/" + String(MAX_ERROR_COUNT));

    // Сброс всех значений на 0
    d.voltage = 0;
    d.current = 0;
    d.power = 0;
    d.energy = 0;
    d.frequency = 0;
    d.pf = 0;

    // Переинициализация PZEM
    pzem.resetEnergy();
    delay(1000);

    // Если превышено максимальное количество ошибок — перезагрузка
    if (errorCount >= MAX_ERROR_COUNT) {
      Serial.println("Критическое количество ошибок. Перезагрузка ESP...");
      delay(2000);
      ESP.restart();
    }
  } else {
    errorCount = 0;  // Сброс счётчика при успешных данных
  }

  return d;
}

// Функция подключения к MQTT-серверу с повторными попытками
void connectToMQTT() {
  int attempts = 0; // Счётчик попыток подключения

  // Пытаемся подключиться, пока не удастся или не исчерпаем попытки (максимум 5)
  while (!mqttClient.connected() && attempts < 5) {
    // Пытаемся подключиться с указанием имени хоста, логина и пароля
    if (mqttClient.connect(ota_hostname, mqtt_user, mqtt_pass)) {
      Serial.println("Успешное подключение к MQTT серверу!");
      return; // Успешное подключение — выходим из функции
    } else {
      // Вывод кода ошибки подключения
      Serial.print("Неудачное подключение к MQTT, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" — повтор попытки через 2 секунды");
      delay(2000); // Ждём 2 секунды перед следующей попыткой
      attempts++;   // Увеличиваем счётчик попыток
    }
  }

  // Если после 5 попыток подключение не удалось — перезапускаем устройство
  if (!mqttClient.connected()) {
    Serial.print("Перезагрузка ESP...");
    delay(1000);
    ESP.restart();
  }
}

// Функция публикации конфигурационных сообщений для Home Assistant (MQTT Discovery)
// Позволяет автоматически обнаруживать устройства и датчики в системе Home Assistant
void publishDiscovery(const String& name, uint8_t addr) {
  // Массивы с параметрами для каждого типа данных, получаемых от PZEM
  const String keys[] = {"voltage", "current", "power", "energy", "frequency", "pf"};      // Названия параметров
  const String units[] = {"V", "A", "W", "kWh", "Hz", ""};                         // Единицы измерения
  const String device_class[] = {"voltage", "current", "power", "energy", "frequency", "power_factor"}; // Классы устройств (для правильной интерпретации в HA)
  // Русскоязычные понятные названия для friendly_name (без технических префиксов)
  const String friendlyNames[] = {
    "Напряжение",           // voltage
    "Ток",                  // current
    "Мощность",             // power
    "Энергопотребление",    // energy (лучше чем "Энергия" для счётчика)
    "Частота",              // frequency
    "Коэффициент мощности"  // pf
  };
  // Массив иконок для каждого параметра (MDI-иконки)
  const String icons[] = {
    "mdi:flash",            // voltage — молния (напряжение)
    "mdi:current-ac",       // current — переменный ток
    "mdi:lightning-bolt",   // power — разряд молнии (мощность)
    "mdi:counter",          // energy — счётчик (энергопотребление)
    "mdi:wave",             // frequency — волна (частота)
    "mdi:function"          // pf — математическая функция (коэф. мощности)
  };

  // Проходим по всем 6 типам параметров (напряжение, ток, мощность и т. д.)
  for (int i = 0; i < 6; i++) {
    String key = keys[i];  // Получаем название параметра (например, "voltage")

    // Формируем MQTT-топпик для конфигурационного сообщения
    // Пример: homeassistant/sensor/pzem1_voltage/config
    String topic = mqtt_base_topic + "/sensor/" + name + "_" + key + "/config";

    // Начинаем формировать JSON-payload для MQTT Discovery
    String payload = "{";

    // Добавляем поле "name" — читаемое имя датчика в Home Assistant
    //payload += "\"name\": \"" + name + "_" + key + "\",";
    //payload += "\"name\": \"EnergyCounter\",";
    payload += "\"name\": \"" + mqttChipID + "_pzem" + String(addr) + "_" + friendlyNames[i] + "\",";

    // Добавляем поле "state_topic" — топпик, откуда HA будет читать текущие значения
    // Пример: homeassistant/pzem1/voltage
    payload += "\"state_topic\": \"" + mqtt_base_topic + "/" + name + "/" + key + "\",";

    // Добавляем поле "unit_of_measurement" — единица измерения (В, А, Вт и т. д.)
    payload += "\"unit_of_measurement\": \"" + units[i] + "\",";

    // Читаемое имя на русском
    payload += "\"friendly_name\": \"" + friendlyNames[i] + "_" + String(addr) + "\",";

    // Добавляем поле "device_class" — класс устройства для правильной иконки и интерпретации в HA
    payload += "\"device_class\": \"" + device_class[i] + "\",";

    // Добавляем иконку
    payload += "\"icon\": \"" + icons[i] + "\"";

    // Для параметра "energy" (энергия) добавляем специальное поле "state_class"
    // "total_increasing" означает, что значение только растёт (счётчик кВт·ч)
    if (key.equals("energy")) {
      payload += ",\"state_class\": \"total_increasing\"";
    }

    // Для параметра "power" (мощность) добавляем специальное поле "state_class"
    // "measurement" означает, сенсор измеряет текущее значение, а не предсказанное или агрегированное,
    // и необходим для сбора долгосрочной статистики
    if (key.equals("power")) {
      payload += ",\"state_class\": \"measurement\"";
    }

    // Добавляем поле "unique_id" — уникальный идентификатор датчика
    payload += ",\"unique_id\": \"" + name + "_" + key + "\"";

    // Добавляем объект "device" — информация об устройстве в целом
    // Используется для группировки датчиков в один девайс в Home Assistant
    //payload += ",\"device\": {\"identifiers\": [\"" + name + "\"], \"name\": \"Energy Monitor\",\"model\": \"PZEM004T v3\",\"manufacturer\": \"Xenon\" }";
    payload += ",\"device\": {";
    payload += "\"identifiers\": [\"" + name + "\"],";
    payload += "\"name\": \"Energy Monitor\",";
    payload += "\"model\": \"PZEM004T v3\",";
    payload += "\"manufacturer\": \"Xenon\" }";

    // Завершаем JSON-объект
    payload += "}";

    // Публикуем конфигурационное сообщение в MQTT
    // Параметры:
    // - topic.c_str() — топпик (конвертируем String в C-строку)
    // - payload.c_str() — тело сообщения (JSON)
    // - true — флаг retained: сообщение сохраняется на сервере и доставляется новым подписчикам
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
  }
}

void publishData(const String& name, const PZEMData& d) {
  // Публикуем текущие значения всех параметров для указанного устройства (name)
  // Каждое значение публикуется в отдельный топпик с флагом retained (true)
  mqttClient.publish((mqtt_base_topic + "/" + name + "/voltage").c_str(), String(d.voltage).c_str(), true); // Напряжение (V)
  mqttClient.publish((mqtt_base_topic + "/" + name + "/current").c_str(), String(d.current).c_str(), true); // Ток (A)
  mqttClient.publish((mqtt_base_topic + "/" + name + "/power").c_str(), String(d.power).c_str(), true); // Мощность (W)
  mqttClient.publish((mqtt_base_topic + "/" + name + "/energy").c_str(), String(d.energy).c_str(), true); // Энергия (kWh)
  mqttClient.publish((mqtt_base_topic + "/" + name + "/frequency").c_str(), String(d.frequency).c_str(), true); // Частота (Hz)
  mqttClient.publish((mqtt_base_topic + "/" + name + "/pf").c_str(), String(d.pf).c_str(), true); // Коэффициент мощности (power factor)
}
