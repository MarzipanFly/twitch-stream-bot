'use strict';
const byId=id=>document.getElementById(id);
const text=(id,v)=>{byId(id).textContent=v==null||v===''?'—':String(v)};
let latest=null, pending=new Set(), toastTimeout=0, previousObsSeq=0, obsCommandPending=false;
function toast(msg) {
 const el=byId('toast'); el.textContent=msg; el.classList.add('show');
 clearTimeout(toastTimeout);toastTimeout=setTimeout(()=>el.classList.remove('show'),4500);
}
function elapsed(v) {
 const x=Math.max(0,Number(v)||0);
 return x>=3600?Math.floor(x/3600)+' ч '+Math.floor(x%3600/60)+' мин':Math.floor(x/60)+' мин';
}
async function refresh() {
 try {
  const response=await fetch('/api/status',{cache:'no-store'});
  if(!response.ok) throw Error('Сервер не отвечает: '+response.status);
  const d=await response.json();latest=d;
  byId('service').textContent='● Подключено';byId('service').classList.add('connected');
  byId('liveTag').textContent=d.live?'● LIVE':'○ OFFLINE';
  byId('liveTag').classList.toggle('connected',Boolean(d.live));
  text('viewers',d.live?d.viewers:0);text('peak',d.peak);text('profiles',d.profiles);text('queue',d.queue);
  text('streamTitle',d.live?(d.title||'Прямой эфир'):'Сейчас трансляция не идёт');
  text('streamSubtitle',d.live?'Twitch · В прямом эфире':'Бот продолжает работать и ждать новый стрим');
  text('channel',d.broadcaster);text('game',d.live?(d.game||'Не указана'):'Офлайн');
  text('chat',d.chat?'Подключён':'Не подключён');text('uptime',elapsed(d.uptime));
  text('track',d.track||'Нет активного трека');
  renderLeaderboard(d.leaderboard||[]);
  text('obs',!d.obs?'Выключен в настройках':d.obs_connected?'OBS подключён':'OBS недоступен');
  text('promoPeriod',d.promo_min+'–'+d.promo_max+' мин');
  renderObs(d);
  text('updated','Обновлено '+new Date().toLocaleTimeString('ru-RU'));
  for(const feature of ['ai','promo','reports']) {
    if(pending.has(feature)) continue;
    byId('toggle-'+feature).setAttribute('aria-checked',String(Boolean(d[feature])));
  }
 } catch(e) {
  byId('service').textContent='● Нет связи';byId('service').classList.remove('connected');
  text('updated','Нет связи с программой');
 }
}
function renderLeaderboard(people) {
 const parent=byId('leaderRows');
 const fragment=document.createDocumentFragment();
 if(!Array.isArray(people)||!people.length) {
   const tr=document.createElement('tr');const td=document.createElement('td');
   td.colSpan=3;td.textContent='Рейтинг пока пуст';tr.append(td);fragment.append(tr);
 } else {
   people.slice(0,5).forEach((person,index)=>{
     const tr=document.createElement('tr');
     const rank=document.createElement('td');rank.textContent=(index+1)+'.';
     const name=document.createElement('td');name.textContent=person.name||'Зритель';
     const balance=document.createElement('td');
     balance.textContent=new Intl.NumberFormat('ru-RU').format(person.balance||0)+' 🍊';
     tr.append(rank,name,balance);fragment.append(tr);
   });
 }
 parent.replaceChildren(fragment);
}
function renderObs(d) {
 const usable=Boolean(d.obs&&d.obs_connected);
 const status=byId('obsConnection');
 status.textContent=!d.obs?'OBS выключен':usable?'● OBS подключён':'○ OBS недоступен';
 status.classList.toggle('connected',usable);
 text('obsCurrentScene',usable?(d.obs_scene||'Сцена не определена'):'—');
 const label=x=>x===1?'Идёт':x===0?'Не идёт':'Неизвестно';
 text('obsStream','Эфир: '+label(d.obs_streaming));
 text('obsRecord','Запись: '+label(d.obs_recording));
 const select=byId('obsSceneSelect');
 const list=Array.isArray(d.obs_scenes)?d.obs_scenes:[];
 const previous=select.options[select.selectedIndex]?.textContent;
 const unchanged=list.length===select.options.length &&
   list.every((name,index)=>select.options[index]?.textContent===name);
 if(!unchanged) {
   select.replaceChildren();
   if(!list.length) {
     const option=new Option('Сцен нет или OBS недоступен','');
     select.add(option);
   } else {
     list.forEach((name,index)=>select.add(new Option(name,String(index))));
   }
 }
 if(list.length) {
   const choice=list.indexOf(previous);
   const current=list.indexOf(d.obs_scene);
   const selected=(choice>=0?choice:current>=0?current:0);
   select.value=String(selected);
 }
 select.disabled=!usable||!list.length||obsCommandPending;
 byId('obsSwitch').disabled=!usable||!list.length||obsCommandPending;
 const record=byId('obsRecordButton');
 record.textContent=d.obs_recording===1?'Остановить запись':'Начать запись';
 record.disabled=!usable||d.obs_recording<0||obsCommandPending;
 if(d.obs_result_seq!==previousObsSeq&&d.obs_result_seq>0) {
    previousObsSeq=d.obs_result_seq;
    byId('obsFeedback').textContent=d.obs_result||'Операция OBS завершена';
    toast(d.obs_result||'Операция OBS завершена');
 }
}
async function obsAction(body) {
 if(obsCommandPending||!latest?.obs_connected) return;
 obsCommandPending=true;
 byId('obsSwitch').disabled=true;byId('obsRecordButton').disabled=true;
 try {
  const response=await fetch('/api/control',{
    method:'POST',headers:{
    'Content-Type':'application/x-www-form-urlencoded','X-Apelsin-Local':'1'},
    body,cache:'no-store'});
  if(response.status!==202)throw Error('Команда не принята ботом ('+response.status+')');
  byId('obsFeedback').textContent='Команда отправлена, проверяем OBS…';
 } catch(error) {
  byId('obsFeedback').textContent='Ошибка: '+error.message;
  toast('Ошибка OBS: '+error.message);
 } finally {
  obsCommandPending=false;
  await refresh();
 }
}
byId('obsSwitch').addEventListener('click',()=>{
  const select=byId('obsSceneSelect');
  if(!select.value)return;
  obsAction(new URLSearchParams({action:'scene',index:select.value}).toString());
});
byId('obsRecordButton').addEventListener('click',()=>{
  if(!latest||!latest.obs_connected)return;
  const recording=latest.obs_recording===1;
  if(recording&&!window.confirm('Остановить запись в OBS? Несохранённый материал может быть потерян при сбое.'))return;
  obsAction(new URLSearchParams({action:'record',enabled:recording?'0':'1'}).toString());
});

async function toggle(feature) {
 if(pending.has(feature))return;
 const button=byId('toggle-'+feature);
 const enabled=button.getAttribute('aria-checked')!=='true';
 button.disabled=true;pending.add(feature);
 try {
  const body=new URLSearchParams({feature,enabled:enabled?'1':'0'});
  const response=await fetch('/api/control',{
    method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Apelsin-Local':'1'},
    body:body.toString(),cache:'no-store'});
  if(response.status!==202)throw Error('Не удалось изменить параметр');
  toast('Команда отправлена боту. Применение может занять несколько секунд.');
  await new Promise(r=>setTimeout(r,1300));
 } catch(e) {toast('Ошибка: '+e.message);}
 finally {pending.delete(feature);button.disabled=false;await refresh();}
}
for(const feature of ['ai','promo','reports'])byId('toggle-'+feature).addEventListener('click',()=>toggle(feature));
refresh();setInterval(refresh,5000);
