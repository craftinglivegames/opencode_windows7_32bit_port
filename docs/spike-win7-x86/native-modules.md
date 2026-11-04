# Нативные модули opencode 1.19.3 под win32-ia32 (Win7 x86)

Дата: 2026-11-05. Скоуп: подзадача t3. Рабочий каталог: `spike/`.

Источники, по которым сверялось (всё скачано/опрошено вживую):

- npm registry: `opencode-ai@1.19.3`, `@lydell/node-pty`, `@parcel/watcher`, `@opentui/core`, `@ff-labs/fff-bun`, `@silvia-odwyer/photon-node`, `tree-sitter-*`, `better-sqlite3`, `koffi`.
- Исходники: `codeload.github.com/anomalyco/opencode` tag `v1.19.3` (распаковано в `spike/_src/repo/packages/*`), `bun.lock` оттуда же.
- Официальные сборки Node: `nodejs.org/dist/v22.23.4/win-x86/node.exe` (скачан, запущен, `process.arch == ia32`).

## Главный вывод

`opencode-ai@1.19.3` в npm — это **не** набор JS + нативных модулей, а **готовый Bun-бинарник**
(пакеты `opencode-windows-x64`, `-arm64`, …; `os: [darwin, linux, win32]`, `cpu: [arm64, x64]`).
Порта под ia32 нет ни в одном слое: ни платформенного пакета, ни сборки.

Но **сам исходник уже содержит node-ветки** через `imports`-условия:

```json
// packages/core/package.json
"#sqlite": { "bun": "sqlite.bun.ts", "node": "sqlite.node.ts" }
"#pty":    { "bun": "pty.bun.ts",    "node": "pty.node.ts" }
"#fff":    { "bun": "fff.bun.ts",    "node": "fff.node.ts" }
```

То есть архитектурно порт на Node предусмотрен авторами, и нативных модулей в node-пути
остаётся всего два с половиной: `@parcel/watcher` (есть ia32 prebuilt), `@lydell/node-pty`
(ia32 нет, но заменяется сборкой MSVC x86), `@opentui/core` (только для TUI, ia32 нет и
сборка тяжёлая). SQLite — вообще не нативный модуль: node-путь использует `node:sqlite`.

## Сводная таблица

| # | Модуль (версия в 1.19.3) | Где используется | Prebuilt win32-ia32 | Сборка из исходников MSVC x86 | Win7 | Риск |
|---|---|---|---|---|---|---|
| 1 | `@lydell/node-pty@1.2.0-beta.12` | `core/src/pty/pty.node.ts` (лазийно, `#pty`) | **Нет.** Пакета `@lydell/node-pty-win32-ia32` не существует (npm 404); в опц. зависимостях только x64/arm64. В prebuilt win32-x64 **нет winpty**, только `conpty.dll` + `OpenConsole.exe` | **Невозможна из npm-пакета**: тарбол 14.6 КБ, 5 файлов, только обёртка + платформенные пакеты, исходников нет | **Нет**: ConPTY требует Win10 1809+; winpty в форке выпилен | **Высокий** (но обходится, см. п.1) |
| 1a | `node-pty@1.1.0` (microsoft, замена) | тот же `#pty` (нужен патч импорта) | Нет prebuilt ia32 (в релизе только win32-x64/arm64) | **Да, проверено:** `node-gyp rebuild --arch=ia32` + MSBuild `Platform=Win32` → `pty.node`, `winpty.dll`, `winpty-agent.exe`, все `machine=0x014c`; spawn `cmd.exe` под node 22 ia32 работает с `useConpty:false` | **Да**, через winpty-фолбэк (встроен в node-pty: ConPTY определяется по версии ОС) | **Средний** |
| 2 | `@parcel/watcher@2.5.1` | `core/src/filesystem/watcher.ts` (require `@parcel/watcher-<platform>-<arch>`) | **Есть:** `@parcel/watcher-win32-ia32@2.5.1` (в `bun.lock` тоже прописан) | Не требуется (есть prebuilt); при желании — node-gyp/napi, тоже собирается | **Да:** в импортах `watcher.node` нет ни одного Win8+/Win10 API | **Низкий** |
| 3 | `tree-sitter-bash@0.25.0`, `tree-sitter-powershell@0.25.10` | `opencode/src/tool/shell.ts`, `tui/src/parsers-config.ts` | **Нет** (prebuilds только win32-x64/arm64) | **Да, проверено** (tree-sitter-bash: `.node` собран, `machine=0x014c`, грузится) | Да (но не нужно, см. ниже) | **Нет** — в рантайме используется WASM, а не нативный биндинг |
| 4 | `@opentui/core@0.4.5` | `packages/tui/*`, `opencode/src/cli/cmd/run/*` (весь TUI) | **Нет.** Платформенные пакеты только `win32-x64`/`win32-arm64`; внутри — один `opentui.dll` (8.9 МБ, Zig + ffmpeg/libwebp/ghostty) | Теоретически да (Zig-исходники есть в `anomalyco/opentui`, `packages/native/build.zig`), но x86-мишень + бандл ffmpeg/ghostty не проверялись и тяжёлые | **Под вопросом:** `opentui.dll` статически импортирует `GetSystemTimePreciseAsFileTime` (Win8) и `api-ms-win-crt-*` (UCRT), плюс нужен VT-режим консоли, которого в conhost Win7 нет | **Критический** (только для TUI; сервер не затронут) |
| 5 | `@ff-labs/fff-bun@0.9.4` | `core/src/filesystem/fff.bun.ts` (индекс файлов) | Нет (`cpu: [x64, arm64]`, бинарники `@ff-labs/fff-bin-*`) | Неприменимо (Bun-only) | — | **Нет:** в node-пути `fff.node.ts` отдаёт `available() === false`, `create()` → `{ok:false}`. Деградация функций, не падение |
| 6 | `@silvia-odwyer/photon-node@0.3.4` | `core/src/image/photon.ts` (обработка картинок) | Не нужен: пакет целиком WASM (`photon_rs_bg.wasm`, 1.8 МБ), 7 файлов, нативного кода нет | — | Работает (WASM в Node) | **Нет** |
| 7 | `better-sqlite3` | **не используется** в 1.19.3 | — (для справки: node-ABI prebuilt ia32 **нет** в релизах v11.10.0/v12.4.1/v13.0.3, только x64/arm64 + electron-ia32) | Собирается (SQLite amalgamation), но не требуется | — | **Нет** |
| 8 | `bun:ffi` (не модуль, но нативный слой) | `packages/tui/src/terminal-win32.ts` (kernel32: `GetConsoleMode`/`SetConsoleMode`/`FlushConsoleInputBuffer`) | — | Замена: `koffi` (есть `@koromix/koffi-win32-ia32`) или свой addon | — | **Средний**, только TUI |

## Детали

### 1. node-pty — единственный реальный блокер вне TUI

Что в коде (фактически, `packages/core/src/pty/pty.node.ts`):

```ts
import * as pty from "@lydell/node-pty"
...
...(process.platform === "win32" ? { useConptyDll: true } : {})
```

- `@lydell/node-pty` — форк, которым пользуется VS Code; распространяется **только бинарниками**
  (`node_modules/@lydell/node-pty` = 5 файлов, 14.6 КБ; всё внутри — `prebuilds/win32-x64/conpty/*`).
- В prebuilt win32-x64 **нет `winpty.dll`/`winpty-agent.exe`** — только `conpty.dll` и `OpenConsole.exe`.
  Значит даже на x64 под Win7 этот пакет бесполезен: ConPTY появился в Win10 1809.
- ia32-варианта нет вообще: `https://registry.npmjs.org/@lydell/node-pty-win32-ia32` → 404.

Обход (проверен): взять upstream `node-pty@1.1.0` (там `deps/winpty` в исходниках и winpty-фолбэк
по версии ОС) и собрать под x86:

```
npx node-gyp rebuild --arch=ia32 --target=22.23.4 --dist-url=https://nodejs.org/dist
# если MSB8040 (нет Spectre-библиотек):
MSBuild build/binding.sln -p:Configuration=Release;Platform=Win32;SpectreMitigation=false
```

Результат: `pty.node`, `conpty.node`, `conpty_console_list.node`, `winpty.dll`, `winpty-agent.exe`,
все `machine=0x014c` (i386). Тест под скачанным `node.exe` v22.23.4 win-x86:
`pty.spawn('cmd.exe', ['/c','echo HELLO_PTY_IA32'], {useConpty:false})` → вывод получен, exit=0.

Патчи в коде порта: убрать `useConptyDll: true` (опция форка) и на Win7 принудительно
`useConpty: false`. Плюс `@lydell/node-pty` → `node-pty` в `package.json`/`build-node.ts` (там он в `external`).

Важно: `#pty` импортируется **лениво** (`core/src/pty.ts`: `const pty = lazy(() => import("#pty"))`),
а bash-инструмент работает через `ChildProcess` (`core/src/tool/bash.ts`), а не через pty.
Т.е. без pty сервер и агент живы, отваливаются встроенный терминал/attach.

### 2. @parcel/watcher — единственный «повезло»

`watcher.ts` делает `require('@parcel/watcher-' + process.platform + '-' + process.arch)`,
т.е. для ia32 попросит `@parcel/watcher-win32-ia32` — он опубликован (2.5.1, 446 КБ, 4 файла).
Проверено вживую: `npm i --cpu=ia32 --os=win32 @parcel/watcher@2.5.1` → под node 22 ia32
`subscribe()` даёт события `create`/`update`. Импорты `watcher.node` — без Win8+/Win10 API.

Грабли: `npm i` **без** `--cpu=ia32 --os=win32` тихо ставит x64-пакет (проверено: после обычного
`npm i` в том же каталоге `watcher-win32-ia32` заменился на `watcher-win32-x64`). Для сборки порта
нужен `npm_config_arch=ia32` / `--cpu=ia32 --os=win32` в CI.

### 3. tree-sitter — нативные биндинги не нужны

`opencode/src/tool/shell.ts` грузит **WASM**:

```ts
const { Parser } = await import("web-tree-sitter")
const { default: bashWasm } = await import("tree-sitter-bash/tree-sitter-bash.wasm", { with: { type: "wasm" } })
```

Проверено на node 22.23.4 ia32:
- `import ... with { type: "wasm" }` **не работает** (`ERR_IMPORT_ATTRIBUTE_UNSUPPORTED`, и с
  `--experimental-wasm-modules` тоже) → нужен патч на `fs.readFileSync` + `Parser.init({wasmBinary})`
  + `Language.load(bytes)`. С патчем: `program → (list (command …))` парсится, arch=ia32. ~10 строк правок.
- На всякий случай собрал и нативный `tree-sitter-bash` под ia32 (`machine=0x014c`, грузится) —
  но это тупиковая ветка, WASM проще и не требует MSVC.

### 4. @opentui/core — блокер TUI

- В `bun.lock` зафиксирован `@opentui/core@0.4.5`, платформенные пакеты только
  `win32-x64` / `win32-arm64` (аналогично 0.8.2). ia32 нет.
- Пакет — обёртка на `opentui.dll` (грузится через ffi/dlopen; в node-сборке есть `index.node.js`,
  так что Node-рантайм формально поддержан, но только на готовых платформах).
- `opentui.dll` импортирует `GetSystemTimePreciseAsFileTime` (Win8), `api-ms-win-crt-*` (UCRT),
  `bcrypt.dll`; это Zig/mingw-сборка. Даже x64-вариант на Win7 потребует UCRT + шим.
- Сборка из исходников: репозиторий `anomalyco/opentui`, `packages/native/build.zig`; внутри
  ffmpeg/libwebp/ghostty (см. лицензии в пакете). Кросс-сборка под `x86-windows` + Win7-консоль
  без VT — отдельный исследовательский проект, не «собрать одной командой».

Смягчение: TUI изолирован. `opencode serve` (`cli/cmd/serve.ts`) тянет только
`server/server`, `@opentui/*` в `packages/server` и `packages/core` не встречается вообще.
Значит порт «сервер + web/HTTP API + CLI-команды без TUI» реален, а TUI — отдельное решение
(свой рендер на ANSI, как уже сделанный `src/ansi_console.cpp`, либо внешний терминал).

### 5. SQLite: node:sqlite вместо better-sqlite3

`core/src/database/sqlite.node.ts` (и `packages/effect-sqlite-node`) используют `node:sqlite`
(`DatabaseSync`), `drizzle-orm/node-sqlite`. `better-sqlite3` в зависимостях нет.

Проверено на официальном `node-v22.23.4-win-x86`:
```
v22.23.4 ia32 win32
node:sqlite OK, exports: DatabaseSync,StatementSync,constants,backup
query: [{"a":42,"b":"hello"}]
sqlite version: 3.53.4
(ExperimentalWarning: SQLite is an experimental feature…)
```
Флаг не нужен (предупреждение, не ошибка). Для справки: `better-sqlite3` prebuilt под
node-ABI win32-ia32 **не публикует** (в релизах v11.10.0 / v12.4.1 / v13.0.3 только x64/arm64,
ia32 — лишь electron-ABI), так что если бы он был нужен — только сборка из исходников.

### 6. Мелочи, влияющие на решение

- `bun:ffi` в TUI (`terminal-win32.ts`) — замена `koffi` (`@koromix/koffi-win32-ia32` публикуется).
- Сам `node.exe` x86 (22.23.4) статически импортирует `GetSystemTimePreciseAsFileTime` (Win8) —
  под Win7 обязателен шим (это уже территория t1/t2, но именно от этого зависит, заработает ли
  `node:sqlite`, т.к. он вкомпилирован в бинарник).

## Что реально запускалось (журнал)

| Проверка | Команда (сокр.) | Результат |
|---|---|---|
| npm-метаданные opencode 1.19.3 | `registry.npmjs.org/opencode-ai/1.19.3` | только платформенные Bun-пакеты, ia32 нет |
| `@lydell/node-pty-win32-ia32` | GET registry | **404** |
| `@parcel/watcher-win32-ia32` | `npm i --cpu=ia32 --os=win32` | установлен, `subscribe` даёт события, arch=ia32 |
| node-pty 1.1.0 ia32 | `node-gyp --arch=ia32` + MSBuild Win32 | собралось: `pty.node`/`winpty.dll`/`winpty-agent.exe` (i386), spawn работает |
| tree-sitter-bash ia32 native | `npm i tree-sitter-bash --cpu=ia32` | `.node` i386, грузится (не нужно) |
| tree-sitter WASM | `web-tree-sitter` + `Language.load(fs.readFileSync(...))` | парсинг bash работает на ia32 |
| `node:sqlite` | `node.exe -e "new DatabaseSync(':memory:')"` | работает, SQLite 3.53.4, без флага |
| импорты PE на Win8+ | `tools/pe_imports.py` по `watcher.node`, `pty.node`, `winpty.dll`, `opentui.dll` | watcher/pty/winpty — чисто; `opentui.dll` — `GetSystemTimePreciseAsFileTime` + UCRT |
| `import ... with {type:"wasm"}` | node 22.23.4 ia32 | `ERR_IMPORT_ATTRIBUTE_UNSUPPORTED` (и с флагом) → нужен патч |

Артефакты экспериментов лежат в `spike/_src/` (скачанные тарболы, распакованный `node.exe` x86,
тестовый `ia32test/` с собранными модулями). Их можно удалить, в отчёт они не входят.

## Вердикт

- **Без TUI порт реален**: watcher (prebuilt ia32 ✓), SQLite (`node:sqlite` ✓), tree-sitter (WASM ✓),
  fff (graceful fallback ✓), photon (WASM ✓), pty (собрать upstream node-pty под x86 ✓, либо
  отключить — `#pty` ленивый).
- **TUI — единственный жёсткий блокер ia32**: `@opentui/core` не имеет ни ia32-сборки, ни дешёвого
  пути сборки, и вдобавок требует Win8+/VT даже на x64.
- Трудозатраты по нативке: ~1 день (сборка node-pty x86 + 3 мелких патча: `useConpty:false`,
  wasm-импорт через fs, `@lydell` → `node-pty`).
