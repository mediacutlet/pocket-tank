/* Living Lagoon: native shaded geometry, no textures or per-frame allocation.
 * The light ramp is computed once per shape, then quantized to 24 RGB565 tones.
 * Existing transforms, damage tracking, night dimming and species state apply. */
static void lagoon_poly(ctx_t *c,const float *x,const float *y,int n,uint32_t rgb) {
    float top=1e6f,bottom=-1e6f;
    for(int i=0;i<n;i++){top=fminf(top,y[i]);bottom=fmaxf(bottom,y[i]);}
    src_t shade[24];
    for(int i=0;i<24;i++) {
        float q=i/23.f;
        shade[i]=src_color(q<.42f?mix(rgb,0xf0e8c9,.58f*(1-q/.42f)):
            mix(rgb,0x123b3d,(q-.42f)*.95f),c->dim);
    }
    int lo=(int)top<c->oy?c->oy:(int)top,hi=(int)bottom;
    if(hi>=c->oy+c->h)hi=c->oy+c->h-1;
    for(int yy=lo;yy<=hi;yy++) {
        float left=1e6f,right=-1e6f;
        for(int i=0;i<n;i++){int j=(i+1)%n;
            if((y[i]<=yy&&y[j]>yy)||(y[j]<=yy&&y[i]>yy)) {
                float xx=x[i]+(x[j]-x[i])*(yy-y[i])/(y[j]-y[i]);
                left=fminf(left,xx);right=fmaxf(right,xx);
            }
        }
        if(left>right)continue;
        float v=(yy-top)/fmaxf(1,bottom-top),mid=(left+right)*.5f;
        /* Nine spans per row give a rounded cross section without pixel shaders. */
        int start=(int)left,end=(int)right,width=end-start+1;
        for(int j=0;j<9;j++) {
            int x0=start+width*j/9,x1=start+width*(j+1)/9-1;
            if(x1<x0)continue;
            float u=((x0+x1)*.5f-mid)/fmaxf(1,(right-left)*.5f);
            float q=v*.65f+u*u*.28f+u*.08f;
            int s=(int)(fminf(1,fmaxf(0,q))*23);
            span(c,x0,x1,yy,&shade[s],255);
        }
    }
}
static void lagoon_shape(sk_t *k,const float *x,const float *y,int n,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<n;i++)sk_pt(k,x[i],y[i],&X[i],&Y[i]);
    lagoon_poly(k->c,X,Y,n,rgb);
}
/* the body's thickness (2026-10-10): a shaded cross-section at (x, y), `depth` px deep and
   `ry` tall, that widens as the creature turns toward the glass (sqrt(1 - yaw^2), as the
   Original fish's FISH_THICK) - so a turn never folds a lagoon creature to a line. Before
   this only the ellipse bodies had one, a sliver a fifth of their width, and only head-on. */
static void lagoon_volume(sk_t *k,float x,float y,float depth,float ry,uint32_t rgb) {
    float w=fabsf(sk_yaw(k,x)),f=sqrtf(fmaxf(0,1-w*w));
    float RX=depth*f*k->s,RY=ry*k->s;if(RX<1)return;
    float X,Y;sk_pt(k,x,y,&X,&Y);
    float px[20],py[20];for(int i=0;i<20;i++){float a=i*TAU/20;px[i]=X+RX*fast_cos(a);py[i]=Y+RY*fast_sin(a);}
    lagoon_poly(k->c,px,py,20,rgb);
}
static void lagoon_body(sk_t *k,float x,float y,float rx,float ry,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<20;i++){float a=i*TAU/20;X[i]=x+rx*fast_cos(a);Y[i]=y+ry*fast_sin(a);}
    lagoon_shape(k,X,Y,20,rgb);
    lagoon_volume(k,x,y,fminf(rx,ry)*.8f,ry*.95f,rgb);   /* a rounded body: as deep as it is tall, nearly */
}
static void lagoon_limb(sk_t *k,const float *x,const float *y,const float *r,int n,uint32_t rgb) {
    sk_tube(k,x,y,r,n,mix(rgb,0x123b3d,.35f));
    float yy[SK_TUBE_MAX],rr[SK_TUBE_MAX];
    for(int i=0;i<n;i++){yy[i]=y[i]-r[i]*.25f;rr[i]=r[i]*.65f;}
    sk_tube(k,x,yy,rr,n,mix(rgb,0xe2dec0,.25f));
}
static void lagoon_fin(sk_t *k,float x,float y,float tx,float top,float bottom) {
    sk_tri(k,x,y,tx,top,tx+1,bottom,mix(k->fin,0x436f6b,.35f),175);
    for(int j=0;j<5;j++)sk_line(k,x,y,tx+.6f,top+(bottom-top)*j/4,.23f,.14f,mix(k->fin,0xd8dabc,.35f));
}
static void lagoon_pattern(sk_t *k,float cx,float cy,float width,float height) {
    if(!k->marked)return;
    uint32_t rgb=mix(k->acc,k->body,.4f);
    for(int i=0;i<9;i++) {
        float x=cx+(i%3-1)*width,y=cy+(i/3-1)*height;
        sk_mark(k,x,y,k->f->variant==2?.7f:.4f,k->f->variant==1?1.2f:.5f,rgb,90*k->fade);
    }
}
static void draw_lagoon_species(sk_t *k,int species) {
    float beat=k->asleep?0:fast_sin(k->t*(2+k->f->speed*.025f)+k->f->wander);
    uint32_t body=k->body;
    if(species==SP_OCTOPUS&&k->f->camo>0) {
        body=mix(body,k->f->camo_rgb,clamp01(k->f->camo));
        k->fade=1-clamp01(k->f->camo)*.8f;
    }
    switch(species) {
    case SP_FISH: {
        lagoon_fin(k,-12,0,-27,-8-beat*2,8+beat*2);
        if(k->f->stage>=STAGE_ADULT)lagoon_fin(k,4,-4,-3,k->f->stage==STAGE_ELDER?-16:-13,-4);
        lagoon_fin(k,5,4,1,7,13);
        float x[12]={17,14,7,-3,-12,-17,-13,-5,4,12,16,17};
        float y[12]={0,-4,-7,-8,-5,0,5,7,7,4,1,0};
        lagoon_shape(k,x,y,12,body);lagoon_volume(k,0,0,5.5f,7.2f,body);
        for(int i=0;i<5;i++)for(int j=0;j<3;j++)sk_mark(k,-8+i*3,j*3-3,.8f,.3f,0xe0dfbf,85);
        sk_line(k,9,-5,7,0,.35f,.3f,mix(body,0x123b3d,.5f));
        sk_line(k,7,0,10,5,.3f,.2f,mix(body,0x123b3d,.5f));
        lagoon_pattern(k,-2,0,3,2);lagoon_fin(k,3,1,-5,4,8);sk_eye(k,12,-2,1.15f,true);break;
    }
    case SP_SEAHORSE: {
        float x[10]={0,-3,-6,-7,-4,1,4,3,0,-1},y[10]={5,10,14,18,21,21,18,15,15,17},r[10]={3,3,2.7f,2.3f,2,1.8f,1.4f,1.2f,.9f,.4f};
        lagoon_limb(k,x,y,r,10,body);lagoon_fin(k,-4,-3,-10,-5+beat,5);
        lagoon_body(k,0,0,5.5f,10,body);lagoon_body(k,0,-13,5,5,body);
        sk_line(k,3,-13,11,-11,2,1.4f,body);sk_dot(k,11,-11,.5f,1,0x204743,220,false);
        sk_tri(k,-3,-16,-4,-21,1,-17,k->fin,220);
        for(int i=0;i<5;i++)sk_line(k,-2,-5+i*3,3,-6+i*3,.3f,.25f,mix(body,0xf0dec0,.35f));
        sk_eye(k,2,-15,1.1f,true);break;
    }
    case SP_OCTOPUS: case SP_SQUID: {
        bool squid=species==SP_SQUID;draw_ink(k);
        for(int i=0;i<(squid?6:8);i++) {
            float u=i-(squid?2.5f:3.5f),w=k->asleep?0:fast_sin(k->t*3+i*.8f);
            float x[5]={-2,-9,-15-k->pose*2,-20-k->pose*2,-19-k->pose*2+w};
            float y[5]={u,u*1.5f,u*(2.5f-k->pose)+w,u*(2-k->pose)+w*2,u*(1.5f-k->pose)+w};
            float r[5]={1.7f,1.35f,1,.65f,.3f};lagoon_limb(k,x,y,r,5,body);
            if(!squid)for(int j=1;j<4;j++)sk_dot(k,x[j],y[j]+.6f,.4f,.35f,0xd4d4b7,130,false);
        }
        if(squid) {
            lagoon_fin(k,15,0,-3,-12,0);lagoon_fin(k,15,0,-3,0,12);
            float x[8]={17,11,1,-6,-8,-6,1,11},y[8]={0,-5,-7,-5,0,5,7,5};lagoon_shape(k,x,y,8,body);lagoon_volume(k,3,0,5,6.3f,body);
        }else lagoon_body(k,3,-1,10,9.5f,body);
        lagoon_pattern(k,1,-1,2.5f,2);sk_eye(k,squid?-4:8,-2,1.4f,true);break;
    }
    case SP_PUFFER: {
        float puff=clamp01(k->f->puff),rx=11+4*puff,ry=8+7*puff;
        lagoon_fin(k,-7,0,-20,-5-beat,5+beat);
        for(int i=0;i<16;i++){float a=i*TAU/16;sk_line(k,fast_cos(a)*rx,fast_sin(a)*ry,fast_cos(a)*(rx+1+puff*3),fast_sin(a)*(ry+1+puff*3),.45f,.15f,k->fin);}
        lagoon_body(k,0,0,rx,ry,body);lagoon_pattern(k,-2,-1,4,3);
        for(int i=0;i<8;i++)sk_mark(k,-7+(i%4)*4,-4+(i/4)*7,.5f,.7f,0xd9d8b7,170);
        lagoon_fin(k,-1,3,-7,4,10);sk_eye(k,8,-3,1.6f,true);sk_dot(k,rx,2,1,.7f,0x2a5149,255,false);break;
    }
    case SP_ANGLER: {
        lagoon_fin(k,-10,1,-23,-6,7);lagoon_fin(k,4,-5,-5,-14,-4);
        lagoon_body(k,0,1,13,10,body);lagoon_pattern(k,-4,-1,3,2);
        float x[5]={0,0,3,9,13},y[5]={-7,-15,-18,-18,-13+beat},r[5]={.8f,.7f,.6f,.5f,.3f};lagoon_limb(k,x,y,r,5,k->fin);
        sk_dot(k,13,-13+beat,3,3,0xd6dfac,35+(int)(90*k->f->lure),false);
        sk_dot(k,13,-13+beat,1.3f,1.3f,0xfff0bd,255,false);
        sk_dot(k,10,5,3,3,0x183c3c,255,true);
        for(int i=0;i<3;i++)sk_line(k,8+i*1.4f,3,8.3f+i*1.4f,4.5f,.2f,.1f,0xddddbc);
        sk_eye(k,8,-3,1.3f,true);break;
    }
    case SP_EEL: {
        float x[16],y[16],r[16];for(int i=0;i<16;i++){float u=i/15.f;x[i]=23-u*60;y[i]=fast_sin(u*7-k->t*(k->asleep?.5f:3))*u*6;r[i]=4.1f*(1-u)+.4f;}
        lagoon_limb(k,x,y,r,16,body);lagoon_body(k,21,0,6,3.8f,body);sk_eye(k,24,-1,1,true);
        for(int i=2;i<13;i+=2)sk_mark(k,x[i],y[i],.4f,.8f,k->acc,120);
        if(k->f->spark>0)for(int j=0;j<4;j++){float xx=-25+j*12,sgn=j%2?1:-1,w=fast_sin(k->t*18+j)*2;sk_line(k,xx,sgn*5,xx+3+w,sgn*9,.5f,.35f,0xb4ebd5);sk_line(k,xx+3+w,sgn*9,xx+1,sgn*13,.35f,.2f,0xe4f8d6);}
        break;
    }
    case SP_SHARK: {
        lagoon_fin(k,-18,0,-32,-12-beat,8+beat);sk_tri(k,-10,-4,-3,-15,2,-4,k->fin,220);lagoon_fin(k,5,3,-7,7,14);
        float x[10]={16,11,-1,-15,-22,-15,-1,10,16,17},y[10]={-3,-5,-6,-3,0,3,5,4,2,0};lagoon_shape(k,x,y,10,body);lagoon_volume(k,-2,0,5.2f,5.2f,body);
        lagoon_body(k,15,0,3,9,body);sk_eye(k,16,-7,1,true);sk_eye(k,16,7,.8f,true);
        for(int i=0;i<3;i++)sk_line(k,8+i*1.3f,-2,7.5f+i*1.3f,2,.22f,.2f,mix(body,0x123b3d,.5f));
        break;
    }
    case SP_SWORDFISH: {
        float tw=k->asleep?0:fast_sin(k->f->jet*TAU)*2.2f;
        lagoon_fin(k,-19,0,-31,-12+tw,12+tw*.7f);
        lagoon_fin(k,5,-4,-1,-18,-4);lagoon_fin(k,-14,-2.5f,-18,-6.5f,-2);lagoon_fin(k,-13,2.5f,-17,2,6);
        float x[12]={16,12,4,-6,-14,-20,-21.5f,-20,-14,-6,4,12},y[12]={-1.6f,-4.2f,-5.6f,-5.3f,-3.6f,-1.8f,0,1.8f,3.4f,4.8f,4.9f,3.4f};
        lagoon_shape(k,x,y,12,body);lagoon_volume(k,0,0,4.6f,5.2f,body);
        for(int i=0;i<6;i++)for(int j=0;j<2;j++)sk_mark(k,-12+i*4,j*3-1,.8f,.3f,0xe0dfbf,70);
        sk_line(k,12,-.3f,-16,.4f,.3f,.25f,mix(body,0x123b3d,.5f));
        lagoon_fin(k,5,3,-3,4,10);
        sk_line(k,15.5f,-1,33,-.4f,1.1f,.25f,mix(k->fin,0x123b3d,.45f));
        sk_line(k,16,-1.6f,24,-1.3f,.3f,.2f,mix(k->acc,0xf0e8c9,.4f));
        sk_eye(k,10.5f,-1.8f,1.4f,true);break;
    }
    case SP_CRAB: case SP_LOBSTER: {
        bool lobster=species==SP_LOBSTER;
        /* The crab's broad carapace remains visible when it sidles head-on. */
        sk_t crab=*k;if(!lobster){crab.yh=crab.yh<0?-1:1;crab.yt=crab.yh;k=&crab;}
        if(lobster){for(int i=5;i>=0;i--)lagoon_body(k,-8-i*3,1+beat*i*.2f,3.8f,3.4f-i*.3f,body);lagoon_fin(k,-22,1,-28,-4,6);}
        for(int side=-1;side<=1;side+=2) {
            for(int i=0;i<4;i++){float x[3]={-5+i*3,-10+i*5,-12+i*6},y[3]={side*4,side*(8+i)+beat,side*(11+i)+beat},r[3]={.8f,.55f,.25f};lagoon_limb(k,x,y,r,3,k->fin);}
            float cy=side*(lobster?7:11)-k->f->puff*5;
            float x[3]={5,11,16},y[3]={side*3,cy,cy-1},r[3]={1.5f,1.2f,1};lagoon_limb(k,x,y,r,3,body);
            lagoon_body(k,17,cy-1,4,3,body);sk_line(k,19,cy-2,23,cy-4-beat*.5f,1.2f,.2f,body);sk_line(k,19,cy,23,cy+1+beat*.5f,1,.2f,body);
            if(lobster)sk_line(k,8,side*2,22,side*9,.3f,.12f,k->fin);
        }
        lagoon_body(k,0,0,lobster?10:9,lobster?5:8,body);lagoon_pattern(k,0,0,2.5f,2);
        for(int side=-1;side<=1;side+=2){sk_line(k,6,side*3,9,side*5,.6f,.4f,k->fin);sk_eye(k,9,side*5,.9f,true);}break;
    }
    }
}
static void draw_lagoon_fish(ctx_t *c,const fish_t *f,float clock,bool asleep) {
    sk_t k;sk_setup(&k,c,f,clock,asleep,-27,17,.61f,1);
    draw_lagoon_species(&k,SP_FISH);
}
