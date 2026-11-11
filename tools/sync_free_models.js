// sync_free_models.js — refresh free-model entries in opencode.json from models.dev.
// Runs on Windows 7 with the bundled Node: no npm deps, plain https.
//
//   C:\OpenCode\node\node.exe C:\OpenCode\sync_free_models.js [provider ...]
//
// What it does:
//   1. Downloads https://models.opencode.ai/api.json (60 s timeout).
//      On failure falls back to the on-disk cache and warns.
//   2. Refreshes %USERPROFILE%\.cache\opencode\models.json with the download.
//   3. For every target provider (args, or all config providers that exist in
//      the catalog) adds missing LIVE free models
//      (status !== "deprecated" and "free" in id) to opencode.json.
//      Existing entries are never overwritten, nothing is deleted.
//   4. Backs opencode.json up to opencode.json.bak.<timestamp> first.
//   5. Prints a summary and reminds you to RESTART the OpenCode server
//      (the server reads the provider list once at startup).
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const https = require('https');

const CATALOG_URL = 'https://models.opencode.ai/api.json';
const HOME = os.homedir();
const CACHE_FILE = path.join(HOME, '.cache', 'opencode', 'models.json');
const CONFIG_FILE = path.join(HOME, '.config', 'opencode', 'opencode.json');

function readJson(file) {
  // strip BOM: Windows Notepad saves UTF-8 with BOM, JSON.parse chokes on it
  return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}

function fetchJson(url, timeoutMs) {
  return new Promise((resolve, reject) => {
    const req = https.get(url, { timeout: timeoutMs, headers: { 'User-Agent': 'opencode-win7-sync/1.0' } }, (res) => {
      if (res.statusCode !== 200) {
        res.resume();
        reject(new Error('HTTP ' + res.statusCode + ' for ' + url));
        return;
      }
      let text = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { text += c; });
      res.on('end', () => {
        try { resolve(JSON.parse(text)); }
        catch (e) { reject(new Error('bad JSON: ' + e.message)); }
      });
    });
    req.on('timeout', () => { req.destroy(new Error('timeout after ' + timeoutMs + 'ms')); });
    req.on('error', reject);
  });
}

function toConfigModel(entry) {
  const limit = entry.limit || {};
  const cost = entry.cost || {};
  const model = {
    name: entry.name || entry.id,
    attachment: !!entry.attachment,
    reasoning: !!entry.reasoning,
    reasoning_options: [],
    tool_call: entry.tool_call !== undefined ? !!entry.tool_call : true,
    cost: { input: cost.input || 0, output: cost.output || 0 },
  };
  // Do not write limit:{context:0,output:0}: the web UI treats an explicit
  // zero as "free limit exhausted". Omit the block when the catalog has no
  // usable values so the server falls back to its own defaults.
  const ctx = Number(limit.context) || 0;
  const out = Number(limit.output) || 0;
  if (ctx > 0 || out > 0) {
    model.limit = { context: ctx, output: out };
  }
  return model;
}

async function main() {
  const onlyProviders = process.argv.slice(2);
  let catalog = null;
  try {
    console.log('fetching ' + CATALOG_URL + ' ...');
    catalog = await fetchJson(CATALOG_URL, 60000);
    fs.mkdirSync(path.dirname(CACHE_FILE), { recursive: true });
    fs.writeFileSync(CACHE_FILE, JSON.stringify(catalog), 'utf8');
    console.log('cache refreshed: ' + CACHE_FILE);
  } catch (err) {
    console.log('WARN: download failed (' + err.message + '), trying on-disk cache...');
    try {
      catalog = readJson(CACHE_FILE);
      console.log('using cached catalog: ' + CACHE_FILE);
    } catch (e2) {
      console.error('ERROR: no catalog available: ' + e2.message);
      process.exitCode = 2;
      return;
    }
  }

  let cfg;
  try {
    cfg = readJson(CONFIG_FILE);
  } catch (e) {
    console.error('ERROR: cannot read ' + CONFIG_FILE + ': ' + e.message);
    process.exitCode = 2;
    return;
  }
  const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
  const bak = CONFIG_FILE + '.bak.' + stamp;
  fs.copyFileSync(CONFIG_FILE, bak);
  console.log('backup: ' + bak);

  cfg.provider = cfg.provider || {};
  const targets = Object.keys(cfg.provider).filter((id) => {
    if (onlyProviders.length && !onlyProviders.includes(id)) return false;
    if (!catalog[id]) { console.log('skip ' + id + ': not in catalog'); return false; }
    return true;
  });

  let totalAdded = 0;
  for (const id of targets) {
    const catModels = (catalog[id] && catalog[id].models) || {};
    const liveFree = Object.keys(catModels)
      .filter((mid) => /free/i.test(mid) && catModels[mid].status !== 'deprecated')
      .sort();
    const cfgModels = cfg.provider[id].models = cfg.provider[id].models || {};
    let added = 0;
    for (const mid of liveFree) {
      if (!cfgModels[mid]) { cfgModels[mid] = toConfigModel(catModels[mid]); added++; }
    }
    const stale = Object.keys(cfgModels).filter((mid) => {
      const c = catModels[mid];
      return c && c.status === 'deprecated';
    });
    console.log(id + ': +' + added + ' free models (live in catalog: ' + liveFree.length + ')'
      + (stale.length ? ' | DEPRECATED in catalog (left untouched): ' + stale.join(', ') : ''));
    totalAdded += added;
  }

  fs.writeFileSync(CONFIG_FILE, JSON.stringify(cfg, null, 2) + '\n', 'utf8');
  console.log('updated ' + CONFIG_FILE + ' (+' + totalAdded + ' models)');
  console.log('IMPORTANT: restart the OpenCode server (run OpenCode.cmd again) so it picks up the new list.');
}

main().catch((e) => { console.error('ERROR: ' + (e && e.message || e)); process.exitCode = 1; });
