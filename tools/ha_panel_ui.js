// Drive the ET-Core sidebar page in the throwaway HA (sim devices with ETCORE_SIM_RICH=1):
// screenshots of each tab (desktop, phone, dark) and every control type, checked via HA states.
// node ha_panel_ui.js <out_dir>
const { chromium } = require(process.env.HOME + '/kate2.0/node_modules/playwright');
const fs = require('fs');
const A = JSON.parse(fs.readFileSync(process.env.HOME + '/ha-test/auth.json'));
const out = process.argv[2];
let ok = 0, bad = 0;
const check = (label, cond) => { cond ? ok++ : bad++; console.log(`  ${cond ? 'ok ' : 'BAD'} ${label}`); };

async function login(page) {
  await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  if (await page.locator('input[name=username], input[autocomplete=username]').count()) {
    await page.locator('input[name=username], input[autocomplete=username]').first().fill('test');
    await page.locator('input[type=password]').first().fill(A.password);
    await page.getByRole('button', { name: 'Log in' }).click();
    await page.waitForTimeout(4000);
    await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  }
  await page.waitForTimeout(3500);
  return page.frames().find(f => f.url().includes('/etcore/etcore.html'));
}
const state = (page, eid) => page.evaluate((e) => {
  const s = document.querySelector('home-assistant').hass.states[e]; return s ? { s: s.state, a: s.attributes } : null;
}, eid);
async function waitState(page, eid, pred, ms = 5000) {
  const end = Date.now() + ms;
  while (Date.now() < end) { const s = await state(page, eid); if (s && pred(s)) return s; await page.waitForTimeout(100); }
  return state(page, eid);
}

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1400, height: 1000 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', e => errors.push(String(e)));
  const f = await login(page);
  check('iframe loaded', !!f);
  f.page().on && 0;
  await f.waitForSelector('.card', { timeout: 10000 });
  const cards = await f.locator('.card').count();
  check(`device cards shown (${cards})`, cards >= 6);
  await page.screenshot({ path: `${out}/1-devices.png` });

  // Switch toggle
  const relay = f.locator('.card[data-id="uiRelay"] .toggle');
  const before = (await state(page, 'switch.uirelay')).s;
  await relay.click();
  let s = await waitState(page, 'switch.uirelay', x => x.s !== before);
  check(`relay toggle ${before} -> ${s.s}`, s.s !== before);
  await page.waitForTimeout(1300);
  check('relay toggle shows new state', (await relay.getAttribute('class')).includes('is-on') === (s.s === 'on'));

  // Multi switch: second switch only
  await f.locator('.card[data-id="uiMulti"] .toggle').nth(1).click();
  s = await waitState(page, 'switch.uimulti_lamp', x => x.s === 'on');
  const pump = await state(page, 'switch.uimulti_pump');
  check(`multi: lamp on (${s.s}), pump untouched (${pump.s})`, s.s === 'on' && pump.s === 'off');

  // Light: on, brightness, colour, effect
  const L = '.card[data-id="uiRgb"]';
  await f.locator(`${L} .toggle`).click();
  s = await waitState(page, 'light.uirgb', x => x.s === 'on');
  check(`light on (${s.s})`, s.s === 'on');
  await f.locator(`${L} input[data-act=bri]`).fill('64');
  s = await waitState(page, 'light.uirgb', x => x.a.brightness === 64);
  check(`light brightness 64 (${s.a.brightness})`, s.a.brightness === 64);
  await f.locator(`${L} input[data-act=rgb]`).evaluate(el => { el.value = '#00ff80'; el.dispatchEvent(new Event('change', { bubbles: true })); });
  s = await waitState(page, 'light.uirgb', x => x.a.rgb_color && x.a.rgb_color[1] === 255 && x.a.rgb_color[0] === 0);
  check(`light colour (${JSON.stringify(s.a.rgb_color)})`, s.a.rgb_color && s.a.rgb_color[0] === 0 && s.a.rgb_color[1] === 255);
  const fx = await f.locator(`${L} select[data-act=effect] option`).nth(3).textContent();
  await f.locator(`${L} select[data-act=effect]`).selectOption(fx);
  s = await waitState(page, 'light.uirgb', x => x.a.effect === fx);
  check(`light effect ${fx} (${s.a.effect})`, s.a.effect === fx);
  await f.locator(`${L} input[data-act=num]`).fill('200');
  s = await waitState(page, 'number.uirgb_effect_speed', x => Number(x.s) === 200);
  check(`effect speed 200 (${s.s})`, Number(s.s) === 200);

  // Fans
  await f.locator('.card[data-id="uiFan"] input[data-act=pct]').fill('80');
  s = await waitState(page, 'fan.uifan', x => x.a.percentage === 80);
  check(`fan speed 80 (${s.s} ${s.a.percentage})`, s.a.percentage === 80);
  await f.locator('.card[data-id="uiFanP"] select[data-act=preset]').selectOption('high');
  s = await waitState(page, 'fan.uifanp', x => x.a.preset_mode === 'high');
  check(`fan preset high (${s.a.preset_mode})`, s.a.preset_mode === 'high');

  // Sensor value shown
  await page.waitForTimeout(1500);
  const temp = await f.locator('.card[data-id="uiTemp"] [data-v=sensor]').textContent();
  check(`sensor shows value (${temp})`, /21\.5/.test(temp));
  await page.screenshot({ path: `${out}/2-devices-after.png` });

  // Drawer
  await f.locator('.card[data-id="uiRgb"] .card-h').click();
  await page.waitForTimeout(1500);
  const dtext = await f.locator('#d-body').innerText();
  check('drawer shows details + hello + state', /Library/.test(dtext) && /"effect"/.test(dtext) && /Recent messages/i.test(dtext));
  await page.screenshot({ path: `${out}/3-drawer.png` });
  await f.locator('#d-close').click();

  // Live log
  await f.locator('nav button[data-tab=log]').click();
  await page.waitForTimeout(1500);
  const rows = await f.locator('.row').count();
  check(`log rows (${rows})`, rows > 5);
  const types = await f.locator('.row .type').allTextContents();
  check('log has commands and acks', types.includes('command') && types.includes('ack'));
  check('leases hidden by default', !types.includes('lease'));
  const acktxt = await f.locator('.row:has(.type.ack) .r').first().textContent();
  check(`ack row shows round trip (${acktxt})`, /ms/.test(acktxt));
  await f.locator('#ltype button[data-t=command]').click();
  await page.waitForTimeout(300);
  const onlyCmd = await f.locator('.row .type').allTextContents();
  check(`filter Commands (${onlyCmd.length} rows)`, onlyCmd.length > 0 && onlyCmd.every(t => t === 'command'));
  await f.locator('#ltype button[data-t=""]').click();
  await f.locator('.row').first().click();
  await page.waitForTimeout(300);
  check('row expands to JSON', await f.locator('.row .detail pre').count() === 1);
  const dl = page.waitForEvent('download', { timeout: 5000 }).catch(() => null);
  await f.locator('#excsv').click();
  const d = await dl;
  check(`CSV export (${d ? d.suggestedFilename() : 'none'})`, !!d && /\.csv$/.test(d.suggestedFilename()));
  await f.locator('#pause').click();
  await page.waitForTimeout(2500);
  const info = await f.locator('#loginfo').textContent();
  check(`pause holds new messages (${info})`, /paused/.test(info));
  await f.locator('#pause').click();
  await page.screenshot({ path: `${out}/4-log.png` });

  // Health
  await f.locator('nav button[data-tab=health]').click();
  await page.waitForTimeout(1500);
  const htxt = await f.locator('#tab-health').innerText();
  check('health shows security + per-device', /Failed to decrypt/.test(htxt) && /uiRgb/.test(htxt));
  await page.screenshot({ path: `${out}/5-health.png` });

  // Phone width
  await page.setViewportSize({ width: 390, height: 844 });
  await f.locator('nav button[data-tab=devices]').click();
  await page.waitForTimeout(1500);
  const wide = await f.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  check(`no sideways scroll on phone (${wide}px)`, wide <= 0);
  await page.screenshot({ path: `${out}/6-phone.png` });
  await f.locator('nav button[data-tab=log]').click();
  await page.waitForTimeout(1200);
  const wide2 = await f.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  check(`no sideways scroll on phone log (${wide2}px)`, wide2 <= 0);
  await page.screenshot({ path: `${out}/7-phone-log.png` });
  await page.setViewportSize({ width: 1400, height: 1000 });
  await f.locator('nav button[data-tab=devices]').click();
  await page.waitForTimeout(1200);
  const cut = await f.locator('.fact .v').evaluateAll(els => els.filter(e => e.scrollWidth > e.clientWidth).length);
  check(`card facts not cut off (${cut} cut)`, cut === 0);

  check(`no JS errors (${errors.slice(0, 3).join(' | ')})`, errors.filter(e => !/turbojpeg|favicon/i.test(e)).length === 0);
  console.log(`RESULT ${ok} ok, ${bad} bad`);
  await browser.close();
})();
