'use strict';
const species=['Classic fish','Seahorse','Octopus','Squid','Pufferfish','Anglerfish','Eel','Hammerhead','Crab','Lobster'];
const decorations=['Sword plant','Weeds','Castle','Coral','Coral reef','Snail','Food','Bubbles'];
const $=id=>document.getElementById(id);
species.forEach((name,i)=>$('species').add(new Option(name,i)));
let feeding=false;
let serial=0, nameLetters=['M','I','S','T',' ',' ',' '],cursor=0,paused=matchMedia('(prefers-reduced-motion: reduce)').matches;
const alphabet=' ABCDEFGHIJKLMNOPQRSTUVWXYZ';
function artist(mode){
 const soft=mode==='porcelain',id='a'+serial++,ref=n=>`url(#${id+n})`;
 const colors=soft?['#eef1df','#b7d2bd','#70968c','#35584f']:['#e4dbc0','#92b6a4','#438b81','#123e41'];
 const defs=`<defs><linearGradient id="${id}leaf" x2="1" y2=".6"><stop stop-color="${soft?'#d8e6ce':'#87ad78'}"/><stop offset=".4" stop-color="${soft?'#9dbba4':'#4c865f'}"/><stop offset="1" stop-color="${soft?'#608674':'#204e40'}"/></linearGradient><radialGradient id="${id}body" cx="35%" cy="22%" r="80%"><stop stop-color="${colors[0]}"/><stop offset=".35" stop-color="${colors[1]}"/><stop offset=".74" stop-color="${colors[2]}"/><stop offset="1" stop-color="${colors[3]}"/></radialGradient><linearGradient id="${id}fin" x2=".25" y2="1"><stop stop-color="${soft?'#d1ddd0':'#90b9ae'}" stop-opacity=".9"/><stop offset="1" stop-color="${soft?'#4d7c70':'#255f62'}" stop-opacity="${soft?1:.5}"/></linearGradient><radialGradient id="${id}warm" cx="35%" cy="22%" r="80%"><stop stop-color="${soft?'#f2dcc3':'#ead5b3'}"/><stop offset=".45" stop-color="${soft?'#c29a7c':'#bd8c65'}"/><stop offset="1" stop-color="${soft?'#755b4c':'#654438'}"/></radialGradient><linearGradient id="${id}stone" x2=".8" y2="1"><stop stop-color="${soft?'#f9f3e3':'#b1b6a0'}"/><stop offset=".55" stop-color="${soft?'#d6d6bf':'#778d7c'}"/><stop offset="1" stop-color="${soft?'#9dab99':'#3e6259'}"/></linearGradient><linearGradient id="${id}water" x2=".15" y2="1"><stop stop-color="${soft?'#e4e9dc':'#47857c'}"/><stop offset=".4" stop-color="${soft?'#d5e0d3':'#1c5658'}"/><stop offset="1" stop-color="${soft?'#a7c0ae':'#0e333b'}"/></linearGradient><radialGradient id="${id}sand" cy="0" r="1"><stop stop-color="${soft?'#f6f0df':'#b7b8a0'}"/><stop offset="1" stop-color="${soft?'#c7d0b9':'#43675e'}"/></radialGradient><filter id="${id}shadow" x="-60%" y="-60%" width="220%" height="220%"><feDropShadow dx="1" dy="3" stdDeviation="2" flood-color="#062e32" flood-opacity="${soft?.19:.35}"/></filter><pattern id="${id}scales" width="8" height="6" patternUnits="userSpaceOnUse"><path d="M0 0Q4 6 8 0M-4 3Q0 9 4 3M4 3Q8 9 12 3" fill="none" stroke="#ddedc5" stroke-opacity=".17" stroke-width=".65"/></pattern></defs>`;
 const path=(d,fill='body',stroke=soft?'#52796b':'#214e4e',width=.8,extra='')=>`<path d="${d}" fill="${['body','warm','fin','stone','leaf'].includes(fill)?ref(fill):fill}" stroke="${stroke}" stroke-width="${width}" stroke-linejoin="round" stroke-linecap="round" ${extra}/>`;
 const line=(d,color=soft?'#eff4df':'#c1d6be',w=1,opacity=.5)=>path(d,'none',color,w,`opacity="${opacity}"`);
 const ellipse=(x,y,rx,ry,fill='body',extra='')=>`<ellipse cx="${x}" cy="${y}" rx="${rx}" ry="${ry}" fill="${['body','warm','fin','stone','leaf'].includes(fill)?ref(fill):fill}" ${extra}/>`;
 const eye=(x,y,r=3)=>ellipse(x,y,r+1,r+1,soft?'#9fbdab':'#c8caa4')+ellipse(x,y,r,r,'#153a37')+ellipse(x-.65,y-.8,.8,.8,'#faf9e4');
 const shadow=(x,y,rx)=>ellipse(x,y,rx,4,'#103d38','opacity=".14"');
 const rays=(x,y,tx,top,bot,n=8)=>Array.from({length:n},(_,i)=>line(`M${x} ${y}Q${(x+tx)/2} ${y+(top+(bot-top)*i/(n-1)-y)*.7} ${tx} ${top+(bot-top)*i/(n-1)}`,soft?'#e6efdc':'#d0dcc1',.6,.35)).join('');
 function creature(n){let a='';
 if(n===0){
  const body=soft?'M-51 1C-40-36 19-40 50-13Q69-8 72 2Q53 35 13 30C-22 32-41 20-51 1Z':'M-54 0Q-20-32 20-24Q56-21 69-1Q49 26 9 24Q-28 22-54 0Z';
  a=path(soft?'M-43 0Q-78-35-85-22Q-77 0-85 24Q-69 33-43 0':'M-48 0L-88-29Q-77-1-88 29Z','fin')+rays(-47,0,-84,-26,26)+path('M-22-18Q-12-52 19-37L34-17','fin')+rays(2,-22,15,-39,-20)+path('M-2 18Q7 47 29 29L36 14','fin')+path(body);
  if(!soft)a+=path(body,ref('scales'),'none');
  a+=line('M37-16Q25 0 40 15','#234f4a',1.2,.6)+path('M8 1Q-16 15-14 26Q10 24 25 7','fin')+rays(23,7,-10,16,24,5)+eye(51,-5,soft?3.4:2.4)+line('M61 7L68 2','#153a37',1,.7)+line('M-27-13Q6-28 34-16',undefined,soft?2.8:1.3,.7);
 }else if(n===1){
  a=path(soft?'M14-37Q-17-52-19-26Q-20-10-9 2C-37 25-17 57 11 46Q29 35 10 26Q-5 22-6 34Q0 43 8 34Q-8 42-8 21Q13 12 12-6L30-13L32-25L12-25Z':'M8-46L-5-55L-11-44L-22-39L-18-28L-25-21L-13-14L-22-1L-10 7Q-30 35-9 49Q10 61 22 43Q31 24 13 23Q-1 24 5 37Q10 43 16 35Q7 44 1 33Q-7 22 11 8L12-14L34-19L34-28L10-30Z','warm');
  a+=path('M-12-8Q-44-18-31 8L-10 10','fin')+eye(3,-33,2.4)+line('M12-20L30-23','#f2dfbb',1.5,.5);
  for(let i=0;i<7;i++)a+=line(`M-12 ${-12+i*7}Q-4 ${-17+i*7} 7 ${-12+i*7}`,'#704c37',soft?1:1.6,.35);
 }else if(n===2){
  for(let i=0;i<8;i++){const x=-28+i*8,end=(i-3.5)*18;a+=path(`M${x} 5Q${x*1.7} 35 ${end} ${37+Math.abs(i-3)*2}Q${end+(i<4?-16:16)} 50 ${end+(i<4?-15:15)} 31`,'none',ref('body'),soft?10:7);if(!soft)for(let j=0;j<4;j++)a+=ellipse(x+(end-x)*(j/5),18+j*5,1.8,1,'#c3cab0','opacity=".5"');}
  a+=path(soft?'M-27 9C-52-41-20-61 9-51C39-48 39-15 23 10Q0 20-27 9Z':'M-26 8C-35-13-43-54-5-58C34-63 40-18 23 8Q2 24-26 8Z')+eye(-13,3,3)+eye(15,3,3)+line('M-22-27Q-23-46-8-48',undefined,3,.55);
 }else if(n===3){
  a=path(soft?'M0-55Q-45-18-48 3Q-29 10-11-3L13-3Q30 13 48 1Q37-28 0-55Z':'M0-62L-48-3Q-20 2-15 15L17 13Q29 0 49-4Z','fin');
  for(let i=0;i<6;i++)a+=path(`M${-14+i*6} 23Q${-20+i*9} 56 ${-28+i*11} 45Q${-24+i*11} 32 ${-32+i*12} 38`,'none',ref('body'),soft?5:3);
  a+=path(soft?'M0-53C-14-35-28 1-17 23Q0 39 17 23C29 1 14-35 0-53Z':'M0-58Q-30 0-17 23Q0 34 17 23Q30 0 0-58Z')+eye(-11,19,3)+eye(11,19,3)+line('M-4-38Q-14-7-9 3',undefined,soft?3:1.5,.55);
 }else if(n===4){
  a=path('M30 0L65-20Q53 0 65 18Z','fin')+path('M-3-20Q7-46 24-30L24-17','fin')+ellipse(0,0,soft?36:42,soft?34:31,'body');
  for(let i=0;i<29;i++){const angle=i*2.399,r=8+Math.sqrt(i)*5.3,x=Math.cos(angle)*r,y=Math.sin(angle)*r*.72;a+=soft?ellipse(x,y,1.8,1.4,'#527969','opacity=".48"'):line(`M${x} ${y}l${x*.1} ${-3+y*.07}`,'#dce1b9',1.6,.8);}
  a+=path('M6 7Q-3 26 15 25L22 10','fin')+eye(-25,-8,3.3)+ellipse(-39,3,3.3,2,'#3d5f4d')+line('M-17-21Q0-33 16-21',undefined,soft?3:1,.6);
 }else if(n===5){
  a=path('M20 5L70-28Q54 3 70 30L24 20','fin')+path('M-18-23Q-17-53 21-26L30-8','fin')+path(soft?'M-45 14Q-54-22-13-29Q24-29 42 5Q35 35-7 33Q-36 33-45 14Z':'M-48 19Q-50-36-11-32Q20-27 45 10Q14 38-21 30Z')+line('M-15-27Q-12-65-42-52L-50-39','#789b82',2,1)+ellipse(-50,-37,soft?6:4.5,soft?6:4.5,'stone')+ellipse(-51,-39,2,2,'#fcf7d6')+eye(-27,-10,3.2)+path('M-40 9Q-18 0-12 17Q-25 31-39 20Z','#244b45','#567b66');
  if(!soft)for(let i=0;i<6;i++)a+=path(`M${-36+i*4} ${9+Math.abs(i-2)}l2 5 2-5`,'#b4c5a0','none');
  a+=line('M-7-20Q15-16 27 1',undefined,2,.4);
 }else if(n===6){
  a=path(soft?'M-77 15C-36-37-19 27 14 3Q43-35 77-6Q91 10 72 17C45 29 33-9 8 25Q-25 60-55 14Q-65 2-77 15Z':'M-88 25C-41-49-28 30 8 0Q49-46 76-11Q99 9 72 15C46 25 44-22 15 21Q-25 62-57 9Q-67-5-88 25Z')+line('M-75 13C-42-20-27 46 11 12Q43-27 73 0',undefined,soft?3:1,.5)+eye(73,-2,2.3)+line('M75 10L85 6','#214b46',1,.6);
  if(!soft)for(let i=0;i<16;i++)a+=line(`M${-48+i*7} ${12+Math.sin(i*.45)*11}l-2 5`,'#203f39',.7,.3);
 }else if(n===7){
  a=path('M-37 0L-88-36L-72-1L-86 18Z','fin')+path(soft?'M-17-14Q-8-51 4-39L19-13':'M-23-13L0-51L13-12','fin')+path('M9 11L-14 42L32 20','fin')+path(soft?'M-51 0Q-14-29 35-12Q40-40 58-37Q65-7 62 23Q42 38 37 12Q-8 28-51 0Z':'M-53 0Q-17-22 39-12L39-34Q51-42 61-35L61 31Q52 40 40 31L39 10Q-9 26-53 0Z')+eye(53,-29,2)+eye(53,27,2)+line('M-30-3Q-2-16 31-5',undefined,soft?3:1.5,.6);
  for(let i=0;i<3;i++)a+=line(`M${25+i*4} -7l-2 12`,'#224d47',1,.5);
 }else if(n===8||n===9){
  const lobster=n===9;
  for(let side of [-1,1]){for(let i=0;i<4;i++)a+=path(`M${side*15} ${i*8-2}L${side*(35+i*3)} ${i*12-5}L${side*(43+i*4)} ${i*13+8}`,'none',ref(soft?'body':'warm'),soft?4:2.8);
   a+=path(`M${side*19} 0Q${side*50}-12 ${side*46}-26`,'none',ref(soft?'body':'warm'),soft?8:6)+path(`M${side*45}-19Q${side*73}-26 ${side*65}-51L${side*53}-36L${side*48}-54Q${side*29}-37 ${side*45}-19Z`,soft?'body':'warm');}
  if(lobster){for(let i=5;i>=0;i--)a+=ellipse(0,12+i*7,13-i,6,soft?'body':'warm',`stroke="${soft?'#648b79':'#724e3d'}" stroke-width="1"`);a+=path('M-5 43L-18 59Q0 66 18 59L5 43',soft?'fin':'warm');}
  a+=ellipse(0,2,lobster?17:32,lobster?25:22,soft?'body':'warm')+eye(-9,-20,2.5)+eye(9,-20,2.5)+line('M-19-1Q-11-17 7-13',undefined,soft?3:1,.6);
  if(lobster)a+=line('M-6-20Q-14-52-33-60','#c4c4a2',1,.8)+line('M6-20Q14-52 33-60','#c4c4a2',1,.8);
  else if(!soft)for(let i=0;i<18;i++)a+=ellipse(Math.sin(i*2.3)*23,Math.cos(i*1.9)*13,1.1,1,'#e4c99f','opacity=".4"');
 }
 return `<g filter="${ref('shadow')}">${a}</g>`;
 }
 function decor(n){let a='';
 if(n===0||n===1){
  for(let i=0;i<(n===0?7:9);i++){let dx=(i-(n===0?3:4))*(n===0?13:12),h=45+(i*23)%58;const d=n===1?`M${dx*.25} 49C${dx-27} 2 ${dx+24} ${12-h} ${dx} ${35-h}C${dx+39} ${9-h} ${dx-6} 17 ${dx*.25+5} 49Z`:soft?`M0 49Q${dx-21} ${30-h} ${dx} ${48-h}Q${dx+25} ${10-h} 0 49Z`:`M0 49Q${dx-25} ${15-h} ${dx} ${44-h}Q${dx+15} ${18-h} 0 49Z`;a+=path(d,'leaf');a+=line(`M0 49Q${dx} 8 ${dx} ${48-h}`,soft?'#eff2dc':'#c2cdae',soft?1.5:.9,.5);if(!soft)for(let j=1;j<6;j++)a+=line(`M${dx*j/7} ${49-h*j/7}l${i%2?9:-9} -9`,'#cadaac',.5,.3);}
  a+=shadow(0,50,30);
 }else if(n===2){
  a=shadow(0,49,79);
  if(soft){a+=path('M-65 43L-65-22Q-58-45-35-41L35-41Q58-45 65-22L65 43Z','stone')+path('M65-22L76-14L76 39L65 43','fin')+path('M-37 43L-37-3A37 37 0 0 1 37-3L37 43Z','#6d9685','#e8e6d2',4)+path('M-26 44L-26-1A26 26 0 0 1 26-1L26 44Z','#c3d4bf','none')+path('M-76-24Q0-61 76-24L69-15Q0-37-69-15Z','stone');
   a+=line('M-58-30Q0-51 58-30','#fff9df',2,.8)+path('M-78 43L78 43L71 51L-71 51Z','stone');
  }else{a+=path('M-67 43L-67-29L-60-29L-60-40L-48-40L-48-27L-35-27L-35-43L-24-43L-24-11L27-11L27-43L38-43L38-31L49-31L49-47L61-47L61-30L68-30L68 43Z','stone')+path('M68-30L77-25L77 38L68 43','fin')+path('M-20 44L-20 10Q0-14 20 10L20 44Z','#173e3e','#b2b9a1',5);
   for(let i=0;i<7;i++)a+=line(`M-65 ${-20+i*10}H-27M28 ${-20+i*10}H65`,'#284e47',1,.6);
   for(let i=0;i<25;i++){let x=-62+(i*29)%124,y=-24+(i*17)%60;if(Math.abs(x)>23)a+=line(`M${x} ${y}l0 8`,'#334f46',1,.5);}
   a+=path('M-66 33Q-51 17-43 40Q-30 35-26 45L-70 45Z','#527a59','none')+path('M35-9Q43-22 48-12L62-15L62-7Z','#5d7a57','none');}
 }else if(n===3){
  for(let i=0;i<7;i++){let x=(i-3)*17;let y=-18-(i*19)%37;let d=`M${(i-3)*4} 49Q${x} 24 ${x} ${y}`;a+=path(d,'none',ref(soft?'body':'warm'),soft?12:7);a+=path(`M${x*.8} 13Q${x-14} 2 ${x-18} -12M${x} -5Q${x+16} -13 ${x+18} -31`,'none',ref(soft?'body':'warm'),soft?8:4);a+=line(d,soft?'#e4e9cc':'#e1b993',soft?2:1,.4);}
 }else if(n===4){
  a+=shadow(0,46,70);
  for(let i=0;i<5;i++){let x=-49+i*24,y=16+Math.sin(i*2)*10;a+=path(`M${x-13} 43Q${x-21} ${y} ${x-13} ${y-22}Q${x} ${y-34} ${x+13} ${y-22}Q${x+23} ${y+9} ${x+14} 43Z`,i%2?'body':'stone')+ellipse(x,y-22,13,6,soft?'#608c79':'#254e49')+ellipse(x,y-23,10,3,soft?'#9eb9a4':'#40675b');
   if(!soft)for(let j=0;j<6;j++)a+=line(`M${x-9} ${y-8+j*7}q10 5 18 0`,'#dbdbb7',.7,.3);}
  a+=ellipse(-21,36,30,15,soft?'body':'warm');for(let i=0;i<5;i++)a+=line(`M${-45+i*8} 34q-4-13 4-8q12 2 2 9q-7 6 4 7`,soft?'#e7ecd7':'#eed0a5',soft?1.3:1,.6);
 }else if(n===5){a+=shadow(0,42,54)+path('M-59 36Q-12 27 23 31Q46 18 52 28L67 36Q65 47 39 44L-47 46Z','body')+ellipse(-12,9,31,32,soft?'stone':'warm')+path('M-27 30C-59-1-18-37 7-11C35 16-2 41-20 16C-34-1-9-16 2-2Q12 17-2 18Q-15 15-6 5','none',soft?'#73927b':'#624638',soft?3:2.3)+line('M45 29L46 8M53 31L63 14',soft?'#57816e':'#84a791',2,1)+eye(46,8,1.5)+eye(63,14,1.5);}
 else if(n===6){for(let i=0;i<7;i++){const x=-48+i*16,y=Math.sin(i*2)*22;a+=soft?ellipse(x,y,5,4,'warm'):path(`M${x-5} ${y}l6-5 5 4-4 6Z`,'warm');a+=line(`M${x-3} ${y-1}l4-1`,'#f1ddb6',1,.6);}}
 else {for(let i=0;i<6;i++){const x=-39+i*16,y=Math.sin(i*2)*30,r=4+(i%3)*3;a+=ellipse(x,y,r,r,'none',`stroke="${soft?'#678d7c':'#b5d6cc'}" stroke-width="1" opacity=".65"`)+line(`M${x-r*.6} ${y}a${r*.6} ${r*.6} 0 0 1 ${r*.6} ${-r*.6}`,'#fffde7',1.6,.8);}}
 return `<g filter="${ref('shadow')}">${a}</g>`;
 }
 return {defs,creature,decor,ref,soft,path,line,ellipse};
}
function thumbnail(mode,index,isDecor=false){const a=artist(mode);return `<svg viewBox="-100 -75 200 150" role="img" aria-label="${mode==='living'?'Living Lagoon':'Porcelain Cove'} ${isDecor?decorations[index]:species[index]}">${a.defs}<rect x="-100" y="-75" width="200" height="150" fill="${a.soft?'#e1e6d8':'#204c4c'}"/>${a.ellipse(0,55,62,5,a.soft?'#6f9380':'#062d32','opacity=".18"')}${isDecor?a.decor(index):a.creature(index)}</svg>`;}
const dims={pendant:[466,466],rectangle:[448,368],watch:[410,502]};
function renderDevice(mode){
 const a=artist(mode),soft=a.soft,[w,h]=dims[$('device').value],screen=$('screen').value,selected=+$('species').value,pct=+$('charge').value;
 const ink=soft?'#294f43':'#edf0df',muted=soft?'#527666':'#add0bf',panel=soft?'#f4f0e3':'#173f42',stroke=soft?'#aabfac':'#57817a';
 const text=(x,y,s,size=15,color=ink,extra='')=>`<text x="${x}" y="${y}" fill="${color}" font-family="Trebuchet MS,sans-serif" font-size="${size}" ${extra}>${s}</text>`;
 const centered=(y,s,size=15,color=ink,extra='')=>text(233,y,s,size,color,`text-anchor="middle" ${extra}`);
 const placed=(art,x,y,s=1,r=0)=>`<g transform="translate(${x} ${y}) rotate(${r}) scale(${s})">${art}</g>`;
 const battery=(x,y,scale=1)=>`<g transform="translate(${x} ${y}) scale(${scale})"><rect width="38" height="17" rx="${soft?8:4}" fill="${soft?'#c7d3c1':'#12383b'}" stroke="${stroke}"/><rect x="3" y="3" width="${32*pct/100}" height="11" rx="${soft?5:2}" fill="${pct<20?'#c48762':soft?'#608f79':'#a5c8ae'}"/>${soft?a.ellipse(3+32*pct/100,8.5,2,2,'#fff7df'):''}<path d="M39 5v7" stroke="${muted}" stroke-width="2"/></g>`;
 const action=(name,content)=>`<g class="screen-action" tabindex="0" role="button" aria-label="${name}" data-action="${name}">${content}</g>`;
 let world=`<path d="M-20 326Q155 289 303 333T490 328V520H-20Z" fill="${a.ref('sand')}"/><path d="M-20 355Q125 315 260 357T490 350" fill="none" stroke="${soft?'#f8f6e5':'#9cad95'}" opacity=".2" stroke-width="2"/>`;
 world+=placed(a.decor(1),50,307,.88)+placed(a.decor(0),395,303,.94)+placed(a.decor(2),279,322,.73)+placed(a.decor(4),155,351,.55)+placed(a.decor(3),369,354,.54)+placed(a.decor(5),209,385,.35);
 for(let i=0;i<16;i++)world+=a.ellipse((i*79)%466,341+(i*29)%74,1.3,.7,soft?'#638371':'#d4d5bb','opacity=".4"');
 world+=`<g class="bubble-rise">${placed(a.decor(7),92,280,.4)}</g>`;
 if(screen==='tank')world+=`<g class="drift">${placed(a.creature(selected),195,205,.78)}</g><g class="drift reverse">${placed(a.creature(selected===0?7:0),321,132,.4)}</g>`;
 if(feeding&&screen==='tank')world+=placed(a.decor(6),223,149,.65);
 let content=world;
 if(screen==='tank'){
  content+=centered(75,'QUIET LAGOON',12,ink,'letter-spacing="3"')+battery(303,86,.8);
  const feedY=Math.min(391,h/2+180);
  content+=action('Feed',`<rect x="146" y="${feedY}" width="174" height="37" rx="18" fill="${panel}" stroke="${stroke}"/>${centered(feedY+24,'＋   Feed your aquarium',13)}`);
  content+=action('Settings',`<circle cx="233" cy="107" r="13" fill="${panel}" stroke="${stroke}"/>${centered(112,'≡',18)}`);
 }else if(screen==='creature'){
  content=`<path d="M-30 348Q233 309 500 348V510H-30Z" fill="${a.ref('sand')}"/>${placed(a.decor(0),405,319,.68)}<g class="drift">${placed(a.creature(selected),233,231,1.3)}</g>${centered(102,species[selected].toUpperCase(),13,ink,'letter-spacing="2"')}${centered(334,soft?'Sculpted celadon · engraved detail':'Natural profile · soft directional light',13,muted)}${centered(360,'Adult specimen',11,muted)}`;
 }else{
  content+=`<rect x="70" y="73" width="326" height="324" rx="${soft?30:13}" fill="${panel}" opacity=".98" stroke="${stroke}"/>`;
  if(screen==='settings'){
   content+=centered(109,'Settings',24)+centered(131,soft?'A little order. A little quiet.':'Make yourself at home.',11,muted);
   ['Theme','Sound','Brightness','Battery'].forEach((s,i)=>{let y=150+i*44;content+=action(s,`<rect x="96" y="${y}" width="274" height="39" rx="${soft?13:6}" fill="${soft?'#e1e7d7':'#214d4e'}"/>${text(111,y+25,s,14)}${text(353,y+25,i===0?'Quiet Lagoon  ›':i===1?'On  ›':i===2?'70%  ›':pct+'%  ›',11,muted,'text-anchor="end"')}`)});
   content+=action('Aquarium',centered(366,'←  Return to aquarium',12,muted));
  }else if(screen==='shop'){
   content+=centered(108,'Your little sanctuary',21)+centered(131,'DECORATIONS',10,muted,'letter-spacing="2"');
   [['Sword plant',0],['Castle',2],['Coral reef',4]].forEach(([s,n],i)=>{let y=148+i*60;content+=action('Place '+s,`<rect x="96" y="${y}" width="274" height="54" rx="${soft?14:7}" fill="${soft?'#e1e7d7':'#214d4e'}"/>${placed(a.decor(n),130,y+28,.25)}${text(170,y+25,s,14)}${text(170,y+41,'Preview placement',10,muted)}${text(347,y+33,'＋',20)}`)});
   content+=action('Aquarium',centered(366,'←  Return to aquarium',12,muted));
  }else if(screen==='naming'){
   content+=centered(116,'A name to grow into',21)+centered(142,'Roll each letter. Keep it yours.',12,muted);
   nameLetters.forEach((letter,i)=>{let x=110+i*36;content+=action('Select '+i,`<rect x="${x}" y="198" width="32" height="48" rx="${soft?10:4}" fill="${cursor===i?soft?'#789b84':'#638d7b':soft?'#dbe3d2':'#285555'}"/>${text(x+16,230,letter===' '?'·':letter,24,cursor===i?'#fff9e5':ink,'text-anchor="middle"')}`)});
   const x=126+cursor*36;content+=action('Letter up',text(x,184,'▴',24,ink,'text-anchor="middle"'))+action('Letter down',text(x,275,'▾',24,ink,'text-anchor="middle"'));
   content+=centered(306,'Seven characters · no keyboard',11,muted)+action('Save name',`<rect x="151" y="331" width="164" height="38" rx="${soft?19:8}" fill="${soft?'#396352':'#9cbda6'}"/>${centered(356,'Save name',14,soft?'#fff7e3':'#163d34')}`);
  }else if(screen==='battery'){
   content+=centered(117,'A little energy',22)+battery(176,161,3)+centered(262,pct+'%',48)+centered(290,pct===55?'Charging gently':pct===100?'Fully charged':pct<20?'Time for a recharge':'Ready for a quiet day',14,muted);
   content+=centered(321,soft?'Jade inset · pearl charge marker':'Liquid fill · low-charge amber',11,muted)+action('Aquarium',centered(366,'←  Return to aquarium',12,muted));
  }
 }
 let rays=soft?'':`<g class="ray" opacity=".16"><path d="M90-30L170-30L285 335L225 335Z" fill="#e5edd1"/><path d="M194-30L222-30L355 335L324 335Z" fill="#e5edd1"/></g>`;
 return `<svg viewBox="0 0 ${w} ${h}" role="img" aria-label="${soft?'Porcelain Cove':'Living Lagoon'}, ${screen}, ${w} by ${h}">${a.defs}<rect width="${w}" height="${h}" fill="${a.ref('water')}"/><g transform="translate(${(w-466)/2} ${(h-466)/2})">${rays}${content}</g></svg>`;
}
function update(){for(const mode of ['living','porcelain']){const stage=$(mode+'-stage');stage.className='stage '+$('device').value;stage.innerHTML=renderDevice(mode);stage.parentElement.querySelector('.scale-note').textContent=$('device').selectedOptions[0].textContent+' · scaled to fit';}document.body.classList.toggle('paused',paused);$('motion').textContent=paused?'Play motion':'Pause motion';$('motion').setAttribute('aria-pressed',String(!paused));}
for(const id of ['device','screen','species','charge'])$(id).addEventListener('change',()=>{if(id==='species')$('screen').value='creature';update()});
$('motion').onclick=()=>{paused=!paused;update()};
function screenAction(action){if(action==='Settings')$('screen').value='settings';else if(action==='Battery')$('screen').value='battery';else if(action==='Aquarium'||action==='Save name'||action.startsWith('Place '))$('screen').value='tank';else if(action.startsWith('Select '))cursor=+action.split(' ')[1];else if(action==='Letter up'||action==='Letter down'){let index=alphabet.indexOf(nameLetters[cursor]);nameLetters[cursor]=alphabet[(index+(action==='Letter up'?1:alphabet.length-1))%alphabet.length];}else if(action==='Theme')$('screen').value='creature';else if(action==='Feed')feeding=!feeding;else if(action==='Sound'||action==='Brightness'){const el=document.activeElement;document.querySelectorAll(`[data-action="${action}"] text:last-child`).forEach(t=>t.textContent=action==='Sound'?(t.textContent.startsWith('On')?'Off  ›':'On  ›'):(t.textContent.startsWith('70')?'40%  ›':'70%  ›'));return;}update();}
document.addEventListener('click',e=>{const a=e.target.closest('[data-action]');if(a)screenAction(a.dataset.action)});
document.addEventListener('keydown',e=>{const a=e.target.closest('[data-action]');if(a&&(e.key==='Enter'||e.key===' ')){e.preventDefault();screenAction(a.dataset.action);}});
$('creature-grid').innerHTML=species.map((s,i)=>`<button class="specimen" data-species="${i}" aria-label="Preview ${s}"><div class="art-pair">${thumbnail('living',i)}${thumbnail('porcelain',i)}</div><div class="spec-label"><span>${s}</span><small>01 / 02</small></div></button>`).join('');
$('decor-grid').innerHTML=decorations.map((s,i)=>`<div class="specimen"><div class="art-pair">${thumbnail('living',i,true)}${thumbnail('porcelain',i,true)}</div><div class="spec-label"><span>${s}</span><small>01 / 02</small></div></div>`).join('');
$('creature-grid').addEventListener('click',e=>{const b=e.target.closest('[data-species]');if(!b)return;$('species').value=b.dataset.species;$('screen').value='creature';update();document.querySelector('.toolbar').scrollIntoView({behavior:matchMedia('(prefers-reduced-motion: reduce)').matches?'instant':'smooth'});});
update();
