# Нативный порт OpenCode на Windows 7 x32 — статус (ноябрь 2026)

Итог спайка «Bun → Node x86»: **сервер OpenCode работает нативно на Win7 x32** —
второй x64-компьютер больше не нужен. Полный отчёт: [docs/spike-win7-x86/REPORT.md](spike-win7-x86/REPORT.md).

## Что работает (проверено на реальном нетбуке Win7 Ultimate 32-bit)

| Компонент | Статус |
|---|---|
| Node 22.23.4 x86 + шимы (ocshim32/ocadv32) | ✅ сервер грузится и работает |
| Бандл opencode 1.19.3 под Node (без Bun.*) | ✅ 0 ссылок на Bun в бандле |
| Встроенный web-UI (Supermium `--app`) | ✅ UI 200, ассеты отдаются |
| node-pty x86 (winpty на Win7) | ✅ spawn cmd.exe e2e |
| LLM через OpenRouter (free-модели) | ✅ 10 из 12 моделей, exit 0 |
| **OpenCode Go провайдер** | ✅ см. ниже |
| Сервер по LAN | ✅ http://192.0.2.20:4096 с хоста |

## Тест OpenCode Go (ключ временный, выдан владельцем)

Батч на нетбуке, все запросы через `opencode run`, exit 0:

```
[1/4] deepseek-v4.1-flash (1 request):  → DEEPSEEK_41_FLASH_OK   [exit 0]
[2/4] iceman-contributor-free (req 1):  → ICEMAN_1_OK            [exit 0]
[3/4] iceman-contributor-free (req 2):  → 391  (17×23 — верно)   [exit 0]
[4/4] iceman-contributor-free (req 3):  → Paris                  [exit 0]
```

Скриншоты с экрана нетбука: [screenshots/](../screenshots/).

## Free-модели OpenRouter (батч-тест в opencode на нетбуке)

10 из 12 работают (`opencode run` → OK, exit 0): nvidia/nemotron-3.5-{lightning,super-vl,
ultra-550b-a55b,3-nano-omni-30b-a3b-reasoning}:free, cohere/north-mini-code:free,
recursive/flint-v1.0:free, xingchen-agi/xing4.0-29b-a4b:free, liquid/lfm-{2.5-2.6b,3-1.7b}:free.
Не работают: dots-studio/dots-3-note-preview:free (UnknownError провайдера),
google/gemma-4-*:free (upstream rate-limit). Groq заблокирован Cloudflare (403) из сети нетбука.

## Портативный пакет

`dist/OpenCode-Win7-x32-portable.zip` (54 МБ): node\ (патченый node.exe x86 + ocshim32/ocadv32),
app\ (бандл + webui + node-pty ia32), `OpenCode.cmd` (сервер 127.0.0.1:4096 + Supermium в
режиме приложения), `run_server.cmd` (0.0.0.0 для LAN), README с инструкцией.
Генераторы: `tools/make_portable.py`, `tools/make_assets_zip.py`, `spike/patch_bundle.py`.

## Известные хвосты

- Создание сессии через web-UI падает во фронтенде (TypeError ... 'child', v1.19.3);
  CLI-путь (`opencode run`) и сервер полностью рабочие — требуется фикс связки UI↔server.
- Firewall нетбука при тестах был выключен (netsh-правило через WMI не применилось).
