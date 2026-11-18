// The "Update from GitHub" card in a build that has the GitHub update off
// (-DFW_NO_GITHUB_UPDATE, a fork's build option). Property: the page never
// offers an update there, says why in plain words, and works as before when
// the build has the update on.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('src/webui.html','utf8');
const start=html.indexOf('// GitHub self-update');
const end=html.indexOf('// settings backup');
assert.ok(start>0&&end>start,'update code not found in src/webui.html');
const code=html.slice(start,end);
const showStart=html.indexOf('function showStatus(');
const showEnd=html.indexOf("var fv=$('footVer')",showStart);
assert.ok(showStart>0&&showEnd>showStart,'showStatus not found in src/webui.html');
// The part of showStatus that handles the version and the update card.
const showCode='function showUpdateCard(s){'+html.slice(html.indexOf("var fw=$('fwVer')",showStart),showEnd)+'}';

const nodes={};
function reset(){
 for(const id of ['chkBtn','ghUpBtn','ghMsg','fwVer'])nodes[id]={disabled:id==='ghUpBtn',textContent:'',innerHTML:''};
 delete context.window._otaShown;
}
let response,requests,confirmed;
const context={
 $:id=>nodes[id],window:{},JSON,Promise,setInterval:()=>1,clearInterval(){},
 confirm:()=>confirmed,
 j:async(url,opt)=>{requests.push([url,opt&&opt.method||'GET']);if(response instanceof Error)throw response;return response}
};
vm.createContext(context);vm.runInContext(code+'\n'+showCode,context);
const settle=()=>new Promise(resolve=>setImmediate(resolve));

(async()=>{
 // 1. The status of a build with the switch off: both buttons go dead and the card says why.
 reset();requests=[];
 context.showUpdateCard({fw:'smalltv-mod',version:'3.7.0-fork',githubUpdate:false});
 assert.equal(nodes.chkBtn.disabled,true);
 assert.equal(nodes.ghUpBtn.disabled,true);
 assert.match(nodes.ghMsg.textContent,/updates are off in this build/i);
 assert.match(nodes.ghMsg.textContent,/manual/i);
 assert.equal(nodes.fwVer.textContent,'smalltv-mod 3.7.0-fork');
 // Even with a leftover "Last update" text the card keeps saying it is off.
 reset();context.showUpdateCard({fw:'smalltv-mod',version:'3.7.0-fork',githubUpdate:false,updateMsg:'download failed'});
 assert.match(nodes.ghMsg.textContent,/updates are off in this build/i);
 // An update button an earlier check had opened is shut again.
 reset();nodes.ghUpBtn.disabled=false;nodes.chkBtn.disabled=false;
 context.showUpdateCard({fw:'smalltv-mod',version:'3.7.0-fork',githubUpdate:false});
 assert.equal(nodes.ghUpBtn.disabled,true);
 assert.equal(nodes.chkBtn.disabled,true);

 // 2. A normal build (the key is absent): nothing changes, the check button stays usable.
 reset();context.showUpdateCard({fw:'smalltv-mod',version:'3.7.0'});
 assert.equal(nodes.chkBtn.disabled,false);
 assert.equal(nodes.ghUpBtn.disabled,true);   // as before: it opens only after a check finds a newer release
 assert.equal(nodes.ghMsg.textContent,'');
 reset();context.showUpdateCard({fw:'smalltv-mod',version:'3.7.0',githubUpdate:true});
 assert.equal(nodes.chkBtn.disabled,false);
 assert.equal(nodes.ghMsg.textContent,'');

 // 3. The check answers "disabled": same result, and the update button stays shut.
 reset();requests=[];
 response={current:'3.7.0',ok:false,latest:'',newer:false,disabled:true,error:'updates are off in this build'};
 context.checkUpdate();await settle();
 assert.deepEqual(requests,[['/api/checkupdate','GET']]);
 assert.equal(nodes.chkBtn.disabled,true);
 assert.equal(nodes.ghUpBtn.disabled,true);
 assert.match(nodes.ghMsg.textContent,/updates are off in this build/i);
 assert.doesNotMatch(nodes.ghMsg.textContent,/Check failed/);

 // 4. An ordinary failed check is still a failed check, and the buttons come back.
 reset();response={ok:false,error:'low heap'};
 context.checkUpdate();await settle();
 assert.equal(nodes.chkBtn.disabled,false);
 assert.equal(nodes.ghMsg.textContent,'Check failed: low heap');
 // And a newer release still opens the update button.
 reset();response={ok:true,newer:true,latest:'v9.9.9',current:'3.7.0'};
 context.checkUpdate();await settle();
 assert.equal(nodes.ghUpBtn.disabled,false);

 // 5. If the update request is refused (403 with the error text), the page shows that text.
 reset();requests=[];confirmed=true;response=new Error('updates are off in this build');
 context.selfUpdate();await settle();
 assert.deepEqual(requests,[['/api/selfupdate','POST']]);
 assert.match(nodes.ghMsg.textContent,/updates are off in this build/i);
 assert.equal(nodes.chkBtn.disabled,true);
 assert.equal(nodes.ghUpBtn.disabled,true);
 // An ordinary failure to start keeps the old text and gives the check button back.
 reset();requests=[];confirmed=true;response=new Error('Request failed');
 context.selfUpdate();await settle();
 assert.match(nodes.ghMsg.textContent,/^Could not start update/);
 assert.equal(nodes.chkBtn.disabled,false);
 console.log('webui update card tests passed');
})().catch(e=>{console.error(e);process.exit(1)});
