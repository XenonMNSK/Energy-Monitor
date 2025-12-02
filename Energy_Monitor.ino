#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>
#include <SettingsGyver.h>
#include <PubSubClient.h>

// ===== КОНФИГУРАЦИЯ СИСТЕМЫ =====
#define PZEM_RX_PIN D4           // Пин RX для SoftwareSerial (приём данных от PZEM)
#define PZEM_TX_PIN D3           // Пин TX для SoftwareSerial (передача данных к PZEM)
#define WIFI_SSID "SSID"     // SSID Wi‑Fi сети для подключения
#define WIFI_PASS "PASSWORD" // Пароль Wi‑Fi сети

const char* ota_hostname = "PZEM004MQTT";        // Имя хоста для OTA и MQTT
const char* mqtt_host = "10.10.10.10";            // IP-адрес MQTT-сервера
const int mqtt_port = 1883;                      // Порт MQTT-сервера (стандартный)
const char* mqtt_user = "UserMQTT";                 // Логин для аутентификации в MQTT
const char* mqtt_pass = "PassMQTT";    // Пароль для MQTT (символ " экранирован)
const String mqtt_base_topic = "homeassistant";  // Базовый топпик для публикаций в MQTT
// =============================

WiFiClient espClient;                            // Клиент для работы с Wi‑Fi
PubSubClient mqttClient(espClient);              // Клиент для работы с MQTT

// Инициализация SoftwareSerial для связи с устройствами PZEM
SoftwareSerial pzemSerial(PZEM_RX_PIN, PZEM_TX_PIN);
// Объекты для трёх устройств PZEM с уникальными адресами (0x01, 0x02, 0x03)
PZEM004Tv30 pzem1(pzemSerial, 0x01);
PZEM004Tv30 pzem2(pzemSerial, 0x02);
PZEM004Tv30 pzem3(pzemSerial, 0x03);

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
PZEMData data1, data2, data3;

// Объект для работы с пользовательским интерфейсом
SettingsGyver sett("Energy Monitor");

unsigned long lastMsg = 0;
int slider;                                      // Переменная для слайдера в интерфейсе
String input;                                    // Переменная для текстового ввода в интерфейсе
String mqttChipID;                               // Переменная для хранения Chip ID в строковом формате
bool swit;                                       // Переменная для переключателя в интерфейсе

// Функция построения пользовательского интерфейса
void build(sets::Builder& b) {
  //b.Slider("My slider", 0, 50, 1, "", &slider); // Слайдер: диапазон 0–50, шаг 1
  //b.Input("My input", &input);                  // Поле для ввода текста
  //b.Switch("My switch", &swit);                 // Переключатель (on/off)
}

void setup() {
  // Инициализация Serial‑порта для отладки (скорость 115200 бод)
  Serial.begin(115200);
  Serial.println();

  // Устанавливаем режим работы Wi‑Fi как станция (STA)
  WiFi.mode(WIFI_STA);
  // Начинаем подключение к указанной Wi‑Fi сети
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  // Ожидаем установления соединения с Wi‑Fi
  // Каждые 500 мс выводим точку для индикации процесса
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // После успешного подключения выводим информацию о подключении 
  Serial.println();
  Serial.print("Connected: ");
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
  publishDiscovery(mqttChipID + "_pzem2", 0x02);
  publishDiscovery(mqttChipID + "_pzem3", 0x03);

  // Запускаем пользовательский интерфейс
  sett.begin();
  
  // Указываем функцию для построения интерфейса
  sett.onBuild(build);
}

void loop() {
  // Обновляем пользовательский интерфейс
  // Обрабатываем события интерфейса (нажатия, изменения значений и т. д.)
  sett.tick();

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

    // Считываем данные со второго устройства PZEM
    data2 = readPZEM(pzem2, "PZEM2");
    // Публикуем полученные данные в MQTT c уникальным именем "mqttChipID_pzem2"
    publishData(mqttChipID + "_pzem2", data2);

    // Считываем данные с третьего устройства PZEM
    data3 = readPZEM(pzem3, "PZEM3");
    // Публикуем полученные данные в MQTT c уникальным именем "mqttChipID_pzem3"
    publishData(mqttChipID + "_pzem3", data3);
  }
}

// Функция чтения данных с устройства PZEM
PZEMData readPZEM(PZEM004Tv30 &pzem, const String &label) {
  PZEMData d;

  // Считываем параметры с устройства PZEM
  d.voltage = pzem.voltage();
  d.current = pzem.current();
  d.power = pzem.power();
  d.energy = pzem.energy();
  d.frequency = pzem.frequency();
  d.pf = pzem.pf();

  // Проверка на невалидное значение напряжения (NaN)
  // Если значение невалидно — перезапускаем устройство
  if (isnan(d.voltage)) {
    delay(1000);
    Serial.println("Устройства PZEM не обнаружены");
    ESP.restart();
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
      Serial.println(" присоединён!");
      return; // Успешное подключение — выходим из функции
    } else {
      // Вывод кода ошибки подключения
      Serial.print(" неудачно, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" — повтор попытки через 2 секунды");
      delay(2000); // Ждём 2 секунды перед следующей попыткой
      attempts++;   // Увеличиваем счётчик попыток
    }
  }

  // Если после 5 попыток подключение не удалось — перезапускаем устройство
  if (!mqttClient.connected()) {
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

  // Проходим по всем 6 типам параметров (напряжение, ток, мощность и т. д.)
  for (int i = 0; i < 6; i++) {
    String key = keys[i];  // Получаем название параметра (например, "voltage")

    // Формируем MQTT-топпик для конфигурационного сообщения
    // Пример: homeassistant/sensor/pzem1_voltage/config
    String topic = mqtt_base_topic + "/sensor/" + name + "_" + key + "/config";

    // Начинаем формировать JSON-payload для MQTT Discovery
    String payload = "{";

    // Добавляем поле "name" — техническое имя датчика в Home Assistant
    payload += "\"name\": \"" + name + "_" + key + "\",";

    // Добавляем поле "state_topic" — топпик, откуда HA будет читать текущие значения
    // Пример: homeassistant/pzem1/voltage
    payload += "\"state_topic\": \"" + mqtt_base_topic + "/" + name + "/" + key + "\",";

    // Добавляем поле "unit_of_measurement" — единица измерения (В, А, Вт и т. д.)
    payload += "\"unit_of_measurement\": \"" + units[i] + "\",";

    // Читаемое имя на русском
    payload += "\"friendly_name\": \"" + friendlyNames[i] + "\",";

    // Добавляем поле "device_class" — класс устройства для правильной иконки и интерпретации в HA
    payload += "\"device_class\": \"" + device_class[i] + "\"";

    // Для параметра "energy" (энергия) добавляем специальное поле "state_class"
    // "total_increasing" означает, что значение только растёт (счётчик кВт·ч)
    if (key.equals("energy")) {
      payload += ",\"state_class\": \"total_increasing\"";
    }

    // Добавляем поле "unique_id" — уникальный идентификатор датчика
    payload += ",\"unique_id\": \"" + name + "_" + key + "\"";

    // Добавляем объект "device" — информация об устройстве в целом
    // Используется для группировки датчиков в один девайс в Home Assistant
    payload += ",\"device\": {\"identifiers\": [\"" + name + "\"], \"name\": \"" + name + "\",\"model\": \"PZEM004T v3\",\"manufacturer\": \"Xenon\" }";

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
