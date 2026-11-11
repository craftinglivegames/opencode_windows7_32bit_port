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

Всё собираемое — `OpenCode.exe`, `OCShellClient.exe`, шимы и архив — в git
**не коммитится** (в `.gitignore`): это выходы `tools\build_launcher.cmd`,
`tools\build_client.cmd`, `tools\build_shims*.cmd` и `tools\make_portable.py`.
В Release загружается только архив.

| Файл | Размер | SHA-256 |
|---|---|---|
| `dist/OpenCode-Win7-x32-portable.zip` | 54 493 028 | `af28981853bd37f0f6bd946b3e0644c2b9ec5aa8a162364deb7b5f38b403dafa` |
| `OpenCode.exe` внутри архива | 159 232 | `bd580bc0f2a319e839e0e5d3e8f83dc6b18d13606d057dcfeaba66d3b406d1e2` |

Это суммы релиза **`v0.2.6-x32`**. Для нового релиза их надо пересчитать и
обновить в трёх местах: `README.md`, `README.ru.md` и таблице выше —
иначе пользователи получат «несовпадение» при проверке:

```bat
certutil -hashfile dist\OpenCode-Win7-x32-portable.zip SHA256
```

- [ ] Архив распаковывается, внутри `OpenCode.exe`, `node\`, `app\`, `README.txt`.
- [ ] В архиве есть `LICENSE.txt`, `LICENSE-opencode.txt`, `LICENSE-nodejs.txt`,
      `THIRD_PARTY_NOTICES.txt` (требование MIT: уведомления upstream должны
      ехать вместе с бинарной поставкой).
- [ ] В архиве нет `*.vcxproj`, `*.test.js`, `bench/` и `config.gypi`
      (их вырезает `make_portable.py`) и нет путей вида `C:\Users\...`.
- [ ] `OpenCode.exe --selftest` пишет `logs\launcher.log`.
- [ ] На чистой Win7 SP1 x32 после распаковки: запуск → браузер → health 200.
- [ ] `README.md` и `docs/PORT-native-x32.md` не противоречат друг другу.

## 3. Коммит и тег

```bat
git add -A
git commit -m "Release: native Win7 x32 port with OpenCode.exe launcher, UI fixes and updated docs"
git tag -a vX.Y.Z-x32 -m "OpenCode for Windows 7 x32 (native Node port)"
git push origin main
git push origin vX.Y.Z-x32
```

## 4. GitHub Release

1. Releases → Draft a new release → тег `vX.Y.Z-x32`.
2. Заголовок: **OpenCode 1.19.3 for Windows 7 x32 (native port)**.
3. Приложить `dist/OpenCode-Win7-x32-portable.zip` как asset (в git архива нет;
   GitHub ограничивает 100 МБ на файл, архив проходит).
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
- [ ] Скачать ассет и сверить SHA-256 с таблицей в README (а не только локально).
- [ ] В шапке репозитория GitHub показывает «MIT license» (в `LICENSE` —
      только текст MIT, всё остальное лежит в `THIRD_PARTY_NOTICES.md`).
- [ ] Проверить, что ссылки в README (`README.md` и `README.ru.md`) ведут на
      существующие файлы и друг на друга.
- [ ] Убедиться, что release-бейдж в README показывает версию, а не
      `no releases or repo not found`.
