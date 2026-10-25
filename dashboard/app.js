'use strict';
const byId=id=>document.getElementById(id);
const text=(id,v)=>{byId(id).textContent=v==null||v===''?'—':String(v)};
let latest=null, pending=new Set(), toastTimeout=0;
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
  text('top',d.top||'Пока нет данных о зрителях');
  text('obs',d.obs?'Функция включена':'Выключен');text('promoPeriod',d.promo_min+'–'+d.promo_max+' мин');
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
