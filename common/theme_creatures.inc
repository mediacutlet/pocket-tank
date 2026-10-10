/* Native vector art for the two modern themes. Included after the sk_* primitives.
 * Existing species motion/state and per-creature colours remain the inputs. */
static void draw_blackwater_species(sk_t *k,int species);   /* blackwater.inc, included after this file */
static void theme_creature_pattern(sk_t *k,float x,float y,float spread) {
    if(!k->marked)return;
    bool club=theme_active()==THEME_TIDEPOOL_CLUB;
    for(int i=0;i<3;i++) {
        float xx=x+(i-1)*spread, yy=y+((k->f->variant&1)?(i%2?1.5f:-1.5f):0);
        float rx=club?1.8f:1.0f,ry=club?1.8f:3.0f;
        if(k->f->variant==2){rx*=1.4f;ry*=.6f;}
        if(k->f->variant==3){rx*=.65f;ry*=.65f;}
        sk_mark(k,xx,yy,rx,ry,k->acc,club?235:155);
    }
}
static void draw_theme_species(sk_t *k,int species) {
    if(theme_active()==THEME_QUIET_LAGOON){draw_lagoon_species(k,species);return;}
    if(theme_active()==THEME_BLACKWATER){draw_blackwater_species(k,species);return;}
    bool club=theme_active()==THEME_TIDEPOOL_CLUB;
    float t=k->t, beat=sinf(t*3+k->f->wander), round=club?1.15f:1;
    uint32_t belly=mix(k->body,0xffefd1,club?.42f:.26f);
    float eye=club?2:1.4f;
    switch(species) {
    case SP_SEAHORSE: {
        float x[9]={0,-4,-7,-6,-2,1,0,-3,-4},y[9]={3,10,16,21,22,19,17,17,19},r[9]={4,3.5f,3,2.5f,2,1.7f,1.2f,1,.6f};
        sk_tube(k,x,y,r,9,k->body);
        sk_dot(k,-1,0,6*round,10,k->body,255,false);
        sk_dot(k,0,-12,6*round,6,k->body,255,false);
        sk_line(k,3,-11,11,-9,club?3:2,2,k->body);
        sk_dot(k,-1,2,3,6,belly,255,true);
        sk_tri(k,-5,-3,-11,-6+beat,-7,6,k->fin,210);
        sk_dot(k,-2,-18,club?3:1.5f,2,k->fin,255,false);
        theme_creature_pattern(k,-1,-1,2);sk_eye(k,3,-14,eye,false);break;
    }
    case SP_OCTOPUS:
    case SP_SQUID: {
        bool squid=species==SP_SQUID;draw_ink(k);
        float jet=k->pose;
        for(int i=0;i<(squid?6:8);i++) {
            float u=(i-(squid?2.5f:3.5f)),wave=sinf(t*3+i*.8f)*(k->asleep?.3f:1);
            float x[4]={-3+u*.6f,-9-jet*2,-16-jet*3,-20-jet*3+wave*2};
            float y[4]={u, u*2+wave,u*(2.5f-jet)+wave*2,u*(2-jet)+wave*3};
            float r[4]={club?2:1.4f,1.4f,1,.5f};sk_tube(k,x,y,r,4,k->body);
        }
        if(squid){sk_tri(k,13,0,1,-13*round,-7,0,k->fin,230);sk_tri(k,13,0,1,13*round,-7,0,k->fin,230);}
        sk_dot(k,3,0,squid?12:10,squid?6:10*round,k->body,255,false);
        sk_dot(k,4,3,6,squid?2:4,belly,240,true);
        theme_creature_pattern(k,0,-2,3);sk_eye(k,9,-2,eye,false);break;
    }
    case SP_PUFFER: {
        float puff=clamp01(k->f->puff),r=10+5*puff;
        for(int i=0;i<12;i++){float a=i*TAU/12;sk_line(k,cosf(a)*r,sinf(a)*r,cosf(a)*(r+1+puff*3),sinf(a)*(r+1+puff*3),.7f,.4f,k->fin);}
        sk_tri(k,-8,0,-19,-5-beat,-19,5+beat,k->fin,255);
        sk_dot(k,0,0,r,r*(club?1:.9f),k->body,255,false);
        sk_dot(k,1,4,r*.7f,r*.45f,belly,255,true);
        theme_creature_pattern(k,-2,-3,4);sk_eye(k,7,-3,eye+1,false);
        sk_dot(k,11,2,2,1.3f,k->acc,255,false);break;
    }
    case SP_ANGLER: {
        sk_tri(k,-10,1,-22,-6,-22,7,k->fin,255);
        sk_dot(k,0,0,13,club?12:9,k->body,255,false);
        sk_dot(k,4,4,9,4,belly,230,true);
        float x[4]={0,1,9,14},y[4]={-7,-17,-19,-15+beat},r[4]={1.4f,1,1,.6f};sk_tube(k,x,y,r,4,k->fin);
        sk_dot(k,14,-15+beat,club?3:2,club?3:2,0xffe9ad,255,false);
        sk_eye(k,7,-4,eye,false);theme_creature_pattern(k,-5,0,3);break;
    }
    case SP_EEL: {
        float x[12],y[12],r[12];
        for(int i=0;i<12;i++){float u=i/11.f;x[i]=20-u*59;y[i]=sinf(u*6-t*4)*u*6;r[i]=(club?5:3.8f)*(1-u)+.7f;}
        sk_tube(k,x,y,r,12,k->body);
        for(int i=2;i<10;i+=2)sk_mark(k,x[i],y[i],club?2:1,club?2:2.5f,k->acc,210);
        sk_dot(k,18,0,7,club?5:4,k->body,255,false);sk_eye(k,21,-1,eye,true);
        if(k->f->spark>0)for(int j=0;j<4;j++) {
            float xx=-27+j*12,side=j%2?1:-1,w=sinf(t*15+j)*2;
            uint32_t electric=club?0xd5a137:0xb4ebd5;
            if(club){sk_line(k,xx,side*5,xx+4,side*9,1,1,electric);sk_line(k,xx+4,side*9,xx-1+w,side*12,1,1,electric);sk_line(k,xx-1+w,side*12,xx+4,side*16,.8f,.5f,electric);}
            else{sk_line(k,xx,side*5,xx+3+w,side*9,.6f,.6f,electric);sk_dot(k,xx+3+w,side*11,1,1,0xe4f8c5,220,false);}
        }
        break;
    }
    case SP_SHARK: {
        float x[10]={19,12,-2,-15,-22,-17,-9,1,14,19},y[10]={0,-6,-7,-4,0,4,6,7,4,0};
        sk_poly(k,x,y,10,k->body,255);
        sk_tri(k,-17,0,-30,-11-beat,-26,8+beat,k->fin,255);
        sk_tri(k,0,-4,-5,club?-15:-12,-11,-4,k->fin,255);
        sk_dot(k,1,3,10,2.7f,belly,255,true);
        sk_line(k,15,-7*round,15,7*round,club?3.5f:2.4f,club?3.5f:2.4f,k->body);
        sk_eye(k,17,-6*round,eye,true);theme_creature_pattern(k,-4,0,3);break;
    }
    case SP_SWORDFISH: {
        float tw=sinf(k->f->jet*TAU)*2.5f;
        float x[12]={16,12,4,-6,-14,-20,-21.5f,-20,-14,-6,4,12},y[12]={-2,-5*round,-6.5f*round,-6*round,-4,-2,0,2,4,5.5f*round,6*round,4*round};
        sk_poly(k,x,y,12,k->body,255);
        sk_tri(k,-19,0,-31,-12+tw,-24,0,k->fin,255);sk_tri(k,-19,0,-31,12+tw*.7f,-24,0,k->fin,255);
        sk_tri(k,5,-5,-2,club?-19:-17,-11,-4,k->fin,255);sk_tri(k,-14,-3,-18,-7,-18.5f,-2,k->fin,255);
        sk_dot(k,-2,3,12,club?3:2.5f,belly,255,true);
        theme_creature_pattern(k,-4,-1,5);
        sk_tri(k,6,3,-3,club?11:9,2,4.5f,k->fin,255);
        sk_line(k,15.5f,-1,club?31:33,-.4f,club?1.6f:1.2f,club?.6f:.3f,mix(k->fin,0x000000,.3f));
        sk_eye(k,10.5f,-2,eye,false);break;
    }
    case SP_CRAB:
    case SP_LOBSTER: {
        bool lobster=species==SP_LOBSTER;float raise=clamp01(k->f->puff)*8;
        if(lobster)for(int i=5;i>=0;i--)sk_dot(k,-8-i*3,1+sinf(t*2-i*.5f),4,3-i*.25f,i%2?k->fin:k->body,255,false);
        for(int side=-1;side<=1;side+=2){
            for(int i=0;i<3;i++) {
                float y=side*(4+i*2),w=sinf(t*4+i)*1.5f;
                sk_line(k,-4+i*4,y,-9+i*6,y+side*6+w,1.1f,.6f,k->fin);
            }
            float cy=side*(lobster?6:12)-raise;
            sk_line(k,6,side*3,15,cy,1.8f,1.3f,k->body);
            sk_dot(k,17,cy,club?5:4,club?4:3,k->body,255,false);
            sk_line(k,19,cy-1,23,cy-3-beat*.5f,1.2f,.5f,k->acc);
            sk_line(k,19,cy+1,23,cy+3+beat*.5f,1.2f,.5f,k->acc);
        }
        sk_dot(k,0,0,lobster?10:9,lobster?5:8*round,k->body,255,false);
        theme_creature_pattern(k,0,0,3);
        sk_line(k,5,-4,8,-7,1,.7f,k->fin);sk_eye(k,8,-7,eye,true);
        if(!lobster)sk_eye(k,8,4,eye,true);
        break;
    }
    default:break;
    }
}
