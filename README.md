## Energy-Monitor для Home Assistant

![Home Assistant](https://img.shields.io/badge/HomeAssistant-latest-yellowgreen?style=plastic&logo=homeassistant)

## Содержание.
* [**_Описание._**](#описание)
* [**_Возможности._**](#возможности)

## Описание.
Скетч написан на Arduino IDE для создания счетчика потребления электроэнергии на базе Wemos D1 Mini и модулей PZEM-004T.
В данном скетче описано подключение 3х модулей PZEM-004T, но по аналогии можно как уменьшить, так и увеличить количество считываемых модулей. 
> [!NOTE]
> PZEM‑004T работает по протоколу Modbus RTU. По умолчанию все модули имеют одинаковый адрес (0x01). При подключении нескольких модулей PZEM-004T нужно заранее изменить уникальный адрес: 0x01, 0x02, 0x03 и так далее. Максимальное число устройств на одной шине Modbus — до 247, но на практике ограничиваются 10–20 из‑за падения скорости.

```
#include <CODE>

```

Notes:
* Will messages will only be active if `will.topic` is not empty.
* The `will` structure can be modified at any time, even when a connection is active.  However, it changes will take effect only after the client reconnects (after connection loss or after calling `mqtt.disconnect()`).
* Default values of `will.qos` and `will.retain` are `0` and `false` respectively.

### Notes

* When consuming or producing a message using the advanced API, don't call other MQTT methods.  Don't try to publish multiple messages at a time or publish a message while consuming another.
* Even with this API, the topic size is still limited.  The limit can be increased by overriding values from [config.h](src/PicoMQTT/config.h).

## Json

It's easy to publish and subscribe to JSON messages by integrating with [ArduinoJson](https://arduinojson.org/).  Of course, you can always simply use `serializeJson` and `deserializeJson` with strings, but it's much more efficient to use the advanced API for this.  Check the examples below or try the [arduinojson.ino](examples/arduinojson/arduinojson.ino) example.

### Subscribing

```
mqtt.subscribe("picomqtt/json/#", [](const char * topic, Stream & stream) {
    JsonDocument json;

    // Deserialize straight from the Stream object
    if (deserializeJson(json, stream)) {
        // don't forget to check for errors
        Serial.println("Json parsing failed.");
        return;
    }

    // work with the object as usual
    int value = json["foo"].as<int>();
});
```

### ESP8266

![ESP8266 broker performance](doc/img/benchmark-esp8266.svg)

[Get CSV](doc/benchmark/esp8266.csv)

### ESP32

![ESP32 broker performance](doc/img/benchmark-esp32.svg)

[Get CSV](doc/benchmark/esp32.csv)

## Special thanks

Большое спасибо [Santa Claus](https://github.com/santa) for his support with the MQTT over WebSocket feature.

## License

Эта библиотека распространяется с открытым исходным кодом по лицензии GNU LGPLv3.

## Разработчик
**[Деревягин Вадим](https://github.com/XenonMNSK)**
