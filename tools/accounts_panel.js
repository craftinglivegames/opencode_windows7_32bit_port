#!/usr/bin/env node
"use strict";
// accounts_panel.js - local "Accounts & limits" panel for the Win7 x32 port.
//
// Runs with the node.exe shipped in the package (no npm deps, node:sqlite).
// It lists the OpenCode Console accounts stored by the port's server
// (%USERPROFILE%\.local\share\opencode\opencode.db, table `credential`,
// integration `opencode`), starts the device-flow login, renames/removes
// accounts and makes one of them active.
//
// How "active" works in opencode 1.19.3: the provider request uses the FIRST
// connection returned for the integration, and connections are ordered by
// `credential.time_created` ascending and then reversed -> the newest
// credential wins. Activating therefore touches time_created of the chosen
// row, and the server is restarted (config/credentials are read at startup).
//
// Usage:
//   node accounts_panel.js [--port 4099] [--server http://127.0.0.1:4096]
//                          [--dir <project dir>] [--launcher <OpenCode.exe>]
//                          [--no-restart]

const http = require("http");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFile, spawn } = require("child_process");

let DatabaseSync = null;
try {
  ({ DatabaseSync } = require("node:sqlite"));
} catch (err) {
  console.error("node:sqlite is required (bundled node.exe has it). " + err.message);
  process.exit(2);
}

// ---------------------------------------------------------------- options ---
function arg(name, fallback) {
  const i = process.argv.indexOf("--" + name);
  if (i === -1 || i + 1 >= process.argv.length) return fallback;
  return process.argv[i + 1];
}
const PANEL_PORT = Number(arg("port", process.env.ACCOUNTS_PORT || 4099));
const SERVER = String(arg("server", "http://127.0.0.1:4096")).replace(/\/+$/, "");
const WORKDIR = path.resolve(arg("dir", process.cwd()));
const NO_RESTART = process.argv.includes("--no-restart");
const LAUNCHER = path.resolve(arg("launcher", path.join(__dirname, "OpenCode.exe")));
const XDG_DATA = process.env.XDG_DATA_HOME || path.join(os.homedir(), ".local", "share");
const DATA_DIR = path.join(XDG_DATA, "opencode");
const INTEGRATION = "opencode";

// The port uses `opencode-<channel>.db` (channel "local" -> opencode-local.db),
// while the desktop/V2 builds use `opencode.db`. Pick the database the running
// server actually writes to: the freshest file (main or WAL) that has the
// `credential` table. --db overrides the guess.
function candidateDbs() {
  const list = [];
  const explicit = arg("db", process.env.OPENCODE_DB || "");
  if (explicit) list.push(path.isAbsolute(explicit) ? explicit : path.join(DATA_DIR, explicit));
  if (!fs.existsSync(DATA_DIR)) return list;
  for (const name of fs.readdirSync(DATA_DIR)) {
    if (!/^opencode.*\.db$/.test(name)) continue;
    list.push(path.join(DATA_DIR, name));
  }
  return list.filter((p, i) => list.indexOf(p) === i && fs.existsSync(p));
}

function dbFreshness(file) {
  const stamps = [file, file + "-wal"].map((p) => {
    try {
      return fs.statSync(p).mtimeMs;
    } catch (err) {
      return 0;
    }
  });
  return Math.max.apply(null, stamps);
}

function dbStats(file) {
  try {
    const db = new DatabaseSync(file, { readOnly: true });
    try {
      if (!db.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='credential'").get()) return null;
      const row = db.prepare("SELECT count(*) c FROM credential WHERE integration_id = ?").get(INTEGRATION);
      return { file, count: row ? Number(row.c) : 0 };
    } finally {
      db.close();
    }
  } catch (err) {
    return null;
  }
}

function channelDb(channel) {
  const ch = String(channel || "").toLowerCase();
  if (!ch) return null;
  const name = ["latest", "beta", "prod"].includes(ch)
    ? "opencode.db"
    : "opencode-" + ch.replace(/[^a-zA-Z0-9._-]/g, "-") + ".db";
  const file = path.join(DATA_DIR, name);
  return fs.existsSync(file) ? file : null;
}

function resolveDb(channel) {
  const explicit = arg("db", process.env.OPENCODE_DB || "");
  if (explicit) return path.isAbsolute(explicit) ? explicit : path.join(DATA_DIR, explicit);
  // Exact match first: the running server reports its installation channel,
  // and database.ts names the file after it.
  const exact = channelDb(channel);
  if (exact && dbStats(exact)) return exact;
  const stats = candidateDbs()
    .map(dbStats)
    .filter(Boolean);
  if (!stats.length) return path.join(DATA_DIR, "opencode.db");
  stats.sort((a, b) => b.count - a.count || dbFreshness(b.file) - dbFreshness(a.file));
  return stats[0].file;
}

async function serverChannel() {
  const r = await httpJson(SERVER + "/global/health");
  if (r.status !== 200 || !r.body) return null;
  return typeof r.body.version === "string" ? r.body.version : null;
}

let DB_PATH = resolveDb();

function log(msg) {
  const line = new Date().toISOString().replace("T", " ").slice(0, 19) + "  " + msg;
  console.log(line);
  try {
    fs.appendFileSync(path.join(__dirname, "logs", "accounts_panel.log"), line + "\r\n");
  } catch (err) {
    /* logs/ may not exist yet */
  }
}

// ------------------------------------------------------------- http helpers ---
function httpJson(url, options) {
  return new Promise((resolve) => {
    let u;
    try {
      u = new URL(url);
    } catch (err) {
      return resolve({ status: 0, body: null, error: String(err) });
    }
    const req = http.request(
      {
        hostname: u.hostname,
        port: u.port,
        path: u.pathname + u.search,
        method: (options && options.method) || "GET",
        headers: Object.assign({ Accept: "application/json" }, (options && options.headers) || {}),
        timeout: 30000,
      },
      (res) => {
        let raw = "";
        res.setEncoding("utf8");
        res.on("data", (c) => (raw += c));
        res.on("end", () => {
          let body = null;
          try {
            body = JSON.parse(raw);
          } catch (err) {
            body = raw;
          }
          resolve({ status: res.statusCode, body });
        });
      },
    );
    req.on("timeout", () => req.destroy(new Error("timeout")));
    req.on("error", (err) => resolve({ status: 0, body: null, error: err.message }));
    if (options && options.body !== undefined) req.write(JSON.stringify(options.body));
    req.end();
  });
}

const LOC = "location[directory]=" + encodeURIComponent(WORKDIR);

function serverUp() {
  return httpJson(SERVER + "/global/health").then((r) => r.status === 200);
}

// ----------------------------------------------------------------- database ---
// The opencode 1.19.3 server keeps exactly ONE credential per integration
// (Credential.create deletes the previous row first), so the panel keeps the
// user's list of accounts itself and writes the selected one into the server
// database on activation.
function openDb() {
  return new DatabaseSync(DB_PATH, { readOnly: false });
}

function readServerCredential() {
  if (!fs.existsSync(DB_PATH)) return null;
  const db = new DatabaseSync(DB_PATH, { readOnly: true });
  try {
    const row = db
      .prepare("SELECT id, label, value, method_id, time_created FROM credential WHERE integration_id = ? ORDER BY time_created DESC")
      .get(INTEGRATION);
    if (!row) return null;
    return row;
  } finally {
    db.close();
  }
}

function writeServerCredential(account) {
  const db = openDb();
  try {
    const now = Date.now();
    db.exec("BEGIN IMMEDIATE");
    try {
      db.prepare("DELETE FROM credential WHERE integration_id = ?").run(INTEGRATION);
      db.prepare(
        "INSERT INTO credential (id, integration_id, label, value, connector_id, method_id, active, time_created, time_updated) " +
          "VALUES (?, ?, ?, ?, NULL, ?, 1, ?, ?)",
      ).run(account.dbid || account.id, INTEGRATION, account.label || "default", account.value, account.method || null, now, now);
      db.exec("COMMIT");
    } catch (err) {
      db.exec("ROLLBACK");
      throw err;
    }
  } finally {
    db.close();
  }
}

function clearServerCredential() {
  const db = openDb();
  try {
    db.prepare("DELETE FROM credential WHERE integration_id = ?").run(INTEGRATION);
  } finally {
    db.close();
  }
}

// --------------------------------------------------------- port account store ---
const STORE_PATH = path.join(DATA_DIR, "port_accounts.json");

function readStore() {
  try {
    const parsed = JSON.parse(fs.readFileSync(STORE_PATH, "utf8"));
    if (parsed && Array.isArray(parsed.accounts)) return parsed;
  } catch (err) {
    /* first run or damaged file */
  }
  return { version: 1, active: null, accounts: [] };
}

function writeStore(store) {
  fs.mkdirSync(path.dirname(STORE_PATH), { recursive: true });
  const tmp = STORE_PATH + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify(store, null, 2), "utf8");
  fs.renameSync(tmp, STORE_PATH);
}

function accountView(account, activeId) {
  const meta = (account.meta || {});
  const value = account.value ? safeJson(account.value) : {};
  return {
    id: account.id,
    label: account.label,
    email: meta.email || (value.metadata && value.metadata.email) || null,
    org: meta.org || (value.metadata && value.metadata.orgName) || null,
    type: account.type || (value.type === "key" ? "key" : "oauth"),
    expires: value.expires || null,
    created: account.created || null,
    active: account.id === activeId,
  };
}

function safeJson(text) {
  try {
    return JSON.parse(text);
  } catch (err) {
    return {};
  }
}

// Import the credential the server already has (migration from earlier builds
// or an account added from the standard web UI) as the first store entry.
function importServerCredential(store) {
  const row = readServerCredential();
  if (!row) return null;
  const existing = store.accounts.find((a) => a.value === row.value);
  if (existing) return existing;
  const value = safeJson(row.value);
  const meta = value.metadata || {};
  const account = {
    id: "acc_" + Date.now().toString(36) + Math.random().toString(36).slice(2, 7),
    dbid: row.id,
    label: row.label || (meta.email ? meta.email.split("@")[0] : "аккаунт"),
    type: value.type === "key" ? "key" : "oauth",
    method: row.method_id || null,
    value: row.value,
    meta: { email: meta.email || null, org: meta.orgName || null, server: meta.server || null },
    created: row.time_created || Date.now(),
  };
  store.accounts.push(account);
  if (!store.active) store.active = account.id;
  return account;
}

function listAccounts() {
  const store = readStore();
  let dirty = false;
  const imported = importServerCredential(store);
  if (imported) dirty = true;
  // Keep the "active" pointer honest if the server row was changed elsewhere
  // (standard web UI login, manual edit).
  const row = readServerCredential();
  if (row) {
    const match = store.accounts.find((a) => a.value === row.value);
    if (match && store.active !== match.id) {
      store.active = match.id;
      dirty = true;
    }
  }
  if (dirty) {
    try {
      writeStore(store);
    } catch (err) {
      log("store write failed: " + err.message);
    }
  }
  return {
    store: store,
    accounts: store.accounts.map((a) => accountView(a, store.active)),
  };
}

function renameAccount(id, label) {
  const store = readStore();
  const account = store.accounts.find((a) => a.id === id);
  if (!account) throw new Error("аккаунт не найден");
  account.label = label;
  if (store.active === id) {
    try {
      writeServerCredential(account);
    } catch (err) {
      log("rename: server row update failed: " + err.message);
    }
  }
  writeStore(store);
}

function removeAccount(id) {
  const store = readStore();
  const before = store.accounts.length;
  store.accounts = store.accounts.filter((a) => a.id !== id);
  if (store.accounts.length === before) throw new Error("аккаунт не найден");
  if (store.active === id) {
    const next = store.accounts[0];
    store.active = next ? next.id : null;
    try {
      if (next) writeServerCredential(next);
      else clearServerCredential();
    } catch (err) {
      log("remove: server row update failed: " + err.message);
    }
  }
  writeStore(store);
}

function activateAccount(id) {
  const store = readStore();
  const account = store.accounts.find((a) => a.id === id);
  if (!account) throw new Error("аккаунт не найден");
  if (store.active === id) return false;
  writeServerCredential(account);
  store.active = id;
  writeStore(store);
  return true;
}

// ------------------------------------------------------------------ restart ---
function restartServer() {
  if (NO_RESTART) return Promise.resolve({ ok: true, detail: "restart disabled (--no-restart)" });
  if (!fs.existsSync(LAUNCHER)) {
    return Promise.resolve({
      ok: false,
      detail: "OpenCode.exe не найден рядом с панелью — перезапустите сервер вручную (OpenCode.exe)",
    });
  }
  return new Promise((resolve) => {
    execFile(LAUNCHER, ["--stop"], { cwd: path.dirname(LAUNCHER), timeout: 60000 }, () => {
      try {
        const child = spawn(LAUNCHER, ["--serve-only"], {
          cwd: path.dirname(LAUNCHER),
          detached: true,
          stdio: "ignore",
        });
        child.unref();
        log("server restart requested");
        resolve({ ok: true, detail: "сервер перезапускается" });
      } catch (err) {
        resolve({ ok: false, detail: "не удалось запустить сервер: " + err.message });
      }
    });
  });
}

// ---------------------------------------------------------------------- API ---
async function loginStart() {
  let r = null;
  for (let attempt = 0; attempt < 3; attempt++) {
    r = await httpJson(SERVER + "/api/integration/" + INTEGRATION + "/connect/oauth?" + LOC, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: { methodID: "device", inputs: {} },
    });
    if (r.status && r.status < 500) break;
    await new Promise((done) => setTimeout(done, 800));
  }
  if (r.status !== 200 || !r.body || !r.body.data) {
    return { ok: false, error: "не удалось начать вход: " + (r.error || r.status) + " " + JSON.stringify(r.body).slice(0, 300) };
  }
  const d = r.body.data;
  log("device login started: " + d.attemptID);
  return { ok: true, attemptID: d.attemptID, url: d.url, instructions: d.instructions, expires: d.time && d.time.expires };
}

async function loginStatus(attemptID) {
  const r = await httpJson(SERVER + "/api/integration/attempt/" + encodeURIComponent(attemptID) + "?" + LOC);
  if (r.status !== 200 || !r.body) return { ok: false, status: "unknown", error: r.error || r.status };
  const status = (r.body.data && r.body.data.status) || "pending";
  if (status === "complete") {
    // The server stored the new credential; move it into the panel's list.
    const store = readStore();
    const account = importServerCredential(store);
    if (account) {
      store.active = account.id;
      writeStore(store);
      log("account imported: " + account.label);
    }
  }
  return { ok: true, status: status, message: r.body.data && r.body.data.message };
}

async function addKey(key, label) {
  let r = null;
  for (let attempt = 0; attempt < 3; attempt++) {
    r = await httpJson(SERVER + "/api/integration/" + INTEGRATION + "/connect/key?" + LOC, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: { key: key, label: label || undefined },
    });
    if (r.status && r.status < 500) break;
    await new Promise((done) => setTimeout(done, 800));
  }
  if (r.status !== 204 && r.status !== 200) {
    return { ok: false, error: "сервер отклонил ключ: " + (r.status || r.error) + " " + JSON.stringify(r.body).slice(0, 300) };
  }
  const store = readStore();
  const account = importServerCredential(store);
  if (account) {
    account.type = "key";
    if (label) account.label = label;
    store.active = account.id;
    writeStore(store);
    log("key account imported: " + account.label);
  }
  return { ok: true };
}

// --------------------------------------------------------------------- page ---
const PAGE = `<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<title>OpenCode — аккаунты и лимиты</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body { margin: 0; font: 14px/1.45 "Segoe UI", Tahoma, sans-serif; background: #f5f6f8; color: #1d1f23; }
  header { padding: 16px 20px; background: #14161a; color: #fff; display: flex; align-items: baseline; gap: 12px; }
  header h1 { margin: 0; font-size: 16px; font-weight: 600; }
  header .st { font-size: 12px; opacity: .75; }
  main { max-width: 860px; margin: 0 auto; padding: 18px 16px 40px; }
  .card { background: #fff; border: 1px solid #dfe3e8; border-radius: 10px; padding: 14px 16px; margin-bottom: 14px; }
  .card h2 { margin: 0 0 10px; font-size: 13px; text-transform: uppercase; letter-spacing: .05em; color: #5b6572; }
  .row { display: flex; gap: 10px; align-items: center; padding: 9px 0; border-top: 1px solid #eef1f4; }
  .row:first-of-type { border-top: 0; }
  .who { flex: 1; min-width: 0; }
  .who b { display: block; font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .who span { display: block; color: #6b7480; font-size: 12px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .badge { font-size: 11px; padding: 2px 7px; border-radius: 999px; background: #e7f5ec; color: #14713a; border: 1px solid #bfe4cc; }
  .badge.key { background: #eef2ff; color: #2c3ea8; border-color: #ccd6fb; }
  button { font: inherit; padding: 5px 11px; border-radius: 7px; border: 1px solid #ccd2da; background: #fff; cursor: pointer; }
  button:hover { background: #f2f4f7; }
  button.primary { background: #1f6feb; border-color: #1f6feb; color: #fff; }
  button.primary:hover { background: #1a5fd0; }
  button.danger { color: #a2191f; border-color: #e6b8bb; }
  button:disabled { opacity: .55; cursor: default; }
  input[type=text] { font: inherit; padding: 5px 9px; border: 1px solid #ccd2da; border-radius: 7px; width: 100%; }
  .hint { color: #6b7480; font-size: 12px; margin: 8px 0 0; }
  .code { font-family: Consolas, monospace; font-size: 22px; letter-spacing: .12em; padding: 8px 12px; background: #14161a; color: #fff; border-radius: 8px; display: inline-block; }
  .status { font-size: 13px; margin-top: 8px; }
  .status.err { color: #a2191f; }
  .status.ok { color: #14713a; }
  a { color: #1f6feb; }
  ol { margin: 6px 0 0 18px; padding: 0; }
  li { margin: 3px 0; }
</style>
</head>
<body>
<header>
  <h1>OpenCode · аккаунты и лимиты</h1>
  <span class="st" id="srv">проверяю сервер…</span>
</header>
<main>
  <section class="card">
    <h2>Аккаунты OpenCode Console</h2>
    <div id="list"><p class="hint">загрузка…</p></div>
    <p class="hint">Активный аккаунт определяет квоту бесплатных моделей. Список читается из базы порта; переключение перезапускает сервер (он читает учётные данные при старте).</p>
  </section>

  <section class="card">
    <h2>Добавить аккаунт (вход через браузер)</h2>
    <button class="primary" id="addBtn">Войти по коду</button>
    <div id="login" class="status"></div>
  </section>

  <section class="card">
    <h2>Добавить сервисный ключ</h2>
    <div class="row" style="border-top:0">
      <input type="text" id="key" placeholder="oc_sk_... (OpenCode API key)">
      <button id="keyBtn">Добавить</button>
    </div>
    <div id="keyStatus" class="status"></div>
    <p class="hint">Ключ даёт отдельную квоту, если это другой аккаунт/сервисный ключ. Хранится в базе порта.</p>
  </section>

  <section class="card">
    <h2>Про лимиты</h2>
    <p class="hint" style="margin-top:0">
      Без входа бесплатные модели работают на общем публичном ключе: квота считается <b>по внешнему IP</b>,
      поэтому все устройства одной сети (и офис за NAT) делят один лимит, а сброс происходит в полночь UTC.
      После входа под своим аккаунтом квота считается уже по аккаунту.
      Один и тот же аккаунт на двух устройствах всё равно делит лимит.
    </p>
    <p class="hint">Последний отказ по лимиту: <span id="lastLimit">нет данных</span></p>
    <p class="hint">База порта: <span id="db">—</span></p>
  </section>
</main>
<script>
const $ = (id) => document.getElementById(id);
let accounts = [];
let pollTimer = null;

function esc(s) {
  return String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}
function fmtDate(ms) {
  if (!ms) return "";
  try { return new Date(ms).toLocaleDateString(); } catch (e) { return ""; }
}
async function api(path, body) {
  const r = await fetch(path, {
    method: body === undefined ? "GET" : "POST",
    headers: { "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  return r.json();
}

function render() {
  const box = $("list");
  if (!accounts.length) {
    box.innerHTML = '<p class="hint">Аккаунтов пока нет — панель работает на публичном ключе (квота по IP).</p>';
    return;
  }
  box.innerHTML = accounts.map((a) => {
    const isKey = a.type === "key";
    const meta = [a.email, a.org, a.expires ? "до " + fmtDate(a.expires) : ""].filter(Boolean).join(" · ");
    return '<div class="row">' +
      '<div class="who"><b>' + esc(a.label || "без имени") + "</b><span>" + esc(meta || (isKey ? "сервисный ключ" : "")) + "</span></div>" +
      (a.active ? '<span class="badge">активный</span>' : '<button data-act="' + esc(a.id) + '">Сделать активным</button>') +
      (isKey ? '<span class="badge key">ключ</span>' : "") +
      '<button data-ren="' + esc(a.id) + '">Имя</button>' +
      '<button class="danger" data-del="' + esc(a.id) + '">Удалить</button>' +
      "</div>";
  }).join("");
}

async function refresh() {
  const st = await api("/api/state");
  $("srv").textContent = st.server ? "сервер работает" : "сервер не отвечает";
  accounts = st.accounts || [];
  render();
    $("lastLimit").textContent = st.lastLimit || "нет данных";
  $("db").textContent = st.db || "";
  return st;
}

document.addEventListener("click", async (ev) => {
  const t = ev.target;
  if (t.dataset.act) {
    t.disabled = true;
    t.textContent = "переключаю…";
    const r = await api("/api/activate", { id: t.dataset.act });
    alert(r.ok ? "Готово: " + r.detail : "Ошибка: " + r.detail);
    await refresh();
  }
  if (t.dataset.ren) {
    const a = accounts.find((x) => x.id === t.dataset.ren);
    const name = prompt("Имя аккаунта", a ? a.label : "");
    if (!name) return;
    const r = await api("/api/rename", { id: t.dataset.ren, label: name });
    if (!r.ok) alert("Ошибка: " + r.error);
    await refresh();
  }
  if (t.dataset.del) {
    const a = accounts.find((x) => x.id === t.dataset.del);
    if (!confirm("Удалить аккаунт «" + (a ? a.label : "") + "»?")) return;
    const r = await api("/api/remove", { id: t.dataset.del });
    if (!r.ok) alert("Ошибка: " + r.error);
    await refresh();
  }
});

$("addBtn").addEventListener("click", async () => {
  $("login").className = "status";
  $("login").textContent = "запрашиваю код…";
  const r = await api("/api/login/start", {});
  if (!r.ok) {
    $("login").className = "status err";
    $("login").textContent = r.error;
    return;
  }
  $("login").className = "status";
  $("login").innerHTML =
    'Откройте <a href="' + esc(r.url) + '" target="_blank" rel="noreferrer">страницу входа</a> и введите код:<br>' +
    '<div class="code" style="margin-top:8px">' + esc((r.instructions || "").replace(/^Enter code:\\s*/i, "")) + "</div>" +
    '<ol><li>Откроется opencode.ai — подтвердите вход под своим аккаунтом.</li><li>Вернитесь сюда: статус обновится сам.</li></ol>' +
    '<div id="poll" class="status">ожидаю подтверждения…</div>';
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(async () => {
    const s = await api("/api/login/status?attempt=" + encodeURIComponent(r.attemptID));
    const el = $("poll");
    if (!el) return;
    if (s.status === "complete") {
      clearInterval(pollTimer);
      el.className = "status ok";
      el.textContent = "Готово: аккаунт добавлен.";
      await refresh();
    } else if (s.status === "failed") {
      clearInterval(pollTimer);
      el.className = "status err";
      el.textContent = "Не удалось: " + (s.message || "отказ авторизации");
    } else if (s.status === "expired") {
      clearInterval(pollTimer);
      el.className = "status err";
      el.textContent = "Код истёк — нажмите «Войти по коду» ещё раз.";
    }
  }, 2000);
});

$("keyBtn").addEventListener("click", async () => {
  const key = $("key").value.trim();
  if (!key) return;
  $("keyStatus").className = "status";
  $("keyStatus").textContent = "проверяю ключ…";
  const r = await api("/api/key", { key });
  $("keyStatus").className = "status " + (r.ok ? "ok" : "err");
  $("keyStatus").textContent = r.ok ? "Ключ добавлен." : r.error;
  if (r.ok) $("key").value = "";
  await refresh();
});

refresh();
setInterval(refresh, 15000);
</script>
</body>
</html>`;

// ------------------------------------------------------------------- routes ---
function send(res, status, body, type) {
  const payload = typeof body === "string" || Buffer.isBuffer(body) ? body : JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": type || "application/json; charset=utf-8",
    "Cache-Control": "no-store",
  });
  res.end(payload);
}

function readBody(req) {
  return new Promise((resolve) => {
    let raw = "";
    req.on("data", (c) => (raw += c));
    req.on("end", () => {
      try {
        resolve(raw ? JSON.parse(raw) : {});
      } catch (err) {
        resolve({});
      }
    });
  });
}

function lastLimitInfo() {
  const candidates = [
    path.join(DATA_DIR, "log", "server.log"),
    path.join(path.dirname(LAUNCHER), "logs", "server.log"),
  ];
  for (const file of candidates) {
    if (!fs.existsSync(file)) continue;
    try {
      const text = fs.readFileSync(file, "utf8");
      const idx = text.lastIndexOf("FreeUsageLimitError");
      if (idx === -1) continue;
      const stamp = text.slice(Math.max(0, idx - 400), idx).match(/\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}/g);
      const tail = text.slice(idx, idx + 800);
      const retry = tail.match(/"retry-after"\\s*:\\s*"?(\d+)/i) || tail.match(/retry-after[^0-9]{0,8}(\d+)/i);
      const minutes = retry ? Math.max(0, Math.round(Number(retry[1]) / 60)) : null;
      return (
        (stamp && stamp.length ? stamp[stamp.length - 1] : "недавно") +
        (minutes === null ? " (время сброса сервер не сообщил)" : " (ждём ~" + minutes + " мин до полуночи UTC)")
      );
    } catch (err) {
      /* ignore */
    }
  }
  return null;
}

// Only the panel's own page may talk to the panel. Requests from another
// origin (an open browser tab on evil.example that POSTs to 127.0.0.1:4099)
// are rejected, so nobody can add, remove or switch accounts behind the
// user's back; the Host check also stops DNS-rebinding tricks.
const PANEL_HOSTS = new Set(["127.0.0.1:" + PANEL_PORT, "localhost:" + PANEL_PORT]);

function isPanelRequest(req) {
  if (!PANEL_HOSTS.has(String(req.headers.host || "").toLowerCase())) return false;
  const origin = req.headers.origin;
  if (origin) {
    try {
      if (!PANEL_HOSTS.has(new URL(String(origin)).host.toLowerCase())) return false;
    } catch (err) {
      return false;
    }
  }
  if (String(req.headers["sec-fetch-site"] || "").toLowerCase() === "cross-site") return false;
  return true;
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://127.0.0.1");
  const p = url.pathname;

  if (!isPanelRequest(req)) {
    log("rejected request from host=" + req.headers.host + " origin=" + (req.headers.origin || "-"));
    return send(res, 403, { ok: false, error: "forbidden" });
  }

  try {
    if (p === "/" || p === "/index.html") {
      return send(res, 200, PAGE, "text/html; charset=utf-8");
    }
    if (p === "/api/state") {
      const up = await serverUp();
      let accounts = [];
      let error = null;
      try {
        DB_PATH = resolveDb(await serverChannel());
        accounts = listAccounts().accounts;
      } catch (err) {
        error = err.message;
      }
      return send(res, 200, { server: up, accounts, error, db: DB_PATH, lastLimit: lastLimitInfo(), workdir: WORKDIR });
    }
    if (p === "/api/login/start" && req.method === "POST") {
      return send(res, 200, await loginStart());
    }
    if (p === "/api/login/status") {
      return send(res, 200, await loginStatus(url.searchParams.get("attempt") || ""));
    }
    if (p === "/api/activate" && req.method === "POST") {
      const body = await readBody(req);
      const id = String(body.id || "");
      try {
        DB_PATH = resolveDb(await serverChannel());
        if (!activateAccount(id)) {
          return send(res, 200, { ok: true, detail: "этот аккаунт уже активен" });
        }
      } catch (err) {
        return send(res, 200, { ok: false, detail: err.message });
      }
      const restart = await restartServer();
      return send(res, 200, { ok: restart.ok, detail: restart.detail });
    }
    if (p === "/api/rename" && req.method === "POST") {
      const body = await readBody(req);
      const id = String(body.id || "");
      const label = String(body.label || "").slice(0, 120);
      if (!label) return send(res, 200, { ok: false, error: "пустое имя" });
      try {
        DB_PATH = resolveDb(await serverChannel());
        renameAccount(id, label);
      } catch (err) {
        return send(res, 200, { ok: false, error: err.message });
      }
      return send(res, 200, { ok: true });
    }
    if (p === "/api/remove" && req.method === "POST") {
      const body = await readBody(req);
      const id = String(body.id || "");
      try {
        DB_PATH = resolveDb(await serverChannel());
        removeAccount(id);
      } catch (err) {
        return send(res, 200, { ok: false, error: err.message });
      }
      return send(res, 200, { ok: true });
    }
    if (p === "/api/key" && req.method === "POST") {
      const body = await readBody(req);
      const key = String(body.key || "").trim();
      if (!key) return send(res, 200, { ok: false, error: "пустой ключ" });
      return send(res, 200, await addKey(key, body.label));
    }
    return send(res, 404, { ok: false, error: "unknown path" });
  } catch (err) {
    log("error " + (err && err.stack ? err.stack : err));
    return send(res, 500, { ok: false, error: String(err && err.message ? err.message : err) });
  }
});

server.listen(PANEL_PORT, "127.0.0.1", () => {
  log("accounts panel on http://127.0.0.1:" + PANEL_PORT + "  server=" + SERVER + "  dir=" + WORKDIR);
  console.log("Accounts panel: http://127.0.0.1:" + PANEL_PORT + "/");
});
