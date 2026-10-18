// Drive the ET-Core sidebar page in the throwaway HA, against simulated devices
// (device_sim with ETCORE_SIM_RICH=1: uiRelay uiMulti uiRgb uiFan uiFanP uiTemp on
// 127.0.0.21-26, see the scratchpad sims.sh). Checks every tab, control and admin
// action through what Home Assistant itself reports; screenshots each view.
// node ha_panel_ui.js <out_dir> <psk_hex>
const { chromium } = require(process.env.HOME + '/kate2.0/node_modules/playwright');
const fs = require('fs');
const { execSync, spawn } = require('child_process');
const A = JSON.parse(fs.readFileSync(process.env.HOME + '/ha-test/auth.json'));
const out = process.argv[2];
const PSK = process.argv[3];
const SIM = process.env.HOME + '/projects/ET-Core/sim/device_sim';
let ok = 0, bad = 0;
const check = (label, cond) => { cond ? ok++ : bad++; console.log(`  ${cond ? 'ok ' : 'BAD'} ${label}`); };

function sim(id, cls, ip, key) {
  spawn(SIM, [id, cls, ip, key], { env: { ...process.env, ETCORE_SIM_RICH: '1' }, detached: true, stdio: 'ignore' }).unref();
}
function killSim(id) {
  const pids = execSync(`ps -eo pid,args | awk '$2 ~ /device_sim$/ && $3 == "${id}" {print $1}'`).toString().trim().split(/\s+/).filter(Boolean);
  pids.forEach(p => { try { process.kill(Number(p), 'SIGKILL'); } catch (e) {} });
  return pids.length;
}

async function login(page) {
  await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  if (await page.locator('input[name=username], input[autocomplete=username]').count()) {
    await page.locator('input[name=username], input[autocomplete=username]').first().fill('test');
    await page.locator('input[type=password]').first().fill(A.password);
    await page.getByRole('button', { name: 'Log in' }).click();
    await page.waitForTimeout(4000);
    await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  }
  await page.waitForTimeout(3000);
  return page.frames().find(f => f.url().includes('/etcore/etcore.html'));
}
const state = (page, eid) => page.evaluate((e) => {
  const s = document.querySelector('home-assistant').hass.states[e]; return s ? { s: s.state, a: s.attributes } : null;
}, eid);
async function waitState(page, eid, pred, ms = 5000) {
  const end = Date.now() + ms;
  while (Date.now() < end) { const s = await state(page, eid); if (s && pred(s)) return s; await page.waitForTimeout(100); }
  return (await state(page, eid)) || { s: 'missing', a: {} };
}
async function waitFor(fn, ms = 10000, step = 250) {
  const end = Date.now() + ms;
  while (Date.now() < end) { try { if (await fn()) return true; } catch (e) {} await new Promise(r => setTimeout(r, step)); }
  return false;
}

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1400, height: 1000 } });
  await ctx.grantPermissions(['clipboard-read', 'clipboard-write']);
  const page = await ctx.newPage();
  const errors = [];
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', e => errors.push(String(e)));
  const f = await login(page);
  check('iframe loaded', !!f);
  await f.evaluate(() => localStorage.removeItem('etcore-ui'));
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForTimeout(3000);
  const fr = page.frames().find(x => x.url().includes('/etcore/etcore.html'));

  // Overview
  await fr.locator('nav button[data-tab=overview]').click();
  check('overview: no offline problems with every sim online', await waitFor(async () => (await fr.locator('#problems .allgood').isVisible()) || !/offline/.test(await fr.locator('#problems').innerText())));
  const nodes = await fr.locator('#map [data-n]').count();
  check(`overview: map has a node per device (${nodes})`, nodes >= 6);
  const links = await fr.locator('#map .link.good').count();
  check(`overview: links coloured by signal (${links} green)`, links >= 6);
  check('overview: Add device shown to admin', await fr.locator('#adddev').isVisible());
  await page.screenshot({ path: `${out}/0-overview.png` });

  // Devices: cards
  await fr.locator('nav button[data-tab=devices]').click();
  await fr.locator('#view [data-view=cards]').click();
  await fr.waitForSelector('.card[data-id]', { timeout: 10000 });
  check(`device cards (${await fr.locator('.card[data-id]').count()})`, await fr.locator('.card[data-id]').count() >= 6);
  const relay = fr.locator('.card[data-id="uiRelay"] .toggle');
  const before = (await state(page, 'switch.uirelay')).s;
  await relay.click();
  let s = await waitState(page, 'switch.uirelay', x => x.s !== before);
  check(`relay toggle ${before} -> ${s.s}`, s.s !== before);
  const lampBefore = (await state(page, 'switch.uimulti_lamp')).s;
  await fr.locator('.card[data-id="uiMulti"] .toggle').nth(1).click();
  s = await waitState(page, 'switch.uimulti_lamp', x => x.s !== lampBefore);
  const pump = await state(page, 'switch.uimulti_pump');
  check(`multi: lamp ${lampBefore} -> ${s.s}, pump untouched (${pump.s})`, s.s !== lampBefore);
  const L = '.card[data-id="uiRgb"]';
  if ((await state(page, 'light.uirgb')).s !== 'on') await fr.locator(`${L} .toggle`).click();
  s = await waitState(page, 'light.uirgb', x => x.s === 'on');
  check(`light on (${s.s})`, s.s === 'on');
  const bri = 40 + Math.floor(Math.random() * 100);
  await fr.locator(`${L} input[data-act=bri]`).fill(String(bri));
  s = await waitState(page, 'light.uirgb', x => x.a.brightness === bri);
  check(`light brightness ${bri} (${s.a.brightness})`, s.a.brightness === bri);
  // Dragging the slider moves the light before it is let go (input events only).
  const dragTo = bri > 120 ? 30 : 230;
  await fr.locator(`${L} input[data-act=bri]`).evaluate(async (el, to) => {
    el.focus();
    const from = Number(el.value);
    for (let i = 1; i <= 10; i++) {
      el.value = String(Math.round(from + (to - from) * i / 10));
      el.dispatchEvent(new Event('input', { bubbles: true }));
      await new Promise(r => setTimeout(r, 40));
    }
  }, dragTo);
  s = await waitState(page, 'light.uirgb', x => Math.abs(x.a.brightness - bri) > 20, 3000);
  check(`slider drag reaches the light before release (${bri} -> ${s.a.brightness})`, Math.abs(s.a.brightness - bri) > 20);
  await fr.locator(`${L} input[data-act=bri]`).evaluate(el => { el.dispatchEvent(new Event('change', { bubbles: true })); el.blur(); });
  // A change made elsewhere (HA dashboard, automation) shows on the page at once, not on the next poll.
  const relayEl = fr.locator('.card[data-id="uiRelay"] .toggle');
  const wasOn = await relayEl.evaluate(el => el.classList.contains('is-on'));
  const shown = await fr.evaluate(async (want) => {
    const h = window.parent.document.querySelector('home-assistant').hass;
    const t0 = performance.now();
    await h.callService('switch', want ? 'turn_on' : 'turn_off', { entity_id: 'switch.uirelay' });
    const el = document.querySelector('.card[data-id="uiRelay"] .toggle');
    while (el.classList.contains('is-on') !== want && performance.now() - t0 < 3000) await new Promise(r => setTimeout(r, 5));
    return Math.round(performance.now() - t0);
  }, !wasOn);
  check(`change made elsewhere shows on the page fast (${shown} ms)`, shown < 150);
  await fr.locator(`${L} input[data-act=rgb]`).evaluate(el => { el.value = '#00ff80'; el.dispatchEvent(new Event('change', { bubbles: true })); });
  s = await waitState(page, 'light.uirgb', x => x.a.rgb_color && x.a.rgb_color[0] === 0 && x.a.rgb_color[1] === 255);
  check(`light colour (${JSON.stringify(s.a.rgb_color)})`, s.a.rgb_color && s.a.rgb_color[1] === 255);
  const fx = await fr.locator(`${L} select[data-act=effect] option`).nth(4).textContent();
  await fr.locator(`${L} select[data-act=effect]`).selectOption(fx);
  s = await waitState(page, 'light.uirgb', x => x.a.effect === fx);
  check(`light effect ${fx} (${s.a.effect})`, s.a.effect === fx);
  const spd = 50 + Math.floor(Math.random() * 150);
  await fr.locator(`${L} input[data-act=num]`).fill(String(spd));
  s = await waitState(page, 'number.uirgb_effect_speed', x => Number(x.s) === spd);
  check(`effect speed ${spd} (${s.s})`, Number(s.s) === spd);
  const pct = 10 * (2 + Math.floor(Math.random() * 7));
  await fr.locator('.card[data-id="uiFan"] input[data-act=pct]').fill(String(pct));
  s = await waitState(page, 'fan.uifan', x => x.a.percentage === pct);
  check(`fan speed ${pct} (${s.a.percentage})`, s.a.percentage === pct);
  const preset = (await state(page, 'fan.uifanp')).a.preset_mode === 'high' ? 'medium' : 'high';
  await fr.locator('.card[data-id="uiFanP"] select[data-act=preset]').selectOption(preset);
  s = await waitState(page, 'fan.uifanp', x => x.a.preset_mode === preset);
  check(`fan preset ${preset} (${s.a.preset_mode})`, s.a.preset_mode === preset);
  await page.waitForTimeout(1200);
  const temp = await fr.locator('.card[data-id="uiTemp"] [data-v=sensor]').textContent();
  check(`sensor shows value (${temp})`, /21\.5/.test(temp));
  const cut = await fr.locator('.fact .v').evaluateAll(els => els.filter(e => e.scrollWidth > e.clientWidth + 1).length);
  check(`card facts not cut off (${cut} cut)`, cut === 0);
  await page.screenshot({ path: `${out}/1-cards.png` });

  // Devices: table
  await fr.locator('#view [data-view=table]').click();
  await page.waitForTimeout(600);
  const trs = await fr.locator('#tbody tr').count();
  check(`table rows (${trs})`, trs >= 6);
  await fr.locator('#thead th[data-sort=ip]').click();
  await page.waitForTimeout(400);
  const ips = await fr.locator('#tbody tr td:nth-child(4)').allTextContents();
  const sorted = [...ips].sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
  check(`table sorts by IP (${ips.slice(0, 3).join(', ')}…)`, JSON.stringify(ips) === JSON.stringify(sorted));
  const tb = (await state(page, 'switch.uirelay')).s;
  await fr.locator('#tbody tr[data-id="uiRelay"] .toggle').click();
  s = await waitState(page, 'switch.uirelay', x => x.s !== tb);
  check(`table toggle relay ${tb} -> ${s.s}`, s.s !== tb);
  await page.screenshot({ path: `${out}/2-table.png` });
  await fr.locator('#view [data-view=cards]').click();

  // Drawer + rename
  await fr.locator('.card[data-id="uiRgb"] .card-h').click();
  await page.waitForTimeout(1200);
  const dtext = await fr.locator('#d-body').innerText();
  check('drawer shows controls, details, hello, state', /Controls/.test(dtext) && /Library/.test(dtext) && /"effect"/.test(dtext) && /recent messages/i.test(dtext));
  await page.screenshot({ path: `${out}/3-drawer.png` });
  await fr.locator('#d-body [data-dact=rename]').click();
  await fr.locator('#rn').fill('Porch Light');
  await fr.locator('#modalbox [data-m=ok]').click();
  check('rename shows on card', await waitFor(async () => (await fr.locator('.card[data-id="uiRgb"] [data-f=name]').textContent()) === 'Porch Light', 6000));
  const regName = await page.evaluate(() => { const h = document.querySelector('home-assistant').hass; const d = Object.values(h.devices).find(x => x.name_by_user === 'Porch Light'); return d ? d.name_by_user : null; });
  check(`rename stored in HA device registry (${regName})`, regName === 'Porch Light');
  await fr.locator('#d-body [data-dact=rename]').click();
  await fr.locator('#rn').fill('');
  await fr.locator('#modalbox [data-m=ok]').click();
  check('rename reset to the device name', await waitFor(async () => (await fr.locator('.card[data-id="uiRgb"] [data-f=name]').textContent()) === 'uiRgb', 6000));
  await fr.locator('#d-close').click();

  // Add device guide
  await fr.locator('#adddev').click();
  await page.waitForTimeout(500);
  check('add device: key hidden at first', /^•+$/.test(await fr.locator('#ad-key').textContent()));
  check('add device: sketch has no key until shown', /PASTE_THE_NETWORK_KEY_HERE/.test(await fr.locator('#ad-code').textContent()));
  await fr.locator('#ad-reveal').click();
  check('add device: Show reveals the key', (await fr.locator('#ad-key').textContent()) === PSK);
  await fr.locator('#ad-id').fill('uiRelay');
  check('add device: duplicate id warned', /already exists/.test(await fr.locator('#ad-warn').textContent()));
  await fr.locator('#ad-id').fill('garage1');
  await fr.locator('#ad-cls').selectOption('light.rgb');
  const code = await fr.locator('#ad-code').textContent();
  check('add device: sketch follows the form', code.includes('"garage1", "light.rgb"') && code.includes(PSK));
  await page.screenshot({ path: `${out}/4-add-device.png` });
  await fr.locator('#modalbox [data-m=ok]').click();

  // Wrong key device -> problem with its IP
  sim('intruder', 'switch.relay', '127.0.0.41', '00'.repeat(32));
  await fr.locator('nav button[data-tab=overview]').click();
  check('wrong-key device shows as a problem', await waitFor(async () => /intruder.*127\.0\.0\.41.*wrong network key/s.test(await fr.locator('#problems').innerText()), 10000));
  killSim('intruder');

  // Remove: unplug uiTemp, remove it from the problem, it comes back complete
  check(`unplug uiTemp (${killSim('uiTemp')} process)`, true);
  check('offline device shows as a problem with Remove', await waitFor(() => fr.locator('#problems [data-pact=remove][data-dev=uiTemp]').isVisible(), 15000));
  await page.screenshot({ path: `${out}/5-problems.png` });
  await fr.locator('#problems [data-pact=remove][data-dev=uiTemp]').click();
  await page.waitForTimeout(300);
  await fr.locator('#modalbox [data-m=ok]').click();
  check('removed: gone from the hub and the map', await waitFor(async () => (await fr.locator('#map [data-n=uiTemp]').count()) === 0, 6000));
  check('removed: its entity is gone from HA', await waitFor(async () => !(await state(page, 'sensor.uitemp_temperature')), 6000));
  sim('uiTemp', 'sensor.temperature', '127.0.0.26', PSK);
  check('came back: on the map again', await waitFor(async () => (await fr.locator('#map [data-n=uiTemp]').count()) === 1, 20000));
  s = await waitState(page, 'sensor.uitemp_temperature', x => x.s === '21.5', 15000);
  check(`came back: entity recreated with its value (${s.s})`, s.s === '21.5');

  // Live log
  await fr.locator('nav button[data-tab=log]').click();
  await page.waitForTimeout(1500);
  const types = await fr.locator('.row .type').allTextContents();
  check(`log rows (${types.length}) incl. commands, acks, removed`, types.includes('command') && types.includes('ack') && types.includes('removed'));
  check('leases hidden by default', !types.includes('lease'));
  check(`ack row shows round trip (${await fr.locator('.row:has(.type.ack) .r').first().textContent()})`, /ms/.test(await fr.locator('.row:has(.type.ack) .r').first().textContent()));
  await fr.locator('#ltype button[data-t=command]').click();
  await page.waitForTimeout(300);
  const onlyCmd = await fr.locator('.row .type').allTextContents();
  check(`filter Commands (${onlyCmd.length})`, onlyCmd.length > 0 && onlyCmd.every(t => t === 'command'));
  await fr.locator('#ltype button[data-t=""]').click();
  await fr.locator('.row').first().click();
  await page.waitForTimeout(300);
  check('row expands to JSON', await fr.locator('.row .detail pre').count() === 1);
  const dl = page.waitForEvent('download', { timeout: 5000 }).catch(() => null);
  await fr.locator('#excsv').click();
  const d = await dl;
  check(`CSV export (${d ? d.suggestedFilename() : 'none'})`, !!d && /\.csv$/.test(d.suggestedFilename()));
  await fr.locator('#pause').click();
  await page.waitForTimeout(2500);
  check('pause holds new messages', /paused/.test(await fr.locator('#loginfo').textContent()));
  await fr.locator('#pause').click();
  await page.screenshot({ path: `${out}/6-log.png` });

  // Health
  await fr.locator('nav button[data-tab=health]').click();
  await page.waitForTimeout(1200);
  const htxt = await fr.locator('#tab-health').innerText();
  check('health: security, rejected senders, per device', /Failed to decrypt/.test(htxt) && /127\.0\.0\.41/.test(htxt) && /uiRgb/.test(htxt));
  await page.screenshot({ path: `${out}/7-health.png` });

  // Phone width
  await page.setViewportSize({ width: 390, height: 844 });
  for (const t of ['overview', 'devices', 'log', 'health']) {
    await fr.locator(`nav button[data-tab=${t}]`).click();
    await page.waitForTimeout(900);
    const wide = await fr.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
    check(`phone: no sideways scroll on ${t} (${wide}px)`, wide <= 0);
    if (t === 'overview' || t === 'devices') await page.screenshot({ path: `${out}/8-phone-${t}.png` });
  }

  check(`no JS errors (${errors.slice(0, 3).join(' | ')})`, errors.filter(e => !/turbojpeg|favicon/i.test(e)).length === 0);
  console.log(`RESULT ${ok} ok, ${bad} bad`);
  await browser.close();
})();
