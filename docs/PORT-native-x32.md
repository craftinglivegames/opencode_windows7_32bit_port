# Нативный порт OpenCode на Windows 7 x32 — статус (ноябрь 2026)

Итог: **OpenCode работает нативно на 32-битной Windows 7 SP1** — сервер,
web-UI и терминал целиком на нетбуке, второй x64-компьютер не нужен.
Полный технический отчёт по переносу Bun → Node x86:
[docs/spike-win7-x86/REPORT.md](spike-win7-x86/REPORT.md).

## Проверено на реальном железе

- HP Mini 200-4200, Intel Atom N2600 (2×1.6 ГГц), 2 ГБ RAM
- Windows 7 Максимальная SP1 x32, hostname `WIN7-NETBOOK`
- Скриншоты: [screenshots/](../screenshots/)

| Компонент | Статус |
|---|---|
| Node 22.23.4 x86 + шимы (ocshim32/ocadv32) | ✅ сервер грузится и работает |
| Бандл opencode 1.19.3 под Node (без `Bun.*`) | ✅ 0 ссылок на Bun в бандле |
| Встроенный web-UI (Supermium `--app`) | ✅ UI и ассеты отдаются |
| **Создание сессии из UI** | ✅ (исправлен transition-баг Solid) |
| **Список моделей в UI** | ✅ OpenCode Go + 12 free OpenRouter |
| node-pty x86 (winpty на Win7) | ✅ spawn `cmd.exe`, exit 0 |
| LLM через OpenRouter (free) | ✅ 10 из 12 моделей, exit 0 |
| OpenCode Go | ✅ `deepseek-v4.1-flash`, `iceman-contributor-free` |
| Сервер по LAN | ✅ `http://192.0.2.20:4096` с хоста |

## Релизный пакет

`dist/OpenCode-Win7-x32-portable.zip` (~54 МБ):

```
node\node.exe      Node 22.23.4 x86 + ocshim32.dll, ocadv32.dll
app\index.js       opencode 1.19.3 (bun build --target=node)
app\webui\         встроенный web-UI
OpenCode.exe       нативный лаунчер (GUI, без консоли)
OpenCode.cmd       резервный запуск через bat
run_server.cmd     сервер на 0.0.0.0:4096 (для LAN)
README.txt         инструкция для конечного пользователя
```

Лаунчер `OpenCode.exe` (`src/local_launcher.cpp`):

- если на 4096 уже работает наш `node.exe` — просто открывает окно;
- если порт занят другим `node.exe` (старая сборка) — завершает его;
- запускает сервер скрыто, с `NODE_SKIP_PLATFORM_CHECK=1`, логи в `logs\`;
- ждёт готовности до 90 с и открывает Supermium `--app=http://127.0.0.1:4096`;
- режимы для диагностики: `--serve-only`, `--stop`, `--selftest`.

## Исправления, без которых UI не работал

Внесены в релизный архив; воспроизводятся
`python tools/patch_release.py <папка-пакета>`.

1. **Каталог проекта.** В `Project.fromDirectory` для не-git папки
   подставлялся корень диска (`C:\`), а worktree — `/`. UI запрашивал
   провайдеров для несуществующего каталога (`/provider?directory=/` → 499),
   и список моделей оставался пустым. Теперь используется реальный каталог.
2. **Transition-баг Solid 1.9.10.** Мемо, созданные внутри router-transition
   (`startTransition`), при чтении в том же переходе возвращали `undefined`.
   «Новая сессия» падала с `Cannot read properties of undefined
   (reading 'child' / 'directory' / 'data')`, и страница застревала на
   «Что-то пошло не так». В бандле переходы выполняются синхронно.
3. **Кэш браузера.** Главный чанк помечен `?v=2`, чтобы старый JS не залипал.

## Конфигурация моделей (важно)

Ключи: `%USERPROFILE%\.local\share\opencode\auth.json`
Конфиг: `%USERPROFILE%\.config\opencode\opencode.json`

**Обязательно добавьте отбор провайдеров** — иначе первый запрос списка
моделей тянет весь каталог models.dev (7 МБ, 20–40 с), и UI выглядит пустым:

```json
"enabled_providers": ["opencode-go", "openrouter"]
```

Пример конфига с free-моделями OpenRouter:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "model": "openrouter/nvidia/nemotron-3.5-lightning:free",
  "enabled_providers": ["opencode-go", "openrouter"],
  "provider": {
    "openrouter": {
      "models": {
        "nvidia/nemotron-3.5-lightning:free": {
          "name": "Nemotron 3.5 Lightning (free)",
          "tool_call": true,
          "temperature": true,
          "limit": { "context": 131072, "output": 2048 }
        }
      }
    }
  }
}
```

## Тест OpenCode Go

Батч на нетбуке, все запросы через `opencode run`, exit 0:

```
[1/4] deepseek-v4.1-flash (1 request):  → DEEPSEEK_41_FLASH_OK   [exit 0]
[2/4] iceman-contributor-free (req 1):  → ICEMAN_1_OK            [exit 0]
[3/4] iceman-contributor-free (req 2):  → 391  (17×23 — верно)   [exit 0]
[4/4] iceman-contributor-free (req 3):  → Paris                  [exit 0]
```

## Free-модели OpenRouter (батч-тест)

10 из 12 работают (`opencode run` → OK, exit 0):
`nvidia/nemotron-3.5-lightning:free`, `-super-vl:free`, `-ultra-550b-a55b:free`,
`-3-nano-omni-30b-a3b-reasoning:free`, `cohere/north-mini-code:free`,
`recursive/flint-v1.0:free`, `xingchen-agi/xing4.0-29b-a4b:free`,
`liquid/lfm-2.5-2.6b:free`, `liquid/lfm-3-1.7b:free`.

Не работают: `dots-studio/dots-3-note-preview:free` (UnknownError провайдера),
`google/gemma-4-*:free` (upstream rate-limit). Groq блокируется Cloudflare (403)
из сети нетбука.

## Известные ограничения

- **TUI недоступен**: терминальный интерфейс opencode требует `@opentui/core`
  (x64 + Win8+). На Win7 x32 используется web-UI в Supermium.
- **Сервер без пароля** (`OPENCODE_SERVER_PASSWORD` не задан) и слушает
  `0.0.0.0:4096` — используйте только в доверенной локальной сети.
- **models.dev**: если каталог недоступен, free-модели нужно объявлять в
  конфиге вручную (см. выше); `enabled_providers` обязателен для быстрого UI.
- Порт 4096 должен быть свободен; лаунчер умеет останавливать старый `node.exe`.
- Firewall при тестах был выключен; для LAN-доступа добавьте правило для 4096.
