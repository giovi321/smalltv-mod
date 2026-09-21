// Exercise the real theme UI functions with a small DOM adapter, no browser dependency.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('src/webui.html','utf8');
const code=html.slice(html.indexOf('function themeRequest('),html.indexOf('function upload(){'));
const nodes={};
for(const id of ['themeSelect','themeUseBtn','themeDetails','themeStatus','mode'])nodes[id]={value:'',textContent:'',disabled:false,children:[],appendChild(child){this.children.push(child)}};
let response,requests=[],toast='';
const context={
 $:id=>nodes[id],C:{},Promise,JSON,Math,FormData:class {},
 document:{createElement:()=>({})},j:async()=>response,
 fetch:async(path,options)=>{requests.push([path,JSON.parse(options.body)]);return {ok:true,json:async()=>({id:'broken'})}},
 toast:message=>toast=message,sv:(id,value)=>nodes[id].value=value,modeChanged:()=>{}
};
vm.createContext(context);vm.runInContext(code,context);
(async()=>{
 response={selected:'broken',freeBytes:65536,themes:[
  {id:'healthy',name:'Clock',version:'1',bytes:100,valid:true},
  {id:'broken',name:'broken',bytes:1234,valid:false,error:'Invalid or truncated package'}]};
 await context.loadThemes();
 assert.equal(nodes.themeSelect.children.length,2);
 assert.match(nodes.themeSelect.children[1].textContent,/invalid package/);
 assert.equal(nodes.themeUseBtn.disabled,true);
 assert.match(nodes.themeDetails.textContent,/Invalid or truncated package/);
 context.selectTheme();assert.equal(requests.length,0);assert.match(toast,/valid installed theme/);
 context.deleteTheme();await new Promise(resolve=>setImmediate(resolve));
 assert.equal(requests[0][0],'/api/themes/delete');assert.equal(requests[0][1].id,'broken');
 nodes.themeSelect.value='healthy';context.themeChoiceChanged();assert.equal(nodes.themeUseBtn.disabled,false);
 response={selected:'',freeBytes:65536,themes:[]};await context.loadThemes();assert.equal(nodes.themeUseBtn.disabled,true);
 console.log('theme web UI tests passed');
})().catch(error=>{console.error(error);process.exitCode=1});
