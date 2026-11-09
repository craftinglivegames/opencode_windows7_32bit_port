# Публикация релиза

Чек-лист перед открытием репозитория и созданием GitHub Release.

## 1. Проверить дерево

```bat
git status
git diff --stat
```

Убедиться, что в коммит не попали:

- `auth.json`, `opencode.json` с настоящими ключами провайдеров;
- логи (`logs/`, `*.log`) и локальные профили;
- папки `spike/`, `build/*.obj`, `__pycache__`;
- реальные IP, имена компьютеров и пути с именем пользователя (`C:\Users\...`);
  в документации и конфигах должны остаться только плейсхолдеры
  (`192.0.2.*`, `WIN7-NETBOOK`).

`dist/server.ini` и `dist/bridge.ini` содержат демонстрационный токен
`ocshell` — это значение по умолчанию, но в релизной заметке стоит напомнить
его сменить.

## 2. Проверить артефакты

Архив и exe — сборочные артефакты, **в git не коммитятся** (в `.gitignore`);
они собираются `tools\make_portable.py` / `tools\build_launcher.cmd` и
загружаются в GitHub Release.

| Файл | Размер | SHA-256 |
|---|---|---|
| `dist/OpenCode-Win7-x32-portable.zip` | 54 539 370 | `2743e0aba1e9591141533ea1ceb605dbb30ff23b76592ff964db408b796e61f5` |
| `dist/OpenCode.exe` | 153 600 | `f5c6ee1cbf6297c416b47e0a975eb5f711bc3a45c7d5a073024349a44d035916` |

- [ ] Архив распаковывается, внутри `OpenCode.exe`, `node\`, `app\`, `README.txt`.
- [ ] В архиве есть `LICENSE.txt`, `LICENSE-opencode.txt`, `LICENSE-nodejs.txt`,
      `THIRD_PARTY_NOTICES.txt` (требование MIT: уведомления upstream должны
      ехать вместе с бинарной поставкой).
- [ ] `OpenCode.exe --selftest` пишет `logs\launcher.log`.
- [ ] На чистой Win7 SP1 x32 после распаковки: запуск → браузер → health 200.
- [ ] `README.md` и `docs/PORT-native-x32.md` не противоречат друг другу.

## 3. Коммит и тег

```bat
git add -A
git commit -m "Release: native Win7 x32 port with OpenCode.exe launcher, UI fixes and updated docs"
git tag -a v0.2.0-x32 -m "OpenCode for Windows 7 x32 (native Node port)"
git push origin main
git push origin v0.2.0-x32
```

## 4. GitHub Release

1. Releases → Draft a new release → тег `v0.2.0-x32`.
2. Заголовок: **OpenCode 1.19.3 for Windows 7 x32 (native port)**.
3. Приложить `dist/OpenCode-Win7-x32-portable.zip` как asset
   (файл в репозитории можно оставить или удалить после загрузки — GitHub
   ограничивает 100 МБ на файл, архив проходит).
4. В описании указать:
   - требования: Windows 7 SP1, UCRT (KB2999226), ~200 МБ на диске;
   - запуск: распаковать → `OpenCode.exe`;
   - первый запуск после обновления: `Ctrl+F5`;
   - `enabled_providers` в конфиге для быстрого списка моделей;
   - известные ограничения: нет TUI, сервер без пароля, free-модели зависят
     от доступности провайдера;
   - контрольные суммы SHA-256 (таблица выше);
   - лицензии: MIT на код порта, MIT-уведомления opencode и Node.js сохранены
     в архиве; проект неофициальный и не связан с авторами OpenCode.
5. Если репозиторий публичный — включить Issues и указать в шаблоне,
   что это неофициальный порт.

## 5. После публикации

- [ ] Проверить скачивание архива без авторизации.
- [ ] Проверить, что ссылки в README ведут на существующие файлы.
- [ ] Добавить в README бейдж релиза (по желанию).
