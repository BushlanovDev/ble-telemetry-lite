# BLE Telemetry Lite

ПО для микроконтроллеров **ESP32-C3** и **ESP32-S3**.

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
 - Gnd
 - Tx - подключается к контакту 3 (RX модуля)
 - Rx - подключается к контакту 4 (TX модуля)

Подключение на примере аппаратуры RadioMaster Pocket

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3-supermini_to_rm_pocket.jpg?raw=true" width="50%" alt="Подключение на примере аппаратуры RadioMaster Pocket" title="Подключение на примере аппаратуры RadioMaster Pocket" />

Если используется аппаратура без SerialAUX, необходимо контакт TX внутреннего модуля ELRS/CrossFire подключить к контакту RX(3) модуля.

Подключение к TX внутреннего модуля на примере аппаратуры Jumper T20

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3-supermini_to_jumper-t20.jpg?raw=true" width="50%" alt="Подключение к TX внутреннего модуля на примере аппаратуры Jumper T2" title="Подключение к TX внутреннего модуля на примере аппаратуры Jumper T2t" />

## Прошивка и настройка

Прошить модуль можно с помощью [Web-прошивальщика](https://configurator.skydevices.ru).

Там же осуществляется его настройка.
