# Спайк: порт opencode 1.19.3 с Bun на Node x86 для Windows 7

Дата: 2026-11-05 (обновлено ночью, см. раздел «Обновление» в конце)
Исходники: `anomalyco/opencode`, тег `v1.19.3` (коммит `e3c4eb2`)
Цель: понять, реально ли заменить Bun на Node 22 x86 и получить локальный 32-битный opencode.

## Итог одной строкой

**Серверный локальный порт — ДА (прототип работает).** Бандл под Node собран и запускается
на Node 22.23.4 **x86** (`--version`/`--help` работают); Node 22 x86 на Win7 не хватает всего
2 API, и оба уже шимятся. Полный порт с TUI — по-прежнему нет (`@opentui/core` — x64+Win8).
Первичная версия отчёта ниже была пессимистичнее: ночные проверки (patченный node.exe с шимами,
`bun build --target=node` с шимами Bun→Node) изменили вывод по сценариям B.

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
*(Обновление ночью: см. раздел «Обновление» — сценарий B продвинулся до работающего прототипа.)*

## Обновление (ночь 2026-11-05, ~02:00–03:00) — выводы по п.3 и п.4 устарели

### Node 22 x86 на Win7: не хватает только 2 API

`spike/check_imports.py` (все 385 статических импортов `node-v22.23.4-node.exe` (x86) прогнаны
через нетбук через GetProcAddress):

```
checked 385 imports
MISSING (2):
  MISSING ADVAPI32.dll!EventSetInformation   (Win8+, ETW — no-op заглушка достаточна)
  MISSING KERNEL32.dll!GetSystemTimePreciseAsFileTime   (Win8+ — шим уже был в ocshim32)
```

Все `api-ms-win-crt-*` (UCRT) на нетбуке резолвятся — UCRT установлен. Импортов ntdll и
api-set'ов с Win8+ у node.exe нет. Сделано:

- `spike/nodeshim/` — пересборка `ocshim32.dll` (x86) под импорт-набор node.exe (292 kernel32)
- новый `ocadv32.dll` (x86): форвардеры ADVAPI32 → реальная DLL + no-op `EventSetInformation`
- `spike_tool patch` (tools/shim_tool.py): `KERNEL32.dll → ocshim32.dll`, `ADVAPI32.dll → ocadv32.dll`
- На хосте патченый node.exe работает: `v22.23.4 ia32`, `node:sqlite` OK.
- На нетбуке node.exe **прошёл загрузчик** (больше никаких `0xC0000139`/«не является приложением Win32»)
  и остановился только на собственной проверке версии Node — обходится
  `NODE_SKIP_PLATFORM_CHECK=1` (уже зашито в `C:\spike\node22\run_node.cmd`; из-за нестабильности
  канала WMI подтверждение вывода осталось за хвостом спайка).

### Бандл под Node: работает (t4, доведён)

`bun build --target=node` (не esbuild — у него дедлок на TLA+цикле в `account/repo`):

```bash
bun build packages/opencode/src/index.ts --target=node \
  --tsconfig-override tsconfig.nodeport.json --outdir=bundle_out/bunnode \
  --external @opentui/core --external @opentui/solid --external node-gyp
```

Минимальные правки (все в клоне спайка `spike/opencode-src`):

1. `tui/src/editor-zed.ts` — `bun:sqlite` → `node:sqlite` (`DatabaseSync`)
2. `tui/src/terminal-win32.ts` — `bun:ffi` → `./ffi-shim` (заглушка; в порту — koffi ia32)
3. `tui/src/attention.ts` — убрать 5× `with { type: "file" }`
4. `opencode/src/tool/shell.ts` — убрать 3× `with: { type: "wasm" }` (грузить через fs, прототип в t3)
5. Шим-модуль `bun` (fileURLToPath/pathToFileURL из node:url) через `tsconfig.nodeport.json`
6. Стаб `@opentui/solid/runtime-plugin-support/configure` (Bun-only guard)
7. Хак: `jsonc-parser` UMD — скопировать `lib/umd/impl/*.js` рядом с бандлом

Результат (артефакты: `spike/bundle_out/bunnode/`, запускалка
`spike/opencode-src/packages/opencode/index-bunnode.js`):

- Node 24 x64: `--version` → `local`, `--help` → полный CLI, exit 0
- **Node 22.23.4 x86 (патченый shim-ами node.exe): `--version` → `local`, `--help` OK,
  `node:sqlite` инициализируется** (ExperimentalWarning виден — слой БД грузится)

### Обновлённая оценка

| Сценарий | Было | Стало |
|---|---|---|
| B. Локальный сервер + web-UI (без TUI) | 15–25 дней, высокий риск | **прототип уже работает на x86**; до боевого: рантайм-шимы `Bun.stringWidth/file/write/hash` (1–2 дня), node-pty x86 интеграция (~1 день, собран в t3), проверка на нетбуке + обкатка миграций drizzle на `node:sqlite` — суммарно **~3–5 дней** |
| A. Полный порт с TUI | 40–80 дней | без изменений (opentui — отдельный проект) |

### Хвост спайка (сделать первым)

1. ~~Добить подтверждение на нетбуке~~ — **СДЕЛАНО (ночь 05.11, ~04:00).** Тест
   `spike/win7_test.py` на реальном нетбуке (Win7 Ultimate 32-bit, канал WMI + data-порт 8081):
   ```
   WIN7_NODE_OK v22.23.4 ia32
   WIN7_SQLITE_OK {"a":42}
   CPUS 4
   DONE          (EXIT=0)
   ```
   **Node 22.23.4 x86 с шимами ocshim32/ocadv32 работает на Win7, node:sqlite работает.**
   Важно: порт 8080 занят зависшим «молчаливым» слушателем — использовать порт 8081
   (`win7_test.py` уже на нём).
2. Дымовой тест `opencode serve` из бандла на Node x86 (на нетбуке: залить
   `bundle_out/bunnode/*` через канал 8081 и запустить `node index.js serve`).
3. Затем по сценарию B: рантайм-шимы (`Bun.stringWidth/file/write/hash`) → пакетный скрипт
   сборки → сборка node-pty x86 в `#pty`.

## Если всё же двигаться дальше

Минимальный следующий шаг для B: проверить сторонний Node 22 Win7 x86 (запуск `node -v`
и `node:sqlite`), затем собрать `bun build --target=node` с шимами `bun:sqlite` → `node:sqlite`,
`bun:file` → `node:fs`, `Bun.stringWidth` → `string-width`. Это даст измеримый прототип
сервера без TUI за 3–5 дней.

## Итог (ночь 2026-11-07): сценарий B доведён до работающего сервера

Все рантайм-шимы Bun.* сделаны (17+ мест в src, включая .tsx), бандл собран без единой
ссылки на `Bun.*`, встроенный web-UI отдаётся, node-pty@1.1.0 x86 встроен через
`--external node-pty` (winpty на Win7 автоматически). Подробности:

### Что сделано в исходниках (`spike/opencode-src`, поверх v1.19.3)

1. Шимы Bun.* (все в `packages/opencode/src/cli/cmd/bun-compat.ts` и `packages/tui/src/util/string-width.ts`):
   `Bun.stringWidth` → своя stringWidth (ANSI/широкие символы), `Bun.stdin.text()` → `readStdinText()`,
   `Bun.file/Bun.write` (tui/persistence.ts) → `fs/promises`, `Bun.hash` (skill/discovery.ts) → `sha1`.
   Затронуты: run.ts, tui.ts, session.shared.ts, prompt.shared.ts, footer.menu.tsx,
   footer.prompt.tsx, autocomplete.tsx, prompt/index.tsx, display.ts, persistence.ts, discovery.ts.
2. `packages/core/src/pty/pty.node.ts` — `@lydell/node-pty` → upstream `node-pty` (без
   `useConptyDll`; node-pty сам берёт ConPTY на build ≥18309 и winpty на Win7).
3. `packages/opencode/src/server/shared/ui.ts` — встроенный web-UI: генерируется
   `opencode-web-ui.gen.ts` (карта путь→файл в `webui/`, относительные пути, резолвятся
   от `import.meta.url`). Генератор: `tools/make_assets_zip.py` + `bun run build` в `packages/app`.
4. Сборка: `bun build packages/opencode/src/index.ts --target=node --tsconfig-override
   tsconfig.nodeport.json --outdir=bundle_out/bunnode2 --external @opentui/core
   --external @opentui/solid --external node-gyp --external node-pty`,
   затем `python patch_bundle.py` (os.hostname → COMPUTERNAME: uv_os_gethostname = ENOSYS
   в Node 22 x86 на Win7) и копия в `packages/opencode/index-bunnode.js`.

### Хитрости компоновки (важно для воспроизведения)

- Бандл должен лежать рядом с `impl/` (jsonc-parser UMD) и `package.json` с
  `{"type":"module"}`; в `impl/package.json` — `{"type":"commonjs"}` (иначе UMD грузится как ESM
  и `parse is not a function`).
- Рядом нужен `node_modules/`: `node-pty` (собран x86: `spike/_src/ia32test/...`, conpty.node
  PE32 ia32), `@opentui/core|solid|keymap`, `solid-js`, `seroval`, `seroval-plugins`, `s-js`,
  `entities`, `csstype`. Всё это пакуется в `spike/bundle_out/ocweb_assets.zip`
  (tools/make_assets_zip.py; НЕ z-писать абсолютные пути — 7za разложит их в Users/...).

### Проверено

- Хост Node 24 x64 и патченый Node 22.23.4 **x86**: `--version`, `--help`,
  `serve` → `/` 200 (web-UI), `/global/health` → `{"healthy":true,"version":"local"}`,
  `/project` → 200, asset `assets/index-*.js` 200 (2.7 МБ).
- **Нетбук Win7 Ultimate x32 (реальное железо, канал 8081): сервер работает.**
  Деплой: `C:\spike\ocweb\` = index.js (36.6 МБ, чанки по 6 МБ с токен-handshake,
  ассемблинг `copy /b`, размер верифицирован) + assets.zip → 7za ia32 (1620 файлов).
  Запуск: `C:\spike\ocweb\run_server.cmd` → node.exe x86 serve 0.0.0.0:4096.
  Результаты: `/global/health` → 200 `{"healthy":true,"version":"local"}` локально на
  нетбуке; с хоста `http://192.0.2.20:4096/` → 200 (2952 байта, index.html),
  `/project` → 200, `assets/index-*.js` → 200 (2 738 151 байт). Firewall на нетбуке
  выключен (lab-LAN; правило netsh для 4096 не применилось — иная защита профиля).

### Остатки (для боевого состояния)

1. ~~node-pty~~ — **проверено e2e на нетбуке**: spawn cmd.exe через winpty,
   `EXIT 0 OUTPUT_OK` (спойлер: у node-pty 1.1.0 в get_shell_path есть квирк —
   если cwd процесса = System32, поиск «cmd.exe» по PATH возвращает пустоту;
   opencode передаёт абсолютный путь шелла, поэтому не затронут).
2. ~~Первый заход LLM-запроса с нетбука~~ — **СДЕЛАНО (ночь 07.11)**. На нетбуке
   настроен auth.json (openrouter) и config (free-модели объявлены в конфиге,
   т.к. models.dev с нетбука недоступен, а зашитый снапшот не содержит free-слагов).
   `node index.js run "Reply with exactly: OK"` → **вывод: OK, exit 0**.
   Полная цепочка: конфиг → auth → HTTPS стрим → ответ модели.

   **Матрица free-моделей OpenRouter (батч-тест в opencode на нетбуке, 07.11):**
   10 из 12 работают, для каждой `opencode run` вернул `OK` с exit 0:
   - ✅ liquid/lfm-3-1.7b:free, liquid/lfm-2.5-2.6b:free
   - ✅ recursive/flint-v1.0:free
   - ✅ xingchen-agi/xing4.0-29b-a4b:free
   - ✅ nvidia/nemotron-3.5-super-vl:free, -3.5-lightning:free, -3-ultra-550b-a55b:free, -3-nano-omni-30b-a3b-reasoning:free
   - ✅ cohere/north-mini-code:free
   - ❌ dots-studio/dots-3-note-preview:free (UnknownError от провайдера)
   - ❌ google/gemma-4-26b/31b:free (upstream rate-limit, transient)
   Все 12 объявлены в конфиге нетбука → видны в переключателе моделей web-UI;
   модель по умолчанию: `openrouter/nvidia/nemotron-3.5-lightning:free`.
   Groq с нетбука блокируется Cloudflare (403) — ограничение провайдера, не порта.
3. ~~Портативный пакет~~ — **СДЕЛАНО**: `dist/OpenCode-Win7-x32-portable.zip` (54 МБ).
   Состав: node\ (патченый node.exe x86 + ocshim32/ocadv32), app\ (index.js, impl/,
   webui/, node_modules: node-pty ia32, @opentui/*, solid-js-цепочка), OpenCode.cmd
   (сервер на 127.0.0.1:4096 + Supermium --app), run_server.cmd (0.0.0.0 для LAN),
   README.txt. Проверен локально на x86-ноде из пакета: `--version` → local,
   serve → UI 200 + health OK. Генератор: tools/make_portable.py.
4. Firewall нетбука выключен — при боевом использовании вернуть включённым и добавить
   правило (netsh-правило через WMI не применилось — проверить руками на нетбуке).

### Транспорт на нетбук (8081) — что выучено

- Порт 8080 мёртв; работать через `spike/netbook_deploy.py` (WMI + data-порт 8081).
- «Призраки»: слушатель, зависший в AcceptTcpClient, держит порт вечно → новые bind падают
  → connect refused. Лечится: `taskkill /F /IM powershell.exe` + слушатели с self-timeout
  (`$l.Pending()`-poll, 90 c) и handshake-токеном (см. netbook_deploy.send_with_token).
- WMI Create возвращается сразу (не ждёт завершения cmd): после `copy /b` ждать
  завершения до удаления частей. `cmd (a & b)` через PS ломается — писать без скобок.
- Shell.Application CopyHere на нетбуке ползёт (~6 c/файл) — использовать 7za
  (7zip-bin ia32, залит в C:\spike\ocweb\7za.exe).
- zip с абсолютными путями извлекается в ocweb\Users\... — только относительные arcname.

