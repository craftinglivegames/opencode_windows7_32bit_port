# Спайк: порт opencode 1.19.3 с Bun на Node x86 для Windows 7

Дата: 2026-11-05
Исходники: `anomalyco/opencode`, тег `v1.19.3` (коммит `e3c4eb2`)
Цель: понять, реально ли заменить Bun на Node 22 x86 и получить локальный 32-битный opencode.

## Итог одной строкой

**Полный локальный порт — нет.** Серверная часть близка к Node-совместимости, но TUI
(`@opentui/core`) существует только под x64 и требует Win8+ API, а официальный Node 22 x86
не запускается на Win7. Текущая схема «64-битный сервер + 32-битный клиент» остаётся
единственным практичным вариантом.

## Факты

### 1. Размер и структура (t1, `spike/inventory.md`)

- 32 пакета, ~2 625 prod-файлов, ~513 253 строк TS/TSX.
- Сервер использует `node:http`, а **не** `Bun.serve` (`Bun.serve` в prod — 0 вхождений).
- В коде уже есть условные импорты с Node-вариантами:
  `#sqlite` → `sqlite.node.ts`, `#pty` → `pty.node.ts`, `#fff` → `fff.node.ts`.
- TUI — TSX + SolidJS + `@opentui/core` (нативный `.dll`).

### 2. Зависимость от Bun API (t1)

| Метрика | Значение |
|---|---|
| `Bun.*` всего | 713 |
| `Bun.*` в prod (без тестов) | **137** |
| `Bun.*` в `packages/*/src` | **33** |
| `bun:test` | 699 (только тесты) |
| `bun:sqlite` | 5 (2 в prod) |
| `bun:ffi` | 1 (`tui/src/terminal-win32.ts`) |

Основные prod-вызовы: `Bun.write` (27), `Bun.file` (55), `Bun.stringWidth` (25),
`Bun.Glob` (7), `Bun.spawn` (3), `Bun.env` (10). Почти все заменяются на
`node:fs`, `node:util`, `string-width`, `node:child_process` — 33 места, не сотни.

### 3. Пробный бандл (t4, факт)

Команда:

```
bun build packages/cli/src/index.ts --target=node --outdir=spike/bundle-out \
  --external "bun:*" --external "bun" --external node-pty \
  --external "@parcel/watcher" --external "@opentui/core"
```

Результат: **сборка успешна**, 2738 модулей, `index.js` 15.34 МБ.

Запуск `node index.js --version` падает сразу:

1. `ERR_MODULE_NOT_FOUND: @opentui/core` — нативный TUI-модуль;
2. в бандле остаются `import { Database } from "bun:sqlite"`,
   `import { dlopen, ptr } from "bun:ffi"`, `import { fileURLToPath } from "bun"`.

То есть бандлер собирает, но без слоя JS-шимов Bun API приложение не стартует.

### 4. Node x86 на Win7 (t2, факт)

| Версия | Результат на нетбуке |
|---|---|
| Node 12.22.7 x86 | **работает**: `v12.22.7`, exit 0 |
| Node 22.23.4 x86 (официальная) | **не запускается**: «%1 не является приложением Win32» |

Node 12 слишком старая для opencode 1.19.3. Нужен сторонний бэкпорт Node 20/22 под Win7
(официальной поддержки нет).

### 5. Нативные модули (t3, `spike/native-modules.md`)

| Модуль | win32-ia32 | Вердикт |
|---|---|---|
| `@lydell/node-pty` | prebuilt нет | собирается из исходников MSVC x86 (winpty, Win7 OK) |
| `node-pty` (microsoft) | prebuilt нет | собирается (`node-gyp --arch=ia32`), winpty работает на Win7 |
| `@parcel/watcher` | prebuilt есть | требует Win8+ API — риск |
| `tree-sitter-*` | prebuilt нет | собирается x86; можно заменить на WASM |
| `@opentui/core` | **нет, только x64** | **главный блокер**: Zig-сборка, тянет ffmpeg/ghostty, Win8+ API |
| `@ff-labs/fff-bun` | нет | есть Node-вариант с деградацией |
| `photon-node` | WASM | работает |
| `better-sqlite3` | prebuilt ia32 нет | собирается; либо `node:sqlite` |

## Оценка трудозатрат

| Сценарий | Объём | Оценка | Риск |
|---|---|---|---|
| A. Полный локальный порт (сервер + TUI) | JS-шимы Bun API (33 места), Node 22 x86 бэкпорт, сборка node-pty/watcher x86, **порт @opentui/core под x86** (Zig + ffmpeg/ghostty) | **40–80 чел.-дней** | очень высокий |
| B. Локальный сервер + веб-UI (без TUI) | те же шимы, Node 22 x86 бэкпорт, нативные модули x86; TUI не нужен — интерфейс в Supermium | **15–25 чел.-дней** | высокий (неофициальный Node) |
| C. Текущая схема: сервер x64 + клиент x86 | уже сделано | **0** | низкий |

## Рекомендация

**No-go для A и B на текущих условиях.** Причины:

1. Официального Node 20/22 под Win7 x86 нет; бэкпорт — единственная опора, и он неофициальный.
2. `@opentui/core` под x86 не собирается без отдельного проекта (Zig + нативные библиотеки).
3. Даже серверный вариант (B) требует тащить неофициальный рантайм и поддерживать форк.

**Go для C**: текущая схема работает, не требует 32-битного рантайма и уже проверена на нетбуке.

## Если всё же двигаться дальше

Минимальный следующий шаг для B: проверить сторонний Node 22 Win7 x86 (запуск `node -v`
и `node:sqlite`), затем собрать `bun build --target=node` с шимами `bun:sqlite` → `node:sqlite`,
`bun:file` → `node:fs`, `Bun.stringWidth` → `string-width`. Это даст измеримый прототип
сервера без TUI за 3–5 дней.
