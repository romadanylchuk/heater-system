#pragma once
#include <Arduino.h>

// PROGMEM rescue page (stage 05, D11), served at GET /setup, GET /update and
// at GET / when the LittleFS web image is missing (or a filesystem OTA is
// running). Plain ES5, English only, no external assets, no template
// literals and no '%' (so no template processor can ever touch it).
//
// On load: GET /api/version (public) shows fw/web/mismatch; GET /api/session
// either yields the CSRF token or (401) shows a login form (POST /api/login,
// urlencoded user/pass; 401 -> wrong login, 429 -> wait retryS). Every POST
// and every /ota/* request carries the X-CSRF-Token header.
// Setup section (path /setup, or /api/wifi/status reports apActive): the old
// stage-04 setup page flow -- scan (POST /api/wifi/scan, poll GET /api/wifi/scan),
// pick or type an SSID, password, save (POST /api/wifi), poll the status.
// Update section (path /update or /): firmware/filesystem radio, file and
// admin password: POST /api/ota/grant (pass) -> GET /ota/start?mode=fr|fs ->
// XHR multipart POST /ota/upload with progress.
static const char RESCUE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Controller</title>
<style>body{font-family:sans-serif;max-width:440px;margin:1em auto;padding:0 1em}
input,select,button,progress{display:block;width:26em;max-width:calc(100vw - 2em);box-sizing:border-box;margin:.3em 0;padding:.5em;font-size:1em}
label input{display:inline;width:auto}section{border-top:1px solid #ccc;margin-top:1em}
.m{white-space:pre-wrap;background:#eee;padding:.5em}#v{color:#555}</style></head>
<body><h2 id="h">Controller</h2><div id="v"></div>
<section id="lg" style="display:none"><h3>Login</h3>
<input id="lu" maxlength="32" placeholder="User" autocomplete="username">
<input id="lp" type="password" maxlength="64" placeholder="Password" autocomplete="current-password">
<button id="lb">Log in</button><div id="lm" class="m"></div></section>
<div id="mn" style="display:none">
<section id="su" style="display:none"><h3>Wi-Fi setup</h3>
<button id="sc">Scan networks</button>
<select id="ls"><option value="">(scan results)</option></select>
<input id="ss" maxlength="32" placeholder="SSID">
<input id="pw" type="password" maxlength="64" placeholder="Password (empty or 8-63 chars)">
<button id="sv">Save</button><div id="st" class="m"></div></section>
<section id="up" style="display:none"><h3>Update</h3>
<label><input type="radio" name="t" id="fw" checked> Firmware</label>
<label><input type="radio" name="t" id="fs"> Filesystem</label>
<input id="uf" type="file" accept=".bin">
<input id="uw" type="password" maxlength="64" placeholder="Admin password" autocomplete="current-password">
<button id="ub">Upload</button><progress id="pg" max="100" value="0"></progress>
<div id="um" class="m">Relays are switched OFF during the update. The controller restarts afterwards.</div></section>
</div>
<script>
var $=function(i){return document.getElementById(i)},C='',P=location.pathname,T=0;
function sh(i,b){$(i).style.display=b?'':'none'}
function api(u,m,b){var h={};if(C)h['X-CSRF-Token']=C;return fetch(u,{method:m||'GET',headers:h,body:b,credentials:'same-origin'})}
function js(r){return r.json().catch(function(){return{}})}
function fm(o){var b=new URLSearchParams();for(var k in o)b.append(k,o[k]);return b}
function msg(t){$('st').textContent=t}
function um(t){$('um').textContent=t}
function login(){sh('mn',0);sh('lg',1)}
api('/api/version').then(js).then(function(d){$('v').textContent='Firmware '+(d.fw||'?')+', web '+(d.web||'missing')+(d.mismatch?' - VERSION MISMATCH, update the filesystem':'')}).catch(function(){});
function sess(){api('/api/session').then(function(r){if(r.status==401){login();return}
return js(r).then(function(d){C=d.csrf||'';start()})}).catch(function(){$('v').textContent='Controller not reachable'})}
$('lb').onclick=function(){api('/api/login','POST',fm({user:$('lu').value,pass:$('lp').value})).then(function(r){
return js(r).then(function(d){if(r.ok){C=d.csrf||'';$('lp').value='';$('lm').textContent='';start()}
else $('lm').textContent=r.status==429?'Too many attempts, wait '+d.retryS+' s':'Wrong login or password'})}).catch(function(){$('lm').textContent='Request failed'})};
function show(s){if(s.apSsid){document.title=s.apSsid+' setup';$('h').textContent=document.title}
msg('Mode: '+s.mode+'\nSSID: '+(s.ssid||'-')+'\nConnected: '+s.connected+(s.connected?'\nIP: '+s.ip:''))}
function status(){api('/api/wifi/status').then(function(r){if(r.status==401){login();return}return js(r).then(show)}).catch(function(){})}
function start(){sh('lg',0);sh('mn',1);sh('up',P=='/update'||P=='/');
if(P=='/setup'){sh('su',1);status()}else api('/api/wifi/status').then(js).then(function(s){if(s.apActive){sh('su',1);show(s)}}).catch(function(){})}
function poll(){api('/api/wifi/scan').then(js).then(function(d){
var l=$('ls');l.innerHTML='<option value="">(scan results)</option>';
(d.networks||[]).forEach(function(n){var o=document.createElement('option');o.value=n.ssid;
o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.secure?', secured':'')+')';l.appendChild(o)});
if(d.running)setTimeout(poll,2000)}).catch(function(){setTimeout(poll,2000)})}
$('sc').onclick=function(){api('/api/wifi/scan','POST').then(function(r){if(r.status==401)login();else setTimeout(poll,2000)})};
$('ls').onchange=function(){if(this.value)$('ss').value=this.value};
$('sv').onclick=function(){
var p=$('pw').value;if(!$('ss').value||(p&&(p.length<8||(p.length==64&&!/^[0-9a-fA-F]+$/.test(p))))){msg('Invalid SSID or password (empty or 8-63 chars)');return}
api('/api/wifi','POST',fm({ssid:$('ss').value,pass:p})).then(function(r){
if(r.status==401){login();return}msg(r.ok?'Saved, joining...':'Save failed ('+r.status+')');if(r.ok&&!T)T=setInterval(status,3000)})};
$('ub').onclick=function(){var f=$('uf').files[0],fs=$('fs').checked;if(!f){um('Choose a file');return}
um('Checking password...');
api('/api/ota/grant','POST',fm({pass:$('uw').value})).then(function(r){
if(!r.ok)return js(r).then(function(d){throw r.status==429?'Too many attempts, wait '+d.retryS+' s':r.status==401?'Wrong password or session expired':'Refused ('+r.status+')'});
$('uw').value='';return api('/ota/start?mode='+(fs?'fs':'fr'))}).then(function(r){
if(!r.ok)return r.text().then(function(t){throw 'Start failed: '+t});
var x=new XMLHttpRequest(),d=new FormData();d.append('file',f,f.name);
x.open('POST','/ota/upload');x.setRequestHeader('X-CSRF-Token',C);
x.upload.onprogress=function(e){if(e.lengthComputable)$('pg').value=e.loaded*100/e.total};
x.onload=function(){um(x.status==200?'Update OK. The controller restarts now.':'Update failed: '+x.responseText)};
x.onerror=function(){um('Upload failed (connection lost)')};um('Uploading, relays are OFF...');x.send(d)
}).catch(function(e){um(typeof e=='string'?e:'Request failed')})};
sess();
</script></body></html>
)HTML";

static_assert(sizeof(RESCUE_HTML) <= 6144, "RESCUE_HTML must stay <= 6 KB (D11)");
