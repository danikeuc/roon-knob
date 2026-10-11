/* Real embedded page; mocked read-only transports, no private device session. */
const fs=require('fs'),assert=require('assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=require('path').resolve(__dirname,'../..');
const part=fs.readFileSync(base+'/idf_app/main/admin_page_dial.h','utf8').split('static const char ADMIN_PAGE[] =')[1].split(/;\s*\n/)[0];
const html=part.split('\n').filter(l=>l.trim().startsWith('"')).map(l=>JSON.parse(l.trim())).join('');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.BROWSER_PATH?{executablePath:process.env.BROWSER_PATH}:{})});
 try{
 const page=await browser.newPage({viewport:{width:390,height:844},timezoneId:'Europe/Ljubljana'});
 const errors=[],requests=[];let state='ok',release=null;
 page.on('pageerror',e=>errors.push(e.message));
 const row={occurred_at:'2026-10-11T10:30:00+00:00',mode:'weather_assisted',operation:'AUTO_SUPPLY',command:'SUPPLY',reason:'eligible'};
 await page.route('**/*',async route=>{
  const req=route.request(),url=new URL(req.url());assert.equal(url.host,'dial.test');assert.equal(req.method(),'GET','history view must never mutate settings');
  if(url.pathname==='/admin')return route.fulfill({contentType:'text/html',body:html});
  let body={};const op=url.pathname.split('/').pop();
  if(op==='session')body={authenticated:true,csrf:'c'.repeat(64)};
  else if(op==='settings')body={duration_minutes:6,rotation_degrees:90,generation:1,duration_capability_available:true,short_duration_supported:true};
  else if(op==='weather')body={revision:1,enabled:false,latitude:46,longitude:15,timezone:'Europe/Ljubljana'};
  else if(op==='zones')body={current_zone_id:'roon:sauna',zones:[{id:'roon:sauna',name:'Sauna'}]};
  else if(op==='favorites')body={revision:1,core_epoch:1,zone_id:'roon:sauna',zone_name:'Sauna',favorites:[]};
  else if(op==='shower-history'){
   const offset=Number(url.searchParams.get('offset'));requests.push(offset);
   if(state==='hold')await new Promise(resolve=>release=resolve);
   if(state==='offline')return route.fulfill({status:503,body:'{}'});
   if(state==='unauthorized')return route.fulfill({status:401,body:'{}'});
   body={offset,has_more:offset===0,items:offset===0?Array.from({length:10},(_,i)=>({...row,operation:i===1?'MANUAL_ACTIVE':row.operation,reason:i===2?'<img src=x onerror=evil()>':row.reason})):[{...row,operation:'USER_OFF',command:'DRAIN'}]};
   if(state==='empty')body={offset,has_more:false,items:[]};
   if(state==='bad')body.items=[{...row,occurred_at:'not-a-time'}];
  }else throw Error('Unexpected endpoint '+url.pathname);
  await route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
 });
 await page.goto('http://dial.test/admin');await page.locator('#settings').waitFor({state:'visible'});
 assert.equal(await page.locator('#shower-history').count(),1,'shower history card exists');
 assert.equal(requests.length,0,'history is loaded only on demand');
 await page.locator('#shower-history summary').click();await page.waitForFunction(()=>document.querySelectorAll('#history-list li').length===10);
 assert.match(await page.locator('#history-list').textContent(),/Samodejno/);assert.match(await page.locator('#history-list').textContent(),/Ročno/);
 assert.match(await page.locator('#history-list time').first().textContent(),/12:30/,'UTC displayed in browser timezone');
 assert.equal(await page.locator('#history-list img').count(),0,'upstream strings are never HTML');
 assert.match(await page.locator('#history-timezone').textContent(),/Europe\/Ljubljana/);
 await page.locator('#history-next').click();await page.waitForFunction(()=>document.querySelectorAll('#history-list li').length===1);
 assert.equal(requests.at(-1),10);assert.equal(await page.locator('#history-next').isDisabled(),true);
 await page.locator('#history-prev').click();await page.waitForFunction(()=>document.querySelectorAll('#history-list li').length===10);
 assert.equal(requests.at(-1),0);
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'mobile no overflow');
 if(process.env.SCREENSHOT_DIR){fs.mkdirSync(process.env.SCREENSHOT_DIR,{recursive:true});await page.locator('#shower-history').screenshot({path:process.env.SCREENSHOT_DIR+'/shower-history-mobile.png'});}
 for(const next of ['bad','offline','empty']){
  state=next;await page.locator('#history-refresh').click();await page.waitForFunction(()=>!historyBusy);
  assert.equal(await page.locator('#history-list li').count(),0);
  assert.match(await page.locator('#history-status').textContent(),next==='empty'?/še ni zapisov/:/ni na voljo/);
 }
 state='hold';await page.locator('#history-refresh').click();await page.waitForFunction(()=>historyBusy);while(!release)await new Promise(r=>setTimeout(r,10));
 await page.evaluate(()=>authLost());state='ok';release();await page.waitForTimeout(150);
 assert.equal(await page.locator('#history-list li').count(),0,'late response cannot repopulate private history after logout');
 await page.evaluate(()=>session());await page.locator('#settings').waitFor({state:'visible'});
 state='unauthorized';await page.locator('#history-refresh').click();await page.locator('#login').waitFor({state:'visible'});
 assert.equal(await page.locator('#history-list li').count(),0);
 assert.deepEqual(errors,[]);console.log('shower history browser: PASS (read-only, lazy, pagination, timezone, validation, auth fences, mobile)');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});