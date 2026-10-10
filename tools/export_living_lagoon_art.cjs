/* Export the approved HTML concept's vector art for native menu thumbnails.
 * Run before gen_theme_assets.py when intentionally updating this direction. */
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'docs/design/quiet-lagoon/preview.js'),'utf8');
const artist=vm.runInNewContext('let serial=0;'+source.slice(source.indexOf('function artist('),source.indexOf('function thumbnail('))+';artist');
const base=path.join(root,'assets/themes/quiet-lagoon');
for(const [n,name] of ['fish','seahorse','octopus','squid','puffer','angler','eel','shark','crab','lobster'].entries()){
 const a=artist('living'),svg=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="-100 -75 200 150">${a.defs}${a.creature(n)}</svg>`;
 for(const file of [name,`shop_${name}`])if(fs.existsSync(path.join(base,file+'.svg')))fs.writeFileSync(path.join(base,file+'.svg'),svg+'\n');
}
for(const [n,names] of [[0,['plant','shop_plant']],[2,['castle','shop_castle']],[3,['coral','shop_coral']],[4,['reef','shop_cluster']],[5,['snail','snail_upright','shop_snail']]]){
 const a=artist('living'),svg=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="-100 -75 200 150">${a.defs}${a.decor(n)}</svg>`;
 for(const name of names)fs.writeFileSync(path.join(base,name+'.svg'),svg+'\n');
}
console.log('Exported approved Living Lagoon creature and decoration SVGs.');
