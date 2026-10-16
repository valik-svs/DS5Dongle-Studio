# Desktop toggle — форк, не апстрим

Личная надстройка над [DS5Dongle-Studio](https://github.com/awalol/DS5Dongle). В апстрим это не предлагалось: мейнтейнер вряд ли возьмёт глобальный слот Desktop. При обновлении с апстрима фичу нужно **переносить заново**, а не надеяться, что мёрж сам её сохранит. Портал — один большой `ds5-config-portal.html`, и конфликт там почти наверняка затрёт блок Desktop.

Этот файл — карта переноса. Поведение, которое нельзя «упростить» при мёрже, помечено как инвариант.

Сборка прошивки с этой надстройкой: `./tools/build-uf2.sh` (Pico 2 W → `dist/ds5-desktop-pico2w.uf2`) и `./tools/build-uf2.sh --waveshare` (→ `dist/ds5-desktop-waveshare.uf2`). Скрипт вызывает `tools/build-macos.sh`. Первый запуск скачивает Pico SDK в `.deps/`.

`README.md` специально не трогали: это файл апстрима, правка там создаёт конфликт на каждом обновлении.

---

## Что делает фича

Глобальная комбинация кнопок переключает текущий игровой слот и один слот, помеченный Desktop. Это не режим, не макрос слота, не Playnite и не слежение за процессами Windows. Когда игра закрывается, слот сам не возвращается.

На устройстве хранится:

| Что | Где | Смысл |
|---|---|---|
| `desktop_slot_id` | flash, сектор Desktop | Какой слот — Desktop. `0xFF` = не назначен, chord ничего не делает |
| `fallback_slot_id` | flash, сектор Desktop | С какого слота стартовать после перезагрузки. `0xFF` = не форсировать |
| `last_non_desktop` | только RAM | Слот, с которого ушли на Desktop. После reboot пропадает |
| chord, `hold_cs`, цвет lightbar | flash, сектор Desktop | Комбинация и подсветка на время Desktop |

Правила переключения (`desktop_toggle_plan` в `src/desktop_logic.h`):

1. Активный слот не Desktop → запомнить его в RAM (если это настоящий слот) и включить Desktop.
2. Активный слот уже Desktop → вернуться в `last`, если он валиден и не равен Desktop. Иначе во `fallback` на тех же условиях. Иначе ничего не делать.
3. После reboot RAM пуст, старт с `fallback`, если он занят. Чистая flash **не** сажает устройство на слот 0: fallback нужно выбрать в портале.
4. `fallback == desktop` как цель возврата не используется (это был бы пустой переход). Сохранить такие настройки всё равно можно.

Chord — действие всего устройства, не выход макроса. Срабатывает независимо от маски макросов текущего слота. Одно срабатывание на удержание; повтор только после того, как отпущены **все** кнопки комбинации. `hold_cs == 0` — сразу при полном нажатии. Иначе один раз, когда удержание дошло до `hold_cs` сантисекунд. Частичное отпускание chord не снимает фиксатор.

`slot_activate()` нельзя звать из обработчика входного репорта: он стирает flash и может переподключить USB. Ввод только поднимает `pending`. `desktop_task()` в главном цикле считает цель, вызывает существующий `slot_activate()` и при `needs_reenum` делает тот же reconnect, что HID-команда `0x03` (`wake_note_usb_reconnect`, `tud_disconnect`, 250 мс, `tud_connect`).

Приоритет подсветки полоски геймпада: предупреждение батареи > цвет Desktop > свет игры/профиля (`lightbar_off` в том числе). Цвет Desktop не пишется в слот. **Disable Pico LED** — светодиод на плате донгла, не полоска геймпада. У слота нет своего RGB: слот умеет только **Lightbar Off** (полоска тёмная). RGB задаётся только в блоке Desktop toggle и горит, только пока активен слот Desktop.

---

## Файлы, которых в апстриме нет

Их можно копировать целиком. Конфликта не будет, пока апстрим не заведёт файл с тем же именем.

| Файл | Роль |
|---|---|
| `src/desktop_logic.h` | Чистые решения: план переключения и фиксатор chord. Без flash и USB |
| `src/desktop.h` | API модуля |
| `src/desktop.cpp` | Flash-запись, `desktop_boot` / `desktop_task`, reconnect, lightbar |
| `tools/desktop-tests/desktop-tests.cpp` | Хостовый тест плана и фиксатора, без SDK |
| `tools/build-uf2.sh` | Сборка UF2 в `dist/` |
| `docs/DESKTOP-TOGGLE.md` | Этот файл |

Проверка логики без прошивки:

```sh
c++ -std=c++17 -I src tools/desktop-tests/desktop-tests.cpp -o /tmp/desktop-tests && /tmp/desktop-tests
```

Ожидается строка `desktop-tests ok`.

Запись во flash (`DesktopRecord` в `desktop.cpp`): magic `0x44355344` (`"DS5D"`), version `1`, затем поля и CRC от `version` до `chord` включительно. Пустой сектор (нет magic) → Desktop и fallback оба `0xFF`. Если назначенный слот удалили, designation сбрасывается (`clear_missing_desktop`).

---

## Куда вшито в существующие файлы

При мёрже эти места расходятся. Если файл целиком взят из апстрима, вставки ниже нужно вернуть. Искать по имени функции, не по номеру строки.

### `CMakeLists.txt`

В `add_executable(ds5-bridge ...)` есть `src/desktop.cpp`. Без этой строки линковка не найдёт модуль.

### `src/flash_map.h`

`DESKTOP_FLASH_OFFSET = MACRO_FLASH_OFFSET - FLASH_SECTOR_SIZE` — сектор сразу под таблицей макросов (сейчас это сектор −21, макросы на −20, слоты растут вниз от −4, `SLOT_SECTORS_RESERVED = 16`).

Инвариант: сектор Desktop привязан к сектору макросов, **не** к `SLOT_COUNT`. Поднятие `SLOT_SECTORS_RESERVED` сдвигает макросы и этот сектор тоже — это миграция flash, не правка константы. Рядом стоят `static_assert`.

### `src/config.h` / `src/config.cpp`

Добавлен `uint8_t active_slot_index()`: `0xFF`, если `g_active_slot < 0`. Это тот же RAM-индекс, что пишет `active_profile_set()` из `slot_activate` / `slot_save`. Поля Desktop в `Config_body` нет и быть не должно: тело конфига копируется в каждый слот.

### `src/main.cpp`

- `#include "desktop.h"`
- `desktop_boot()` сразу после `config_load()`, до `bt_init()`. Если fallback занят, активирует его и **игнорирует** `needs_reenum` (USB-хост ещё не поднят).
- В условии синтетического output-репорта рядом с `player_led_wants_report()` добавлен `desktop_lightbar_wants_report()`. Иначе на простое цвет Desktop не доезжает до геймпада.
- `desktop_task()` после `battery_notify_tick()`. `tud_task()` в этом цикле уже отработал, поэтому `tud_connect` из reconnect обрабатывается на следующей итерации. Так же устроен и путь команды `0x03`, только тот вызывается из USB-колбэка.

### `src/macro.cpp`

- `#include "desktop.h"`
- В `macro_on_input`, после debounce и `apply_pad_half`, всегда вызывается `desktop_on_buttons(mask, now, g_suspended)`. Даже когда у слота выключены все макросы. Ранний выход «макросы выключены» не обнуляет `g_stable`: иначе Desktop теряет отпускание.
- `g_suspended` (запись chord в портале) — единственный `frozen`. В этом режиме отпускание учитывается, `pending` сбрасывается, переключения нет.
- Если `wake_host_is_suspended()`, маска всё равно скармливается Desktop, и только потом `macro_reset()` и return. Жесты в этом ветвлении не считаются (`touch_task` пропускается).
- `macro_reset()` **не** вызывает `desktop_disarm()`.

Инвариант, уже один раз сломанный: переключение слота само переподключает USB. `tud_suspended()` на это время истинно. Если в этот момент кормить Desktop нулевой маской или занимать фиксатор из `macro_reset()`, следующее нажатие chord не меняет слот. Хост-сон не должен глушить chord.

### `src/wake.cpp`

В poll-бэкстопе `tud_suspended()` не ставит `host_suspended`, пока `time_us_64() < reconnect_until_us`. `tud_suspend_cb` это окно уже игнорирует (`wake_note_usb_reconnect`, 5 с). Poll раньше не игнорировал, и собственный reconnect Desktop выглядел как сон хоста.

### `src/state_mgr.cpp`

`#include "desktop.h"`. В `state_set`, после блока `lightbar_off` и до `battery_notify_override`, цвет Desktop пишется в байты 44–46 (`AllowLedColor`). Батарея остаётся последней и побеждает.

### `src/cmd.cpp`

В том же `switch`, что и слоты, две команды. Ответ на feature report **0x84** (`SLOT_REPORT`), не на 0x81: опрос портала съедает 0x81.

| Команда | Направление | Полезная нагрузка |
|---|---|---|
| `0x66` / `0x68` | чтение | ответ: `66 68 status desktop fallback lb_en r g b hold_cs chord[4] LE` |
| `0x66` / `0x69` | запись | 11 байт: `desktop, fallback, lb_en, r, g, b, hold_cs, chord[4] LE`. `status` 0 = записано, 1 = отказ |

`desktop_set` отвергает пустой слот как Desktop и `fallback >= SLOT_COUNT`, затем `desktop_disarm()`, чтобы только что записанный и ещё зажатый chord не переключил слот.

`0x68` / `0x69` здесь — подкоманды `0x66`. В портале те же числа есть у полей батареи и auto-haptics (`readField`), это другой канал. Если апстрим заведёт в этом `switch` свои `case 0x68` или `case 0x69`, номера Desktop и портал нужно сдвинуть вместе.

### `src/desktop.cpp` — хвост подсветки

`desktop_lightbar_wants_report()` шлёт репорты, пока слот Desktop активен и цвет включён. В момент ухода со слота ещё ~300 мс репорты идут без цвета Desktop, чтобы на геймпад попал `lightbar_off` нового слота или цвет игры. Геймпад держит последнюю полоску, пока ему не пришлют новую. Без хвоста цвет Desktop остаётся до любого другого писателя (тест Stage 1 батареи как раз такой писатель). Тот же приём уже есть у player LED (`player_led_tail_until_us`).

### `.gitignore`

Добавлен `dist`. Туда кладутся готовые UF2, в git их не коммитить.

---

## Портал: `ds5-config-portal.html`

Один файл. При обновлении проще заново вставить блок, чем мёржить его кусками. Искать по этим якорям:

- `readCmdReply`: к условию `SLOT_REPORT` добавлены `cmdId === 0x68` и `0x69`.
- Функции с префиксом `desktop`: `desktopRead`, `desktopWrite`, `desktopBoxHTML`, `desktopPickHTML`, `desktopCommit`, `desktopSetFallback`, `desktopSetHold`, `desktopSetLightbar`, `desktopSaveColour`, `desktopSetSlot`, `desktopQualifyChord`, `desktopOnReport`, `desktopRecord`, `desktopPickToggle`, `desktopRerender`, `desktopClearChord`.
- В разметке вкладки slots: `<div id="desktopbox">` **над** `<div id="slotbox">`.
- `_refreshSlotsOnce`: `desktopRead()`, `window._desktop`, `window._slotInfos`, чекбокс `desktopSetSlot` только у занятых слотов. Второй Desktop снимает первый. Удаление слота Desktop снимает designation (это делает прошивка, портал просто перечитывает).
- `desktopRerender()` перерисовывает только `#desktopbox` и не делает проход по 32 слотам.

Запись chord: **Record** объединяет все кнопки, нажатые до Stop, а не один кадр с максимумом кнопок. **Pick** отмечает комбинацию галочками, с тем же взаимным исключением половин тачпада, что у макросов (`PAD`, левая, правая). Record **не** пишет `hold_cs`. Старые сохранения могли получить `hold_cs`, если кнопку держали ≥ 400 мс в первой версии рекордера: короткий тап тогда не переключает, пока Hold не выставлен в 0.

---

## Как обновлять с апстрима

1. Посмотреть diff апстрима по файлам из раздела «Куда вшито». Новые файлы из таблицы выше апстрим не приносит.
2. Влить апстрим. Новые файлы (`desktop.cpp`, `desktop.h`, `desktop_logic.h`, тесты, `tools/build-uf2.sh`, этот документ) оставить.
3. В каждом изменённом апстримом файле из списка врезок проверить, что вставка на месте. Если конфликт взят стороной апстрима — вернуть вставку по якорю из этого файла, не по старому номеру строки.
4. Особо проверить четыре инварианта, их уже ломали «упрощением»:
   - Desktop не лежит в `Config_body`.
   - Сектор Desktop сидит сразу под макросами и не считается от `SLOT_COUNT`.
   - `macro_reset()` и ветка `wake_host_is_suspended()` не занимают фиксатор chord и не кормят его нулевой маской.
   - Poll в `wake.cpp` не ставит `host_suspended` внутри `reconnect_until_us`.
   - Уход со слота Desktop ещё немного шлёт output, иначе полоска остаётся цвета Desktop.
5. `c++ -std=c++17 -I src tools/desktop-tests/desktop-tests.cpp -o /tmp/desktop-tests && /tmp/desktop-tests`
6. `./tools/build-uf2.sh` и при необходимости `--waveshare`. Прошить UF2 из `dist/`. Портал — обновить страницу: HTML в прошивку не входит.
7. На устройстве: fallback задан, Hold = 0, chord записан заново через Pick или Record, если комбинация шире, чем кнопки, которыми реально нажимают. Переключение туда и обратно, и что полоска после возврата совпадает со слотом (`Lightbar Off` гасит её сразу).

Команды `0x68`/`0x69` и сектор flash — контракт уже прошитых донглов. Менять номера или сдвигать сектор можно только вместе с миграцией: старые устройства иначе потеряют Desktop и fallback.
