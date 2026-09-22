#Что реализовано
##ESP32 поднимает собственную WiFi-сеть.

###Настройки по умолчанию:

- WiFi SSID:       NUX-Scanner
- WiFi password:   nux12345
- Web login:       admin
- Web password:    nux12345

###Адрес страницы:

http://192.168.4.1/

Логин web-интерфейса защищён через HTTP Basic Auth.

В Serial Monitor на скорости 115200 выводятся:

`
SSID;
WiFi password;
web login/password;
целевой BLE name/MAC;
IP-адрес;
список HTTP endpoints.
`

##Страницы
###/scan
Позволяет:

- отправить одну команду Control Change;
- указать:
- номер CC;
- значение;
- MIDI channel;
- запускать перебор значений;
- задавать список значений, например:
	- 1,32,64,127

- выбирать диапазон MIDI-каналов, например 1...3;
- задавать задержку между командами;
- использовать кнопки:
	- Start;
	- Pause;
	- Stop;
- просматривать лог выполненных команд.
Минимальная задержка ограничена 250 мс.

###/capture
Перехватывает входящие:

- Control Change;
- Program Change.

*Есть кнопки:*

- Start;
- Stop;
- Clear.
Входящие сообщения отображаются в большом текстовом поле и могут выводиться в Serial Monitor.

###/settings
*Настройки:*

- MAC-адрес или имя NUX;
- WiFi AP SSID;
- WiFi AP password;
- web login;
- web password.
Все значения сохраняются в ESP32 NVS. После сохранения устройство автоматически перезапускается, поэтому новый BLE target применяется при следующем запуске.

###/ota
Позволяет загрузить .bin-файл прошивки через браузер. После успешной загрузки ESP32 автоматически перезапускается.

Serial-меню
В Serial Monitor:

1 — включить MIDI capture
2 — выключить MIDI capture
3 — показать статус
4 — показать HTTP endpoints
h — вывести меню

При включённом capture входящие сообщения выводятся примерно так:

[MIDI RX] CC channel=1 control=81 value=64
[MIDI RX] PC channel=1 program=3

##Ограничения безопасности
Новый проект:

не отправляет SysEx;
не регистрирует обработчик SysEx;
блокирует:
bank select;
RPN/NRPN;
data entry;
reset controllers;
all notes off;
all sound off;
остальные системные CC;
не позволяет выполнять перебор быстрее одного сообщения за 250 мс.
В интерфейсе доступны стандартные варианты:

CC 7 — channel volume;
CC 11 — expression;
CC 73 — legacy NUX value;
CC 81 — legacy project volume;
Custom.
CC 73 и CC 81 помечены как неподтверждённые для MP-3 — это только удобные варианты для эксперимента на основе старых скетчей проекта.


`В контейнере нет arduino-cli и PlatformIO, поэтому полноценную компиляцию нужно выполнить в Arduino IDE с установленными:

ESP32 board package;
Arduino BLE-MIDI;
MIDI Library;
NimBLE-Arduino.
`
Создан follow-up для проверки сборки и работы на реальном ESP32 DevKit.