'use strict';
const get=id=>document.getElementById(id);
let data=null,seq=0;
const cfg=[
 ['promo_min','Мин. интервал рекламы, мин',1,60],
 ['promo_max','Макс. интервал рекламы, мин',1,60],
 ['ai_cooldown','ИИ — кулдаун, сек',5,300],
 ['normal_cooldown','Обычные команды, сек',0,300],
 ['game_cooldown','Игры в чате, сек',0,300],
 ['bruh_cooldown','Команда !bruh, сек',0,300]];
function notify(message) {
 const el=get('message');el.textContent=message;el.classList.add('show');
 setTimeout(()=>el.classList.remove('show'),5000);
}
function field(key,title,min,max,value) {
 const wrap=document.createElement('div');wrap.className='field';
 const inner=document.createElement('div'),label=document.createElement('label');
 label.htmlFor=key;label.textContent=title;
 const input=document.createElement('input');input.id=key;input.type='number';
 input.min=String(min);input.max=String(max);input.step='1';input.value=String(value??0);
 const button=document.createElement('button');
 button.dataset.key=key;button.textContent='Сохранить';
 inner.append(label,input);wrap.append(inner,button);return wrap;
}
async function load() {
 const response=await fetch('/api/status',{cache:'no-store'});
 if(!response.ok)throw Error('Бот недоступен');
 data=await response.json();seq=data.command_seq||0;
 for(const key of ['telegram_url','discord_url','telegram_chat_id'])
   get(key).value=data[key]||'';
 get('numericFields').replaceChildren(...cfg.map(([id,title,min,max])=>
   field(id,title,min,max,data[id])));
 const parent=get('soundFields');parent.replaceChildren();
 (data.sounds||[]).forEach((sound,i)=>{
   const section=document.createElement('div');section.className='sound';
   const title=document.createElement('strong');title.textContent='!'+sound.command+' — '+sound.input;
   const grid=document.createElement('div');grid.className='rows';
   const filler=document.createElement('div');filler.textContent='Цена и кулдаун';
   filler.className='muted';
   grid.append(filler,
     field('sound_cost_'+i,'Цена, 🍊',0,1000000000,sound.cost),
     field('sound_cooldown_'+i,'Кулдаун, сек',0,3600,sound.cooldown));
   section.append(title,grid);parent.append(section);
 });
}
async function save(key,button) {
 const input=get(key);
 if(!input || !input.value.trim()){notify('Введите значение');return;}
 if(input.type==='number' && (!input.checkValidity()||
    !Number.isInteger(Number(input.value)))){notify('Проверь число и пределы');return;}
 button.disabled=true;
 try {
  const body=new URLSearchParams({action:'setting',key,value:input.value});
  const r=await fetch('/api/control',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded','X-Apelsin-Local':'1'},
    body:body.toString()});
  if(r.status!==202)throw Error('Параметр отклонён');
  notify('Запрос отправлен. Проверяем сохранение…');
  for(let k=0;k<8;++k) {
   await new Promise(resolve=>setTimeout(resolve,950));
   const response=await fetch('/api/status',{cache:'no-store'});
   const snapshot=await response.json();
   if(snapshot.command_seq!==seq) {
    seq=snapshot.command_seq;
    notify(snapshot.command_result||'Параметр обработан');
    if(input.type==='password')input.value='';
    return;
   }
  }
  notify('Команда в очереди — проверь logs/bot.log');
 }catch(e){notify('Ошибка: '+e.message)}
 finally{button.disabled=false;}
}
document.addEventListener('click',event=>{
 const button=event.target.closest('button[data-key]');
 if(button)save(button.dataset.key,button);
});
load().catch(e=>notify('Ошибка загрузки настроек: '+e.message));