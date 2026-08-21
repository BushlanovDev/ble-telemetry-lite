# BLE Telemetry Lite

[English](./README.md) | [Russian](./README_RU.md)  

ПО для микроконтроллеров **ESP32-C3**, **ESP32-S3** и т.п. для трансляции телеметрии Crossfire и ELRS от аппаратур радиоуправления по Bluetooth или Wi-Fi.

## Возможности

 - Передача телеметрии Crossfire/ELRS на телефоны/планшеты/ноутбуки с аппаратур радиоуправления по Bluetooth или Wi-Fi
 - Подключение не только на аппаратуры с Aux-serial, но и напрямую к внутреннему модулю (Jumper RC до T15)
 - Отображение телеметрии через [TelemetryViewer](https://github.com/RomanLut/android-taranis-smartport-telemetry/releases), [WebTelemetry](http://telemetry.skydevices.ru) или встроенный Web интерфейс

## Аппаратное обеспечение

Плата разработчика **ESP32-C3-SuperMini** (рекомендуется) или **ESP32-S3-SuperMini**. 
Возможно подойдут другие микроконтроллеры esp32 с поддержкой стеков BLE и Wi-Fi, но совместимость не проверялась и не гарантируется.

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3_esp32-s3.jpg?raw=true" width="60%" alt="ESP32-C3-SuperMini & ESP32-S3-SuperMini"/>

Предлагаемые платы - компактные, не дорогие и не обвешены лишними компонентами. 
Если используете другие платы с микроконтроллерами **ESP32-C3** / **ESP32-S3**, помните - номера контактов для подключения, 
соответствуют номерам контактов микроконтроллера. На других платах нумерация контактов платы может быть изменена и не 
соответствовать нумерации процессора. В таком случае изучите datasheet на вашу плату, чтобы найти нужные контакты.

## Подключение

Модуль подключается к SerialAUX аппаратуры. Необходимы 4 провода:
 - 5V
 - GND
 - TX - подключается к контакту 3 (RX модуля)
 - RX - подключается к контакту 4 (TX модуля) (опционально)

Подключение на примере аппаратуры RadioMaster Pocket

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3-supermini_to_rm_pocket.jpg?raw=true" width="50%" alt="Подключение на примере аппаратуры RadioMaster Pocket" title="Подключение на примере аппаратуры RadioMaster Pocket" />

Если используется аппаратура без SerialAUX, необходимо контакт TX внутреннего модуля ELRS/CrossFire подключить к контакту RX(3) модуля.

Подключение к TX внутреннего модуля на примере аппаратуры Jumper T20

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3-supermini_to_jumper-t20.jpg?raw=true" width="50%" alt="Подключение к TX внутреннего модуля на примере аппаратуры Jumper T2" title="Подключение к TX внутреннего модуля на примере аппаратуры Jumper T2t" />

## Прошивка

Скачайте архив под свою плату со страницы [Releases](https://github.com/BushlanovDev/ble-telemetry-lite/releases) и распакуйте его. 
Внутри: `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin` и `merged.bin` (все четыре файла одним образом).
Загрузчики плат не взаимозаменяемы — используйте архив только под свой чип.

### Прошивка через web интерфейс

Переключить режим BLE/Wi-Fi можно нажатием кнопки Boot на модуле.

В режиме Wi-Fi необходимо подлючиться к точке доступа **BLE Telemetry Lite** используя пароль **12345678**.  
Web интерфейс доступен по адресу [http://192.168.4.1](http://192.168.4.1).

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/web-interface.png?raw=true" alt="Web интерфейс" title="Web интерфейс" />

### Прошивка через Flash Download Tool

Скачайте [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32c3/production_stage/tools/flash_download_tool.html), выставите настройки как на скриншоте и не забудьте указать свой com порт.

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/flasher-tool.png?raw=true" alt="Flash Download Tool" />

### Прошивка из релизного архива

**Вариант 1: через браузер, ничего устанавливать не нужно**

1. Откройте [esp.huhn.me](https://esp.huhn.me/) в браузере Chrome, Edge или Opera.
2. Выберите файл `merged.bin`, подключите плату по USB, выберите последовательный порт и нажмите **Connect**.
3. Нажмите **Erase** (полное стирание), затем **Program**.

**Вариант 2: через esptool, одним файлом**

```bash
pip install esptool
esptool --chip esp32c3 write_flash 0x0 merged.bin
```

**Вариант 3: через esptool, отдельными файлами**

```bash
esptool --chip esp32c3 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0xE000 boot_app0.bin 0x10000 firmware.bin
```

Для платы S3 используйте `--chip esp32s3` вместо `--chip esp32c3`. В esptool 4.x и старше команда называется `esptool.py`.

Если на плате была чужая прошивка, сначала полностью сотрите флеш:

```bash
esptool --chip esp32c3 erase_flash
```

Прошивка `merged.bin` (как и полное стирание) сбрасывает настройки устройства (режим, baudrate, имя) к заводским - при первом прошивании это нормально.

## Лицензия

Данные проект распространяется под лицензией MIT - подробности см. в файле [LICENSE](LICENSE).
