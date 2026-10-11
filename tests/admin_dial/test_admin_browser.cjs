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
 let authenticated=false,setup=true,saveCount=0,failSave=false,abortSessionAfterLogout=false;
 let value={duration_minutes:10,rotation_degrees:0,generation:1,duration_capability_available:true,short_duration_supported:true};
 let weather={revision:0,enabled:false,latitude:null,longitude:null,timezone:'Europe/Ljubljana'};
 let observation={operation:'AUTO_SUPPLY',enabled:true,available:true,eligible:true,reason:'eligible',dates:['2026-10-03','2026-10-04','2026-10-05','2026-10-06','2026-10-07'],minima_c:[5,6,7,8,9],last_successful_check:'2026-10-03T10:00:00+00:00'};
 let weatherPuts=0,weatherLostReply=false,weatherOffline=false,weatherConflict=false;
 let favorites={revision:3,core_epoch:7,zone_id:'roon:sauna',zone_name:'Sauna',favorites:[{id:'old',title:'Existing',available:true,reason:null}]};
 let favoritePuts=0,favoriteLost=false,favoriteConflict=false;
 let delayFavoriteOnce=false,releaseFavorite,notifyFavorite;const favoriteQueued=new Promise(resolve=>notifyFavorite=resolve);
 let zone='roon:sauna',zonePuts=0,zoneConflict=false;const zones=[{id:'roon:sauna',name:'Sauna'},{id:'roon:living',name:'Living <room>'}];
 const calls=[];
 await page.route('**/*',async route=>{
  const request=route.request(),url=new URL(request.url());calls.push(url.pathname);
  assert.equal(url.hostname,'dial.test');
  if(url.pathname==='/admin')return route.fulfill({contentType:'text/html',body:embedded('ADMIN_PAGE')});
  if(url.pathname==='/admin/forms.js')return route.fulfill({contentType:'application/javascript',body:embedded('ADMIN_FORMS_JS')});
  let data={};const op=url.pathname.split('/').pop();
  if(op==='session'){if(abortSessionAfterLogout&&!authenticated)return route.abort('failed');data={setup_required:setup,authenticated,...(authenticated?{csrf:'c'.repeat(64)}:{})};}
  else if(op==='settings'){if(!authenticated)return route.fulfill({status:401,contentType:'application/json',body:'{}'});data=value;}
  else if(op==='zones')data={current_zone_id:zone,zones};
  else if(op==='zone'){
   zonePuts++;const posted=request.postDataJSON();assert.equal(request.headers()['x-csrf-token'],'c'.repeat(64));assert.equal(posted.expected_current_zone_id,zone);if(zoneConflict)return route.fulfill({status:409,body:'{}'});zone=posted.zone_id;data={current_zone_id:zone,zones};
  }
  else if(op==='favorites'){
   if(!authenticated)return route.fulfill({status:401,body:'{}'});
   if(request.method()==='PUT'){
    favoritePuts++;assert.equal(request.headers()['x-csrf-token'],'c'.repeat(64));const posted=request.postDataJSON();assert.deepEqual(Object.keys(posted).sort(),['core_epoch','expected_revision','items']);
    if(favoriteConflict)return route.fulfill({status:409,body:'{}'});
    favorites={...favorites,revision:favorites.revision+1,favorites:posted.items.map((item,i)=>({id:item.favorite_id||'saved-'+i,title:item.selection_token==='new-token'?'<img src=x onerror=evil()>':'Existing',available:true,reason:null}))};
    if(favoriteLost)return route.abort('failed');
   }data={...favorites,zone_id:zone,zone_name:zones.find(z=>z.id===zone).name};
   if(request.method()==='GET'&&delayFavoriteOnce){delayFavoriteOnce=false;data={...data,favorites:[{id:'stale',title:'STALE RESPONSE',available:true}]};notifyFavorite();await new Promise(resolve=>releaseFavorite=resolve);}
  }
  else if(op==='playlists')data={core_epoch:7,catalog_revision:11,items:[{selection_token:'new-token',title:'<img src=x onerror=evil()>',subtitle:'same name'}],next_cursor:null};
  else if(op==='weather'){
   if(weatherOffline)return route.abort('failed');
   if(request.method()==='PUT'){
    weatherPuts++;assert.equal(request.headers()['x-csrf-token'],'c'.repeat(64));
    const posted=request.postDataJSON();assert.deepEqual(Object.keys(posted).sort(),['enabled','expected_revision','latitude','longitude','timezone']);
    if(weatherConflict){weather={...weather,revision:weather.revision+1,enabled:false};return route.fulfill({status:409,body:'{}'});}
    assert.equal(posted.expected_revision,weather.revision);
    weather={revision:weather.revision+1,enabled:posted.enabled,latitude:posted.latitude,longitude:posted.longitude,timezone:posted.timezone};
    if(weatherLostReply)return route.abort('failed');
   }data=request.method()==='GET'?{...weather,observation}:weather;
  }
  else if(op==='setup' ||op==='recover'||op==='recovery-code'){data={recovery_code:(op==='recovery-code'?'d':'b').repeat(32)};setup=false;}
  else if(op==='login')authenticated=true;
  else if(op==='logout')authenticated=false;
  else if(op==='shower'||op==='rotation'){
   if(!authenticated)return route.fulfill({status:401,contentType:'application/json',body:'{}'});
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
 await page.locator('#weather').waitFor({state:'visible'});
 assert.match(await page.locator('#weather-operation').textContent(),/vremenska pomoč/);
 assert.match(await page.locator('#weather-minima').textContent(),/2026-10-07.*9/);
 assert.match(await page.locator('#weather-check').textContent(),/2026-10-03T10:00/);
 assert.match(await page.locator('#weather').textContent(),/15 minut/);
 observation={...observation,operation:'MANUAL_IDLE',eligible:false,reason:'below_threshold',minima_c:[4.9,6,7,8,9]};
 await page.locator('#weather-refresh').click();await page.waitForFunction(()=>document.querySelector('#weather-reason').textContent.includes('pod 5'));
 assert.match(await page.locator('#weather-minima').textContent(),/4.9/);
 observation={...observation,operation:'MANUAL_IDLE',available:false,eligible:false,reason:'fetch_failed'};
 await page.locator('#weather-refresh').click();await page.waitForFunction(()=>document.querySelector('#weather-reason').textContent.includes('dosegljiva'));
 assert.match(await page.locator('#weather-minima').textContent(),/zadnja/);
 observation={...observation,operation:'USER_OFF'};await page.locator('#weather-refresh').click();await page.waitForFunction(()=>document.querySelector('#weather-operation').textContent.includes('ročno ustavljen'));
 observation=null;await page.locator('#weather-refresh').click();await page.waitForFunction(()=>document.querySelector('#weather-operation').textContent.includes('ni znano'));
 assert.equal(await page.locator('#weather button').isDisabled(),false);assert.equal(weatherPuts,0);
 await page.locator('#weather summary').click();
 await page.locator('#weather input[name=latitude]').fill('46.05');
 await page.locator('#weather input[name=longitude]').fill('14.5');
 await page.locator('[data-tab=roon]').click();await page.locator('#playlist-browse').click();await page.locator('#playlist-library input').waitFor({state:'visible'});
 await page.locator('#playlist-library input').check();
 assert.equal(await page.locator('#playlist-selected img').count(),0);assert.match(await page.locator('#playlist-selected').textContent(),/<img/);
 await page.locator('#playlist-save').click();await page.waitForFunction(()=>!document.querySelector('#playlist-save').disabled);
 assert.equal(favoritePuts,1);assert.match(await page.locator('#playlist-status').textContent(),/shranjene/);
 favoriteLost=true;await page.locator('#playlist-save').click();await page.waitForFunction(()=>!document.querySelector('#playlist-save').disabled);assert.equal(favoritePuts,2);assert.match(await page.locator('#playlist-status').textContent(),/ni potrjen/);
 favoriteLost=false;favoriteConflict=true;await page.locator('#playlist-save').click();await page.waitForFunction(()=>!playlistBusy);assert.equal(favoritePuts,3);assert.match(await page.locator('#playlist-status').textContent(),/Ponovno preberi/);favoriteConflict=false;
 // Zone choice changes only the target; the same saved favorite survives.
 await page.locator('#roon-zone').selectOption('roon:living');await page.locator('#roon-zone-save').click();await page.waitForFunction(()=>!zoneBusy&&currentZone==='roon:living');
 assert.equal(zonePuts,1);assert.equal(favoritePuts,3);assert.match(await page.locator('#roon-zone-status').textContent(),/Living <room>/);assert.equal(await page.locator('#roon-zone-status img').count(),0);assert.match(await page.locator('#playlist-selected').textContent(),/Existing/);
 zoneConflict=true;await page.locator('#roon-zone').selectOption('roon:sauna');await page.locator('#roon-zone-save').click();await page.waitForFunction(()=>!zoneBusy);assert.equal(await page.locator('#roon-zone').inputValue(),'roon:living');assert.equal(zone,'roon:living');zoneConflict=false;
 // A read begun in Living must not replace Sauna's freshly loaded selection.
 delayFavoriteOnce=true;await page.evaluate(()=>{void reloadPlaylists()});await favoriteQueued;
 await page.locator('#roon-zone').selectOption('roon:sauna');await page.locator('#roon-zone-save').click();await page.waitForFunction(()=>!zoneBusy&&currentZone==='roon:sauna');releaseFavorite();await page.waitForTimeout(100);
 assert.doesNotMatch(await page.locator('#playlist-selected').textContent(),/STALE/);assert.equal(favoritePuts,3);
 // If the old zone disappears, admin still offers live alternatives.
 zones.splice(0,1);await page.locator('#roon-zone-refresh').click();await page.waitForFunction(()=>!zoneBusy&&!currentZoneLive);
 assert.equal(await page.locator('#roon-zone').isEnabled(),true);assert.equal(await page.locator('#playlist-save').isDisabled(),true);assert.equal(await page.locator('#playlist-browse').isDisabled(),true);
 await page.locator('#roon-zone').selectOption('roon:living');await page.locator('#roon-zone-save').click();await page.waitForFunction(()=>!zoneBusy&&currentZoneLive&&currentZone==='roon:living');assert.equal(favoritePuts,3);assert.match(await page.locator('#playlist-selected').textContent(),/Existing/);
 // The same recovery works before the device has any selected zone.
 zone='';await page.locator('#roon-zone-refresh').click();await page.waitForFunction(()=>!zoneBusy&&currentZone==='');assert.equal(await page.locator('#roon-zone').isEnabled(),true);assert.equal(await page.locator('#playlist-save').isDisabled(),true);
 await page.locator('#roon-zone').selectOption('roon:living');await page.locator('#roon-zone-save').click();await page.waitForFunction(()=>!zoneBusy&&currentZoneLive&&currentZone==='roon:living');assert.equal(favoritePuts,3);
 if(process.env.ADMIN_ZONE_SCREENSHOT)await page.screenshot({path:process.env.ADMIN_ZONE_SCREENSHOT,fullPage:true});
 await page.locator('[data-tab=shower]').click();await page.locator('#weather input[name=enabled]').check();
 await page.locator('#weather button').click();
 await page.waitForFunction(()=>!document.querySelector('#weather button').disabled);
 assert.equal(weatherPuts,1);assert.equal(weather.enabled,true);
 assert.match(await page.locator('#weather-confirmed').textContent(),/46.05/);
 weatherLostReply=true;await page.locator('#weather input[name=enabled]').uncheck();
 await page.locator('#weather button').click();
 await page.waitForFunction(()=>!document.querySelector('#weather button').disabled);
 assert.equal(weatherPuts,2);assert.equal(await page.locator('#weather input[name=enabled]').isChecked(),false);
 assert.match(await page.locator('#message').textContent(),/Odgovor/);
 weatherLostReply=false;weatherConflict=true;
 await page.locator('[data-tab=shower]').click();await page.locator('#weather input[name=enabled]').check();await page.locator('#weather button').click();
 await page.waitForFunction(()=>!document.querySelector('#weather button').disabled);
 assert.equal(weatherPuts,3);assert.equal(await page.locator('#weather input[name=enabled]').isChecked(),false);
 weatherConflict=false;weatherOffline=true;await page.reload();
 await page.locator('#shower').waitFor({state:'visible'});
 assert.match(await page.locator('#weather-confirmed').textContent(),/ni potrjeno/);
 assert.equal(await page.locator('#weather button').isDisabled(),true);
 weatherOffline=false;await page.reload();
 await page.locator('#shower').waitFor({state:'visible'});await page.locator('#duration-range').fill('3');
 await page.locator('#shower button').click();assert.equal(await page.locator('#shower button').isDisabled(),true);
 await page.waitForFunction(()=>!document.querySelector('#shower button').disabled);assert.equal(saveCount,1);assert.match(await page.locator('#confirmed').textContent(),/3 min/);
 failSave=true;await page.locator('[data-tab=screen]').click();await page.locator('#rotation select').selectOption('90');await page.locator('#rotation button').click();
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
 await page.locator('#shower select').selectOption('10',{force:true});
 assert.equal(await page.locator('#shower button').isDisabled(),false);
 // Expire the session while settings remain visible, then submit a mutation.
 authenticated=false;await page.locator('#shower button').click();
 await page.locator('#login').waitFor({state:'visible'});
 assert.equal(await page.locator('#settings').isVisible(),false);
 assert.equal(await page.locator('#issued-code').textContent(),'');
 assert.equal(await page.evaluate(()=>csrf),'');
 assert.equal(await page.evaluate(()=>generation),null);
 assert.equal(await page.locator('#pin input[name=pin]').inputValue(),'');
 assert.equal(await page.locator('#shower select').inputValue(),'');
 // Reissue a code and log out before pressing the acknowledgement button.
 await page.locator('#login input[name=pin]').fill('0123');await page.locator('#login button').click();
 await page.locator('#settings').waitFor({state:'visible'});
 await page.locator('[data-tab=security]').click();await page.locator('#recovery-code input[name=pin]').fill('0123');await page.locator('#recovery-code button').click();
 await page.locator('#recovery-once').waitFor({state:'visible'});
 assert.equal(await page.locator('#issued-code').textContent(),'d'.repeat(32));
 abortSessionAfterLogout=true;await page.locator('#logout button').click();await page.locator('#login').waitFor({state:'visible',timeout:3000});
 assert.equal(await page.locator('#settings').isVisible(),false);
 assert.equal(await page.locator('#recovery-once').isVisible(),false);
 assert.equal(await page.locator('#issued-code').textContent(),'');
 assert.equal(await page.evaluate(()=>csrf),'');
 assert.equal(await page.evaluate(()=>generation),null);
 assert.equal(await page.locator('#recovery-code input[name=pin]').inputValue(),'');


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
