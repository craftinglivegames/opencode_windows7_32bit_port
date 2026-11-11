"""Build the portable Windows 7 x32 package and zip it.

This is a build-workspace helper: it expects a prepared opencode build tree
(`spike/` by default, override with the OPENCODE_SPIKE environment variable):

    <spike>/nodeshim/node.exe, ocshim32.dll, ocadv32.dll
    <spike>/opencode-src/packages/opencode/index-bunnode.js  (+ impl/, webui/, node_modules/)
    <spike>/opencode-src/node_modules/.bun/...               (solid-js chain)
    dist/OpenCode.exe                                        (built by tools/build_launcher.cmd)

Run `python tools/make_portable.py`; the result is
`dist/OpenCode-Win7-x32-portable.zip`.  The release hot-fixes are applied
automatically by `tools/patch_release.py`.
"""

import os
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPIKE = os.environ.get("OPENCODE_SPIKE", os.path.join(ROOT, "spike"))
OC = os.path.join(SPIKE, "opencode-src", "packages", "opencode")
PKG = os.path.join(SPIKE, "bundle_out", "portable_x86")
ZIP_PATH = os.path.join(ROOT, "dist", "OpenCode-Win7-x32-portable.zip")

# fresh build dir
if os.path.isdir(PKG):
    shutil.rmtree(PKG)
os.makedirs(os.path.join(PKG, "node"))
os.makedirs(os.path.join(PKG, "app"))

# 1. patched node x86 + shims
for f in ("node.exe", "ocshim32.dll", "ocadv32.dll"):
    shutil.copy2(os.path.join(SPIKE, "nodeshim", f), os.path.join(PKG, "node", f))

# 1b. native launcher (built by tools/build_launcher.cmd)
launcher = os.path.join(ROOT, "dist", "OpenCode.exe")
if os.path.isfile(launcher):
    shutil.copy2(launcher, os.path.join(PKG, "OpenCode.exe"))
else:
    print("WARN: dist/OpenCode.exe not found; package will only have OpenCode.cmd")

# 2. app: bundle + package.json + impl + assets + node_modules subset
shutil.copy2(os.path.join(OC, "index-bunnode.js"), os.path.join(PKG, "app", "index.js"))
shutil.copy2(os.path.join(OC, "impl", "package.json"), os.path.join(PKG, "app", "impl_pkg.json"))
with open(os.path.join(PKG, "app", "package.json"), "w") as f:
    f.write('{"type":"module"}\n')
os.makedirs(os.path.join(PKG, "app", "impl"), exist_ok=True)
shutil.copy2(os.path.join(OC, "impl", "package.json"), os.path.join(PKG, "app", "impl", "package.json"))
for f in os.listdir(os.path.join(OC, "impl")):
    if f.endswith(".js"):
        shutil.copy2(os.path.join(OC, "impl", f), os.path.join(PKG, "app", "impl", f))
for f in os.listdir(OC):
    if f.endswith(".wasm") or f.endswith(".mp3"):
        shutil.copy2(os.path.join(OC, f), os.path.join(PKG, "app", f))
shutil.copytree(os.path.join(OC, "webui"), os.path.join(PKG, "app", "webui"),
                ignore=shutil.ignore_patterns("*.map"))

NM_SRC = os.path.join(OC, "node_modules")
BUN = os.path.join(SPIKE, "opencode-src", "node_modules", ".bun")
SKIP_EXT = (".map", ".pdb", ".ilk", ".exp", ".lib")


def copy_tree(src, dst):
    for r, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d != "obj"]
        rel = os.path.relpath(r, src)
        target = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(target, exist_ok=True)
        for f in files:
            if f.endswith(SKIP_EXT):
                continue
            shutil.copy2(os.path.join(r, f), os.path.join(target, f))


# @opentui trio from packages/opencode/node_modules
for pkg in ("core", "solid", "keymap"):
    copy_tree(os.path.join(NM_SRC, "@opentui", pkg), os.path.join(PKG, "app", "node_modules", "@opentui", pkg))
# node-pty x86
copy_tree(os.path.join(NM_SRC, "node-pty"), os.path.join(PKG, "app", "node_modules", "node-pty"))
# solid-js chain from the .bun store
for store, arc in [
    ("solid-js@1.9.10", "solid-js"),
    ("csstype@3.2.3", "csstype"),
    ("seroval@1.3.2", "seroval"),
    ("seroval-plugins@1.3.3+7c651f2eb938f734", "seroval-plugins"),
    ("s-js@0.4.9", "s-js"),
    ("entities@7.0.1", "entities"),
]:
    src = os.path.join(BUN, store, "node_modules", arc)
    if not os.path.isdir(src):
        print("MISS store", store)
        continue
    copy_tree(src, os.path.join(PKG, "app", "node_modules", arc))

# 2a. drop build-time leftovers from the bundled node_modules: MSVC project
# files, node-gyp configs, tests and benchmarks. They are useless at runtime,
# they bloat the archive and they carry the absolute paths of the machine that
# produced the prebuilt packages (e.g. C:\Users\build\...).
STRIP_SUFFIX = (
    ".vcxproj", ".vcxproj.filters", ".vcxproj.user", ".sln",
    ".test.js", ".test.ts", ".spec.js", ".spec.ts", ".map",
)
STRIP_FILES = ("config.gypi", "binding.gyp", ".npmignore", ".gitignore")


def strip_dev_files(root):
    removed = 0
    for r, dirs, files in os.walk(root, topdown=False):
        for d in list(dirs):
            rel = os.path.relpath(os.path.join(r, d), root).replace(os.sep, "/")
            if d in ("bench", "obj", "docs", "test", "tests", "__tests__") or rel.endswith("node-pty/build/deps"):
                shutil.rmtree(os.path.join(r, d), ignore_errors=True)
                removed += 1
        for f in files:
            if f.endswith(STRIP_SUFFIX) or f in STRIP_FILES:
                try:
                    os.remove(os.path.join(r, f))
                    removed += 1
                except OSError:
                    pass
    return removed


_stripped = strip_dev_files(os.path.join(PKG, "app", "node_modules"))
print("stripped %d dev leftovers from app/node_modules" % _stripped)

# 2b. scrub the build-machine user name from shipped files (node-gyp
# *.vcxproj, config.gypi, PDB paths embedded in node-pty binaries and the
# bundle). The replacement has the same byte length, so PE/PDB structures
# stay intact; no literal user name is hard-coded here.
_build_user = (os.environ.get("USERNAME") or os.path.basename(os.path.expanduser("~"))).encode("ascii", "ignore")
_names = [_build_user] if len(_build_user) >= 3 else []
# generic CI/prebuild users that survive in upstream prebuilt packages
_names += [b"build", b"runneradmin"]
_names += [n for n in (b"builder", b"vagrant") if n != _build_user]
for r, dirs, files in os.walk(PKG):
    for f in files:
        p = os.path.join(r, f)
        try:
            with open(p, "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        new = data
        for name in _names:
            if b"\\Users\\" + name in new:
                new = new.replace(b"\\Users\\" + name, b"\\Users\\" + b"x" * len(name))
        if new != data:
            with open(p, "wb") as fh:
                fh.write(new)

# 2c. licenses and third-party notices (required by the MIT/BSD terms of the
# bundled components: upstream notices must travel with binary redistributions)
for src_rel, dst_name in (
    ("LICENSE", "LICENSE.txt"),
    (os.path.join("third_party", "opencode-LICENSE.txt"), "LICENSE-opencode.txt"),
    (os.path.join("third_party", "node-LICENSE.txt"), "LICENSE-nodejs.txt"),
    ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.txt"),
):
    src_abs = os.path.join(ROOT, src_rel)
    if not os.path.isfile(src_abs):
        raise SystemExit("license file missing: " + src_abs)
    shutil.copy2(src_abs, os.path.join(PKG, dst_name))

# 2d. free-model sync helper (refreshes models.json cache + opencode.json).
# Runs on Win7 with the bundled node, no extra downloads needed.
for sync_rel in (
    os.path.join("tools", "sync_free_models.js"),
    os.path.join("tools", "sync_free_models.cmd"),
    os.path.join("tools", "accounts_panel.js"),
):
    sync_abs = os.path.join(ROOT, sync_rel)
    if not os.path.isfile(sync_abs):
        raise SystemExit("helper missing: " + sync_abs)
    shutil.copy2(sync_abs, os.path.join(PKG, os.path.basename(sync_rel)))

# 3. launchers
OPENCODE_CMD = """@echo off
setlocal
set "DIR=%~dp0"
set NODE_SKIP_PLATFORM_CHECK=1
rem --- start the server if it is not running yet ---
netstat -ano | findstr ":4096 .*LISTENING" >nul
if errorlevel 1 (
  echo Starting OpenCode server...
  start "OpenCode server" /min "%DIR%node\\node.exe" "%DIR%app\\index.js" serve --hostname 127.0.0.1 --port 4096
  rem give the weak machine time to boot the server
  ping -n 12 127.0.0.1 >nul
)
rem --- open the UI in Supermium app mode (or default browser) ---
set "CHROME="
if exist "%LOCALAPPDATA%\\Supermium\\Application\\chrome.exe" set "CHROME=%LOCALAPPDATA%\\Supermium\\Application\\chrome.exe"
if exist "%ProgramFiles%\\Supermium\\Application\\chrome.exe" set "CHROME=%ProgramFiles%\\Supermium\\Application\\chrome.exe"
if exist "%ProgramFiles(x86)%\\Supermium\\Application\\chrome.exe" set "CHROME=%ProgramFiles(x86)%\\Supermium\\Application\\chrome.exe"
if defined CHROME (
  start "" "%CHROME%" --app=http://127.0.0.1:4096 --user-data-dir="%DIR%profile"
) else (
  start "" http://127.0.0.1:4096
)
endlocal
"""
with open(os.path.join(PKG, "OpenCode.cmd"), "w", newline="\r\n") as f:
    f.write(OPENCODE_CMD)

RUN_SERVER_CMD = """@echo off
setlocal
set "DIR=%~dp0"
set NODE_SKIP_PLATFORM_CHECK=1
"%DIR%node\\node.exe" "%DIR%app\\index.js" serve --hostname 0.0.0.0 --port 4096
endlocal
"""
with open(os.path.join(PKG, "run_server.cmd"), "w", newline="\r\n") as f:
    f.write(RUN_SERVER_CMD)

ACCOUNTS_CMD = """@echo off
setlocal
set "DIR=%~dp0"
rem Opens the accounts & limits panel (http://127.0.0.1:4099).
start "" "%DIR%OpenCode.exe" --accounts
endlocal
"""
with open(os.path.join(PKG, "accounts.cmd"), "w", newline="\r\n") as f:
    f.write(ACCOUNTS_CMD)

README = r"""OpenCode для Windows 7 x32 — портативная версия
=================================================

Что это
-------
Нативный (локальный) OpenCode для 32-битной Windows 7 SP1: сервер и веб-интерфейс
работают целиком на этой машине, второй x64-компьютер не нужен.
Рантайм — Node.js 22 x86 с шимами (node\ocshim32.dll, node\ocadv32.dll),
движок OpenCode — app\index.js (порт с Bun на Node), интерфейс — встроенный
web-UI (app\webui), терминал — node-pty x86 (winpty).

Как запустить
-------------
1. Распакуйте папку целиком в любое место (например C:\OpenCode).
2. Запустите OpenCode.exe — это нативный лаунчер (без консоли):
   - если сервер на порту 4096 уже работает, он просто откроет окно;
   - иначе остановит старый node.exe на 4096 и поднимет новый сервер;
   - дождётся готовности и откроет интерфейс в Supermium в режиме
     приложения (окно без адресной строки). Если Supermium не найден —
     откроется браузер по умолчанию.
3. Резервный вариант — OpenCode.cmd (то же самое, но через bat-файл).
4. Для доступа с других машин в локальной сети откройте
   http://<ip-этой-машины>:4096 (лаунчер слушает 0.0.0.0:4096).
   Внимание: сервер без пароля — используйте только в доверенной сети.
5. Закрыть сервер: завершите процесс node.exe (или перезагрузите ПК).

Первый запуск после обновления
------------------------------
Если открылась старая страница с ошибкой — один раз нажмите Ctrl+F5,
чтобы сбросить кэш браузера.

Модели и авторизация
--------------------
Ключи провайдеров: %USERPROFILE%\.local\share\opencode\auth.json
Конфиг: %USERPROFILE%\.config\opencode\opencode.json

Чтобы список моделей открывался быстро (а не тянул весь каталог models.dev),
добавьте в opencode.json отбор провайдеров:
  "enabled_providers": ["opencode-go", "openrouter"]
Без этого первый запрос списка моделей может занимать 20-40 секунд.

Бесплатные модели (OpenRouter, без оплаты)
------------------------------------------
Заведите ключ на openrouter.ai (Keys) и положите в auth.json:
  {"openrouter": {"type": "api", "key": "sk-or-v1-..."}}

Затем в opencode.json объявите нужные модели (models.dev может быть
недоступен с Win7), например:
  {
    "$schema": "https://opencode.ai/config.json",
    "model": "openrouter/nvidia/nemotron-3.5-lightning:free",
    "enabled_providers": ["opencode-go", "openrouter"],
    "provider": { "openrouter": { "models": {
      "nvidia/nemotron-3.5-lightning:free": {
        "name": "Nemotron 3.5 Lightning (free)",
        "tool_call": true, "temperature": true,
        "limit": {"context": 131072, "output": 2048}
      }
    }}}
  }

Проверенные free-модели (тест в opencode на Win7 x32, 10 из 12 работают):
  nvidia/nemotron-3.5-lightning:free          nvidia/nemotron-3.5-super-vl:free
  nvidia/nemotron-3-ultra-550b-a55b:free      nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free
  cohere/north-mini-code:free                 recursive/flint-v1.0:free
  xingchen-agi/xing4.0-29b-a4b:free           liquid/lfm-2.5-2.6b:free
  liquid/lfm-3-1.7b:free                      (google/gemma-4-*:free — нестабильны upstream)

Каталог models.dev меняется: модели уходят в deprecated. Актуальный список
бесплатных подтянет sync_free_models.cmd (лежит рядом, нужен только интернет):
  C:\OpenCode\sync_free_models.cmd
Скрипт обновит кэш %USERPROFILE%\.cache\opencode\models.json и допишет живые
free-модели в opencode.json (с бэкапом). После него перезапустите сервер
(закройте окно сервера и запустите OpenCode.cmd заново) — список в интерфейсе
читается один раз при старте сервера.

Аккаунты и лимиты (accounts.cmd)
--------------------------------
Без входа бесплатные модели идут через общий публичный ключ: квота считается
по внешнему IP (сброс в полночь UTC), поэтому все устройства одной сети —
и офис за NAT — делят один лимит. Вход под своим аккаунтом даёт отдельную
квоту.

Откройте accounts.cmd (или OpenCode.exe --accounts) — откроется локальная
страница http://127.0.0.1:4099:

  - "Войти по коду" — вход через opencode.ai (device flow): показывается код,
    вход подтверждается в браузере, страница сама дождётся результата;
  - "Добавить сервисный ключ" — аккаунт по API-ключу (oc_sk_...);
  - список аккаунтов: "Сделать активным" (перезапускает сервер — он читает
    учётные данные при старте), "Имя", "Удалить".

Список аккаунтов панель хранит сама в
%USERPROFILE%\.local\share\opencode\port_accounts.json; активный аккаунт
записывается в базу порта. Сервер opencode 1.19.3 держит только одну учётную
запись на провайдера, поэтому переключение = запись выбранного аккаунта +
перезапуск сервера.

Важно: один и тот же аккаунт на разных устройствах всё равно делит лимит —
"своя квота" означает "свой аккаунт".

Где лежат данные
----------------
Сессии:   %USERPROFILE%\.local\share\opencode
Конфиг:   %USERPROFILE%\.config\opencode
Кэш:      %USERPROFILE%\.cache\opencode
Логи лаунчера: logs\launcher.log, логи сервера: logs\server.log

Лицензии
--------
Код порта (лаунчер, шимы, скрипты сборки) - MIT, см. LICENSE.txt.
OpenCode - MIT (c) 2025 opencode: LICENSE-opencode.txt. Мы изменили сборку
(перенос Bun -> Node, правки UI); список изменений - THIRD_PARTY_NOTICES.txt.
Node.js 22 x86 - MIT и лицензии встроенных библиотек: LICENSE-nodejs.txt.
Пакеты в app\node_modules сохраняют собственные LICENSE.
Сборка неофициальная, с авторами OpenCode не связана; распространяется
"как есть", без гарантий.

Автор и студия
--------------
Автор порта: Никита Шустол - https://vk.ru/nikitashustol
Студия: CraftingLiveGames - https://craftinglivegames.xyz/

Требования
----------
Windows 7 SP1 x32/x64 с установленным UCRT (KB2999226) — на обновлённой
системе есть. Порт 4096 не должен быть занят. Свободно ~200 МБ на диске.
"""

with open(os.path.join(PKG, "README.txt"), "w", encoding="utf-8-sig", newline="\r\n") as f:
    f.write(README)

# 3b. apply the release hot-fixes (project path + UI transition + cache-bust)
import subprocess
rc = subprocess.call([sys.executable, os.path.join(ROOT, "tools", "patch_release.py"), PKG])
if rc != 0:
    raise SystemExit("patch_release.py failed; refusing to build a broken package")

# 4. zip
if os.path.exists(ZIP_PATH):
    os.remove(ZIP_PATH)
zf = zipfile.ZipFile(ZIP_PATH, "w", zipfile.ZIP_DEFLATED, compresslevel=6)
for r, dirs, files in os.walk(PKG):
    for f in files:
        p = os.path.join(r, f)
        arc = os.path.join("OpenCode-Win7-x32", os.path.relpath(p, PKG))
        zf.write(p, arc.replace(os.sep, "/"))
zf.close()
print("package dir:", PKG)
print("zip:", ZIP_PATH, "%.1f MB" % (os.path.getsize(ZIP_PATH) / 1e6))
