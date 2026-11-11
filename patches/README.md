# Патчи к upstream OpenCode

Релизный бандл `app/index.js` — это сборка OpenCode 1.19.3, в которую порт внёс
изменения. Чтобы её можно было не только скачать, но и проверить/пересобрать,
изменения вынесены в патч.

| | |
|---|---|
| Upstream | [anomalyco/opencode](https://github.com/anomalyco/opencode) (ex `sst/opencode`) |
| База | тег `v1.19.3`, коммит `e3c4eb2` («release: v1.19.3») |
| Лицензия upstream | MIT, © 2025 opencode |
| Патч | [`0001-win7-x32-source.patch`](0001-win7-x32-source.patch) — 20 файлов, +165/−59 |

## Что меняет патч

- **Bun → Node.** `Bun.stringWidth`, `Bun.stdin.text()` и прочие вызовы рантайма
  заменены на Node-реализации: `packages/opencode/src/cli/cmd/bun-compat.ts`,
  `packages/tui/src/ffi-shim.ts`, `packages/tui/src/util/string-width.ts`.
- **PTY.** `@lydell/node-pty` → `node-pty`, без принудительного ConPTY: на
  Win7/ia32 библиотека сама уходит в winpty (`packages/core/src/pty/pty.node.ts`).
- **Сборка и импорты.** Убраны `with { type: "file" }` и обращения к
  Bun-специфичным модулям, из-за которых бандл не стартовал под Node.
- **TUI/CLI.** Мелкие правки под win32-консоль и отключение путей, которых на
  Win7 нет (команды, zed, persistence, attention, prompt).
- **Web-UI сервер.** `packages/opencode/src/server/shared/ui.ts` — отдача
  встроенного UI и путь к `webui/`.

Патчи самого web-UI (`Solid` transition, реестр контекстов, `?v=3`) не входят в
этот файл: они применяются к уже собранному бандлу скриптом
[`tools/patch_release.py`](../tools/patch_release.py), потому что собирать
`packages/app` на Win7-машине не требуется.

## Что намеренно не входит

- `bun.lock` и `package.json` — состояние зависимостей экспериментальное;
  нужные замены (`node-pty` вместо `@lydell/node-pty`, `node:sqlite` вместо
  `bun:sqlite`) описаны в [docs/BUILD.md](../docs/BUILD.md).
- Результаты сборки: `packages/opencode/webui/`, `index-bunnode.js`,
  `*.wasm`, `*.mp3`, `impl/` — это артефакты `bun build`, а не исходники.

## Применение и сборка

```bat
git clone https://github.com/anomalyco/opencode.git opencode-src
cd opencode-src
git checkout e3c4eb2
git apply path\to\repo\patches\0001-win7-x32-source.patch
```

Дальше — [docs/BUILD.md](../docs/BUILD.md): `bun build ... --target=node`,
`python tools\patch_bundle.py`, сборка web-UI, лаунчер, `make_portable.py`.

Патч проверен `git apply --check` на чистом `e3c4eb2`: результат совпадает с
деревом, из которого собран релизный бандл побайтово (сравнение blob-хэшей всех
20 файлов).
