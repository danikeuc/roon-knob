const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../../web/admin_dial.html'), 'utf8');
const code = html.slice(html.indexOf('let lightPollTimer='), html.indexOf('const tabTitles='));
function harness() {
 let now=0, sequence=0, fail=false, held=null;
 const timers=new Map(), nodes=new Map(), events={}, calls=[];
 function node(id) { if(!nodes.has(id)) nodes.set(id,{value:'',textContent:'',hidden:false,dataset:{},replaceChildren(){},append(){}}); return nodes.get(id); }
 const status={capability:'independent_light_v1',revision:7,mode:'AUTO',desired_on:false,gpio_on:false,health:'OK',reason:'off',remaining_seconds:0,manual_remaining_seconds:0,observed_at:'2026-10-16T12:00:00Z'};
 const context=vm.createContext({console,performance:{now:()=>now},setTimeout(fn,ms){const id=++sequence;timers.set(id,{at:now+ms,fn});return id;},clearTimeout:id=>timers.delete(id),$:node,document:{hidden:false,querySelectorAll:()=>[],createElement:()=>node('new'+sequence),addEventListener:(n,f)=>events[n]=f},window:{addEventListener:(n,f)=>events[n]=f},api:async (name,data)=>{calls.push({name,data});if(name==='light/config')return {generation:2,configured:true,url:'http://pi:8082'};if(name==='light/settings')return {revision:2,hold_seconds:300,manual_seconds:1800};if(name==='light/status'){if(held)return await held;if(fail)throw Error('offline');return {...status};}throw Error('unexpected '+name);}});
 vm.runInContext("let csrf='session';"+code,context);
 const run=s=>vm.runInContext(s,context);
 async function advance(ms){const end=now+ms;while(true){let next=[...timers].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];if(!next)break;now=next[1].at;timers.delete(next[0]);await next[1].fn();}now=end;}
 return {run,node,events,calls,advance,context,status,setFail:v=>fail=v,setHeld:v=>held=v};
}
(async()=>{
 const h=harness();await h.run('lightRefresh()');h.node('light-url').value='editing';h.node('light-hold').value='9';
 await h.advance(14000);
 assert.notEqual(h.node('light-state').textContent,'Stanje ni znano','healthy view must refresh beyond the 10-second expiry');
 assert.ok(h.calls.filter(c=>c.name==='light/status').length>=3,'fresh status must be read periodically');
 assert.equal(h.node('light-url').value,'editing');assert.equal(h.node('light-hold').value,'9');
 assert.ok(h.calls.every(c=>c.data===undefined),'background reads must never send actions or settings');
 h.setFail(true);await h.advance(5000);assert.equal(h.node('light-state').textContent,'Stanje ni znano');assert.equal(h.run('lightRevision'),null);
 h.setFail(false);await h.advance(5000);assert.notEqual(h.node('light-state').textContent,'Stanje ni znano');
 h.context.document.hidden=true;h.events.visibilitychange();const count=h.calls.length;await h.advance(20000);assert.equal(h.calls.length,count,'hidden page must not poll');
 h.context.document.hidden=false;h.events.visibilitychange();await Promise.resolve();await Promise.resolve();await Promise.resolve();
 h.run('lightReset();csrf=""');const loggedOut=h.calls.length;await h.advance(20000);assert.equal(h.calls.length,loggedOut,'logout must stop polling');
 const slow=harness();await slow.run('lightRefresh()');let resolve;slow.setHeld(new Promise(r=>resolve=r));const pending=slow.run('lightPoll()');await Promise.resolve();slow.run('lightReset();csrf=""');resolve(slow.status);await pending;assert.equal(slow.run('lightRevision'),null,'late response must not restore logged-out state');
 const busy=harness();await busy.run('lightRefresh()');busy.run('lightBusy=true');const before=busy.calls.length;await busy.advance(5000);assert.equal(busy.calls.length,before,'poll must wait for an active action');busy.run('lightBusy=false');await busy.advance(2000);assert.ok(busy.calls.length>before);
 console.log('production admin light polling: refresh, stale failure, recovery, edit preservation, hidden/logout fences, late response, busy guard, read-only PASS');
})().catch(e=>{console.error(e);process.exit(1)});
