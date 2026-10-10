/* Theme regression tests run headlessly on every board, through real touch/save/render paths. */
#define _POSIX_C_SOURCE 200809L
#include "render.h"
#include "progression.h"
#include "setup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define CHECK(ok, msg) do { if (!(ok)) { fprintf(stderr,"themes: FAIL line %d: %s\n",__LINE__,msg); return 1; } } while (0)
static uint16_t fb[TANK_W*TANK_H], first[TANK_W*TANK_H], scene[TANK_W*TANK_H];
static uint8_t vignette[TANK_W*TANK_H];
static uint32_t dirty[RENDER_DIRTY_WORDS];
static uint16_t card[RENDER_CARD_W*RENDER_CARD_H];
static int tap(tank_t *t,int x,int y,int *value) {
    render_settings_touch(t,x,y,true,value);
    return render_settings_touch(t,x,y,false,value);
}
static int safe_page(void) {
    int x,y,w,h; render_settings_bounds(&x,&y,&w,&h);
    uint16_t bg=fb[0];
    for (int py=0;py<TANK_H;py++) for (int px=0;px<TANK_W;px++) if (fb[py*TANK_W+px]!=bg) {
        if (px<x || px>=x+w || py<y || py>=y+h) { fprintf(stderr,"outside safe page: %d,%d\n",px,py); return 0; }
#if PAGE_BOWL
        float dx=px-233,dy=py-233;
        if (dx*dx+dy*dy>224*224) return 0;
#endif
    }
    return 1;
}
static void shot(const char *prefix,const char *page,int theme) {
    if (!prefix) return;
    char path[512]; snprintf(path,sizeof path,"%s_%d_%s.ppm",prefix,theme,page);
    FILE *f=fopen(path,"wb"); if (!f) return;
    fprintf(f,"P6\n%d %d\n255\n",TANK_W,TANK_H);
    for (int i=0;i<TANK_W*TANK_H;i++) {
        unsigned char p[3]={(fb[i]>>11)*255/31,((fb[i]>>5)&63)*255/63,(fb[i]&31)*255/31}; fwrite(p,1,3,f);
    }
    fclose(f);
}
/* the settings rows' tap spots, page coordinates (sim/main.c has the same three) */
#define SETP_SEG_X(i)  (SET_SEG_X + (i) * SET_SEG_DX + SET_SEG_W / 2)
#define SETP_PREV_X    (SET_SEG_X + SET_ARW_W / 2)
#define SETP_NEXT_X    (SET_SEG_X + SET_SPAN_W - SET_ARW_W / 2)
static void settings_open_themes(tank_t *t) {
    int x,y,w,h,v; render_settings_bounds(&x,&y,&w,&h);
    render_settings_leave(); render_settings(t,fb,TANK_W,60,1);
    (void)x;(void)y;(void)w;(void)h;
    tap(t,PAGE_X+PAGE_W/2,PAGE_Y+SET_TITLE_Y+12,&v);   /* the SETTINGS / THEMES title button, in every theme (2026-10-10) */
    render_settings(t,fb,TANK_W,60,1);
}
/* Native art sheets exercise every new drawing path and the shared cache. */
static int art_sheets(const tank_t *base,const char *prefix) {
    int bx,by,bw,bh;render_settings_bounds(&bx,&by,&bw,&bh);
    for(int id=0;id<THEME_COUNT;id++) {
        tank_t t=*base;t.theme=id;t.n_fish=0;t.stage_fish=-1;t.clock=2;t.night=false;
        render_use_theme(id);
        for(int page=0;page<(SP_COUNT+4)/5;page++) {
            render_rect(fb,TANK_W,-PAGE_X,-PAGE_Y,TANK_W,TANK_H,0x031015);
            render_text(fb,TANK_W,bx-PAGE_X,by-PAGE_Y,2,0xffffff,"MEET YOUR CREATURES");
            for(int i=0;i<5;i++) {
                if(page*5+i>=SP_COUNT)break;
                int species=page*5+i,x=bx+(i%2)*bw/2+bw/4,y=by+57+(i/2)*85;
                render_creature_preview(fb,TANK_W,x-PAGE_X,y-PAGE_Y,1.3f,species,i%4,0xe5a777,0x73a69b,0xf1d29e,t.clock);
                const char *name=SPECIES[species].name;
                render_text(fb,TANK_W,x-PAGE_X-render_text_w(name,2)/2,y-PAGE_Y+32,2,0x9fd8e2,name);
            }
            shot(prefix,page==2?"species3":page?"species2":"species1",id);
        }
        for(int which=0;which<8;which++) {
            tank_t a=t; a.sd_unlocks=0;
            for(int b=0;b<VEG_BEDS_MAX;b++)for(int i=0;i<VEG_FRONDS_MAX;i++)a.veg_h[b][i]=which==3?.34f:VEG_NUB;
            tank_veg_sync(&a);
            const char *label;
            if(which==0){a.sd_unlocks=SD_ITEM_CASTLE;tank_castle_place(&a);a.castle_x=TANK_W/2;label="castle";}
            else if(which==1){a.sd_unlocks=SD_ITEM_CORAL;tank_coral_place(&a);a.coral_x=TANK_W/2;a.coral_growth=1;label="coral";}
            else if(which==2){a.sd_unlocks=SD_ITEM_CLUSTER;tank_cluster_place(&a);a.cluster_x=TANK_W/2;a.cluster_growth=1;label="reef";}
            else if(which==3){a.sd_unlocks=SD_ITEM_PLANT;tank_plant_place(&a);label="plants";}
            else if(which==5){a.sd_unlocks=SD_ITEM_WRECK;tank_wreck_place(&a);a.wreck_x=TANK_W/2;label="wreck";}
            else if(which==6){a.sd_unlocks=SD_ITEM_FROGMAN;tank_frogman_place(&a);a.frog_x=TANK_W/2;a.frog_y=TANK_H*.4f;a.clock=23.8f;label="frogman";}
            else if(which==7){a.sd_unlocks=SD_ITEM_SUB;tank_sub_place(&a);a.sub_x=TANK_W/2;a.sub_y=TANK_H*.3f;a.sub_stop=4;a.sub_peri=1;a.sub_look=1.2f;a.sub_v=0;label="submarine";}
            else{a.sd_unlocks=SD_ITEM_SNAIL|SD_ITEM_SHRIMP|SD_ITEM_URCHIN;tank_snail_place(&a);tank_shrimp_place(&a,3);tank_urchin_place(&a);a.snail_x=TANK_W/2;a.snail_y=SNAIL_FLOOR_Y;a.snail_front=true;a.urchin_x=TANK_W/2+48;label="companions";}
            for(int j=0;j<5;j++){a.food[j].alive=true;a.food[j].x=TANK_W/2-45+j*17;a.food[j].y=TANK_H/2-25+j%2*10;}
            render_tank(&a,fb,TANK_W);render_tank(&a,fb,TANK_W);shot(prefix,label,id);
            /* Re-entering a theme with growth unchanged must rebuild all geometry caches. */
            memcpy(first,fb,sizeof fb);a.theme=(id+1)%THEME_COUNT;render_tank(&a,fb,TANK_W);
            a.theme=id;render_tank(&a,fb,TANK_W);
            CHECK(!memcmp(first,fb,sizeof fb),"decor cache restores exact geometry");
            if(which==0&&id){
                /* the themed castle IN FRONT is baked with a mask (2026-10-10): a busy frame - fish in and
                   around the arch, pellets, bubbles - must come out pixel for pixel as the old per-rect
                   repaint drew it */
                tank_t b=a; b.sd_unlocks=SD_ITEM_CASTLE; tank_castle_place(&b); b.castle_x=TANK_W/2; b.castle_z=DECOR_Z_FRONT;
                for(int f=0;f<6&&f<N_FISH_MAX;f++){ tank_make_fish(&b,f,f%2,.5f,.5f,STAGE_ADULT); b.fish[f].x=TANK_W/2-60+f*24; b.fish[f].y=TANK_BOT-30-(f%3)*40; b.fish[f].heading=f%2?3.1f:0; tank_fish_face(&b.fish[f]); }
                b.n_fish=6; if(b.n_fish>1)tank_set_species(&b,1,SP_OCTOPUS,0);
                static uint16_t masked[TANK_W*TANK_H], live[TANK_W*TANK_H];
                render_debug_castle_live(false); render_tank(&b,fb,TANK_W); b.clock+=.3f; render_tank(&b,fb,TANK_W); memcpy(masked,fb,sizeof masked);
                render_debug_castle_live(true);  render_tank(&b,fb,TANK_W); b.clock-=.3f; render_tank(&b,fb,TANK_W); b.clock+=.3f; render_tank(&b,fb,TANK_W); memcpy(live,fb,sizeof live);
                render_debug_castle_live(false);
                int diff=0; for(int i=0;i<TANK_W*TANK_H;i++) diff+=masked[i]!=live[i];
                CHECK(diff==0,"the baked castle front matches the per-rect repaint pixel for pixel");
            }
        }
        for(int effect=0;effect<5;effect++) {
            tank_t a=t; a.sd_unlocks=0;
            tank_make_fish(&a,0,0,.5f,.5f,STAGE_ADULT);a.n_fish=1;
            int sp=effect==0?SP_PUFFER:effect==1?SP_OCTOPUS:effect==2?SP_SQUID:effect==3?SP_EEL:SP_JELLYFISH;
            tank_set_species(&a,0,sp,0);fish_t *f=&a.fish[0];
            f->x=TANK_W/2;f->y=TANK_H/2;f->size=1.8f;f->heading=0;tank_fish_face(f);
            f->puff=effect==0?1:0;f->ink=(effect==1||effect==2)?2:0;f->spark=effect==3?1:0;
            for(int frame=0;frame<8;frame++) {
                a.clock=2+frame*.15f;if(effect==4)f->jet=frame/8.f;f->ink=(effect==1||effect==2)?2.8f-frame*.25f:0;
                render_tank(&a,fb,TANK_W);
                char name[32];snprintf(name,sizeof name,"effect%d_%d",effect,frame);shot(prefix,name,id);
            }
        }
        render_rect(fb,TANK_W,-PAGE_X,-PAGE_Y,TANK_W,TANK_H,0x031015);
        render_text(fb,TANK_W,bx-PAGE_X,by-PAGE_Y,2,0xffffff,"JELLYFISH GROWTH");
        for(int j=0;j<3;j++) {
            fish_t f={0};f.species=SP_JELLYFISH;f.stage=j==0?STAGE_FRY:j==1?STAGE_ADULT:STAGE_ELDER;
            f.color=0xe5a777;f.fin=0x73a69b;f.accent=0xf1d29e;
            int x=bx+bw*(2*j+1)/6,y=by+93;
            render_fish_portrait(fb,TANK_W,x-PAGE_X,y-PAGE_Y,j==0?.65f:j==1?1:1.25f,&f,2);
            const char *label=j==0?"FRY":j==1?"ADULT":"ELDER";
            render_text(fb,TANK_W,x-PAGE_X-render_text_w(label,2)/2,y-PAGE_Y+60,2,0x9fd8e2,label);
        }
        shot(prefix,"jellyfish_life",id);
        render_shop_leave();render_shop(&t,fb,TANK_W);
        for(int page=1;page<SHP_PAGES;page++)render_shop_tap(&t,SHP_ARROW_X1+PAGE_X+8,SHP_ARROW_Y+PAGE_Y+8);
        render_shop(&t,fb,TANK_W);shot(prefix,"jellyfish_shop",id);
        render_shop_tap(&t,PAGE_X+70,PAGE_Y+SHP_ROW_Y0+12);
        render_shop(&t,fb,TANK_W);shot(prefix,"jellyfish_buy",id);render_shop_leave();
        for(int j=0;j<4;j++) {
            render_rect(fb,TANK_W,-PAGE_X,-PAGE_Y,TANK_W,TANK_H,0x031015);
            render_text(fb,TANK_W,bx-PAGE_X,by-PAGE_Y,2,0xffffff,"BATTERY STATES");
            /* Use the public large indicator page's native renderer, one image per state. */
            bat_info_t bi={0};bi.pct=j==0?10:j==1?55:100;bi.state=j==2?BAT_CHARGING:j==3?BAT_PLUGGED:BAT_ON_BATTERY;
            render_battery_info(fb,TANK_W,&bi,t.clock);
            char name[24];snprintf(name,sizeof name,"battery%d",j);shot(prefix,name,id);
        }
    }
    render_use_theme(base->theme);
    return 0;
}
/* Jellyfish is appended to the persisted roster; verify real shop/save/motion paths. */
static int jellyfish_checks(void) {
    tank_t t,loaded;tank_init(&t,821);progression_fresh(&t);progression_setup_done(&t);
    t.sd_balance=100;
    int item=SD_ITEM_SP_FIRST+SP_JELLYFISH-1,slot=t.n_fish;
    CHECK(progression_buy(&t,item),"jellyfish shop purchase");
    CHECK(t.n_fish==slot+1&&t.fish[slot].species==SP_JELLYFISH&&t.sd_balance==90,"one juvenile at normal species price");
    CHECK(progression_buy(&t,item),"second jellyfish for breeding");
    tank_set_name(&t,slot,"juno");tank_set_species(&t,slot+1,SP_JELLYFISH,2);
    CHECK(progression_save(&t),"save jellyfish");
    tank_init(&loaded,1);progression_boot(&loaded);
    CHECK(loaded.fish[slot].species==SP_JELLYFISH&&!strcmp(loaded.fish[slot].name,"juno")&&loaded.fish[slot+1].variant==2,"jellyfish identity and design reload");
    CHECK((loaded.sd_unlocks&SD_ITEM_SP_JELLYFISH)!=0,"new shop bit survives reload");
    int baby=tank_add_fish(&loaded,slot,slot+1);
    CHECK(baby>=0&&loaded.fish[baby].species==SP_JELLYFISH&&loaded.fish[baby].stage==STAGE_FRY,"jellyfish parents produce jellyfish fry");
    tank_init(&t,123);tank_new_population(&t);t.n_fish=1;t.stage_fish=-1;t.autofeed_off=true;t.trickle_off=true;t.greet_timer=0;
    tank_set_species(&t,0,SP_JELLYFISH,0);fish_t *f=&t.fish[0];
    f->x=TANK_W*.5f;f->y=TANK_H*.45f;f->size=1;float prev=f->jet,distance=0;int pulses=0;
    for(int j=0;j<900;j++){
        f->goal.id=GOAL_EXPLORE;f->hunger=3;f->energy=8;f->stress=0;t.night=false;
        float x=f->x,y=f->y;tank_tick(&t,1.f/30,NULL);
        distance+=hypotf(f->x-x,f->y-y);pulses+=f->jet<prev;prev=f->jet;
        CHECK(isfinite(f->x)&&isfinite(f->y)&&f->x>=tank_glass_x0(f->y)&&f->x<=tank_glass_x1(f->y),"jellyfish remains inside glass");
        CHECK(f->ink==0&&f->spark==0,"jellyfish never inks or sparks");
    }
    CHECK(distance>25&&pulses>=15&&pulses<=21,"jellyfish swims in slow pulses");
    /* the contraction lifts it: from the start of a pulse to the bell's fullest contraction it rises */
    float lift=0,start=0,top=0;int cycles=0;bool in=false;prev=f->jet;
    for(int j=0;j<1800&&cycles<6;j++){
        f->goal.id=GOAL_EXPLORE;f->hunger=3;f->energy=8;f->stress=0;tank_tick(&t,1.f/30,NULL);
        if(f->jet<prev){if(in){lift+=start-top;cycles++;}in=true;start=top=f->y;}
        else if(in&&f->jet<.5f&&f->y<top)top=f->y;
        prev=f->jet;
    }
    CHECK(cycles==6&&lift/cycles>2,"each contraction lifts the jellyfish");
    int slow=0;prev=f->jet;
    for(int j=0;j<900;j++){f->goal.id=GOAL_REST;f->hunger=3;f->energy=8;f->stress=0;tank_tick(&t,1.f/30,NULL);slow+=f->jet<prev;prev=f->jet;}
    CHECK(slow<pulses/2,"rest slows the pulse");
    f->goal.id=GOAL_SEEK_FOOD;f->hunger=8;float meals=f->eaten;
    t.food[0]=(food_t){.alive=true,.x=f->x,.y=f->y};
    tank_tick(&t,1.f/30,NULL);
    CHECK(f->eaten>meals,"jellyfish eats through the normal feeding path");
    for(int id=0;id<THEME_COUNT;id++){
        t.theme=id;f->jet=.25f;render_tank(&t,fb,TANK_W);memcpy(first,fb,sizeof fb);
        f->jet=.75f;render_tank(&t,fb,TANK_W);
        CHECK(memcmp(first,fb,sizeof fb),"bell visibly contracts in every theme");
        for(int v=0;v<SP_VARIANTS;v++){
            const sp_variant_t *look=&SPECIES[SP_JELLYFISH].var[v];
            memset(fb,0,sizeof fb);render_creature_preview(fb,TANK_W,PAGE_W/2,PAGE_H/2,1,SP_JELLYFISH,v,look->color,look->fin,look->accent,1);
            int visible=0;for(int px=0;px<TANK_W*TANK_H;px++)visible+=fb[px]!=0;
            CHECK(visible>250,"all jellyfish designs render at native size");
            if(v==0)memcpy(first,fb,sizeof fb);
            else CHECK(memcmp(first,fb,sizeof fb),"each design keeps its own colours in every theme");
        }
    }
    printf("jellyfish: shop, save/reload, offspring, %d swim pulses / %d resting, three theme renderers PASS\n",pulses,slow);
    return 0;
}
static int flourish_checks(void) {
    static const int species[]={SP_LOBSTER,SP_CRAB,SP_ANGLER,SP_OCTOPUS,SP_PUFFER,SP_SQUID,SP_EEL};
    for(int i=0;i<7;i++) {
        tank_t t;tank_init(&t,73);tank_new_population(&t);t.n_fish=1;t.stage_fish=-1;t.autofeed_off=true;t.greet_timer=0;
        tank_set_species(&t,0,species[i],0);fish_t *f=&t.fish[0];
        f->x=TANK_W*.5f;f->y=TANK_BOT-45;f->flourish_wait=.01f;f->air_s=1000;
        float min_y=f->y,peak_puff=0,peak_ink=0,peak_spark=0;bool visited=false;
        for(int j=0;j<55*30;j++) {
            f->hunger=2;f->energy=8;f->stress=0;f->goal.id=GOAL_EXPLORE;t.night=false;
            tank_tick(&t,1.f/30,NULL);
            if(f->y<min_y)min_y=f->y;
            if(f->puff>peak_puff)peak_puff=f->puff;
            if(f->ink>peak_ink)peak_ink=f->ink;
            if(f->spark>peak_spark)peak_spark=f->spark;
            visited|=f->surface_s>0;
            CHECK(isfinite(f->x)&&isfinite(f->y),"flourish positions remain finite");
        }
        if(i<5){
            if(!(visited&&min_y<tank_glass_top(TANK_W*.5f)+80))fprintf(stderr,"surface %s: min %.1f visited %d\n",SPECIES[species[i]].name,min_y,visited);
            if(!visited)fprintf(stderr,"timer %.2f mode %d cycles %d hunger %.2f goal %d stage %d\n",f->flourish_wait,f->sp_mode,f->flourish_cycle,f->hunger,f->goal.id,t.stage_fish);
            CHECK(visited&&min_y<tank_glass_top(TANK_W*.5f)+80,"surface visitor reaches upper water");
            CHECK(f->surface_s<=0,"surface trip ends");
        }
        if(species[i]==SP_PUFFER)CHECK(peak_puff>.9f,"puffer inflates on its trip");
        if(species[i]==SP_SQUID)CHECK(peak_ink>2,"squid spontaneously inks");
        if(species[i]==SP_EEL)CHECK(peak_spark>1,"eel spontaneously sparks");
        f->surface_s=0;f->sp_mode=SPM_NONE;f->sp_t=0;f->ink=f->spark=0;f->flourish_wait=.01f;f->flourish_cycle=1;
        t.stage_fish=0;tank_tick(&t,.1f,NULL);
        CHECK(f->surface_s==0&&f->ink==0&&f->spark==0,"naming preview suppresses idle displays");
        t.stage_fish=-1;f->hunger=2;f->energy=8;f->goal.id=GOAL_EXPLORE;tank_tick(&t,.1f,NULL);
        if(species[i]==SP_OCTOPUS)CHECK(f->ink>2,"octopus squirts ink on the idle turns after its surface trip");
        f->hunger=9;f->surface_s=5;tank_tick(&t,.1f,NULL);CHECK(f->surface_s==0,"hunger interrupts a trip");
    }
    return 0;
}
int selftest_themes(const char *prefix) {
    char save[128]; snprintf(save,sizeof save,"/tmp/aqua-theme-test-%ld.sav",(long)getpid());
    CHECK(!setenv("AQUA_PETS_SAVE",save,1),"scratch save path");
    tank_t t,loaded; tank_init(&t,2024); progression_boot(&t);
    CHECK(t.theme==THEME_ORIGINAL,"fresh tank defaults to Original");
    tank_set_name(&t,0,"miso"); t.fish[0].trust=6.25f; t.sd_balance=173;
    uint32_t body=t.fish[0].color; int count=t.n_fish;
    int x,y,w,h,v; render_settings_bounds(&x,&y,&w,&h);
    render_set_scene_cache(scene); render_set_vignette_cache(vignette);
    render_set_dirty_mask(dirty); render_set_card_cache(card);
    render_tank(&t,fb,TANK_W); memcpy(first,fb,sizeof fb);
    for (int id=0;id<THEME_COUNT;id++) {
        settings_open_themes(&t); CHECK(safe_page(),"theme picker is inside safe area");
        /* Sliding from one option to another must not silently select it. */
        render_settings_touch(&t,x+w/2,y+90,true,&v);
        CHECK(render_settings_touch(&t,x+w/2,y+152,false,&v)==SET_TAP_NONE,"drag does not select another theme");
        { int ax,ay,aw,ah; render_theme_tile_rect(id,&ax,&ay,&aw,&ah); CHECK(tap(&t,ax+aw/2,ay+ah/2,&v)==SET_TAP_THEME,"theme option touch routes"); }
        CHECK(t.theme==id && v==id,"chosen theme applied");
        CHECK(t.n_fish==count && t.fish[0].color==body && t.fish[0].trust==6.25f && t.sd_balance==173 && !strcmp(t.fish[0].name,"miso"),"theme selection preserves the simulation");
        render_settings(&t,fb,TANK_W,60,1); CHECK(safe_page(),"selected picker safe"); shot(prefix,"picker",id);
        tap(&t,x+w/2,y+h-22,&v);
        render_settings(&t,fb,TANK_W,60,1); shot(prefix,"settings",id);
        /* one layout for every theme (2026-10-10): the Original's rows and foot, in the theme's colours.
           BRIGHTNESS between arrows, VOLUME segments, LIGHTS OUT, AUTO FEED, then ABOUT, RESET, CLOSE */
        {
            CHECK(tap(&t,PAGE_X+SETP_NEXT_X,PAGE_Y+SET_ROW1_Y+10,&v)==SET_TAP_BRIGHT && v==70,"brightness up a step");
            CHECK(tap(&t,PAGE_X+SETP_PREV_X,PAGE_Y+SET_ROW1_Y+10,&v)==SET_TAP_BRIGHT && v==60,"brightness down a step");
            CHECK(tap(&t,PAGE_X+SETP_SEG_X(2),PAGE_Y+SET_ROW2_Y+10,&v)==SET_TAP_VOLUME && v==2,"volume segment");
            CHECK(tap(&t,PAGE_X+SETP_NEXT_X,PAGE_Y+SET_ROW3_Y+10,&v)==SET_TAP_LIGHT && v==1,"lights out: a step to AUTO");
            CHECK(tap(&t,PAGE_X+SETP_PREV_X,PAGE_Y+SET_ROW3_Y+10,&v)==SET_TAP_LIGHT && v==0,"lights out: back to the double-tap");
            CHECK(tap(&t,PAGE_X+SETP_SEG_X(1),PAGE_Y+SET_ROW4_Y+10,&v)==SET_TAP_FEED && !v,"auto feeding off");
            CHECK(tap(&t,PAGE_X+SETP_SEG_X(0),PAGE_Y+SET_ROW4_Y+10,&v)==SET_TAP_FEED && v,"auto feeding restored");
            tank_light_choice_set(&t,0);
            CHECK(tap(&t,PAGE_X+SET_ABT_X+SET_ABT_W/2,PAGE_Y+SET_ABT_Y+MSP_CLOSE_H/2,&v)==SET_TAP_ABOUT && v==1,"the ABOUT button opens the about page");
            t.clock=7; render_settings(&t,fb,TANK_W,60,1); CHECK(safe_page(),"about page safe"); shot(prefix,"about",id);
            CHECK(tap(&t,x+w/2,y+40,&v)==SET_TAP_NONE,"a tap on the about page's words does nothing");
            CHECK(tap(&t,x+w/2,y+h-22,&v)==SET_TAP_ABOUT && v==0,"BACK leaves the about page");
            render_settings(&t,fb,TANK_W,60,1);
            CHECK(tap(&t,PAGE_X+SET_RST_X+SET_RST_W/2,PAGE_Y+SET_RST_Y+MSP_CLOSE_H/2,&v)==SET_TAP_RESET,"RESET asks the platform for the prompt");
            CHECK(t.n_fish==count && t.sd_balance==173,"RESET itself wipes nothing");
            render_settings(&t,fb,TANK_W,60,1);
            CHECK(tap(&t,PAGE_X+SET_CLOSE_X+SET_CLOSE_W/2,PAGE_Y+SET_FOOT_Y+10,&v)==SET_TAP_CLOSE,"CLOSE closes settings");
        }
        CHECK(progression_save(&t),"save theme");
        tank_init(&loaded,1); progression_boot(&loaded);
        CHECK(loaded.theme==id && loaded.n_fish==count && !strcmp(loaded.fish[0].name,"miso") && loaded.sd_balance==173,"theme/name/population/balance survive reload");
        unsigned before; const uint16_t *baked=render_scene_buf(&before); if(baked){memcpy(fb,baked,sizeof fb);render_fb_primed(fb,before);}
        render_tank(&t,fb,TANK_W); shot(prefix,"tank",id);
        if (id) CHECK(memcmp(first,fb,sizeof fb)!=0,"theme changes native tank pixels");
        t.night=true; render_tank(&t,fb,TANK_W); render_tank(&t,fb,TANK_W); shot(prefix,"night",id); t.night=false; render_tank(&t,fb,TANK_W);   /* lights out (2026-10-10: the Blackwater moon) */
        render_stats_card(&t,0,fb,TANK_W); shot(prefix,"card",id);
        render_milestones_leave(); render_milestones(&t,fb,TANK_W); shot(prefix,"milestones",id);
        render_shop_leave(); render_shop(&t,fb,TANK_W); shot(prefix,"shop",id);
        setup_begin_rename(&t,0); render_tank(&t,fb,TANK_W); render_setup(&t,fb,TANK_W,0); shot(prefix,"naming",id);
        if (id) {
            CHECK(setup_hit(x+30,y+h-22)==SETUP_HIT_BACK,"safe wheel cancel hit");
            CHECK(setup_hit(x+w-30,y+h-22)==SETUP_HIT_NEXT,"safe wheel done hit");
            int sx=PAGE_X+SETUP_SLOT_X+15,sy=y+137+10;
            CHECK(setup_hit(sx,sy)==SETUP_HIT_SLOT0,"native wheel slot hit");
            setup_touch(&t,sx,sy,true); setup_touch(&t,sx,sy-31,true); setup_touch(&t,sx,sy-31,false);
        } else { setup_activate(&t,SETUP_HIT_SLOT0); setup_activate(&t,SETUP_HIT_UP); }
        CHECK(t.fish[0].name[0]=='n',"original character wheel still spins");
        setup_activate(&t,SETUP_HIT_BACK);
        CHECK(!strcmp(t.fish[0].name,"miso"),"cancel restores original name");
        /* Every preview icon is a flash-backed replacement with the same dimensions. */
        const icon_t *a=theme_icon(&icon_shop_shark);
        CHECK(a->w==icon_shop_shark.w && a->h==icon_shop_shark.h,"icon extent unchanged");
        CHECK((a==&icon_shop_shark)==(id==0),"Original assets preserved; modern assets used");
    }
    /* Return to Original with all caches live: no tinted leftovers or stale priming. */
    t.theme=0; render_use_theme(0); render_tank(&t,fb,TANK_W); memcpy(first,fb,sizeof fb);
    t.theme=2; render_tank(&t,fb,TANK_W);
    t.theme=0; render_tank(&t,fb,TANK_W);
    CHECK(!memcmp(first,fb,sizeof fb),"Original framebuffer restored exactly after theme switches");
    CHECK(progression_save(&t),"save before corruption check");
    FILE *f=fopen(save,"r+b"); CHECK(f!=NULL,"saved file exists");
    CHECK(!fseek(f,3592,SEEK_SET) && fputc(255,f)!=EOF,"write unknown theme fixture"); fclose(f);
    tank_init(&loaded,2); progression_boot(&loaded); CHECK(loaded.theme==0,"unknown IDs safely fall back to Original");
    CHECK(!truncate(save,3592),"simulate pre-theme save without touching old offsets");
    tank_init(&loaded,3); progression_boot(&loaded); CHECK(loaded.theme==0 && loaded.n_fish==count,"old saves keep Original and all fish");
    CHECK(!flourish_checks(),"occasional creature behaviours");
    CHECK(!art_sheets(&t,prefix),"native art regression sheets");
    CHECK(!jellyfish_checks(),"jellyfish integration");
    unlink(save);
    printf("selftest-themes: PASS (%dx%d): %d themes, safe touch pages, persistence, legacy saves, wheel, native assets, cache invalidation, exact Original restoration\n",TANK_W,TANK_H,THEME_COUNT);
    return 0;
}
