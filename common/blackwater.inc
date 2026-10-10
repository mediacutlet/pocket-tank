/* Blackwater (2026-10-10): the realistic creatures. The same skeleton, polygons and tubes as
 * the lagoon's shaded art, drawn as the animals themselves: a fusiform fish with rayed,
 * translucent fins and a scale lattice, a hammerhead's cephalofoil, an octopus's suckers, a
 * lobster's plated abdomen. Lit by the sun from the upper left over near-black water: every
 * body is countershaded (a dark dorsal rim, a shine a third of the way down, a silver belly)
 * and darkens at its silhouette, so it reads as a rounded thing and not a card. The colours
 * come in vivid (theme_creature_color) and every shade here is mixed from k->body / fin / acc,
 * so the design the keeper bought keeps its hue. Cost as the lagoon's: a light ramp once a
 * shape, nine spans a row, no per-pixel trig, no allocation. */
#define BW_DARK 0x06121c   /* the shade: the water's own dark */
#define BW_SUN  0xfff1d2   /* the sunlight */
#define BW_PALE 0xdfe1d6   /* a belly's silver */
#define BW_JELLY_CENTRE 6.0f   /* as jellyfish.inc's JELLY_CENTRE (defined after this file) */

/* a countershaded, rim-darkened fill of a convex polygon in screen space: 16 tones top to bottom
   (dorsal rim, the shine, the flank, the silver belly, the ventral shadow), the outer spans of
   each row pulled toward the dark so the silhouette rounds off */
static void bw_poly(ctx_t *c,const float *x,const float *y,int n,uint32_t rgb) {
    float top=1e6f,bottom=-1e6f;
    for(int i=0;i<n;i++){top=fminf(top,y[i]);bottom=fmaxf(bottom,y[i]);}
    src_t lit[16],edge[16],rim[16];
    for(int i=0;i<16;i++) {
        float q=i/15.f;uint32_t t;
        if(q<.1f)t=mix(rgb,BW_DARK,.08f+.3f*(1-q/.1f));
        else if(q<.3f)t=mix(rgb,BW_SUN,.42f*(q-.1f)/.2f);
        else if(q<.6f)t=mix(rgb,BW_SUN,.42f*(1-(q-.3f)/.3f));
        else if(q<.9f)t=mix(rgb,BW_PALE,.5f*(q-.6f)/.3f);
        else t=mix(mix(rgb,BW_PALE,.5f),BW_DARK,.45f*(q-.9f)/.1f);
        lit[i]=src_color(t,c->dim);
        edge[i]=src_color(mix(t,BW_DARK,.22f),c->dim);
        rim[i]=src_color(mix(t,BW_DARK,.5f),c->dim);
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
        int start=(int)left,end=(int)right,width=end-start+1;
        if(width<=3){int s=(int)(clamp01(v)*15);span(c,start,end,yy,&edge[s],255);continue;}
        for(int j=0;j<9;j++) {
            int x0=start+width*j/9,x1=start+width*(j+1)/9-1;
            if(x1<x0)continue;
            float u=((x0+x1)*.5f-mid)/fmaxf(1,(right-left)*.5f);
            int s=(int)(clamp01(v+u*u*.08f)*15);
            const src_t *tone=(j==0||j==8)?&rim[s]:(j==1||j==7)?&edge[s]:&lit[s];
            span(c,x0,x1,yy,tone,255);
        }
    }
}
static void bw_shape(const sk_t *k,const float *x,const float *y,int n,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<n;i++)sk_pt(k,x[i],y[i],&X[i],&Y[i]);
    bw_poly(k->c,X,Y,n,rgb);
}
/* the body's thickness: a shaded cross-section that widens as the creature turns to the glass */
static void bw_volume(const sk_t *k,float x,float y,float depth,float ry,uint32_t rgb) {
    float w=fabsf(sk_yaw(k,x)),f=sqrtf(fmaxf(0,1-w*w));
    float RX=depth*f*k->s,RY=ry*k->s;if(RX<1)return;
    float X,Y;sk_pt(k,x,y,&X,&Y);
    float px[20],py[20];for(int i=0;i<20;i++){float a=i*TAU/20;px[i]=X+RX*fast_cos(a);py[i]=Y+RY*fast_sin(a);}
    bw_poly(k->c,px,py,20,rgb);
}
static void bw_body(const sk_t *k,float x,float y,float rx,float ry,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<20;i++){float a=i*TAU/20;X[i]=x+rx*fast_cos(a);Y[i]=y+ry*fast_sin(a);}
    bw_shape(k,X,Y,20,rgb);
    bw_volume(k,x,y,fminf(rx,ry)*.8f,ry*.95f,rgb);
}
/* a tube with an alpha (translucent fin rays, a jelly's tentacles) */
static void bw_tube_a(const sk_t *k,const float *lx,const float *ly,const float *r,int n,uint32_t rgb,int a) {
    float X[SK_TUBE_MAX],Y[SK_TUBE_MAX],R[SK_TUBE_MAX];
    for(int i=0;i<n;i++){sk_pt(k,lx[i],ly[i],&X[i],&Y[i]);R[i]=r[i]*k->s;if(R[i]<.5f)R[i]=.5f;}
    src_t s=src_color(rgb,k->c->dim);
    tube_s(k->c,X,Y,R,n,&s,a);
}
static void bw_line_a(const sk_t *k,float x0,float y0,float x1,float y1,float r0,float r1,uint32_t rgb,int a) {
    float lx[2]={x0,x1},ly[2]={y0,y1},r[2]={r0,r1};
    bw_tube_a(k,lx,ly,r,2,rgb,a);
}
/* a limb: a tube in the shade with a lit core along its upper side (an arm, a tail, a leg) */
static void bw_limb(const sk_t *k,const float *x,const float *y,const float *r,int n,uint32_t rgb) {
    sk_tube(k,x,y,r,n,mix(rgb,BW_DARK,.35f));
    float yy[SK_TUBE_MAX],rr[SK_TUBE_MAX];
    for(int i=0;i<n;i++){yy[i]=y[i]-r[i]*.3f;rr[i]=r[i]*.6f;}
    sk_tube(k,x,yy,rr,n,mix(rgb,BW_SUN,.22f));
}
/* a fin: a translucent membrane between its base (b0 -> b1) and its outer edge (o0 -> o1), the
   rays between them and a lighter edge. A base of one point is a fan (a tail's lobe). */
static void bw_fin(const sk_t *k,float bx0,float by0,float bx1,float by1,float ox0,float oy0,float ox1,float oy1,int rays) {
    float lx[4]={bx0,bx1,ox1,ox0},ly[4]={by0,by1,oy1,oy0};
    sk_poly(k,lx,ly,4,mix(k->fin,BW_SUN,.12f),150);
    uint32_t ray=mix(k->fin,BW_DARK,.4f);
    for(int j=0;j<rays;j++) {
        float u=rays>1?(float)j/(rays-1):.5f;
        bw_line_a(k,bx0+(bx1-bx0)*u,by0+(by1-by0)*u,ox0+(ox1-ox0)*u,oy0+(oy1-oy0)*u,.3f,.18f,ray,125);
    }
    bw_line_a(k,ox0,oy0,ox1,oy1,.25f,.25f,mix(k->fin,BW_SUN,.5f),95);
}
/* a real eye: the orbit's dark ring, a silver sclera, the iris, the pupil, the sun's glint;
   asleep, a lid */
static void bw_eye(const sk_t *k,float lx,float ly,float r,uint32_t iris) {
    float X,Y;sk_pt(k,lx,ly,&X,&Y);float R=r*k->s;if(R<1.4f)R=1.4f;
    if(k->asleep){fill_ellipse(k->c,X,Y,R,R*.3f+.4f,mix(k->body,BW_DARK,.45f),230);return;}
    float dir=sk_yaw(k,lx)<0?-1:1,g=fmaxf(.55f,R*.22f);
    fill_ellipse(k->c,X,Y,R,R,0x0a0c10,255);
    fill_ellipse(k->c,X,Y,R*.82f,R*.82f,0xe6e4d6,255);
    fill_ellipse(k->c,X+dir*R*.12f,Y,R*.62f,R*.62f,iris,255);
    fill_ellipse(k->c,X+dir*R*.2f,Y,R*.36f,R*.36f,0x05070a,255);
    fill_ellipse(k->c,X-R*.3f,Y-R*.32f,g,g,0xffffff,230);
}
/* a small all-dark eye with a glint (sharks, crustaceans), at a screen point */
static void bw_bead(const sk_t *k,float X,float Y,float r) {
    float R=r*k->s;if(R<1)R=1;
    if(k->asleep){fill_ellipse(k->c,X,Y,R,R*.4f,0x202830,220);return;}
    fill_ellipse(k->c,X,Y,R,R,0x05080b,255);
    fill_ellipse(k->c,X-R*.3f,Y-R*.3f,fmaxf(.5f,R*.3f),fmaxf(.5f,R*.3f),0xffffff,210);
}
/* a light of the creature's own (an esca, a firefly squid's photophores): drawn undimmed, so it
   still glows at night */
static void bw_glow(const sk_t *k,float lx,float ly,float r,uint32_t rgb,int a) {
    float X,Y;sk_pt(k,lx,ly,&X,&Y);float R=r*k->s;if(R<.6f)R=.6f;
    src_t s=src_color(rgb,1);
    fill_ellipse_s(k->c,X,Y,R,R,&s,a);
}
/* a scale lattice over the flank: staggered, faint, narrowing with the turn */
static void bw_scales(const sk_t *k,float x0,float x1,float y0,float y1,int cols,int rows,uint32_t rgb) {
    src_t s=src_color(rgb,k->c->dim);
    for(int r=0;r<rows;r++)for(int c=0;c<cols;c++) {
        float x=x0+(x1-x0)*(c+.5f+(r&1)*.5f)/cols,y=y0+(y1-y0)*(r+.5f)/rows;
        sk_dot_s(k,x,y,.95f,.8f,&s,55,true);
    }
}

static void draw_blackwater_species(sk_t *k,int species) {
    const fish_t *f=k->f;float t=k->t;int v=f->variant&3;
    bool grown=f->stage>=STAGE_ADULT,elder=f->stage==STAGE_ELDER;
    uint32_t body=k->body,dark=mix(k->body,BW_DARK,.5f),pale=mix(k->body,BW_PALE,.55f);
    if(species==SP_OCTOPUS&&f->camo>0) {
        body=mix(body,f->camo_rgb,clamp01(f->camo));
        dark=mix(body,BW_DARK,.5f);
        k->fade=1-clamp01(f->camo)*.8f;
    }
    switch(species) {
    case SP_FISH: {
        float tail=k->asleep?0:fast_sin(t*(8+f->speed*.055f)+f->wander)*(.32f+f->speed*.007f);
        float tl=elder?1.2f:1,wag=tail*6;
        /* the forked caudal: two lobes, rayed, wagging */
        bw_fin(k,-12,-1.6f,-12,0,-27*tl,-10+wag,-21*tl,-1+wag*.3f,4);
        bw_fin(k,-12,0,-12,1.6f,-21*tl,1+wag*.3f,-27*tl,10+wag,4);
        /* the dorsal along the back, taller with age; the anal and the pelvic underneath */
        float dh=elder?-15.5f:grown?-12.5f:-10;
        bw_fin(k,5,-6.3f,-8,-6.8f,1,dh,-11,dh+1.2f,5);
        bw_fin(k,-5,5.8f,-10,4.5f,-7,10,-12,8.5f,3);
        bw_fin(k,3,6.5f,0,7,1,10.5f,-3,10,2);
        /* the body: fusiform, countershaded */
        float x[14]={17,15,11,5,-2,-8,-12,-14,-12,-8,-2,5,11,15};
        float y[14]={0,-3,-5.5f,-7.3f,-7.6f,-6.5f,-4,0,4,6.5f,7.6f,7.3f,5.5f,3};
        bw_shape(k,x,y,14,body);bw_volume(k,0,0,5.5f,7.4f,body);
        bw_scales(k,-10,10,-5,5,5,3,dark);
        /* the lateral line, the operculum's curve, the mouth */
        float lx[4]={11,4,-4,-12},ly[4]={-1.5f,-.5f,0,-.5f},lr[4]={.3f,.3f,.3f,.3f};
        bw_tube_a(k,lx,ly,lr,4,dark,90);
        float gx[3]={10,8,10},gy[3]={-5,0,5},gr[3]={.35f,.35f,.35f};
        bw_tube_a(k,gx,gy,gr,3,dark,120);
        bw_line_a(k,17,.8f,14,1.5f,.3f,.3f,dark,170);
        /* the design's pattern (a juvenile on): bars, blotches or spots */
        for(int i=0;i<3;i++) {
            float px=-6+i*5;
            if(v==1)sk_mark(k,px,0,.9f,4.5f,k->acc,130);
            else if(v==2)sk_mark(k,px,-2,1.8f,1,k->acc,130);
            else sk_mark(k,px,(i%2)*3-1.5f,1.2f,1.2f,k->acc,130);
        }
        /* the pectoral behind the gill, then the eye */
        bw_fin(k,6,1.5f,5,4,-1,.5f,-3,6,3);
        bw_eye(k,11.5f,-2.3f,1.6f,mix(0xc8a048,k->acc,.25f));
        break;
    }
    case SP_SEAHORSE: {
        float sw=k->asleep?0:.8f*fast_sin(t*1.3f+f->wander);
        float fl=k->asleep?0:1.5f*fast_sin(t*9+f->jet*TAU);
        /* the prehensile tail, ringed; it grips a frond when anchored */
        static const float TLX[10]={-1.5f,-2.6f,-2.9f,-2.1f,0,3,5.2f,5.3f,3.6f,2.3f};
        static const float TLY[10]={6,10,14,17.5f,20,21,19.5f,17,16,17.2f};
        static const float ALX[10]={-1.5f,-2.6f,-2,.2f,3,5.2f,7.4f,7.2f,5.2f,3.4f};
        static const float ALY[10]={6,9.5f,12.8f,14.4f,13.4f,13,11,8.4f,7.5f,8.6f};
        static const float TLR[10]={3.1f,2.7f,2.3f,1.9f,1.6f,1.4f,1.2f,1,.85f,.7f};
        bool held=f->anchor>=0&&f->speed<6;
        float tx[10],ty[10];
        for(int i=0;i<10;i++){tx[i]=(held?ALX:TLX)[i]+(held?0:sw)*i/9.f;ty[i]=(held?ALY:TLY)[i];}
        bw_limb(k,tx,ty,TLR,10,body);
        for(int i=1;i<8;i++)sk_dot(k,tx[i],ty[i],TLR[i]*.95f,.4f,dark,120,true);
        /* the dorsal fan, fluttering; the little pectoral behind the head */
        bw_fin(k,-4.5f,-3,-4.8f,5,-9.5f+fl,-2,-10-fl,6,4);
        bw_fin(k,-2,-11.5f,-2.5f,-9,-5-fl*.5f,-12.5f,-5.5f+fl*.5f,-9,2);
        /* the trunk: a pot belly in bony rings, their ridges knobbed */
        float bx[10]={2.5f,5,5.8f,4.6f,2,-2,-4.8f,-5.6f,-5,-3.5f},by[10]={-9,-4,1,5.5f,8.6f,8.8f,6,1,-4,-9};
        bw_shape(k,bx,by,10,body);bw_volume(k,0,0,4,8.5f,body);
        for(int j=0;j<5;j++) {
            float yy=-6+j*3.4f;
            bw_line_a(k,-4.6f,yy,4.6f,yy+.4f,.3f,.3f,dark,110);
            sk_dot(k,-4.9f,yy,.7f,.55f,mix(body,BW_SUN,.5f),210,false);
            sk_dot(k,4.9f,yy+.4f,.6f,.5f,mix(body,BW_SUN,.5f),210,false);
        }
        /* the neck, the head, the tubular snout, the coronet */
        float nx[2]={-.5f,1},ny[2]={-8,-12.5f},nr[2]={3.1f,2.7f};
        bw_limb(k,nx,ny,nr,2,body);
        bw_body(k,1.2f,-14.5f,4.2f,3.6f,body);
        float sx[2]={4,11},sy[2]={-14.6f,-13.4f},sr[2]={1.6f,1};
        bw_limb(k,sx,sy,sr,2,body);
        sk_dot(k,11.3f,-13.4f,.6f,.9f,dark,220,false);
        sk_tri(k,-1.5f,-17.5f,-.5f,-21,.8f,-17.8f,k->fin,255);
        sk_tri(k,1,-17.8f,2.6f,-20.5f,3.4f,-17.2f,k->fin,255);
        sk_tri(k,-3,-13,-5.5f,-15,-3.5f,-11.5f,k->fin,255);
        /* the design: bands, speckles, white spots or spines */
        if(v==0)for(int j=0;j<3;j++)sk_mark(k,0,-5+j*5,4,.9f,k->acc,130);
        else if(v==1)for(int j=0;j<6;j++)sk_mark(k,-3+(j%3)*3,-6+(j/3)*6+(j%2),.5f,.5f,k->acc,170);
        else if(v==2)for(int j=0;j<5;j++)sk_mark(k,-2+(j%2)*4,-7+j*3.5f,.8f,.8f,0xf4f0e0,200);
        else for(int j=0;j<4;j++)sk_tri(k,-5.5f,-7+j*4,-8,-8.5f+j*4,-5,-5+j*4,mix(k->acc,BW_SUN,.3f),200);
        bw_eye(k,2.2f,-15,1.25f,mix(0x8a6a30,k->acc,.3f));
        break;
    }
    case SP_OCTOPUS: {
        float jk=k->pose;draw_ink(k);
        /* the head and the mantle's axis: up-back crawling, straight back jetting (the arms lead) */
        float hx=2.5f-1.5f*jk,hy=-2.5f+2.5f*jk;
        float ang=-2.3f-(TAU*.5f-2.3f)*jk,ax=fast_cos(ang+TAU),ay=fast_sin(ang+TAU);
        float mrx=7.8f+2.7f*jk,mry=6.8f-1.3f*jk;
        float jp=f->jet-floorf(f->jet),pulse=jp<.3f?fast_sin(jp*(TAU*.5f/.3f)):0;
        mry*=1-.12f*pulse*jk;
        float mcx=hx+ax*mrx*.85f,mcy=hy+ay*mrx*.85f;
        float rx0=hx+3*jk,ry0=hy+3.2f*(1-jk)+.5f*jk,floor_y=9+40*jk;
        /* eight arms: the near four in front, the far four behind, lying along the ground */
        static const float AC[8]={.15f,.95f,1.75f,2.65f,.55f,1.35f,2.2f,3};
        float AX[8][6],AY[8][6],AR[6];
        for(int j=0;j<6;j++)AR[j]=2.1f-1.65f*j/5.f;
        float open=jp<.3f?.1f:.1f+.9f*fast_sin((jp-.3f)*(TAU*.5f/.7f));
        float wob=k->asleep?0:1;
        for(int i=0;i<8;i++) {
            float aj=((i&3)-1.5f+(i>=4?.5f:0))*.07f*(.5f+1.5f*open);
            float ac=AC[i]+.22f*wob*fast_sin(t*(1.6f+f->speed*.04f)+i*1.9f);
            float a=ac+(aj-ac)*jk;
            float cc=(AC[i]<1.57f?-.3f:.3f)*(1-jk)+.08f*fast_sin(t*5+i*1.1f)*jk;
            float L=(19+5*jk)*(1+.08f*wob*fast_sin(t*1.7f+i*2.3f)),sl=L/5;
            float x=rx0+(i>=4?.8f:0),y=ry0-(i>=4?.6f:0);
            for(int j=0;j<6;j++) {
                AX[i][j]=x;AY[i][j]=y;
                x+=sl*fast_cos(a+TAU);y+=sl*fast_sin(a+TAU);
                if(y>floor_y){y=floor_y;a=fast_cos(a+TAU)>=0?.05f:TAU*.5f-.05f;}
                a+=cc*(.4f+j*.36f);
            }
        }
        for(int i=4;i<8;i++) {
            float fx[3]={AX[i][0],AX[i][2],AX[i][5]},fy[3]={AY[i][0],AY[i][2],AY[i][5]},fr[3]={AR[0],AR[2],AR[5]};
            sk_tube(k,fx,fy,fr,3,dark);
        }
        /* the mantle: an egg, fuller at its apex, its skin in papillae */
        float lx[16],ly[16];
        for(int i=0;i<16;i++) {
            float u=mrx*g_u16c[i],w=mry*g_u16s[i]*(1+.15f*g_u16c[i]);
            lx[i]=mcx+ax*u-ay*w;ly[i]=mcy+ay*u+ax*w;
        }
        bw_shape(k,lx,ly,16,body);
        {float X,Y;sk_pt(k,mcx,mcy,&X,&Y);if(k->zq*mry*k->s>=1){float px[20],py[20];for(int i=0;i<20;i++){float a=i*TAU/20;px[i]=X+k->zq*mry*k->s*fast_cos(a);py[i]=Y+mry*k->s*fast_sin(a);}bw_poly(k->c,px,py,20,body);}}
        for(int i=0;i<6;i++) {
            float u=(-.6f+i*.25f)*mrx,w=(i%2?-.45f:.2f)*mry;
            sk_dot(k,mcx+ax*u-ay*w,mcy+ay*u+ax*w,.55f,.45f,dark,70,false);
        }
        for(int i=0;i<3;i++){float u=(-.3f+i*.3f)*mrx,w=-.95f*mry;sk_dot(k,mcx+ax*u-ay*w,mcy+ay*u+ax*w,.5f,.5f,mix(body,BW_SUN,.5f),150,false);}
        /* the design: mottling, blue rings, a mimic's stripes, spots */
        for(int i=0;i<6;i++) {
            float u=(-.55f+(i%3)*.5f)*mrx,w=(i<3?-.35f:.35f)*mry,px=mcx+ax*u-ay*w,py=mcy+ay*u+ax*w;
            if(v==0)sk_mark(k,px,py,1.3f,1,mix(k->acc,dark,.5f),110);
            else if(v==1){sk_mark(k,px,py,1.3f,1.3f,k->acc,220);sk_mark(k,px,py,.6f,.6f,body,255);}
            else if(v==2)sk_mark(k,px,py,.7f,2.4f,mix(k->acc,0xf0ead8,.4f),170);
            else sk_mark(k,px,py,.7f,.7f,k->acc,180);
        }
        bw_body(k,hx,hy,5-.8f*jk,4.4f-.3f*jk,body);                    /* the head */
        sk_dot(k,rx0,ry0-.5f,4.2f,2.6f,mix(body,BW_DARK,.15f),255,false); /* the web */
        for(int i=0;i<4;i++)bw_limb(k,AX[i],AY[i],AR,6,body);           /* the near arms */
        if(jk<.6f) {                                                      /* a row of suckers along each */
            int a=(int)(200*(1-jk));uint32_t sc=mix(body,0xfff4e4,.6f);
            for(int i=0;i<4;i++)for(int j=1;j<5;j++) {
                sk_dot(k,AX[i][j],AY[i][j]+AR[j]*.55f,.45f,.4f,sc,a,false);
                sk_dot(k,(AX[i][j]+AX[i][j+1])*.5f,(AY[i][j]+AY[i][j+1])*.5f+AR[j]*.5f,.4f,.35f,sc,a,false);
            }
        }
        /* the eye: a bulge, a gold iris, the horizontal pupil */
        {
            float ex=hx+1.8f-1.6f*jk,ey=hy-2.8f;
            sk_dot(k,ex,ey,2.6f,2.3f,body,255,false);
            float X,Y;sk_pt(k,ex,ey,&X,&Y);float R=2*k->s;
            if(k->asleep)fill_ellipse(k->c,X,Y,R,R*.3f+.4f,dark,230);
            else{fill_ellipse(k->c,X,Y,R,R*.9f,0x0a0c10,255);fill_ellipse(k->c,X,Y,R*.8f,R*.72f,mix(0xd8b45c,k->acc,.2f),255);
                fill_ellipse(k->c,X,Y,R*.72f,R*.26f,0x05070a,255);fill_ellipse(k->c,X-R*.35f,Y-R*.4f,fmaxf(.55f,R*.2f),fmaxf(.55f,R*.2f),0xffffff,220);}
        }
        break;
    }
    case SP_SQUID: {
        float jk=k->pose,jp=f->jet-floorf(f->jet);draw_ink(k);
        float pulse=jp<.3f?fast_sin(jp*(TAU*.5f/.3f)):0,my=1-.12f*pulse*jk;
        float big=v==2?1.6f:1,wob=k->asleep?0:1;
        /* the fins: across the glass, triangular at the rear */
        for(int side=-1;side<=1;side+=2) {
            float rp=1.3f*wob*fast_sin(jp*TAU*2+t*3+side+TAU);
            float lz[5]={3.4f*my,(8.5f+rp)*big*(1-.45f*jk),(9.5f-rp)*big*(1-.45f*jk),1.2f,0};
            float lxx[5]={v==2?-4.f:-8.f,-11.5f+rp*.3f,-18,-22.5f,-23};
            float xs[5],ys[5];
            for(int i=0;i<5;i++)sk_pt3(k,lxx[i],0,side*lz[i],1,&xs[i],&ys[i]);
            src_t s=src_color(mix(k->fin,BW_SUN,.1f),k->c->dim);
            fill_poly_s(k->c,xs,ys,5,&s,200);
            for(int i=1;i<3;i++){float ex,ey;sk_pt3(k,lxx[i]*.9f-1,0,side*lz[i]*.75f,1,&ex,&ey);float X[2]={xs[0],ex},Y[2]={ys[0],ey},R[2]={.3f*k->s,.2f*k->s};src_t d=src_color(mix(k->fin,BW_DARK,.4f),k->c->dim);tube_s(k->c,X,Y,R,2,&d,120);}
        }
        /* the far arms and the far tentacle, behind the head */
        float ar[4]={1.2f,.95f,.7f,.45f};
        for(int i=0;i<6;i++) {
            float a=((i-2.5f)*.16f+.14f*wob*fast_sin(t*3+i*1.3f))*(1-jk*.85f);
            float x=9.5f,y=(-2.5f+i)*my*(1-.4f*jk),L=3.2f+.4f*(i&1);
            float px[4],py[4];
            for(int j=0;j<4;j++){px[j]=x;py[j]=y;x+=L*fast_cos(a+TAU);y+=L*fast_sin(a+TAU);a+=(i<3?-.08f:.08f)*(1-jk);}
            if(i==0||i==5)sk_tube(k,px,py,ar,4,dark);
            else bw_limb(k,px,py,ar,4,body);
            if(i>0&&i<5&&jk<.6f)for(int j=1;j<3;j++)sk_dot(k,px[j],py[j]+ar[j]*.5f,.35f,.3f,mix(body,0xfff4e4,.6f),150,false);
        }
        for(int q=0;q<2;q++) {                                            /* the two long tentacles, clubbed */
            float a=(q?.12f:-.1f)+.18f*wob*fast_sin(t*2.2f+q*2)*(1-jk),x=9.5f,y=(q?.8f:-.8f)*my,L=(20-11*jk)/5;
            float px[6],py[6],pr[6]={.7f,.6f,.5f,.45f,.4f,.4f};
            for(int j=0;j<6;j++){px[j]=x;py[j]=y;x+=L*fast_cos(a+TAU);y+=L*fast_sin(a+TAU);a+=.04f*(q?1:-1);}
            sk_tube(k,px,py,pr,6,mix(body,BW_DARK,.25f));
            sk_dot(k,px[5],py[5],1.3f,.75f,mix(body,BW_SUN,.15f),255,false);
        }
        /* the mantle: a torpedo, its tip at the rear; the siphon under the head */
        float x[12]={6,4,-2,-9,-16,-21,-23,-21,-16,-9,-2,4};
        float y[12]={0,-3.6f*my,-5*my,-5*my,-4*my,-2*my,0,2*my,4*my,5*my,5*my,3.6f*my};
        bw_shape(k,x,y,12,body);bw_volume(k,-6,0,4.5f,5*my,body);
        bw_line_a(k,4,3.5f*my,1,5.5f*my,.9f,.6f,dark,255);
        /* chromatophores: the design's dots over the mantle (a firefly's glow in the dark) */
        for(int i=0;i<8;i++) {
            float px=2-i*2.8f,py=(i%2?-2.2f:1.8f)*my;
            if(v==1&&k->marked){bw_glow(k,px,py,1.1f,0x9af4ff,k->c->dim<.8f?110:40);bw_glow(k,px,py,.5f,0xd8fcff,230);}
            else if(v==2)sk_mark(k,px,py,.5f,.5f,mix(k->acc,BW_PALE,.4f),140);
            else sk_mark(k,px,py,.7f,.6f,k->acc,170);
        }
        bw_body(k,7.5f,0,3.4f,3.6f*my,body);                             /* the head */
        bw_eye(k,6.5f,-.6f,2.1f,mix(0x1c2330,k->acc,.3f));
        break;
    }
    case SP_PUFFER: {
        float puff=clamp01(f->puff),rx=11+4*puff,ry=8.5f+6.5f*puff;
        float beat=k->asleep?0:fast_sin(t*7+f->wander),fl=k->asleep?0:2*fast_sin(t*10+f->wander);
        bw_fin(k,-rx+1.5f,-2.2f,-rx+1.5f,2.2f,-rx-7,-5.5f+beat*1.5f,-rx-7,5.5f+beat*1.5f,5);
        bw_fin(k,-3,-ry+1,-7,-ry+1.5f,-5,-ry-4+beat*.5f,-9,-ry-3,3);
        bw_fin(k,-3,ry-1,-7,ry-1.5f,-5,ry+4-beat*.5f,-9,ry+3,3);
        if(puff>.05f)for(int i=0;i<16;i++) {                              /* the spines stand out */
            float a=i*TAU/16,ca=fast_cos(a),sa=fast_sin(a);
            bw_line_a(k,ca*rx*.93f,sa*ry*.93f,ca*(rx+1+3.5f*puff),sa*(ry+1+3.5f*puff),.45f,.12f,mix(body,BW_SUN,.5f),235);
        }
        bw_body(k,0,0,rx,ry,body);
        sk_dot(k,1,ry*.5f,rx*.78f,ry*.45f,mix(body,0xf0ead8,.6f),110,false);   /* the pale belly */
        if(v==0)for(int i=0;i<7;i++)sk_mark(k,-8+(i%4)*4.5f,-ry*.55f+(i/4)*4,.9f,.8f,dark,180);
        else if(v==1)sk_mark(k,rx*.55f,-ry*.3f,3.6f,3.2f,mix(body,BW_DARK,.65f),190);
        else if(v==2)for(int i=0;i<2;i++)sk_mark(k,-6+i*7,-ry*.55f,1.7f,ry*.4f,mix(body,BW_DARK,.7f),200);
        for(int i=0;i<5;i++)sk_dot(k,-7+i*3.5f,-ry*.25f+(i%2),.5f,.4f,dark,60,true);
        bw_fin(k,3,0,3,3.5f,-2+fl*.3f,-1.5f,-3-fl*.3f,5,3);               /* the pectoral, sculling */
        sk_dot(k,rx-.4f,1.4f,1.5f,1.1f,0xf3ecd8,235,false);              /* the beak */
        bw_line_a(k,rx+.8f,1.4f,rx-2,1.5f,.3f,.3f,dark,210);
        bw_eye(k,rx*.55f,-ry*.33f,2.1f,mix(0x6a8a3a,k->acc,.3f));
        break;
    }
    case SP_ANGLER: {
        float beat=k->asleep?0:fast_sin(t*3+f->wander),gape=.3f+(f->sp_mode==SPM_LUNGE?.7f:0);
        uint32_t lure=v==0?0x8ff6ff:v==3?0xfff07a:mix(k->acc,0xffffff,.4f);
        bw_fin(k,-19,-2,-19,2,-25,-6.5f+beat,-25,6.5f+beat,4);
        bw_fin(k,-7,-7,-14,-4.5f,-9,-11.5f+beat*.4f,-16,-8.5f,3);
        bw_fin(k,-7,7,-14,4.5f,-9,11.5f,-16,8.5f,3);
        /* the head is most of it: the body tapers from a huge jaw to a small tail */
        float x[14]={18,15,9,1,-8,-15,-20,-20,-15,-8,-1,5,10,15};
        float y[14]={0,-5,-9.5f,-10.5f,-7.5f,-4,-2,2,4.5f,7,8.5f,8,5,2};
        bw_shape(k,x,y,14,body);bw_volume(k,2,-1,6,8.5f,body);
        /* the mouth: the upper jaw's line runs up to the nose; the cavity, the hinged lower jaw
           (dropped in a lunge), needle teeth on both */
        float mx[6]={5,10,16,17,10,5},myy[6]={8,5,2,3.5f+gape*3,7.5f+gape*2,9+gape};
        sk_poly(k,mx,myy,6,0x1a0a0e,235);
        float jx[6]={4,10,17,16.5f,9,3},jy[6]={8.5f,7+gape*2,3.5f+gape*4,6+gape*5,10.5f+gape*2,10.5f};
        sk_poly(k,jx,jy,6,mix(body,BW_DARK,.15f),255);
        for(int i=0;i<5;i++) {
            float tx=14.5f-i*2.1f,ty=2.3f+(15-tx)*.6f;
            sk_tri(k,tx-.35f,ty-.3f,tx+.35f,ty-.3f,tx-.2f,ty+2.3f,0xf6f2e4,240);
        }
        for(int i=0;i<4;i++) {
            float u=i/3.f,tx=11+u*5.5f,ty=7+gape*2+u*(gape*2-3.5f);
            sk_tri(k,tx-.35f,ty+.3f,tx+.35f,ty+.3f,tx+.2f,ty-2.2f,0xf6f2e4,240);
        }
        /* warts, blotches: the design's skin */
        if(v==1)for(int i=0;i<6;i++)sk_mark(k,-6+(i%3)*6,-6+(i/3)*5,.8f,.7f,mix(body,BW_SUN,.45f),170);
        else if(v==2)for(int i=0;i<5;i++)sk_mark(k,-10+i*5,(i%2?-3:3),1.8f,1.4f,mix(k->acc,dark,.4f),140);
        else for(int i=0;i<4;i++)sk_mark(k,-8+i*5,(i%2?-4:2),.6f,.6f,mix(body,BW_SUN,.3f),120);
        /* the pectoral lobe it walks on, the illicium and the esca's glow */
        float px[3]={0,-2,-3.5f},py[3]={6,9,11.5f},pr[3]={1.7f,1.4f,1.1f};
        bw_limb(k,px,py,pr,3,body);
        float ix[5]={3,3,5,8.5f,11},iy[5]={-10,-14,-17,-18.5f,-17+beat},ir[5]={.7f,.6f,.5f,.4f,.3f};
        bw_tube_a(k,ix,iy,ir,5,mix(k->fin,BW_DARK,.3f),255);
        bw_glow(k,11,-17+beat,4,lure,20+(int)(110*f->lure));
        bw_glow(k,11,-17+beat,2,lure,60+(int)(150*f->lure));
        bw_glow(k,11,-17+beat,1.1f,0xfff8dc,255);
        bw_eye(k,8,-4.5f,1.35f,mix(0x9a7a2a,k->acc,.3f));
        break;
    }
    case SP_EEL: {
        float x[16],y[16],r[16],uy[16],ur[16],ly[16],lr[16];
        for(int i=0;i<16;i++) {
            float u=i/15.f;x[i]=23-u*60;y[i]=fast_sin(u*7-t*(k->asleep?.5f:3)+TAU*64)*u*6;r[i]=4.1f*(1-u)+.4f;
            uy[i]=y[i]-r[i]*1.05f;ur[i]=r[i]*.55f+.4f;ly[i]=y[i]+r[i]*.95f;lr[i]=r[i]*.45f+.3f;
        }
        /* the fin ridge the length of the back and the belly, rayed */
        bw_tube_a(k,x,uy,ur,16,k->fin,215);
        bw_tube_a(k,x,ly,lr,16,k->fin,190);
        for(int i=1;i<15;i+=2)bw_line_a(k,x[i],y[i]-r[i]*.5f,x[i],y[i]-r[i]*1.35f,.2f,.15f,mix(k->fin,BW_DARK,.4f),120);
        bw_limb(k,x,y,r,16,body);
        float by[16],br[16];for(int i=0;i<16;i++){by[i]=y[i]+r[i]*.4f;br[i]=r[i]*.42f;}
        bw_tube_a(k,x,by,br,16,v==2?mix(body,0xf0d8a8,.4f):mix(k->acc,BW_PALE,.35f),130);   /* the belly's colour */
        if(v==3)for(int i=1;i<13;i+=2)sk_mark(k,x[i],y[i]-r[i]*.2f,.7f,.6f,mix(k->acc,BW_PALE,.3f),170);
        else if(v==0||v==1)for(int i=2;i<13;i+=3)sk_mark(k,x[i],y[i]-r[i]*.3f,1.2f,.5f,dark,110);
        /* the blunt head: a jaw line, the gill opening, a small eye */
        bw_body(k,21,0,6.5f,3.9f,body);
        bw_line_a(k,27,1,19,1.6f,.3f,.3f,dark,180);
        bw_line_a(k,16.5f,-1.5f,16,2.5f,.3f,.3f,dark,150);
        sk_dot(k,26,-1.2f,.4f,.4f,dark,180,false);
        bw_eye(k,24,-1.2f,1,0x3a3222);
        if(f->spark>0)for(int j=0;j<4;j++) {
            float xx=-25+j*12,sgn=j%2?1:-1,w=fast_sin(t*18+j)*2;
            sk_dot(k,xx+1.5f,sgn*9,4,4,0x9ad8ff,50,false);
            bw_line_a(k,xx,sgn*5,xx+3+w,sgn*9,.5f,.35f,0xd8f8ff,255);
            bw_line_a(k,xx+3+w,sgn*9,xx+1,sgn*13,.35f,.2f,0xeefcff,255);
        }
        break;
    }
    case SP_SHARK: {
        float beat=k->asleep?0:fast_sin(f->jet*TAU);
        uint32_t belly=v==3?mix(body,0xf4f6f0,.7f):0xe4e8e4;
        /* the heterocercal tail: the upper lobe the long one; the far pectoral behind */
        bw_fin(k,-21,-1.5f,-21,.5f,-34,-13+beat*2,-27,-2+beat,4);
        bw_fin(k,-21,.5f,-21,2,-27,beat,-29,6+beat*1.5f,3);
        sk_tri(k,5,2,-2,3,-6,8,mix(k->fin,BW_DARK,.4f),140);
        float x[14]={17,14,8,0,-8,-15,-20,-22,-20,-15,-8,0,8,14};
        float y[14]={-1,-3,-4.6f,-5.2f,-4.6f,-3.2f,-2,0,1.6f,2.8f,4,4.8f,4.4f,2.6f};
        bw_shape(k,x,y,14,body);bw_volume(k,-2,0,4.5f,4.8f,body);
        sk_dot(k,-2,3,13,1.8f,belly,150,true);                            /* the white belly */
        /* the first dorsal, tall and raked; the second; the pectoral; the pelvic */
        bw_fin(k,2,-4.8f,-8,-4.6f,-2,-15,-9,-8,4);
        bw_fin(k,-14,-3.2f,-18,-2.2f,-16,-6.5f,-19,-4.2f,2);
        bw_fin(k,6,3,1,4.2f,-3,10.5f,-8,8.5f,4);
        bw_fin(k,-10,4.2f,-14,3.2f,-12,7,-16,6,2);
        for(int i=0;i<5;i++)bw_line_a(k,9-i*1.3f,-2.2f,8.4f-i*1.3f,2.2f,.25f,.25f,dark,170);   /* five gill slits */
        bw_line_a(k,15,2.3f,11,2.8f,.3f,.3f,dark,160);                     /* the mouth, underslung */
        /* the cephalofoil: a flat lens across the glass, its lobes shown obliquely, an eye at each end */
        {
            static const float HX[9]={18.5f,18,16,13.5f,12,12,13.5f,16,18},HZ[9]={0,-7,-10,-9,-4,4,9,10,7};
            float X[9],Y[9];
            for(int i=0;i<9;i++)sk_pt3(k,HX[i],0,HZ[i],.3f,&X[i],&Y[i]);
            bw_poly(k->c,X,Y,9,body);
            float ex,ey;
            sk_pt3(k,16.5f,0,-9.5f,.3f,&ex,&ey);bw_bead(k,ex,ey,1);
            sk_pt3(k,16.5f,0,9.5f,.3f,&ex,&ey);bw_bead(k,ex,ey,.85f);
        }
        break;
    }
    case SP_SWORDFISH: {
        float beat=k->asleep?0:fast_sin(f->jet*TAU);
        uint32_t belly=mix(k->acc,0xf4f6f8,.5f),bill=mix(k->fin,BW_DARK,.45f);
        bw_fin(k,-19,-1,-19,1,-31,-12+beat*2.2f,-31,12+beat*1.6f,5);                 /* the lunate tail */
        sk_tri(k,6,2.5f,.5f,8.5f,3.5f,3,mix(k->fin,BW_DARK,.4f),140);               /* the far pectoral */
        float x[14]={16,13,7,0,-7,-14,-20,-21.5f,-20,-14,-7,0,7,13};
        float y[14]={-1.6f,-3.8f,-5.2f,-5.6f,-5,-3.6f,-1.8f,0,1.8f,3.4f,4.6f,4.9f,4.4f,3};
        bw_shape(k,x,y,14,body);bw_volume(k,0,0,4.4f,5.2f,body);
        sk_dot(k,-2,3.2f,13,1.6f,belly,150,true);                                    /* the silver belly */
        bw_fin(k,5,-4.6f,-10,-4.2f,-1,-18.5f,-8,-9,4);                               /* the tall sickle dorsal */
        bw_fin(k,-13.5f,-3,-18,-2.2f,-15.5f,-7,-19,-4.5f,2);
        bw_fin(k,-13,3,-17,2.2f,-15,6.2f,-18.5f,4.2f,2);
        bw_fin(k,6,3,1,4.2f,-3,10,-7,8,4);                                           /* the near pectoral */
        bw_line_a(k,12,-.5f,-17,.5f,.3f,.3f,mix(body,BW_DARK,.5f),150);              /* the lateral line */
        if(v==1||v==2)for(int i=0;i<5;i++)bw_line_a(k,9-i*5.5f,-4,8.5f-i*5.5f,2.5f,.5f,.4f,mix(k->acc,k->fin,.5f),90);   /* faint bars */
        sk_tri(k,-19,-1.3f,-22.5f,-1.3f,-19,1.3f,mix(k->fin,BW_DARK,.4f),255);       /* the keel */
        bw_line_a(k,15.5f,-1,33.5f,-.3f,1.2f,.2f,bill,255);                           /* the bill */
        bw_line_a(k,16,-1.7f,25,-1.3f,.35f,.15f,mix(k->acc,0xffffff,.5f),200);
        bw_line_a(k,15,2.3f,11.5f,2.6f,.3f,.3f,mix(body,BW_DARK,.55f),160);          /* the mouth */
        bw_eye(k,10.5f,-1.8f,1.6f,0x1a2a44);
        break;
    }
    case SP_CRAB: {
        /* face-on: the carapace keeps its width whichever way it sidles */
        sk_t crab=*k;crab.yh=crab.yh<0?-1:1;crab.yt=crab.yh;k=&crab;
        float ph=f->jet*TAU,mv=clamp01(f->speed/6),threat=clamp01(f->puff),raise=threat*6;
        float snap=k->asleep?0:.6f*fmaxf(0,fast_sin(t*2.5f+f->wander));
        uint32_t tip=v==1?k->acc:mix(body,BW_DARK,.3f);
        /* eight jointed legs, stepping */
        for(int side=-1;side<=1;side+=2)for(int i=0;i<4;i++) {
            float st=mv*1.6f*fast_sin(ph+i*1.6f+(side>0?0:2.5f)+TAU),lift=mv*fmaxf(0,fast_sin(ph+i*1.6f+1.5f+(side>0?0:2.5f)+TAU))*2;
            float lx[3]={side*(4+i*2.f),side*(10+i*2.2f)+st*.3f,side*(12+i*2.4f)+st},ly[3]={2+i*.5f,-2+i*2.5f-lift,8+i*.8f-lift*.4f},lr[3]={.9f,.65f,.3f};
            bw_limb(k,lx,ly,lr,3,k->fin);
            sk_dot(k,lx[1],ly[1],.7f,.7f,mix(k->fin,BW_DARK,.3f),255,false);
        }
        /* the claws, the near one the bigger; the threat raises them */
        for(int side=-1;side<=1;side+=2) {
            float big=side>0?1.15f:.9f,open=threat*1.5f+snap;
            float ax[3]={side*5.f,side*10.f,side*13.f},ay[3]={1,-1-raise*.5f,-3-raise},ar[3]={1.3f,1.1f,1};
            bw_limb(k,ax,ay,ar,3,body);
            bw_body(k,side*15.f,-4-raise,3.6f*big,2.5f*big,body);
            sk_tri(k,side*16.f,-5-raise,side*21.f,-6-raise,side*17.f,-3-raise,tip,255);
            sk_tri(k,side*16.f,-6-raise,side*20.f,-8.5f-raise-open,side*17.5f,-5.5f-raise,tip,255);
            sk_dot(k,side*15.f,-5-raise,.5f,.5f,dark,120,false);
        }
        /* the carapace, toothed along its front edge */
        float cx[11]={-11,-9,-5,0,5,9,11,9,4,-4,-9},cy[11]={0,-3.5f,-5.5f,-6,-5.5f,-3.5f,0,4,6.5f,6.5f,4};
        bw_shape(k,cx,cy,11,body);
        for(int i=0;i<3;i++)for(int side=-1;side<=1;side+=2){float tx=side*(5.5f+i*1.8f),ty=-5.2f+i*1.1f;sk_tri(k,tx-.6f,ty,tx+.6f,ty,tx+side*.3f,ty-1.6f,mix(body,BW_SUN,.3f),255);}
        bw_line_a(k,-6,-1.5f,6,-1.5f,.3f,.3f,mix(body,BW_SUN,.4f),80);
        for(int i=0;i<5;i++)sk_dot(k,-6+i*3,(i%2?2:-3),.5f,.45f,dark,60,false);
        if(v==2)for(int i=0;i<6;i++)sk_mark(k,-7+(i%3)*7,(i<3?-3:2.5f),.6f,.6f,k->acc,190);
        else if(v==3)for(int i=0;i<3;i++)sk_mark(k,-5+i*5,-2,1.2f,.7f,mix(k->acc,dark,.4f),110);
        /* the eye stalks, the mouthparts */
        for(int side=-1;side<=1;side+=2) {
            bw_line_a(k,side*3.f,-4.5f,side*4.f,-8,.6f,.55f,body,255);
            float X,Y;sk_pt(k,side*4.f,-8.6f,&X,&Y);bw_bead(k,X,Y,.95f);
        }
        sk_dot(k,-1.3f,5.8f,.7f,.5f,pale,200,false);sk_dot(k,1.3f,5.8f,.7f,.5f,pale,200,false);
        break;
    }
    case SP_LOBSTER: {
        float curl=f->sp_mode==SPM_FLIP&&f->jet>.5f?clamp01((f->jet-.5f)*8):0;
        float ph=t*(2+clamp01(f->speed/20)*7),mv=clamp01(f->speed/6),wob=k->asleep?0:1;
        uint32_t ant=v==0?k->acc:mix(body,k->fin,.5f),shade=mix(body,BW_DARK,.4f);
        /* the long antennae, sweeping back; the antennules */
        for(int a=0;a<2;a++) {
            float lx[7],ly[7],lr[7],ang=-.5f-a*.3f+.2f*wob*fast_sin(t*1.3f+a*2+f->wander),x=13,y=-3.5f;
            for(int j=0;j<7;j++){lx[j]=x;ly[j]=y;lr[j]=.55f-.04f*j;x+=4.2f*fast_cos(ang+TAU);y+=4.2f*fast_sin(ang+TAU);ang+=.16f+.03f*wob*fast_sin(t*2+j);}
            sk_tube(k,lx,ly,lr,7,a?shade:ant);
        }
        for(int a=0;a<2;a++)bw_line_a(k,14,-2.5f,19,-4+a*1.5f,.4f,.3f,k->fin,255);
        /* the far claw (the cutter) and the far legs, behind */
        {
            float ax[3]={8,14,19},ay[3]={-1,-3,-3.5f},ar[3]={1,.9f,.8f};
            sk_tube(k,ax,ay,ar,3,shade);
            sk_dot(k,22,-3.5f,3.2f,2,shade,255,false);
            sk_tri(k,24,-4,28.5f,-4.5f,24.5f,-2.5f,shade,255);sk_tri(k,24,-4.5f,28,-6.5f,25,-3.5f,shade,255);
        }
        for(int j=0;j<4;j++) {
            float st=mv*1.8f*fast_sin(ph+j*1.6f+2.4f+TAU),lift=mv*fmaxf(0,fast_sin(ph+j*1.6f+4+TAU))*1.4f,bx=9.5f-j*3.2f;
            float lx[3]={bx+.8f,bx+2.5f,bx+1.2f+st},ly[3]={2.5f,5-lift,7-lift*.6f},lr[3]={.7f,.55f,.4f};
            sk_tube(k,lx,ly,lr,3,shade);
        }
        /* the abdomen: six plates, each overlapping the one behind, curling under in a flip; the fan */
        float sx[6],sy[6];
        for(int i=0;i<6;i++){sx[i]=-6-i*3.2f*(1-.45f*curl);sy[i]=1+curl*i*i*.3f;}
        float fx=sx[5]-3.2f*(1-.45f*curl),fy=sy[5]+curl*6;
        for(int j=0;j<5;j++)sk_dot(k,fx-(j==2?1:0),fy+(j-2)*1.5f,3.2f,1.2f,mix(k->fin,BW_SUN,.15f),225,false);
        for(int i=5;i>=0;i--) {
            bw_body(k,sx[i],sy[i],3.8f,3.3f-i*.25f,body);
            bw_line_a(k,sx[i]+1.9f,sy[i]-2.6f+i*.2f,sx[i]+1.9f,sy[i]+2.4f-i*.2f,.3f,.3f,dark,150);
            if(i>0)sk_dot(k,sx[i],sy[i]+3-i*.25f,.6f,.5f,mix(body,BW_PALE,.4f),150,false);   /* a swimmeret */
            if(v==2)sk_mark(k,sx[i],sy[i]-1,.6f,.6f,mix(k->acc,BW_SUN,.3f),200);
            if(v==3&&i%2)sk_mark(k,sx[i],sy[i]+.5f,1.4f,1.6f,mix(k->acc,BW_PALE,.5f),160);
        }
        /* the cephalothorax with its rostrum and groove; the near legs */
        float cx[11]={12,11,6,0,-5,-6,-5,0,6,11,12.5f},cy[11]={-1.5f,-4.5f,-5.5f,-5.5f,-4.5f,0,4,5.5f,5.5f,4,1.5f};
        bw_shape(k,cx,cy,11,body);bw_volume(k,3,0,4,5,body);
        sk_tri(k,11.5f,-2.5f,17,-4.5f,12,-1,body,255);
        bw_line_a(k,2,-5,1,5,.3f,.3f,dark,120);
        for(int i=0;i<4;i++)sk_dot(k,8-i*3,-3.5f,.45f,.4f,mix(body,BW_SUN,.4f),140,false);
        if(v==3)sk_mark(k,3,0,2.2f,2,mix(k->acc,BW_PALE,.5f),150);
        for(int j=0;j<4;j++) {
            float st=mv*1.8f*fast_sin(ph+j*1.6f+.8f+TAU),lift=mv*fmaxf(0,fast_sin(ph+j*1.6f+2.4f+TAU))*1.4f,bx=9-j*3.2f;
            float lx[3]={bx,bx+1.5f+st*.5f,bx-.5f+st},ly[3]={3.5f,6.5f-lift,9.5f-lift*.6f},lr[3]={.75f,.55f,.4f};
            bw_limb(k,lx,ly,lr,3,k->fin);
        }
        /* the near claw: the crusher, knobbed, its dactyl opening and closing */
        {
            float snap=k->asleep?0:1.2f*fmaxf(0,fast_sin(t*2+f->wander));
            float ax[3]={8,14,18},ay[3]={2.5f,4.5f,4},ar[3]={1.3f,1.2f,1.1f};
            bw_limb(k,ax,ay,ar,3,body);
            bw_body(k,21.5f,3.2f,4.3f,2.8f,body);
            sk_tri(k,24,2.5f,29,2,25,4.8f,body,255);
            sk_tri(k,24,1.5f,28.5f,-1-snap,25.5f,2.8f,body,255);
            for(int i=0;i<3;i++)sk_dot(k,19.5f+i*1.6f,1.6f,.45f,.4f,dark,150,false);
        }
        bw_line_a(k,11,-3,13,-4.5f,.5f,.45f,body,255);
        {float X,Y;sk_pt(k,13.3f,-5,&X,&Y);bw_bead(k,X,Y,.9f);}
        break;
    }
    case SP_JELLYFISH: {
        sk_t local=*k;k=&local;
        k->yh=k->yt=f->facing<0?-1:1;
        k->y-=BW_JELLY_CENTRE*k->s;
        bool fry=f->stage==STAGE_FRY;
        float pulse=.5f+.5f*fast_sin(f->jet*TAU),sx=1-.17f*pulse,sy=1+.1f*pulse;
        float trail=.5f+.5f*fast_sin(f->jet*TAU-1.1f+TAU),length=elder?1.16f:fry?.74f:1,wob=k->asleep?0:1;
        uint32_t rim=mix(body,0xffffff,.55f),arm=k->fin;
        /* the four frilled oral arms (two on a fry) */
        int arms=fry?2:4;
        for(int i=0;i<arms;i++) {
            float x=(-7.5f+i*5)*(arms==2?1.6f:1)+(arms==2?1.25f:0),w=wob*fast_sin(t*1.8f+i*.8f)*2.3f;
            float end=(22+(i%2)*3)*length,spread=1+(.5f-trail)*.3f;
            float X[5]={x*sx,x+w,(x-w*.7f)*spread,(x+w*.9f)*spread,(x+w*.3f)*spread};
            float Y[5]={2,8,14,end-3,end},R[5]={1.3f,1.1f,.9f,.6f,.3f};
            bw_tube_a(k,X,Y,R,5,arm,190);
            for(int j=1;j<4;j++)sk_dot(k,X[j]+(j%2?1.3f:-1.3f),Y[j]+2,.9f,.6f,mix(arm,BW_SUN,.35f),150,false);
        }
        /* the tentacle fringe from the hem */
        int fringe=fry?6:12;
        for(int i=0;i<fringe;i++) {
            float x=(-15+i*30.f/(fringe-1))*sx,w=wob*fast_sin(t*1.4f+i*1.1f)*1.5f,len=(10+(i%3)*3)*length*(elder?1.3f:1);
            bw_line_a(k,x,4.5f,x+w,4.5f+len,.25f,.15f,mix(arm,0xffffff,.4f),115);
        }
        /* the bell: a translucent dome, its canals, the gonads, a scalloped hem, the lit rim */
        float X[16],Y[16];int n=0;
        for(int i=0;i<=12;i++){float a=TAU*.5f+i*TAU*.5f/12;X[n]=17*sx*fast_cos(a);Y[n++]=2+17*sy*fast_sin(a);}
        X[n]=11*sx;Y[n++]=5;X[n]=-11*sx;Y[n++]=5;
        sk_poly(k,X,Y,n,body,150);
        sk_dot(k,0,-4,12*sx,9*sy,mix(body,0xffffff,.25f),70,false);
        sk_dot(k,-4*sx,-8,5*sx,3.5f*sy,0xffffff,45,false);
        for(int s=-1;s<=1;s+=2){bw_line_a(k,s*3*sx,-10*sy,s*9*sx,3,.25f,.25f,rim,80);bw_line_a(k,s*1.5f*sx,-12.5f*sy,s*14*sx,1.5f,.25f,.25f,rim,80);}
        if(!fry){sk_dot(k,-4.5f*sx,-5,1.7f,1.2f,k->acc,120,false);sk_dot(k,4.5f*sx,-5,1.7f,1.2f,k->acc,120,false);
            sk_dot(k,-1.6f*sx,-8.5f,1.3f,1.5f,k->acc,120,false);sk_dot(k,1.6f*sx,-8.5f,1.3f,1.5f,k->acc,120,false);}
        for(int i=0;i<8;i++)sk_dot(k,(-14+i*4)*sx,4.6f,2.1f,1.4f,rim,200,false);
        {float xx[5]={-16*sx,-9*sx,0,9*sx,16*sx},yy[5]={2,4,5,4,2},rr[5]={.5f,.65f,.7f,.65f,.5f};bw_tube_a(k,xx,yy,rr,5,rim,230);}
        sk_dot(k,0,2.5f,15*sx,4,mix(body,0xffffff,.6f),35,false);          /* a faint glow inside the rim */
        break;
    }
    }
}
static void draw_blackwater_fish(ctx_t *c,const fish_t *f,float clock,bool asleep) {
    sk_t k;sk_setup(&k,c,f,clock,asleep,-27,17,.61f,1);
    draw_blackwater_species(&k,SP_FISH);
}
