---
name: NUX Mighty Plug Pro MIDI protocol
description: Verified messages for preset switching and current-preset retrieval over BLE-MIDI.
---

Для NUX Mighty Plug Pro preset переключается MIDI `Program Change` со значениями `0..6` на MIDI channel 1. `CC 49` не является подтверждённым способом переключения preset для этого устройства.

Текущий preset запрашивается private SysEx `F0 43 58 70 0C 02 F7`; ответ имеет форму `F0 43 58 70 0C 03 <preset-index> 32 F7`. BLE-MIDI transport может оставить timestamp bytes перед `F0`, поэтому обработчик должен находить начало SysEx.

**Why:** Исходный проект `mightier_amp` использует Program Change для Plug Pro и отдельный SysEx-запрос текущего канала; отправка `CC 49` давала локальный Serial-лог без реакции NUX.

**How to apply:** При изменениях TTGO/DevKit для Mighty Plug Pro использовать Program Change для выбора preset и не считать отправку `CC 49` запросом состояния.

Аппаратная проверка подтвердила: `Program Change` реально переключает preset на Mighty Plug Pro. Запрос текущего preset при подключении пока не даёт ответа в Serial, поэтому это отдельная нерешённая часть протокола.