# Инвентаризация opencode v1.19.3 (спайк, подзадача t1)

Ревизия: `e3c4eb242585dcd77be1a867391b7c25fd47ce52`, тег `v1.19.3`, дата коммита 2026-10-16, сообщение «release: v1.19.3».
Источник: `git clone --depth 1 --branch v1.19.3 https://github.com/anomalyco/opencode.git spike/opencode-src`.

Методика подсчёта: скрипты `spike/analyze.mjs` и `spike/matrix.mjs` (читают `packages/*`, файлы `.ts/.tsx/.js/.jsx/.mjs/.cjs`, без `node_modules`), регулярка `Bun\.[A-Za-z_$][\w$]*`, модули — `"bun:..."` в кавычках.
Разделение: **prod** = файлы вне каталогов `test/`, `tests/`, `e2e/` и вне `*.test.* / *.spec.*`; **test** — всё остальное. Счёт текстовый: одна и та же строка вида `Bun.file(...)` считается один раз за вхождение, поэтому цифры чуть завышены относительно «уникальных мест вызова» (например, повторное упоминание `Bun.Build.CompileTarget` в типе).

---

## 1. Структура `packages/*`

32 каталога верхнего уровня (плюс вложенные `console/*`, `stats/*`, `sdk/js`). LOC — только `.ts/.tsx` вне тестов.

| Пакет | npm-имя | LOC | Назначение |
|---|---|---|---|
| opencode | `opencode` | 88 526 | Главный продукт: CLI (yargs), сервер, инструменты агента, TUI-команда, сборка SEA-бинарника |
| core | `@opencode-ai/core` | 34 299 | Движок: сессии, БД/миграции, PTY, файловый поиск, npm, Effect-слой `LayerNode` |
| server | `@opencode-ai/server` | 1 720 | Effect `HttpApi` (routes, auth, CORS, middleware), транспорт — `node:http` |
| protocol | `@opencode-ai/protocol` | 1 614 | Контракты HttpApi/схем, независимые от core/server |
| schema | `@opencode-ai/schema` | 3 461 | Доменные схемы (Effect Schema), общие для всех слоёв |
| client | `@opencode-ai/client` | 4 694 | Сгенерированный Promise/Effect клиент API |
| sdk / sdk-next | `@opencode-ai/sdk`, `@opencode-ai/sdk-next` | 30 379 / 81 | Публичный SDK (JS) и новый Effect-нативный in-process хост |
| tui | `@opencode-ai/tui` | 27 761 | Терминальный интерфейс: TSX + SolidJS поверх `@opentui/*` |
| session-ui | `@opencode-ai/session-ui` | 20 386 | Общая UI-логика сессий (без рендера терминала) |
| app | `@opencode-ai/app` | 155 543 | Веб-приложение (SolidStart/Vite), встраивается в бинарник как Web UI |
| ui | `@opencode-ai/ui` | 33 952 | Библиотека компонентов SolidJS |
| web | `@opencode-ai/web` | 6 971 | Сайт документации (Astro/Starlight) |
| docs | — | 0 | Контент документации (mdx, json) |
| llm | `@opencode-ai/llm` | 12 259 | Schema-first ядро LLM-запросов и провайдер-адаптеров |
| plugin | `@opencode-ai/plugin` | 1 695 | API плагинов (в т.ч. peer-зависимость на `@opentui/core`) |
| codemode | `@opencode-ai/codemode` | 6 913 | Изолированное исполнение кода по schema-описанным инструментам |
| desktop | `@opencode-ai/desktop` | 8 501 | Electron-приложение (main/preload/renderer) |
| cli | `@opencode-ai/cli` | 875 | Второй CLI `lildax` на Effect `NodeRuntime` (Node-нативный вход) |
| script | `@opencode-ai/script` | 88 | Общие хелперы сборки/публикации (channel, version) |
| http-recorder | `@opencode-ai/http-recorder` | 1 696 | Запись/воспроизведение Effect HTTP трафика (кассеты) |
| httpapi-codegen | `@opencode-ai/httpapi-codegen` | 1 489 | Генерация SDK из HttpApi |
| effect-drizzle-sqlite | `@opencode-ai/effect-drizzle-sqlite` | 3 360 | Effect-слой над drizzle + sqlite |
| effect-sqlite-node | `@opencode-ai/effect-sqlite-node` | 179 | Тонкая обёртка Effect SqlClient над `node:sqlite` |
| function | `@opencode-ai/function` | 414 | Lambda-энтрипоинты |
| enterprise | `@opencode-ai/enterprise` | 1 243 | SolidStart-приложение (enterprise-сборка) |
| console | `@opencode-ai/console-*` | 43 859 | Платные сервисы: app/core/function/mail/resource/support |
| stats | `@opencode-ai/stats-*` | 19 863 | Отдельный сайт статистики (core/app/server/function) |
| slack | `@opencode-ai/slack` | 156 | Slack-бот |
| storybook | `@opencode-ai/storybook` | 1 198 | Storybook UI-компонентов |
| containers | — | 78 | Docker-образы для CI (`base`, `bun-node`, `rust`) |
| identity | — | 0 | Только ассеты логотипа |

Итого prod TS/TSX: **2 625 файлов, ~513 253 строк**.

Для порта релевантны: `opencode`, `core`, `server`, `protocol`, `schema`, `client`, `tui`, частично `app`/`ui`.

---

## 2. Вхождения `Bun.*` и `bun:` — по API и по пакетам

### 2.1. Топ по частоте (весь `packages/*`)

| API | prod (вне тестов) | test | всего |
|---|---|---|---|
| `Bun.write` | 27 | 377 | **404** |
| `Bun.file` | 55 | 87 | **142** |
| `Bun.stringWidth` | 25 | 16 | **41** |
| `Bun.serve` | **0** | 29 | **29** |
| `Bun.sleep` | **0** | 27 | **27** |
| `Bun.Glob` | 7 | 7 | **14** |
| `Bun.spawn` | 3 | 11 | **14** |
| `Bun.env` | 10 | 0 | **10** |
| `Bun.build` | 4 | 2 | **6** |
| `Bun.which` | 0 | 5 | 5 |
| `Bun.$` | 1 | 4 | 5 |
| `Bun.argv` | 1 | 3 | 4 |
| `Bun.stdin` | 2 | 1 | 3 |
| `Bun.gc` | 0 | 2 | 2 |
| `Bun.readableStreamToText` | 0 | 2 | 2 |
| `Bun.spawnSync` / `Bun.hash` / `Bun.Transpiler` / `Bun.fetch` / `Bun.Build` | по 0–1 | по 0–1 | по 1 |

Всего `Bun.*`: **713** вхождений, из них **137 вне тестов** (и только **33** в рантайм-коде `packages/*/src`).
Крупный перекос в тесты объясняется фикстурами: 86 из 404 `Bun.write` — в одном `packages/opencode/test/plugin/loader-shared.test.ts`.

Модули `bun:`: `bun:test` — **699** (только тесты), `bun:sqlite` — **5** (2 в prod: `core/src/database/sqlite.bun.ts`, `tui/src/editor-zed.ts`), `bun:ffi` — **1** (`tui/src/terminal-win32.ts`).

### 2.2. Только рантайм (`packages/*/src`, без тестов) — реальная поверхность порта

| API | Кол-во | Где |
|---|---|---|
| `Bun.stringWidth` | 25 | `opencode/src/cli/cmd/run/*` (20, в основном `footer.prompt.tsx`), `tui/src/prompt/display.ts`, `tui/src/component/prompt/autocomplete.tsx` (5) |
| `Bun.stdin.text()` | 2 | `opencode/src/cli/cmd/tui.ts:99`, `opencode/src/cli/cmd/run.ts:417` (чтение piped-ввода) |
| `Bun.file().text()/.json()` + `Bun.write` | 4 | только `tui/src/util/persistence.ts` (строки 5, 9, 14, 25) |
| `Bun.hash` | 1 | `core/src/skill/discovery.ts:113` (хэш пути кэша) |
| `Bun.$` | 1 | `opencode/src/plugin/index.ts:167` (проброс в контекст плагина, `typeof Bun === "undefined" ? undefined : Bun.$`) |

То есть **33 места вызова на весь рантайм**, и почти всё — тривиально шимится (`string-width`, `node:fs`, `node:crypto`, `node:child_process`).

### 2.3. По пакетам (prod / test)

| Пакет | Bun.* prod | Bun.* test | `bun:` модули | prod-файлов | test-файлов |
|---|---|---|---|---|---|
| opencode | 55 | 478 | bun:test(263), bun:sqlite(1) | 380 | 303 |
| core | 12 | 34 | bun:test(146), bun:sqlite(1) | 321 | 160 |
| tui | 9 | 28 | bun:test(46), bun:sqlite(1), bun:ffi(1) | 155 | 52 |
| desktop | 12 | 3 | bun:test(15) | 122 | 15 |
| sdk (js) | 12 | 0 | bun:test(1) | 43 | 1 |
| ui | 9 | 0 | bun:test(4) | 243 | 4 |
| cli | 7 | 0 | bun:test(1) | 21 | 1 |
| console | 7 | 1 | bun:test(12) | 253 | 12 |
| http-recorder | 6 | 6 | bun:test(1) | 17 | 1 |
| plugin | 3 | 0 | — | 36 | 0 |
| script | 2 | 0 | — | 2 | 0 |
| stats | 2 | 0 | bun:test(7), bun:sqlite(1) | 86 | 7 |
| llm | 1 | 1 | bun:test(33) | 60 | 45 |
| containers | 1 | 0 | — | 1 | 0 |
| app | 0 | 3 | bun:test(129) | 371 | 227 |
| httpapi-codegen | 0 | 8 | bun:test(3) | 2 | 11 |
| schema / client / codemode / sdk-next | 0 | 2–4 | bun:test(2–7) | 65/12/26/4 | 6/4/7/2 |
| server / protocol / session-ui / web / slack / storybook / enterprise / function | 0 | 0 | bun:test(0–14) | 29/23/82/19/2/24/16/3 | 0/1/14/0/0/0/2/1 |
| effect-drizzle-sqlite | 0 | 1 | bun:sqlite(1), bun:test(1) | 21 | 1 |
| effect-sqlite-node | 0 | 0 | — | 2 | 0 |

Матрица «API × пакет» (prod, ключевые каталоги):

| API | opencode/src | core/src | tui/src | opencode/script | core/script | desktop/scripts | sdk/js | ui/script | всего prod |
|---|---|---|---|---|---|---|---|---|---|
| `Bun.file` | – | – | 2 | 15 | 5 | 4 | 5 | 6 | 55 |
| `Bun.write` | – | – | 2 | 2 | 4 | 5 | 6 | 3 | 27 |
| `Bun.stringWidth` | 20 | – | 5 | – | – | – | – | – | 25 |
| `Bun.env` | – | – | – | 8 | – | 2 | – | – | 10 |
| `Bun.Glob` | – | – | – | 3 | 2 | – | 1 | – | 7 |
| `Bun.build` | – | – | – | 2 | – | – | – | – | 4 |
| `Bun.spawn` | – | – | – | 2 | – | – | – | – | 3 |
| `Bun.stdin` | 2 | – | – | – | – | – | – | – | 2 |

Ключевой вывод: **почти весь `Bun.file`/`Bun.write`/`Bun.build`/`Bun.env`/`Bun.spawn` — в build-скриптах (`packages/*/script`), а не в рантайме**. Рантайм-поверхность — `stringWidth` + 4 файловых вызова + stdin.

### 2.4. Что НЕ использует Bun (важно для порта)

- `Bun.serve` в prod — **0**. Сервер: `NodeHttpServer` из `@effect/platform-node`, `createServer` из `node:http` (`packages/opencode/src/server/server.ts`), `@effect/platform-node` в 10+ местах.
- WebSocket — `ws`, HTTP-клиент — `fetch`/Effect HttpClient.
- Уже есть runtime-абстракции с условием `"node"`:
  - `packages/core/package.json` → `#sqlite` (`sqlite.bun.ts` на `bun:sqlite` / `sqlite.node.ts` на `node:sqlite` + `drizzle-orm/node-sqlite`), `#pty` (`bun-pty` / `@lydell/node-pty`), `#fff` (`@ff-labs/fff-bun` / заглушка `fff.node.ts` с `available() === false`);
  - `packages/opencode/package.json` → `#db`: `./src/storage/db.node.ts` **не существует** (висячая настройка, никем не импортируется — след незавершённой Node-поддержки).
- `node:sqlite` (`DatabaseSync`) — экспериментальный API Node 22.5+; на Node 22 работает с предупреждением, полная поддержка с 24.
- `web-tree-sitter` + `tree-sitter-bash/tree-sitter-powershell` — **WASM**, не нативные (`shell.ts:322–325`).

---

## 3. Нативные зависимости

Собрано из всех `packages/**/package.json` (257 внешних зависимостей) + проверка содержимого npm-пакетов.

| Пакет | Версия | Тип | Windows-бинарники в пакете | Годно для Win7 **x86**? |
|---|---|---|---|---|
| `@opentui/core` | 0.4.5 | нативный Zig-рендерер, грузится через FFI | `@opentui/core-win32-x64` / `-win32-arm64` (`opentui.dll`, PE machine `0x8664`) | **нет** — ia32-сборки нет |
| `@lydell/node-pty` | 1.2.0-beta.12 | prebuilt N-API (ConPTY) | `-win32-x64`, `-win32-arm64` | **нет** — ia32 нет |
| `bun-pty` | 0.4.8 | Rust `rust_pty.dll` через `bun:ffi` | один `rust_pty.dll`, machine `0x8664` (x64) | **нет** (и требует `bun:ffi`) |
| `@parcel/watcher` | 2.5.1 | N-API `.node` | `-win32-x64`, `-win32-arm64` | **нет** — ia32 нет |
| `@ff-labs/fff-bun` | 0.9.4 | Rust-библиотека через `bun:ffi` | `@ff-labs/fff-bin-win32-x64` | **нет** (bun-only + x64) |
| `@silvia-odwyer/photon-node` | 0.3.4 | **WASM** (`photon_rs_bg.wasm`) | — | да |
| `tree-sitter-bash` / `tree-sitter-powershell` | 0.25.0 / 0.25.10 | **WASM** (`*.wasm`) + опциональные node-биндинги | — | да |
| `web-tree-sitter` | 0.25.10 | WASM | — | да |
| `esbuild` | (транзитивно, в `trustedDependencies` корня) | нативный Go-бинарник | win32-x64 | нет (только сборка) |
| `electron` + `electron-builder` и др. | (desktop) | нативный Chromium | win32-x64 | нет; desktop вне scope |

Итого «жёстко нативных» в рантайме CLI/TUI: **5** — `@opentui/core` (через него весь TUI), `@lydell/node-pty`, `bun-pty`, `@parcel/watcher`, `@ff-labs/fff-bun`. **Ни у одного нет win32-ia32 сборки**, и все Windows-артефакты — x64 PE.

Смягчающие обстоятельства: pty/watcher/fff уже имеют node-ветки в `core` (fff просто отключается, поиск падает на ripgrep-слой); остаётся `@opentui/core`.

---

## 4. На чём написан TUI

- **Не Go и не Rust.** TUI — **TypeScript/TSX + SolidJS**: `@opentui/solid` (`render`, `useRenderer`), `@opentui/keymap`, JSX-компоненты в `packages/tui/src/component/*.tsx` (161 prod-файл `.ts/.tsx`, ~27 761 строка).
- Рендерер — **нативное ядро OpenTUI на Zig** (`opentui.dll`), вызывается через FFI. В `packages/tui/src` только одно прямое обращение: `bun:ffi` в `terminal-win32.ts` (GetStdHandle/GetConsoleMode/SetConsoleMode для Win32-консоли).
- Сам `@opentui/core@0.4.5` имеет два бэкенда: `chunk-bun` (`bun:ffi`) и `chunk-node`, который делает `requireModule("node:ffi")` с флагом `--experimental-ffi --allow-ffi`. Проверено на хосте: Node 24.22.0 — `bad option: --experimental-ffi`, модуля `node:ffi` нет. То есть Node-бэкенд OpenTUI рассчитан на будущие Node-сборки, а не на Node 22/24 сегодня.
- Дополнительно TUI запускается в **Web Worker** (`new Worker(...)` в `packages/opencode/src/cli/cmd/tui.ts:249`, RPC через `postMessage`). На хосте Node 24.22 `typeof Worker === "undefined"` — нужен шим на `node:worker_threads`.
- Есть `packages/ui` + `packages/app` (SolidJS веб-UI) и команда `opencode web` — потенциальный обход TUI для слабой машины (браузер Supermium 122 на нетбуке).

Вывод: TUI на Node 22 x86 в текущем виде неисполним — нужен либо x86-порт `opentui.dll` (Zig, самостоятельная сборка), либо замена рендерера, либо отказ от TUI в пользу `web`/`run`-режима.

---

## 5. Точка входа CLI и сборка бинарника

**Точки входа:**
1. `packages/opencode/bin/opencode` — обычный **Node CJS**-шим (`require("child_process")`), который ищет в `node_modules` платформенный бинарник `opencode-<os>-<arch>[-baseline]/bin/opencode.exe`, проверяет AVX2 (через PowerShell `IsProcessorFeaturePresent(40)`) и `spawn`-ит его с пробросом SIGINT/SIGTERM/SIGHUP. Поддерживает `OPENCODE_BIN_PATH`.
2. `packages/opencode/src/index.ts` — сам CLI на `yargs` (`--help`, `run`, `serve`, `web`, `tui`, `db`, `models`, `upgrade`, …), в конце `process.exit()`.
3. `packages/cli/bin/lildax.cjs` + `packages/cli/src/index.ts` — второй, «Node-нативный» CLI на `@effect/platform-node/NodeRuntime` + `NodeServices.layer` (команды `api`, `debug`, `migrate`, `service`, `serve`).

**Сборка** (`bun run build` в `packages/opencode` → `packages/opencode/script/build.ts`):
- `Bun.build({ compile: { target: "bun-windows-x64" | "bun-windows-x64-baseline" | ... , outfile: dist/<name>/bin/opencode } })` — то есть **Bun SEA**, 12 целей (linux/darwin/win32 × x64/arm64, +baseline без AVX2, +musl).
- Плагин `@opentui/solid/bun-plugin` (JSX), `splitting: true`, `minify`, `external: ["node-gyp"]`.
- В бандл инлайнятся: `src/cli/tui/worker.ts`, `opentui-tree-sitter-worker.js`, собранный Web UI (`opencode-web-ui.gen.ts`), models.dev, `Bun.build({ files: ... })`.
- `define`: `OPENCODE_VERSION`, `OPENCODE_CHANNEL`, `OPENCODE_MODELS_DEV`, `OTUI_TREE_SITTER_WORKER_PATH`, `OPENCODE_WORKER_PATH`, `FFF_LIBC`.
- Smoke-тест: запуск `--version` для текущей платформы.
- `packages/cli/script/build.ts` — то же для `lildax`.
- **`script/pack.ts` в корне отсутствует**; `pack.ts` есть только в `packages/http-recorder/script/` и `packages/ui/script/` (публикация npm-пакетов).
- CI: `.github/workflows/publish.yml` (`setup-bun`, matrix `--os/--cpu`, `bun run build`), релиз — `gh release upload` zip/tar.gz.
- Корневой `package.json`: `packageManager: bun@1.3.14`, `postinstall: bun run --cwd packages/core fix-node-pty`, `trustedDependencies` включают `node-pty`, `tree-sitter*`, `electron`.

**Целей win32-ia32 в сборке нет** — только `win32-x64` и `win32-arm64`. Бинарник для 32-битной Windows придётся собирать отдельно (Bun не выпускает `bun-windows-x86`), что и есть главный системный блокер спайка.

---

## 6. Сводка для оценки порта

| Аспект | Оценка |
|---|---|
| Рантайм-код Bun-специфичен | слабо: **33** места вызова, все с готовыми node-аналогами |
| Сервер | **Node-нативный** (`node:http` + Effect `NodeHttpServer`), `Bun.serve` в prod нет |
| БД | уже есть `node:sqlite`-ветка (`#sqlite`), но `node:sqlite` — experimental в Node 22 |
| PTY | уже есть `@lydell/node-pty`-ветка, но только win32-**x64** |
| FFF (быстрый поиск) | на Node отключается штатно (`available() === false`), деградация на ripgrep |
| Watcher | `@parcel/watcher` только x64 |
| TUI | **блокер**: `opentui.dll` (Zig, x64) + `node:ffi`/Web Worker; нужна отдельная x86-сборка ядра OpenTUI или отказ от TUI |
| Бинарник | сборка через `Bun.build --compile`, цели win32 только x64/arm64; для x86 нужен другой способ упаковки (node SEA / jpackage-подобный launcher) |

Остаточные риски, не проверенные в этой подзадаче: поведение `node:sqlite` на Node 22 x86, наличие x86-сборок зависимостей `effect`/`ai-sdk` (чистый JS — вероятно ок), воспроизведение `Bun.build`-специфики (`files`, `splitting`, inline-ассеты) вне Bun.
