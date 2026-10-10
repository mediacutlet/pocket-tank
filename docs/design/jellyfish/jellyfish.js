'use strict';
const themes=[
 {name:'Original',label:'THE FAMILIAR WORLD · REVISED',title:'Part of the same little school.',bg:'#031015',light:'#031015',text:'#9fd8e2',colors:['#e5a777','#73a69b','#f1d29e'],palette:'Body / fins / accent',description:'A compact, flat-colour bell with a pale underside, a small white eye and four tapered arms. The peach, sea-green and cream sample colours match the existing species sheet below.',bell:'A simple polygon silhouette, solid body colour and small accent marks. One side-facing eye follows the existing fish and pufferfish style.',arms:'Four short, jointed arms in the fin colour, tapering to narrow tips. Their gentle sway follows the bell pulse.'},
 {name:'Quiet Lagoon',label:'LIVING LAGOON',title:'Light held in water.',bg:'#143e41',light:'#4d8377',text:'#e1e9ce',colors:['#b6d2c1','#e4dec0','#639e92'],palette:'Sea glass / pearl / sage',description:'A translucent moon jelly with a softly shaded bell, a rolled rim and four petal-like internal structures. Fine trailing filaments surround two gently folded oral arms.',bell:'Directional highlights and a darker underside give the bell a soft sense of volume.',arms:'Long hair-fine tentacles and layered, translucent oral arms. No cartoon eyes.'},
 {name:'Tidepool Club',label:'WARM & PLAYFUL',title:'A floating little parasol.',bg:'#d7e2ba',light:'#f5f0d6',text:'#3c6754',colors:['#e4ad87','#f9d7ad','#528b76'],palette:'Apricot / cream / jade',description:'A generous apricot bell, a scalloped cream edge and three springy jade ribbons. Two tiny dark eyes add a friendly expression without crowding the silhouette.',bell:'A rounded parasol shape with bold lobes and three warm spots.',arms:'Three wider ribbon arms, with distinct curls that remain visible on the pendant.'}
];
const $=id=>document.getElementById(id),reduce=matchMedia('(prefers-reduced-motion: reduce)').matches;
let view='close',paused=reduce,t=0,last=0,rendered=0,serial=0;
const dims={pendant:[466,466],rectangle:[448,368],watch:[410,502]};
function jelly(theme,time,stage='adult',scope='live'){
 const id='j'+scope+serial++,quiet=theme===1,club=theme===2,phase=$('phase').value;
 const pulse=phase==='contract'?1:phase==='open'?0:(.5+.5*Math.sin(time*2.4));
 const sx=1-pulse*.17,sy=1+pulse*.10,elder=stage==='elder',fry=stage==='fry';
 const armLength=elder?1.18:fry?.73:1;
 if(theme===0){
  let original='';
  const count=fry?3:4;
  for(let i=0;i<count;i++){
   const x=-29+i*58/(count-1),wave=Math.sin(time*1.8+i*.8)*5;
   const len=(elder?69:fry?36:52)+(i%2)*7;
   const points=[[x*sx,0],[x+wave,18],[x-wave*.7,34],[x+wave*.9,len-7],[x+wave*.3,len]];
   const widths=[4,3.2,2.2,1.2,.35];
   const left=points.map(([px,py],j)=>[px-widths[j],py]);
   const right=points.map(([px,py],j)=>[px+widths[j],py]).reverse();
   original+=`<polygon points="${left.concat(right).map(p=>p.join(',')).join(' ')}" fill="#73a69b"/>`;
  }
  original+=`<g transform="scale(${sx} ${sy})"><path d="M-46 5L-41-13L-29-29L-13-38L6-39L24-31L38-17L46 5L31 12L-28 12Z" fill="#e5a777"/><path d="M-43 4L-24 8L4 9L28 6L43 3L46 5L31 12L-28 12Z" fill="#f2d3b8"/>`;
  if(!fry)for(let i=0;i<3;i++)original+=`<ellipse cx="${-20+i*12}" cy="-10" rx="3.6" ry="7" fill="#f1d29e"/>`;
  original+=`<ellipse cx="30" cy="-10" rx="5.6" ry="5.2" fill="#ffffff"/><ellipse cx="32" cy="-10" rx="2.5" ry="2.6" fill="#031015"/></g>`;
  return original;
 }

 const bright=quiet?'#e8e6c9':club?'#ffe2b8':'#b8edf0';
 let a=`<defs><radialGradient id="${id}bell" cx="32%" cy="19%" r="83%"><stop stop-color="${bright}" stop-opacity="${quiet?.72:1}"/><stop offset=".4" stop-color="${quiet?'#a5c9b7':club?'#ebbb96':'#77c4d0'}" stop-opacity="${quiet?.52:.95}"/><stop offset=".78" stop-color="${quiet?'#649f97':club?'#d89b77':'#4e98b4'}" stop-opacity="${quiet?.42:.88}"/><stop offset="1" stop-color="${quiet?'#214f58':club?'#ac725c':'#326d91'}" stop-opacity="${quiet?.7:1}"/></radialGradient><linearGradient id="${id}ribbon" x2=".7" y2="1"><stop stop-color="${quiet?'#d7d9bd':'#a5c9a1'}" stop-opacity=".85"/><stop offset=".6" stop-color="${quiet?'#8bbca9':'#548d77'}" stop-opacity="${quiet?.55:1}"/><stop offset="1" stop-color="${quiet?'#548b88':'#376956'}" stop-opacity="${quiet?.15:1}"/></linearGradient></defs>`;
 const line=(d,color,w,alpha=1)=>`<path d="${d}" fill="none" stroke="${color}" stroke-width="${w}" stroke-linecap="round" opacity="${alpha}"/>`;
 const count=quiet?10:club?3:4;
 for(let i=0;i<count;i++){
  const spread=quiet?40:club?32:34,x=-spread+i*spread*2/(count-1),l=(quiet?81:club?69:73)+(i%3)*8;
  const wave=Math.sin(time*1.8+i*.8)*7,lag=Math.sin(time*1.8+i*.8-.8)*9;
  const d=`M${x*sx} 1C${x+wave} 25 ${x-wave*.7} 39 ${x+lag} 52S${x+wave+lag} ${l-5} ${x+lag*.4} ${l}`;
  a+=`<g transform="scale(1 ${armLength})">${line(d,club?`url(#${id}ribbon)`:quiet?'#b8d9c8':i%2?'#79aac5':'#a8dadd',club?7:quiet?.8:2.4,quiet?.56:.9)}${quiet?line(d,'#e7e9d4',.3,.28):''}</g>`;
 }
 if(quiet&&!fry){for(let i=0;i<2;i++){const x=i?12:-13,w=Math.sin(time*1.7+i*2)*5;
 a+=`<path d="M${x-5} 1Q${x+12+w} 23 ${x-2} 39Q${x-11-w} 51 ${x+5} 68Q${x-14} 55 ${x-9} 38Q${x+2+w} 22 ${x-9} 2Z" fill="url(#${id}ribbon)"/><path d="M${x} 7Q${x+6+w} 25 ${x-5} 40T${x+2} 61" fill="none" stroke="#e4e4c5" stroke-width=".8" opacity=".6"/>`;}}
 a+=`<g transform="scale(${sx} ${sy})">`;
 const bell=club?'M-51 4C-50-33-24-55 0-53C30-54 53-28 51 4Q43 16 34 5Q25 18 17 6Q8 19 0 7Q-10 18-18 6Q-27 17-35 5Q-44 14-51 4Z':quiet?'M-50 2C-47-30-25-51 0-51C28-51 48-28 50 2Q27 14 0 12Q-30 12-50 2Z':'M-45 3C-44-29-24-46 0-46C26-46 44-26 45 3Q0 17-45 3Z';
 a+=`<path d="${bell}" fill="url(#${id}bell)" stroke="${club?'#a66e56':quiet?'#c1ddc7':'#b0e0e7'}" stroke-width="${club?1.7:quiet?.65:1.2}" stroke-opacity="${quiet?.6:.9}"/>`;
 if(quiet){a+=`<ellipse cx="0" cy="4" rx="46" ry="8" fill="#2e686b" opacity=".30"/>`;
  if(!fry)for(let i=0;i<4;i++){let angle=i*90;a+=`<path d="M0-15C-14-35-27-18-12-11Q-4-8 0-15Z" transform="rotate(${angle} 0 -15)" fill="#e1dfc0" fill-opacity=".22" stroke="#d1dfc2" stroke-opacity=".48" stroke-width="1.1"/>`;}
  a+=line('M-37-14Q-27-40-6-42','#fff4d6',2,.6)+line('M-42-6Q-22-14 0-12T43-3','#dce6cd',.8,.5)+line('M-48 3Q0 20 48 3','#d5e5cf',2.1,.72);
  if(elder)for(let i=0;i<11;i++){const x=-40+i*8;a+=`<circle cx="${x}" cy="${5+5*(1-Math.abs(x)/45)}" r="1" fill="#e8e8c7" opacity=".65"/>`;}
 }else if(club){a+=line('M-43 4Q-35 13-27 5Q-18 15-9 6Q0 17 9 6Q18 15 27 5Q36 13 44 4','#ffdfae',4)+line('M-31-28Q-17-44 1-43','#fff1d0',3.5,.75);
  for(let i=0;i<3;i++)a+=`<ellipse cx="${-19+i*19}" cy="${i===1?-31:-24}" rx="4" ry="3.4" fill="#c98263" opacity=".65"/>`;
  if(!fry)a+=`<circle cx="-10" cy="-8" r="2.2" fill="#36584b"/><circle cx="10" cy="-8" r="2.2" fill="#36584b"/>`;
 }else{a+=line('M-38 4Q0 14 38 4','#c9f3ed',2.2)+line('M-28-21Q-20-34-9-35','#d6f5ed',2.2,.75);
  if(!fry)a+=`<ellipse cx="-10" cy="-12" rx="6" ry="9" fill="#b0e7e2" opacity=".4"/><ellipse cx="10" cy="-12" rx="6" ry="9" fill="#b0e7e2" opacity=".4"/>`;
 }
 return a+'</g>';
}
$('designs').innerHTML=themes.map((d,i)=>`<article class="design" style="--bg:${d.bg};--light:${d.light};--text:${d.text}"><div class="title"><span class="number">0${i+1}</span><div><small>${d.label}</small><h2>${d.name}</h2></div></div><div class="viewport" id="view${i}"><div class="studiobg"></div><img class="scene" alt="${d.name} native aquarium background"><span class="watermark">JELLYFISH / 0${i+1}</span><svg role="img" aria-label="Proposed ${d.name} jellyfish"></svg></div><p class="caption" id="caption${i}"></p><div class="description"><h3>${d.title}</h3><p>${d.description}</p></div><div class="palette">${d.colors.map(c=>`<i style="--c:${c}"></i>`).join('')}<span>${d.palette}</span></div><dl><div><dt>The bell</dt><dd>${d.bell}</dd></div><div><dt>The trailing arms</dt><dd>${d.arms}</dd></div></dl></article>`).join('');
const reference=document.createElement('div');
reference.className='original-reference';
reference.innerHTML=`<p>EXISTING ORIGINAL SPECIES · NATIVE RENDERS</p><div><figure><svg viewBox="102 108 96 57" role="img" aria-label="Existing Original fish"><image href="../native/pendant_0_species1.png" width="466" height="466"/></svg><figcaption>Classic fish</figcaption></figure><figure><svg viewBox="268 193 96 57" role="img" aria-label="Existing Original pufferfish"><image href="../native/pendant_0_species1.png" width="466" height="466"/></svg><figcaption>Pufferfish</figcaption></figure></div>`;
document.querySelector('.design').append(reference);
$('life-grid').innerHTML=themes.map((d,i)=>`<div class="life-card" style="--bg:${d.bg};--text:${d.text}"><p>${d.name.toUpperCase()}</p><div class="sizes">${['fry','adult','elder'].map((s,j)=>`<div class="size"><svg width="${[26,46,58][j]}" height="${[36,70,88][j]}" viewBox="-60 -60 120 180" role="img" aria-label="${d.name} ${s}">${jelly(i,0,s,'life')}</svg><span>${s}</span></div>`).join('')}</div></div>`).join('');
function layout(){
 const device=$('device').value,[w,h]=dims[device],native=device==='rectangle'?'classic':device;
 themes.forEach((d,i)=>{const box=$('view'+i);box.className='viewport'+(view==='tank'?' device '+device:'');box.querySelector('img').src=`../native/${native}_${i}_plants.png`;box.querySelector('svg').setAttribute('viewBox',view==='tank'?`0 0 ${w} ${h}`:'-100 -90 200 220');$('caption'+i).textContent=view==='tank'?`${w} × ${h} device view · adult bell ≈ 38 display pixels`:'Enlarged design study · inspect the silhouette and materials';});
 $('close').setAttribute('aria-pressed',String(view==='close'));$('tank').setAttribute('aria-pressed',String(view==='tank'));
 $('motion').textContent=paused?'Play animation':'Pause animation';$('motion').setAttribute('aria-pressed',String(paused));draw();
}
function draw(){serial=0;const [w,h]=dims[$('device').value];themes.forEach((d,i)=>{let y=Math.sin(t*1.2)*3.5;const transform=view==='tank'?`translate(${w*.5} ${h*.4+y}) scale(.38)`:`translate(0 ${y})`; $('view'+i).querySelector('svg').innerHTML=`<g transform="${transform}">${jelly(i,t)}</g>`;});}
$('close').onclick=()=>{view='close';layout()};$('tank').onclick=()=>{view='tank';layout()};$('device').onchange=layout;$('phase').onchange=()=>{if($('phase').value!=='animated')paused=true;else paused=reduce;layout()};$('motion').onclick=()=>{paused=!paused;if(!paused)$('phase').value='animated';layout()};
function frame(now){if(last&&!paused&&document.visibilityState==='visible')t+=Math.min(.05,(now-last)/1000);last=now;if(now-rendered>50&&!paused){draw();rendered=now;}requestAnimationFrame(frame);}
layout();requestAnimationFrame(frame);
