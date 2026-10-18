/* Production forms and script; isolated browser and mocked HTTP, no devices. */
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const base = path.resolve(__dirname, '../..');
function literal(file, marker) {
  const source = fs.readFileSync(path.join(base, file), 'utf8').split(marker)[1];
  assert(source, marker);
  return [...source.split(/;\s*\r?\n/)[0].matchAll(/"(?:\\.|[^"\\])*"/g)]
    .map(m => JSON.parse(m[0])).join('');
}
const script = literal('idf_app/main/admin_page_dial.h', 'static const char ADMIN_FORMS_JS[] =');
const valve = literal('idf_app/main/config_server.c', 'static const char *HTML_VALVES_CONFIG =');
const config = literal('idf_app/main/config_server.c', 'static const char *HTML_CONFIG =');
(async () => {
  const browser = await chromium.launch({headless:true, ...(process.env.BROWSER_PATH ? {executablePath:process.env.BROWSER_PATH} : {})});
  try {
    for (const kind of ['bridge-save', 'bridge-clear', 'valve-save', 'valve-clear', 'expired-session', 'foreign-action']) {
      const page = await browser.newPage();
      page.setDefaultTimeout(2500);
      const isValve = kind.startsWith('valve');
      const expected = isValve ? '/valves-config' : '/config';
      const posts = [];
      await page.route('**/*', async route => {
        const request = route.request(), url = new URL(request.url());
        assert.equal(url.hostname, 'dial.test');
        if (url.pathname === '/admin/forms.js') return route.fulfill({contentType:'application/javascript', body:script});
        if (url.pathname === '/admin/api/session') return route.fulfill({json:{authenticated:kind !== 'expired-session', csrf:'test-csrf'}});
        if (request.method() === 'POST') {
          posts.push({path:url.pathname, body:new URLSearchParams(request.postData()), csrf:request.headers()['x-csrf-token']});
          return route.fulfill({status:url.pathname === expected ? 200 : 404, contentType:'text/html', body:'<p id="result">response</p>'});
        }
        return route.fulfill({contentType:'text/html', body:isValve ? valve : config});
      });
      await page.goto('http://dial.test' + (isValve ? '/valves-config' : '/'));
      const form = page.locator(isValve ? 'form:has([name=pi_url])' : 'form:has([name=bridge])');
      if (isValve && kind.endsWith('save')) {
        await form.locator('[name=pi_url]').fill('http://pi.test:8081');
        await form.locator('[name=pi_token]').fill('dummy-display-token');
        await form.locator('[name=weather_token]').fill('dummy-weather-token');
      } else if (!isValve) {
        await form.locator('[name=bridge]').fill('http://bridge.test:8088');
      }
      if (kind === 'foreign-action') await form.evaluate(f => f.setAttribute('action','http://foreign.test/config'));
      const submit = kind.endsWith('clear') ? form.locator('[value="'+(isValve ? 'clear' : 'Clear')+'"]') : (isValve ? form.locator('[value=save]') : form.locator('[value=Save]'));
      await submit.click();
      if (kind === 'expired-session') {
        await page.waitForURL('http://dial.test/admin');
        assert.equal(posts.length,0);
      } else if (kind === 'foreign-action') {
        await page.waitForTimeout(100);
        assert.equal(posts.length,0);
      } else {
        await page.waitForFunction(() => document.querySelector('#result') || document.querySelector('#admin-form-error'));
        assert.equal(posts.length,1,kind);
        assert.equal(posts[0].path,expected,kind+' must post to the form action, not its named controls');
        assert.equal(posts[0].csrf,'test-csrf');
        if (isValve) {
          assert.deepEqual(posts[0].body.getAll('action'),[kind.endsWith('clear')?'clear':'save']);
          if (kind.endsWith('save')) {
            assert.equal(posts[0].body.get('pi_token'),'dummy-display-token');
            assert.equal(posts[0].body.get('weather_token'),'dummy-weather-token');
          }
        } else if (kind.endsWith('save')) assert.equal(posts[0].body.get('bridge'),'http://bridge.test:8088');
        else assert.equal(posts[0].body.get('action'),'Clear');
      }
      await page.close();
      console.log(kind+': PASS');
    }
  } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode=1;});
