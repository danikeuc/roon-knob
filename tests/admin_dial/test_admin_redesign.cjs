const fs=require('fs'),assert=require('assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=process.env.DIAL_SOURCE||require('path').resolve(__dirname,'../..');
const text=fs.readFileSync(base+'/idf_app/main/admin_page_dial.h','utf8').split('static const char ADMIN_PAGE[] =')[1].split(/;\s*\n/)[0];
const html=text.split('\n').filter(l=>l.trim().startsWith('"')).map(l=>JSON.parse(l.trim())).join('');
(async()=>{
const browser=await chromium.launch({headless:true,...(process.env.BROWSER_PATH?{executablePath:process.env.BROWSER_PATH}:{})});
try{
const page=await browser.newPage({viewport:{width:390,height:844}});let writes=0,fail=true,epoch=1,authenticated=true,browseFail=true,browseCount=0,holdCatalog=false,releaseCatalog=null;
const observation={operation:'USER_OFF',enabled:true,available:true,eligible:true,reason:'eligible',dates:['2026-10-05','2026-10-06','2026-10-07','2026-10-08','2026-10-09'],minima_c:[5,6,7,8,9],weather_codes:[0,2,3,61,71],pipe_temperature_c:24.36,last_successful_check:'2026-10-05T08:00:00Z'};
const errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.route('**/*',async route=>{
const req=route.request(),u=new URL(req.url());assert.equal(u.host,'dial.test');let body={};
if(u.pathname==='/admin')return route.fulfill({contentType:'text/html',body:html});
if(u.pathname.endsWith('/session'))body={authenticated,setup_required:false,csrf:'c'.repeat(64)};
else if(!authenticated)return route.fulfill({status:401,body:'{}'});
else if(u.pathname.endsWith('/settings'))body={duration_minutes:10,rotation_degrees:0,generation:1,duration_capability_available:true,short_duration_supported:true};
else if(u.pathname.endsWith('/zones'))body={current_zone_id:'roon:sauna',zones:[{id:'roon:sauna',name:'Sauna'}]};
else if(u.pathname.endsWith('/weather'))body={revision:1,enabled:true,latitude:46,longitude:15,timezone:'Europe/Ljubljana',observation};
else if(u.pathname.endsWith('/favorites')){
 if(req.method()==='PUT'){writes++;assert.equal(req.headers()['x-csrf-token'],'c'.repeat(64));const selected=req.postDataJSON().items;assert(!selected.some(x=>x.favorite_id)&&selected.some(x=>x.selection_token),'new selection has one catalog provenance');if(fail)return route.fulfill({status:409,body:'{}'});}
 body={revision:1,core_epoch:1,zone_id:'roon:sauna',zone_name:'Sauna',favorites:[{id:'old',title:'Existing',available:true,reason:null}]};
}else if(u.pathname.endsWith('/playlists')){browseCount++;if(browseFail)return route.fulfill({status:503,body:'{}'});if(holdCatalog)await new Promise(resolve=>releaseCatalog=resolve);body={core_epoch:epoch,catalog_revision:3,items:Array.from({length:6},(_,i)=>({selection_token:'token-'+(u.search?'b':'a')+i,title:i===0?'<img src=x onerror=evil()>':'Playlist '+i,subtitle:i===0?null:'20 tracks'})),next_cursor:u.search?null:'next'};}
else throw Error('Unexpected endpoint '+u.pathname);
return route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
});
await page.goto('http://dial.test/admin');await page.locator('#settings').waitFor({state:'visible'});
assert.equal(await page.locator('[data-tab]').count(),5,'five navigation tabs');
assert.equal(await page.locator('#forecast .day').count(),5);
assert.match(await page.locator('#weather-operation').textContent(),/ustavljen|USER_OFF/);
assert.equal(await page.locator('#pipe-temperature').textContent(),'24,4°');
if(process.env.SCREENSHOT_DIR){
 fs.mkdirSync(process.env.SCREENSHOT_DIR,{recursive:true});
 await page.screenshot({path:process.env.SCREENSHOT_DIR+'/admin-mobile.png',fullPage:true});
 await page.setViewportSize({width:1280,height:1100});
 await page.screenshot({path:process.env.SCREENSHOT_DIR+'/admin-desktop.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});
}
await page.locator('[data-tab=roon]').click();await page.locator('#playlist-browse').click();await page.waitForFunction(()=>!playlistBusy);assert.match(await page.locator('#playlist-selected').textContent(),/Existing/,'failed browse preserves saved choice');browseFail=false;await page.locator('#playlist-browse').click();await page.waitForFunction(()=>!playlistBusy);const requestsBefore=browseCount;await page.locator('#playlist-browse').click();await page.waitForFunction(()=>!playlistBusy);assert.equal(browseCount,requestsBefore,'double browse does not restart catalog');
await page.locator('#playlist-library input').first().check();
assert.equal(await page.locator('#playlist-selected img').count(),0);
assert.equal(await page.locator('#playlist-selected li').count(),1,'new catalog replaces only the editable draft');
for(let i=1;i<5;i++)await page.locator('#playlist-library input').nth(i).check();
assert.equal(await page.locator('#playlist-library input').nth(5).isDisabled(),true);
const draftBefore=await page.locator('#playlist-selected').textContent();
await page.locator('#playlist-save').click();await page.waitForFunction(()=>!playlistBusy);
assert.equal(writes,1);assert.equal(await page.locator('#playlist-selected').textContent(),draftBefore,'conflict must preserve draft');
assert.match(await page.locator('#playlist-status').textContent(),/ohranjen/);
await page.locator('#playlist-search').fill('Playlist 3');assert.equal(await page.locator('#playlist-library label:not([hidden])').count(),1);
await page.locator('#playlist-more').click();await page.waitForFunction(()=>!playlistBusy);
assert.equal(await page.locator('#playlist-selected').textContent(),draftBefore,'pagination preserves draft');
await page.locator('[data-tab=shower]').click();observation.available=false;observation.eligible=false;observation.reason='fetch_failed';observation.pipe_temperature_c=null;
await page.locator('#weather-refresh').click();await page.waitForFunction(()=>document.querySelector('#forecast').textContent.includes('—'));
assert.equal(await page.locator('#forecast use[href="#weather-sun"]').count(),0,'stale weather not shown as sunny');
assert.equal(await page.locator('#pipe-temperature').textContent(),'—','missing temperature is not zero');
assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
await page.reload();await page.locator('#settings').waitFor({state:'visible'});
await page.locator('[data-tab=roon]').click();holdCatalog=true;await page.locator('#playlist-browse').click();
await page.waitForFunction(()=>playlistBusy);while(!releaseCatalog)await new Promise(r=>setTimeout(r,10));
await page.evaluate(async()=>{authLost();await session()});releaseCatalog();
await page.waitForFunction(()=>!playlistBusy);
assert.match(await page.locator('#playlist-selected').textContent(),/Existing/,'old session browse cannot erase new session favorites');
await page.locator('[data-tab=shower]').click();
authenticated=false;await page.locator('#weather-refresh').click();await page.locator('#login').waitFor({state:'visible'});
assert.equal(await page.evaluate(()=>playlistDraft.length),0,'session loss clears private draft');
assert.equal(await page.evaluate(()=>localStorage.length+sessionStorage.length),0);assert.deepEqual(errors,[]);
console.log('admin redesign: PASS (tabs, truthful forecast, checkbox cap, draft retention, pagination, auth, mobile)');
}finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
