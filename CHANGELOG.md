# История изменений

Формат — по мотивам [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/).
Версии порта нумеруются отдельно от upstream OpenCode (в скобках — версия
OpenCode, на которой собран релиз). Подробности и файлы — на странице
[releases](https://github.com/craftinglivegames/opencode_windows7_32bit_port/releases).

## [v0.2.3-x32] — 2026-11-11 (OpenCode 1.19.3)

### Добавлено

- Панель **Accounts & limits**: `accounts.cmd` или `OpenCode.exe --accounts`
  открывает локальную страницу `http://127.0.0.1:4099`.
- Вход по коду (device flow OpenCode Console), добавление сервисного ключа
  `oc_sk_...`, список аккаунтов: «Сделать активным», «Имя», «Удалить».
- Список аккаунтов хранится в `%USERPROFILE%\.local\share\opencode\port_accounts.json`,
  выбранный аккаунт записывается в базу порта; переключение перезапускает
  сервер.

### Исправлено

- Панель отклоняет запросы с чужим `Host`/`Origin` (защита от CSRF со стороны
  открытых в браузере страниц).

## [v0.2.2-x32] — 2026-11-11 (OpenCode 1.19.3)

### Изменено

- Ошибка лимита бесплатного тарифа (`429`) показывает время ожидания из
  `retry-after` вместо голого «Subscribe to Go».
- В документации явно описано, что бесплатная квота считается на внешний IP и
  сбрасывается в полночь UTC.

## [v0.2.1-x32] — 2026-11-10 (OpenCode 1.19.3)

### Исправлено

- Создание сессии в web-UI падало с `Cannot read properties of undefined`.
- Половина экрана закрывалась ошибкой `context must be used within a context
  provider` (баги Solid 1.9.10 в переходах и отложенных мемо; upstream
  anomalyco/opencode#28857, #28771).
- `useDialog` и контекст Kobalte больше не роняют вкладку.

### Изменено

- Главный JS-чанк помечен `?v=3`, поэтому старый кэш браузера не залипает.

## [v0.2.0-x32] — 2026-11-09 (OpenCode 1.19.3)

Первый рабочий релиз нативного x32-порта.

### Добавлено

- Node.js 22.23.4 x86 с шимами `ocshim32.dll` / `ocadv32.dll` (Win7 не хватает
  двух импортов: `GetSystemTimePreciseAsFileTime`, `EventSetInformation`).
- Сборка OpenCode под Node (`bun build --target=node`), `bun:sqlite` →
  `node:sqlite`, node-pty x86 с winpty.
- Нативный лаунчер `OpenCode.exe`: освобождает порт 4096, поднимает сервер
  скрыто, открывает UI в Supermium в режиме `--app`.
- Портативный архив, лицензии и `THIRD_PARTY_NOTICES.txt` внутри.

### Ограничения

- Нет TUI (только web-UI).
- Сервер без пароля слушает `0.0.0.0` — только доверенная сеть.
- Список моделей читается один раз при старте сервера; при недоступном
  `models.opencode.ai` нужен `sync_free_models.cmd`.

## Не выпущено

- Патчи порта к upstream (`patches/`) — чтобы бандл можно было пересобрать
  из репозитория, а не только скачать.
