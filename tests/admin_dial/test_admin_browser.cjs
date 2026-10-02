/* Mocked transport only: no device or private browser profile. */
const fs = require('fs');
const assert = require('assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const base = require('path').resolve(__dirname, '../..');
function embedded(name) {
 const h = fs.readFileSync(base+'/idf_app/main/admin_page_dial.h','utf8');
 const text = h.split('static const char '+name+'[] =')[1];
 assert(text, 'Admin mobile page must exist');
 return text.split(/;\s*\n/)[0].split('\n').filter(l=>l.trim().startsWith('"')).map(l=>JSON.parse(l.trim())).join('');
}
(async()=>{
 const browser=await chromium.launch({...(process.env.BROWSER_PATH?{executablePath:process.env.BROWSER_PATH}:{}),headless:true});
 const page=await browser.newPage({viewport:{width:390,height:844},isMobile:true});
 let authenticated=false,setup=true,saveCount=0,failSave=false;
 let value={duration_minutes:10,rotation_degrees:0,generation:1,duration_capability_available:true,short_duration_supported:true};
 const calls=[];
 await page.route('**/*',async route=>{
  const request=route.request(),url=new URL(request.url());calls.push(url.pathname);
  assert.equal(url.hostname,'dial.test');
  if(url.pathname==='/admin')return route.fulfill({contentType:'text/html',body:embedded('ADMIN_PAGE')});
  if(url.pathname==='/admin/forms.js')return route.fulfill({contentType:'application/javascript',body:embedded('ADMIN_FORMS_JS')});
  let data={};const op=url.pathname.split('/').pop();
  if(op==='session')data={setup_required:setup,authenticated,...(authenticated?{csrf:'c'.repeat(64)}:{})};
  else if(op==='settings')data=value;
  else if(op==='setup'||op==='recover'||op==='recovery-code'){data={recovery_code:'b'.repeat(32)};setup=false;}
  else if(op==='login')authenticated=true;
  else if(op==='shower'||op==='rotation'){
   assert.equal(request.headers()['x-csrf-token'],'c'.repeat(64));saveCount++;
   const posted=request.postDataJSON();value={...value,...posted,generation:value.generation+1};
   await new Promise(r=>setTimeout(r,180));
   if(failSave)return route.abort('failed');data=value;
  }
  return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
 });
 await page.goto('http://dial.test/admin');
 await page.locator('#setup').waitFor({state:'visible'});
 assert.equal(await page.locator('html').getAttribute('lang'),'sl');
 assert.equal(await page.locator('#setup input[name=pin]').getAttribute('maxlength'),'4');
 assert.equal(await page.locator('#setup input[name=pin]').getAttribute('type'),'password');
 await page.locator('#setup input[name=pin]').fill('0123');await page.locator('#setup input[name=repeated_pin]').fill('0123');await page.locator('#setup button').click();
 await page.locator('#recovery-once').waitFor({state:'visible'});assert.equal(await page.locator('#issued-code').textContent(),'b'.repeat(32));
 assert.equal(await page.evaluate(()=>localStorage.length+sessionStorage.length),0);
 await page.locator('#code-saved').click();assert.equal(await page.locator('#issued-code').textContent(),'');
 await page.locator('#login input[name=pin]').fill('0123');await page.locator('#login button').click();
 await page.locator('#shower').waitFor({state:'visible'});await page.locator('#shower select').selectOption('3');
 await page.locator('#shower button').click();assert.equal(await page.locator('#shower button').isDisabled(),true);
 await page.waitForFunction(()=>!document.querySelector('#shower button').disabled);assert.equal(saveCount,1);assert.match(await page.locator('#confirmed').textContent(),/3 min/);
 failSave=true;await page.locator('#rotation select').selectOption('90');await page.locator('#rotation button').click();
 await page.waitForFunction(()=>!document.querySelector('#rotation button').disabled);assert.match(await page.locator('#message').textContent(),/Odgovor/);assert.match(await page.locator('#confirmed').textContent(),/90°/);
 assert(calls.filter(p=>p==='/admin/api/settings').length>=2);
 await page.reload();await page.locator('#shower').waitFor({state:'visible'});assert.equal(await page.locator('#issued-code').textContent(),'');
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 value={...value,duration_minutes:3,duration_capability_available:false,short_duration_supported:false};
 await page.reload();await page.locator('#shower').waitFor({state:'visible'});
 assert.equal(await page.locator('#shower select').inputValue(),'3');
 assert.equal(await page.locator('#shower option[value="3"]').evaluate(option=>option.disabled),true);
 assert.equal(await page.locator('#shower button').isDisabled(),true);
 assert.match(await page.locator('#duration-support').textContent(),/START/);
 await page.locator('#shower select').selectOption('10');
 assert.equal(await page.locator('#shower button').isDisabled(),false);


 // Exercise the shipped legacy form and script, including the clicked button's
 // value (FormData alone omits it) and the session-expired login path.
 const source=fs.readFileSync(base+'/idf_app/main/config_server.c','utf8');
 const formText=source.split('static const char *HTML_VALVES_CONFIG =')[1].split(';\n')[0];
 const valveForm=[...formText.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');
 const legacy=await browser.newPage({viewport:{width:390,height:844}});
 let legacyPost=null,sessionExpired=false;
 await legacy.route('**/*',async route=>{
  const request=route.request(),path=new URL(request.url()).pathname;
  if(path==='/admin/forms.js')return route.fulfill({contentType:'application/javascript',body:embedded('ADMIN_FORMS_JS')});
  if(path==='/admin/api/session')return route.fulfill({contentType:'application/json',body:JSON.stringify({authenticated:!sessionExpired,csrf:'c'.repeat(64)})});
  if(request.method()==='POST'){
   legacyPost={body:request.postData(),csrf:request.headers()['x-csrf-token']};
   return route.fulfill({contentType:'text/html',body:'<p id="saved">Shranjeno</p>'});
  }
  return route.fulfill({contentType:'text/html',body:valveForm});
 });
 await legacy.goto('http://dial.test/valves-config');await legacy.locator('button[value=clear]').click();
 await legacy.locator('#saved').waitFor();assert.equal(new URLSearchParams(legacyPost.body).get('action'),'clear');assert.equal(legacyPost.csrf,'c'.repeat(64));
 legacyPost=null;sessionExpired=true;await legacy.goto('http://dial.test/valves-config');await legacy.locator('button[value=clear]').click();await legacy.waitForURL('http://dial.test/admin');assert.equal(legacyPost,null);
 await browser.close();console.log('admin browser: PASS (mobile Slovene forms, no duplicate save, lost reply reload, one-time code)');
})().catch(e=>{console.error(e);process.exit(1)});
