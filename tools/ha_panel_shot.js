// Open the ET-Core sidebar panel in the throwaway HA as a logged-in user; screenshot + report.
const { chromium } = require(process.env.HOME + '/kate2.0/node_modules/playwright');
const fs = require('fs');
(async () => {
  const A = JSON.parse(fs.readFileSync(process.env.HOME + '/ha-test/auth.json'));
  const tok = process.argv[2];
  const out = process.argv[3];
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const fails = [];
  page.on('response', r => { if (r.url().includes('/etcore/')) fails.push(`${r.status()} ${r.url()}`); });
  await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  if (await page.locator('input[name=username], input[autocomplete=username]').count()) {
    await page.locator('input[name=username], input[autocomplete=username]').first().fill('test');
    await page.locator('input[type=password]').first().fill(A.password);
    await page.getByRole('button', { name: 'Log in' }).click();
    await page.waitForTimeout(4000);
    await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  }
  const fin = page.getByRole('button', { name: 'Finish' });
  if (await fin.count()) {
    await fin.click();
    await page.waitForTimeout(3000);
    await page.goto('http://127.0.0.1:8123/et-core', { waitUntil: 'networkidle' });
  }
  await page.waitForTimeout(4000);
  await page.screenshot({ path: out });
  const frame = page.frames().find(f => f.url().includes('/etcore/'));
  console.log('etcore requests:', fails.join(' | ') || 'none');
  console.log('iframe text:', frame ? (await frame.evaluate(() => document.body.innerText)).slice(0, 400).replace(/\n+/g, ' / ') : 'no iframe');
  await browser.close();
})();
