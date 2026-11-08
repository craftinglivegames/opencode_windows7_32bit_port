# OpenCode для Windows 7 SP1

Неофициальный порт [OpenCode](https://opencode.ai) (v1.19.3) для **Windows 7 SP1**.
Два независимых направления:

| Направление | Статус |
|---|---|
| **Нативный x32** — Node 22 x86 + шимы, сервер и web-UI целиком на 32-битной Win7 | ✅ **Работает**, проверено на реальном нетбуке |
| **x64** — патченный Bun SEA (`OCShellWin7.exe`) | ⚠️ Лаунчер и шимы работают, но Bun-рантайм завершается с кодом 3 |

> **Для обычного использования берите нативный x32-пакет.**
> Он не требует второго компьютера и рассчитан на 32-битную Win7; на 64-битной
> запускается как 32-битное приложение (WoW64, отдельно не тестировалось).

---

## Скачать и запустить (x32, рекомендуется)

**Файл:** [`dist/OpenCode-Win7-x32-portable.zip`](dist/OpenCode-Win7-x32-portable.zip) (~54 МБ)

1. Распакуйте архив целиком в папку без пробелов, например `C:\OpenCode`.
2. Запустите **`OpenCode.exe`**.
   - Лаунчер остановит старый сервер на порту 4096 (если он был),
     поднимет новый и откроет интерфейс в Supermium в режиме приложения.
   - Если Supermium не найден — откроется браузер по умолчанию.
   - При повторном запуске просто открывается окно, сервер не перезапускается.
3. Если после обновления открылась старая страница с ошибкой — один раз
   нажмите **Ctrl+F5** (сброс кэша браузера).
4. Для доступа с других машин: `http://<ip-этого-ПК>:4096`
   (сервер слушает `0.0.0.0`, пароля нет — только для доверенной сети).

Резервный запуск без exe: `OpenCode.cmd` (то же самое через bat-файл).

### Модели и ключи

- Ключи: `%USERPROFILE%\.local\share\opencode\auth.json`
- Конфиг: `%USERPROFILE%\.config\opencode\opencode.json`
- Чтобы список моделей открывался быстро, добавьте в конфиг:
  ```json
  "enabled_providers": ["opencode-go", "openrouter"]
  ```
  Иначе сервер один раз тянет полный каталог models.dev (7 МБ, 20–40 с) —
  именно из-за этого список моделей мог выглядеть пустым.

Пример конфига с бесплатными моделями OpenRouter — в
[`docs/PORT-native-x32.md`](docs/PORT-native-x32.md) и в `README.txt` внутри архива.

---

## Что проверено

Реальное железо: **HP Mini 200-4200**, Intel Atom N2600 (2×1.6 ГГц), 2 ГБ RAM,
Windows 7 Максимальная SP1 x32, hostname `WIN7-NETBOOK`.

| Проверка | Результат |
|---|---|
| Сервер `/global/health` | ✅ `{"healthy":true}` |
| Web-UI и ассеты | ✅ 200 |
| Создание сессии из UI | ✅ (после фикса transition-бага Solid) |
| Список моделей в UI | ✅ OpenCode Go + 12 free OpenRouter (10 рабочих) |
| `opencode run` через OpenCode Go | ✅ `deepseek-v4.1-flash`, `iceman-contributor-free` |
| Free-модели OpenRouter | ✅ 10 из 12 (см. ниже) |
| Терминал (node-pty / winpty) | ✅ `cmd.exe`, exit 0 |
| Доступ по LAN | ✅ `http://192.0.2.20:4096` |

Free-модели (10 из 12, `opencode run` → OK):
`nvidia/nemotron-3.5-lightning:free`, `-super-vl:free`, `-ultra-550b-a55b:free`,
`-3-nano-omni-30b-a3b-reasoning:free`, `cohere/north-mini-code:free`,
`recursive/flint-v1.0:free`, `xingchen-agi/xing4.0-29b-a4b:free`,
`liquid/lfm-2.5-2.6b:free`, `liquid/lfm-3-1.7b:free`.
Не работают: `dots-studio/dots-3-note-preview:free` (ошибка провайдера),
`google/gemma-4-*:free` (нестабильный upstream). Groq блокируется Cloudflare.

### Скриншоты

| | |
|---|---|
| ![Система](screenshots/1_netbook_win7_x32_system.png) | ![Диспетчер задач](screenshots/2_netbook_task_manager.png) |
| Win7 SP1 x32, Atom N2600, 2 ГБ RAM | Процессы и память на нетбуке |
| ![Окружение](screenshots/3_netbook_ui_environment.jpg) | ![Ответ](screenshots/4_netbook_ui_live_answer.jpg) |
| OpenCode в Supermium: отчёт об окружении | Живой ответ модели в UI |

---

## Как это устроено

```
node\node.exe      Node 22.23.4 x86, патченный под Win7 + ocshim32/ocadv32
app\index.js       opencode 1.19.3, собранный bun build --target=node
app\webui\         встроенный web-UI (SolidJS), отдаётся сервером
OpenCode.exe       нативный x86-лаунчер (GUI, без консоли)
```

- **Node 22 x86.** Официальной сборки Node 22 под Win7 нет; бинарник пропатчен:
  `KERNEL32!GetSystemTimePreciseAsFileTime` и `ADVAPI32!EventSetInformation`
  перенаправлены на шимы (`ocshim32.dll`, `ocadv32.dll`). Всего проверено
  385 статических импортов — остальные на Win7 резолвятся.
- **Bun → Node.** Бандл собран `bun build --target=node`; все prod-вызовы `Bun.*`
  заменены на Node-аналоги (33 места), `bun:sqlite` → `node:sqlite`,
  `@lydell/node-pty` → `node-pty` (winpty на Win7).
- **Лаунчер.** `OpenCode.exe` проверяет порт 4096, при необходимости завершает
  чужой `node.exe`, запускает сервер скрыто, ждёт готовности до 90 с и открывает
  Supermium с `--app=http://127.0.0.1:4096`. Логи: `logs\launcher.log`,
  `logs\server.log`.

### Исправления, без которых UI не работал

Эти правки уже внесены в релизный архив и воспроизводятся
`python tools/patch_release.py <папка-пакета>`:

1. **Путь проекта.** Для не-git папки сервер подставлял корень диска
   (`C:\`) и worktree `/`; UI запрашивал провайдеров для несуществующего
   каталога — список моделей был пуст. Теперь используется реальный каталог.
2. **Transition-баг Solid 1.9.10.** Созданные внутри router-transition мемо
   читались как `undefined`, из-за чего «Новая сессия» падала с
   `Cannot read properties of undefined (reading 'child' / 'directory' / 'data')`.
   Переходы в бандле выполняются синхронно.
3. **Кэш браузера.** Главный чанк помечен `?v=2`, чтобы старый JS не залипал.

---

## Сборка из исходников

Нужны Visual Studio (MSVC, x86), Python 3, а для пересборки `app\index.js` —
Bun и клон `anomalyco/opencode` v1.19.3
(подробно: [`docs/BUILD.md`](docs/BUILD.md),
технический отчёт: [`docs/spike-win7-x86/REPORT.md`](docs/spike-win7-x86/REPORT.md)).

```bat
:: 1. шимы и патч node.exe x86 (нужен исходный node.exe)
tools\build_shims_x86.cmd путь\к\node.exe

:: 2. нативный лаунчер -> dist\OpenCode.exe
tools\build_launcher.cmd

:: 3. портативный пакет -> dist\OpenCode-Win7-x32-portable.zip
python tools\make_portable.py
```

`make_portable.py` сам вызывает `tools\patch_release.py` и кладёт в архив
`OpenCode.exe` + `OpenCode.cmd` + `run_server.cmd` + README.

---

## x64-ветка (Bun SEA) — историческая

[`dist/OCShellWin7.exe`](dist/OCShellWin7.exe) — самораспаковывающийся лаунчер
для Win7 SP1 x64, который патчит `opencode.exe` (Bun SEA) на шимы
`ocshim32.dll` / `ocn.dll` и перерисовывает ANSI в консоли Win7.

**Текущий статус:** самораспаковка и шимы работают, но Bun 1.3.14 на Win7
завершается с кодом 3 без вывода. Это ограничение самого Bun, а не лаунчера;
рабочее решение — нативный x32-порт выше. Исходники лаунчера и шимов
(`src/`, `tools/`) сохранены для истории и экспериментов.

### 32-битный клиент к x64-серверу (схема «сервер + клиент») — legacy

До нативного порта использовалась схема: Bun-сервер на x64-ПК + 32-битный
клиент [`dist/OCShellClient.exe`](dist/OCShellClient.exe), открывающий web-UI
в Supermium. Мост автозапуска — [`dist/OCShellBridge.cmd`](dist/OCShellBridge.cmd)
(`http://host:8011`, общий токен в `server.ini` / `bridge.ini`).
Сейчас схема не нужна, но файлы оставлены для совместимости.

> Токен `ocshell` в `dist/server.ini` и `dist/bridge.ini` — значение по умолчанию.
> Если пользуетесь мостом, смените его и не публикуйте свой конфиг.

---

## Структура репозитория

```
dist/     релизные артефакты (x32-архив, лаунчеры, ini)
src/      C++: лаунчер x32, ANSI-прокси, шимы, self-extract payload
tools/    сборка (MSVC/Python), патч PE, упаковка, release-патчи
docs/     описание порта, отчёт спайка, сборка
screenshots/  скриншоты с реального нетбука
build/    промежуточные DLL (ocshim32, ocn)
LICENSE   MIT для кода порта
RELEASE.md  чек-лист публикации
```

Документация:
- [docs/PORT-native-x32.md](docs/PORT-native-x32.md) — что сделано и как проверялось.
- [docs/BUILD.md](docs/BUILD.md) — полная цепочка сборки x32-пакета.
- [docs/spike-win7-x86/REPORT.md](docs/spike-win7-x86/REPORT.md) — технический отчёт
  по переносу Bun → Node x86 (шимы, бандл, node-pty, матрица моделей).
- [RELEASE.md](RELEASE.md) — чек-лист публикации релиза.

---

## Лицензия и благодарности

Код порта (лаунчеры, шимы, скрипты сборки) — MIT, см. [LICENSE](LICENSE).
OpenCode — проект его авторов, используется по лицензии оригинала.
Node.js, Bun, Supermium и сторонние пакеты — под своими лицензиями.
Проект не связан с авторами OpenCode; используйте на свой риск.
