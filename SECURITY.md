# Security policy / Политика безопасности

[English](#english) · [Русский](#русский)

## English

**What this project is.** An unofficial port of OpenCode (MIT) to Windows 7
SP1 x32. It is not affiliated with the OpenCode authors. The release archive
contains a patched `node.exe`, the OpenCode bundle and our launchers.

**Supported versions.** Only the latest release (`v0.2.5-x32` at the time of
writing) is supported. Older archives are kept for history and do not receive
fixes.

**Designed behaviour, not a vulnerability:**

- the local server listens on `0.0.0.0:4096` **without authentication** —
  anyone who can reach the port can use the agent, read sessions and run
  commands as the current user. Run it on a trusted network only, or change
  `--hostname` to `127.0.0.1`;
- the launcher and the shims are unsigned binaries, so Windows may warn about
  them;
- API keys are stored in plain text in `%USERPROFILE%\.local\share\opencode\`
  (`auth.json`, `port_accounts.json`) — that is how upstream OpenCode stores
  them. Protect the profile, do not sync that folder to untrusted places.

**Please report privately** (do not open a public issue):

1. GitHub → **Security** tab → **Report a vulnerability**
   (private advisory), or
2. e-mail <craftinglivegames@gmail.com> with the subject `[security] opencode win7 port`.

Include: package version, Windows version, what you did, what happened, and a
proof of concept if you have one. Do not attach real API keys or personal
sessions.

**In scope:** the port code in this repository (`src/`, `tools/`, the
`OpenCode.exe` launcher, `ocshim32.dll` / `ocadv32.dll`), the packaging
scripts, and the modified parts of the bundle (`tools/patch_release.py`,
`tools/patch_bundle.py`).

**Out of scope:** vulnerabilities that exist in upstream OpenCode or Node.js and
reproduce on official platforms (report them upstream:
<https://github.com/sst/opencode/issues>), issues caused by exposing the
no-auth server to the internet, and problems in browsers/Supermium.

This is a hobby project: there is no bounty and no SLA, but reports are read and
answered as time allows. We aim to acknowledge a report within a week.

**Safe use, short version:**

- keep the folder out of `Program Files` and out of shared/cloud folders;
- run the server on `127.0.0.1` unless you really need LAN access;
- do not reuse a personal account key on a machine you do not trust;
- verify the archive checksum from the release page before running it.

## Русский

**Что это.** Неофициальный порт OpenCode (MIT) на Windows 7 SP1 x32. С авторами
OpenCode не связан. В архиве релиза — пропатченный `node.exe`, сборка OpenCode
и наши лаунчеры.

**Поддерживаемые версии.** Только последний релиз (`v0.2.5-x32` на момент
написания); старые архивы лежат для истории и не обновляются.

**Это задумано, а не уязвимость:**

- локальный сервер слушает `0.0.0.0:4096` **без пароля** — тот, кто дотянется
  до порта, получит доступ к агенту, сессиям и запуску команд от вашего имени.
  Используйте только в доверенной сети либо поменяйте `--hostname` на
  `127.0.0.1`;
- лаунчер и шимы не подписаны, Windows может показывать предупреждение;
- ключи провайдеров лежат открытым текстом в
  `%USERPROFILE%\.local\share\opencode\` (`auth.json`, `port_accounts.json`) —
  так их хранит upstream OpenCode. Берегите профиль и не синхронизируйте эту
  папку куда попало.

**Сообщайте приватно** (не открывайте публичный issue):

1. вкладка **Security** в GitHub → **Report a vulnerability** (private
   advisory), или
2. письмо на <craftinglivegames@gmail.com> с темой `[security] opencode win7 port`.

В письме: версия пакета, версия Windows, что делали, что получилось и, если
есть, proof of concept. Не прикладывайте настоящие ключи и личные сессии.

**В области действия:** код порта в этом репозитории (`src/`, `tools/`,
лаунчер `OpenCode.exe`, `ocshim32.dll` / `ocadv32.dll`), скрипты упаковки и
изменённые части бандла (`tools/patch_release.py`, `tools/patch_bundle.py`).

**Вне области:** уязвимости самого OpenCode и Node.js, воспроизводимые на
официальных платформах (сообщайте upstream:
<https://github.com/sst/opencode/issues>), последствия выставления сервера без
пароля в интернет, проблемы браузеров/Supermium.

Проект хоббийный: вознаграждений и SLA нет, но отчёты читаются и разбираются по
мере возможности; подтверждение получения — в течение недели.

**Безопасная эксплуатация, коротко:**

- держите папку вне `Program Files` и общих/облачных каталогов;
- сервер — на `127.0.0.1`, если доступ по LAN действительно не нужен;
- не используйте личный ключ аккаунта на машине, которой не доверяете;
- сверяйте SHA-256 архива со страницей релиза перед запуском.
