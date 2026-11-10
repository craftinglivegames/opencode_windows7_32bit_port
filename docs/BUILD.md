# Сборка x32-порта (Windows 7 SP1)

Здесь описана полная цепочка сборки нативного Win7-x32 пакета.
Готовый архив собирается скриптом `tools\make_portable.py`, но ему нужны
подготовленные компоненты (Node с шимами и бандл opencode).

## Что нужно

| Компонент | Версия / примечание |
|---|---|
| Windows + Visual Studio (MSVC) | x86-инструменты, проверено на VS 2022/2026 |
| Python | 3.9+ |
| Bun | 1.x — только для сборки бандла opencode |
| `node-v22.23.4-win-x86.zip` | официальный x86-архив Node 22 |
| исходники opencode | тег `v1.19.3`, коммит `e3c4eb2` (`anomalyco/opencode`) |

Дерево сборки (пути по умолчанию, можно переопределить `OPENCODE_SPIKE`).
Всё, что здесь получается, — результаты сборки: в git они не хранятся.

```
spike/
  nodeshim/                 node.exe + ocshim32.dll + ocadv32.dll
  opencode-src/             клон opencode с патчами порта
  bundle_out/portable_x86/  собранный пакет (создаёт make_portable.py)
build/                      объектники и DLL шимов (tools\build_*.cmd)
dist/OpenCode.exe           лаунчер (tools\build_launcher.cmd)
dist/OCShellClient.exe      legacy-клиент (tools\build_client.cmd)
```

## 1. Node 22 x86 и шимы

На Win7 x86 у официального `node.exe` не хватает двух импортов:
`KERNEL32!GetSystemTimePreciseAsFileTime` и `ADVAPI32!EventSetInformation`.
Остальные 385 импортов резолвятся (UCRT должен быть установлен — KB2999226).

```bat
:: извлеките node.exe из node-v22.23.4-win-x86.zip, затем:
tools\build_shims_x86.cmd C:\path\to\node.exe
```

Скрипт сам вызывает `shim_tool.py gen_defs`, собирает `ocshim32.dll` /
`ocadv32.dll` (x86) и патчит `node.exe`: `KERNEL32 → ocshim32.dll`,
`ADVAPI32 → ocadv32.dll`. Результат — в `build\nodeshim_x86\`.

> В скриптах сборки путь к `vcvarsall.bat` указан для Visual Studio 18
> (2026). Если у вас другая версия — поправьте переменную `VC` в начале
> `tools\build_*.cmd`.

Рядом с `node.exe` должны лежать обе DLL. Проверка на Win7:

```
set NODE_SKIP_PLATFORM_CHECK=1
node.exe -v          → v22.23.4
node.exe -e "require('node:sqlite')"   → без ошибок
```

## 2. Бандл opencode (Bun → Node)

В клоне opencode применены патчи порта (подробно —
[spike-win7-x86/REPORT.md](spike-win7-x86/REPORT.md)): замена `Bun.*` на
Node-аналоги, `bun:sqlite` → `node:sqlite`, `@lydell/node-pty` → `node-pty`,
удаление `with { type: "file" }` и т.п.

Сборка:

```bash
bun build packages/opencode/src/index.ts --target=node \
  --tsconfig-override tsconfig.nodeport.json \
  --outdir=spike/bundle_out/bunnode2 \
  --external @opentui/core --external @opentui/solid \
  --external node-gyp --external node-pty

python tools/patch_bundle.py spike/bundle_out/bunnode2/index.js
```

`patch_bundle.py` заменяет `os.hostname()` (в Node 22 x86 на Win7
`uv_os_gethostname` возвращает ENOSYS).

Рядом с `index.js` нужны:

- `impl/` — jsonc-parser UMD (парсер конфигов);
- `package.json` с `{"type":"module"}`;
- `node_modules/node-pty` (собран `--arch=ia32`, winpty);
- `node_modules/@opentui/*`, `solid-js`, `seroval`, `seroval-plugins`, `s-js`,
  `entities`, `csstype`.

## 3. Web-UI

```bash
cd packages/app
bun run build          # dist/ — index.html + assets/
```

Затем карта ассетов и архив для пакета:

```bat
python tools\gen_webui_map.py
python tools\make_assets_zip.py
```

`webui/` кладётся рядом с `index.js` (сервер отдаёт файлы с диска).

## 4. Лаунчер

```bat
tools\build_launcher.cmd
```

Результат — `dist\OpenCode.exe` (PE32 x86, GUI-подсистема 6.01, статический CRT).
Исходник: `src/local_launcher.cpp`. Режимы: обычный запуск, `--serve-only`,
`--stop`, `--selftest`.

## 5. Портативный пакет

```bat
python tools\make_portable.py
```

Скрипт собирает `spike\bundle_out\portable_x86`, кладёт `OpenCode.exe`,
`OpenCode.cmd`, `run_server.cmd`, README, лицензии, применяет release-патчи
(`tools\patch_release.py`), вычищает локальные пути сборочной машины из
`node-pty` (`config.gypi`, `*.vcxproj`, PDB-пути в `.node`/`.dll`) и упаковывает
`dist\OpenCode-Win7-x32-portable.zip`.

Архив в git не коммитится (см. `.gitignore`) — это артефакт релиза.

## 6. Release-патчи

`tools\patch_release.py <папка>` делает четыре правки в готовом бандле:

1. каталог проекта — реальный каталог вместо корня диска;
2. worktree проекта — реальный каталог вместо `/`;
3. web-UI — синхронные переходы (фикс `undefined` при создании сессии);
4. `index.html` — `?v=2` для сброса кэша.

Скрипт идемпотентен и завершается с ошибкой, если строка-цель не найдена
(значит, бандл изменился и патч нужно обновить).

## 7. Проверка

```bat
C:\OpenCode\OpenCode.exe --selftest
C:\OpenCode\OpenCode.exe --serve-only
curl http://127.0.0.1:4096/global/health
curl "http://127.0.0.1:4096/provider?directory=C%%3A%%2FOpenCode"
```

Ожидаемо: `{"healthy":true}`, `/provider` — 200 и небольшой JSON
(при заданном `enabled_providers`), в `logs\launcher.log` — `server is up`.
