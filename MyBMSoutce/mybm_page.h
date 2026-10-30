#pragma once

namespace mybm {
inline const char PAGE[] = R"HTML(<!doctype html>
<html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MyBM</title><style>
body{font:16px system-ui;margin:auto;max-width:950px;padding:20px;background:#101823;color:#eef3fa}
section{background:#1b2838;padding:18px;border-radius:12px;margin:15px 0}
nav{display:flex;gap:12px}button{padding:10px 18px;cursor:pointer}label{display:block;margin:14px 0}
input,select{display:block;padding:8px;width:180px}canvas{width:100%;height:230px}
.metrics{display:flex;gap:35px;flex-wrap:wrap}.metric{font-size:28px}
#message{white-space:pre-wrap;color:#ffd17d}small{color:#bac7d8}
</style>
<h1>MyBM</h1><p id="status">Laczenie z modulem...</p>
<nav><button id="view">Widok</button><button id="settings">Ustawienia</button></nav>
<main id="readings"><section class="metrics">
<div>Napiecie<div id="voltage" class="metric">--</div></div>
<div>Temperatura ESP<div id="temperature" class="metric">--</div></div></section>
<section><h2>Napiecie [V]</h2><p id="vcount"></p><canvas id="vchart"></canvas></section>
<section><h2>Temperatura ESP [C]</h2><p id="tcount"></p><canvas id="tchart"></canvas></section>
<p id="history"></p><small>Temperatura dotyczy ukladu ESP32, nie akumulatora. Historia RTC znika po odlaczeniu zasilania lub zwyklym restarcie.</small></main>
<section id="configuration" hidden><h2>Ustawienia</h2>
<form id="form">
<label>ADC factor <input name="factor" type="number" min="0.5" max="1.5" step="0.0001" required>
<output id="factor-voltage" aria-live="polite">Brak pomiaru</output></label>
<label>Docelowy odstep miedzy startami reklam [ms]<input name="period_ms" type="number" min="1000" max="60000" step="1" required></label>
<label>Czas nadawania jednej serii [ms]<input name="burst_ms" type="number" min="50" max="500" step="1" required></label>
<label>Odstep sprawdzania USER [s]<input name="button_interval_s" type="number" min="1" max="3600" step="1" required></label>
<label>Odstep pomiarow ADC i temperatury<select name="measurement_interval_s" required>
<option value="1">1 sekunda</option><option value="2">2 sekundy</option>
<option value="5">5 sekund</option><option value="10">10 sekund</option>
<option value="30">30 sekund</option><option value="60">1 minuta</option>
<option value="120">2 minuty</option></select></label>
<label>Interwal probek historii [s]<input name="history_interval_s" type="number" min="1" max="3600" step="1" required></label>
<label>Usrednianie: liczba probek na punkt<input name="average_count" type="number" min="1" max="60" step="1" required></label>
<label>Czas portalu po uruchomieniu [min]<input name="portal_minutes" type="number" min="5" max="20" step="1" required></label>
<label>Prog podtrzymania portalu WiFi [V]<input name="portal_voltage" type="number" min="0.5" max="30" step="0.001" required></label>
<small>Powyzej progu portal jest aktywny bez deep sleep. Po spadku powrot najwczesniej po 60 s, przy kolejnym poprawnym pomiarze. Czas portalu po resecie i USER pozostaje niezalezny.</small>
<button>Zapisz</button></form>
<p><button id="extend-portal" type="button">Przedluz portal</button>
<button id="stop-portal" type="button">Zakoncz portal</button></p>
<small>Przedluz uruchamia pelny zapisany czas od teraz. Zakoncz wylacza WiFi i przywraca normalny cykl BLE.</small>
<p>Pomiar ADC: dzielnik 6:1 (1 MOhm od plusa, 200 kOhm do masy) x ADC factor. Factor = dotychczasowy factor x napiecie multimetru / wskazanie MyBM.</p>
<p>Reklama niepolaczalna: MyBM:12.456. Brak GATT. Po kazdej serii BLE jest wylaczane, a ESP32-C6 przechodzi w deep sleep do kolejnego zaplanowanego cyklu. Ustawiony odstep dotyczy startow cykli; jesli pelny rozruch i seria zajma wiecej niz ustawiony odstep, rzeczywisty odstep bedzie dluzszy i deep sleep moze byc bardzo krotki. Krotkie okno emisji nie gwarantuje liczby odebranych pakietow. Portal WiFi jest trybem aktywnym i nie oszczedza energii.</p>
<p>Portal uruchamia sie po zwyklym restarcie lub po wykryciu USER na zapisany czas (5-20 minut). Podczas portalu urzadzenie pozostaje aktywne dla WiFi i WWW; mierzy napiecie i temperature wedlug wybranego odstepu bez uruchamiania BLE. Historia stosuje zapisany interwal i usrednianie. Dopiero po zakonczeniu portalu wylacza WiFi i przechodzi w deep sleep. Sprawdzanie USER, pomiary i emisja BLE maja niezalezne terminy; BLE nadaje ostatni poprawny pomiar zachowany w RTC. GPIO9 nie moze bezposrednio wybudzic ESP32-C6 z deep sleep: timer budzi uklad wedlug ustawionego odstepu USER lub wczesniejszego terminu pomiaru albo BLE. Sam odczyt przycisku nie uruchamia radia. Trzymaj USER do wykrycia przy najblizszym sprawdzeniu, a nastepnie pusc w ciagu 3 s.</p></section>
<p id="message" role="status"></p>
<script>
const $=id=>document.getElementById(id);
let loaded=false, busy=false;
let uncalibratedVoltage=null;
let portalStopped=false;
async function portalAction(action){
 const button=$(action==='extend'?'extend-portal':'stop-portal');button.disabled=true;
 try{
  const response=await fetch(`/api/portal/${action}`,{method:'POST'});
  const text=await response.text();if(!response.ok)throw Error(text);
  if(action==='stop'){
   portalStopped=true;
   $('message').textContent='Portal konczy prace. WiFi zostanie wylaczone; powrot do normalnego cyklu BLE.';
   $('extend-portal').disabled=true;
   $('form').querySelector('button').disabled=true;
  }else{
   $('message').textContent='Portal przedluzony o pelny zapisany czas.';await refresh();
  }
 }catch(e){$('message').textContent=`Blad sterowania portalem: ${e.message}`}
 finally{if(!portalStopped)button.disabled=false}
}
$('extend-portal').onclick=()=>portalAction('extend');
$('stop-portal').onclick=()=>portalAction('stop');
function updateFactorVoltage(){
 const input=$('form').elements.namedItem('factor'),factor=input.valueAsNumber;
 $('factor-voltage').textContent=!input.validity.valid||!Number.isFinite(factor)
  ?'Podaj factor od 0.5 do 1.5'
  :uncalibratedVoltage===null?'Brak pomiaru'
  :`Napiecie z tym factorem: ${(uncalibratedVoltage*factor).toFixed(3)} V (podglad ostatniego pomiaru)`;
}
$('form').elements.namedItem('factor').addEventListener('input',updateFactorVoltage);
$('view').onclick=()=>{$('readings').hidden=false;$('configuration').hidden=true};
$('settings').onclick=()=>{$('readings').hidden=true;$('configuration').hidden=false};
function chart(id,points,key){
 const canvas=$(id), width=canvas.clientWidth||800, height=230, ratio=devicePixelRatio||1;
 canvas.width=width*ratio;canvas.height=height*ratio;
 const c=canvas.getContext('2d');c.scale(ratio,ratio);c.clearRect(0,0,width,height);
 if(!points.length){c.fillStyle='#bac7d8';c.fillText('Brak probek',15,30);return}
 let lo=Math.min(...points.map(p=>p[key])),hi=Math.max(...points.map(p=>p[key]));
 const margin=Math.max((hi-lo)*0.1, key==='voltage'?0.01:0.1);lo-=margin;hi+=margin;
 const start=points[0].seconds,end=points[points.length-1].seconds;
 c.strokeStyle='#39495f';c.fillStyle='#bac7d8';c.font='12px system-ui';
 for(let i=0;i<5;i++){const y=20+i*45;c.beginPath();c.moveTo(60,y);c.lineTo(width-12,y);c.stroke();c.fillText((hi-(hi-lo)*i/4).toFixed(2),2,y+4)}
 c.strokeStyle=key==='voltage'?'#65daaa':'#f3be6a';c.beginPath();
 points.forEach((p,i)=>{const x=60+(width-75)*(end===start?0:(p.seconds-start)/(end-start));const y=20+180*(hi-p[key])/(hi-lo);i?c.lineTo(x,y):c.moveTo(x,y)});
 c.stroke();if(points.length===1){c.beginPath();c.arc(60,20+180*(hi-points[0][key])/(hi-lo),3,0,Math.PI*2);c.fill()}
 c.fillText(`${end-start} s historii`,60,225);
}
async function refresh(){
 if(busy||portalStopped)return;busy=true;
 try{
  const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw Error(`HTTP ${response.status}`);
  const data=await response.json();
  uncalibratedVoltage=Number.isFinite(data.uncalibrated_voltage)?data.uncalibrated_voltage:null;
  $('voltage').textContent=data.voltage===null?'brak pomiaru':`${data.voltage.toFixed(3)} V`;
  $('temperature').textContent=data.temperature===null?'brak pomiaru':`${data.temperature.toFixed(1)} C`;
  $('status').textContent=`Portal: pozostalo ${data.portal_remaining_s} s${data.voltage_portal_active?' | podtrzymanie napieciem / opoznienie powrotu':''} | serie BLE: ${data.bursts}`;
  const countText=`Zapisano lacznie: ${data.total_points}/${data.capacity}; zajetosc ringa: ${data.points.length}/${data.capacity}.`;
  $('vcount').textContent=countText;$('tcount').textContent=countText;
  $('history').textContent=`Probka co ${data.settings.history_interval_s} s; srednia z ${data.settings.average_count} probek. Po zapelnieniu najstarsze punkty sa nadpisywane.`;
  if(!loaded){for(const [key,value]of Object.entries(data.settings))$('form').elements.namedItem(key).value=value;loaded=true}
  updateFactorVoltage();
  chart('vchart',data.points,'voltage');chart('tchart',data.points,'temperature');
  if(data.error)$('message').textContent=data.error;
 }catch(e){$('message').textContent=`Brak odpowiedzi: ${e.message}. Portal mogl zakonczyc ustawiony czas pracy.`}
 finally{busy=false}
}
$('form').onsubmit=async event=>{
 event.preventDefault();const button=$('form').querySelector('button');button.disabled=true;
 try{
  const params=new URLSearchParams(new FormData($('form')));
  const response=await fetch(`/api/settings?${params}`,{method:'POST'});
  const text=await response.text();if(!response.ok)throw Error(text);
  $('message').textContent='Ustawienia zapisane w pamieci flash.';loaded=false;await refresh();
 }catch(e){$('message').textContent=`Blad zapisu: ${e.message}`}
 finally{button.disabled=false}
};
refresh();setInterval(refresh,2000);
</script></html>)HTML";
}  // namespace mybm
