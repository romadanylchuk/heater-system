#pragma once
#include <Arduino.h>

// Minimal Wi-Fi setup page served at GET /setup (D18), <= 3 KB, no external
// assets and no template processor (the page contains no '%'). The title
// "<AP SSID> setup" is filled in by the script from GET /api/wifi/status.
// Flow: Scan -> POST /api/wifi/scan, poll GET /api/wifi/scan every 2 s while
// running; pick a network from the list or type it; Save -> POST /api/wifi
// (form ssid, pass), then poll GET /api/wifi/status to show the new IP.
// The password rule (empty, or 8..63 chars / 64 hex) mirrors checkWifiCreds;
// the server re-validates (400).
static const char SETUP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Setup</title>
<style>body{font-family:sans-serif;max-width:420px;margin:1em auto;padding:0 1em}
input,select,button{display:block;width:26em;max-width:calc(100vw - 2em);box-sizing:border-box;margin:.3em 0;padding:.5em;font-size:1em}
#st{white-space:pre-wrap;background:#eee;padding:.5em}</style></head>
<body><h2 id="h">Setup</h2>
<button id="sc">Scan networks</button>
<select id="ls"><option value="">(scan results)</option></select>
<input id="ss" maxlength="32" placeholder="SSID">
<input id="pw" type="password" minlength="8" maxlength="64" placeholder="Password (empty or 8-63 chars)">
<button id="sv">Save</button>
<div id="st"></div>
<script>
var $=function(i){return document.getElementById(i)};
function j(u,o){return fetch(u,o).then(function(r){return r.json()})}
function msg(t){$('st').textContent=t}
function status(){j('/api/wifi/status').then(function(s){
if(s.apSsid){document.title=s.apSsid+' setup';$('h').textContent=document.title}
msg('Mode: '+s.mode+'\nSSID: '+(s.ssid||'-')+'\nConnected: '+s.connected+(s.connected?'\nIP: '+s.ip:''));
}).catch(function(){})}
function poll(){j('/api/wifi/scan').then(function(d){
var l=$('ls');l.innerHTML='<option value="">(scan results)</option>';
d.networks.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;
o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.secure?', secured':'')+')';l.appendChild(o)});
if(d.running)setTimeout(poll,2000)}).catch(function(){setTimeout(poll,2000)})}
$('sc').onclick=function(){fetch('/api/wifi/scan',{method:'POST'}).then(function(){setTimeout(poll,2000)})};
$('ls').onchange=function(){if(this.value)$('ss').value=this.value};
$('sv').onclick=function(){
var p=$('pw').value;if(!$('ss').value||(p&&(p.length<8||(p.length==64&&!/^[0-9a-fA-F]+$/.test(p))))){msg('Invalid SSID or password (empty or 8-63 chars)');return}
var b=new URLSearchParams();b.append('ssid',$('ss').value);b.append('pass',$('pw').value);
fetch('/api/wifi',{method:'POST',body:b}).then(function(r){
msg(r.ok?'Saved, joining...':'Save failed ('+r.status+')');if(r.ok)setInterval(status,3000)})};
status();
</script></body></html>
)HTML";
