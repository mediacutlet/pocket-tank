/* Approved jellyfish study (docs/design/jellyfish): an upright pulsing bell,
 * no bitmap in the live tank. f->jet is shared with the native locomotion so
 * the contraction, the rise and the thrust agree (tank.c, the jellyfish branch).
 * The creature's own saved colours are the inputs in every theme: body = the
 * bell, fin = the arms, accent = the markings. The themes change the material
 * (flat / translucent / scalloped), never the design the keeper bought. */
#define JELLY_CENTRE 6.0f   /* bell + arms span ~ -16..+28: drawn about f->y's middle (rings, cards, rows) */
static void draw_jellyfish(sk_t *source) {
    if(theme_active()==THEME_BLACKWATER){draw_blackwater_species(source,SP_JELLYFISH);return;}   /* blackwater.inc centres it itself */
    sk_t local=*source,*k=&local;
    bool quiet=theme_active()==THEME_QUIET_LAGOON,club=theme_active()==THEME_TIDEPOOL_CLUB;
    bool fry=k->f->stage==STAGE_FRY,elder=k->f->stage==STAGE_ELDER;
    /* Radial bell: a horizontal turn flips the eye's side, it never thins to a sliver. */
    k->yh=k->yt=k->f->facing<0?-1:1;
    k->y-=JELLY_CENTRE*k->s;
    float pulse=.5f+.5f*fast_sin(k->f->jet*TAU),sx=1-.17f*pulse,sy=1+.10f*pulse;
    float trail=.5f+.5f*fast_sin(k->f->jet*TAU-1.1f);         /* the arms follow the bell with a lag */
    float length=elder?1.16f:fry?.74f:1;
    uint32_t body=k->body,fin=k->fin,accent=k->acc;
    /* the theme's material, lightly: the design's hue stays readable (a rose jelly is rose) */
    if(quiet){body=mix(body,0xa9c6b8,.22f);fin=mix(fin,0xc4dccd,.3f);}
    if(club){body=mix(body,0xf1c6a4,.2f);}
    uint32_t rim=quiet?mix(body,0xeef1dc,.6f):club?mix(body,0xfff0d0,.62f):mix(body,0xffffff,.5f);
    int arms=quiet?(fry?6:10):club?3:fry?3:4;
    for(int i=0;i<arms;i++) {
        float x=-12+i*24.f/(arms-1),w=fast_sin(k->t*(k->asleep?.5f:1.8f)+i*.8f)*2.3f;
        float end=((quiet?29:club?25:20)+(i%3)*2)*length;
        float spread=1+(.5f-trail)*.3f;                        /* relaxed bell: the arms drift apart */
        float X[5]={x*sx,x+w,(x-w*.7f)*spread,(x+w*.9f)*spread,(x+w*.3f)*spread};
        float Y[5]={1,8,14,end-3,end};
        float R[5]={club?1.4f:quiet?.3f:1.15f,club?1.2f:quiet?.25f:.9f,club?1:quiet?.22f:.6f,.4f,.15f};
        if(quiet){for(int j=0;j<4;j++){
            float ax,ay,bx,by;sk_pt(k,X[j],Y[j],&ax,&ay);sk_pt(k,X[j+1],Y[j+1],&bx,&by);
            int n=(int)(fabsf(bx-ax)+fabsf(by-ay))+1;
            for(int q=0;q<=n;q++)px_blend(k->c,(int)(ax+(bx-ax)*q/n),(int)(ay+(by-ay)*q/n),fin,135);
        }}else if(club)lagoon_limb(k,X,Y,R,5,fin);
        else sk_tube(k,X,Y,R,5,fin);
    }
    if(quiet&&!fry)for(int side=-1;side<=1;side+=2){              /* the moon jelly's two folded oral arms */
        float w=fast_sin(k->t*1.7f+side)*2;
        float X[6]={side*4,side*5+w,side*2,side*4-w,side*3,side*5};
        float Y[6]={1,7,13,18,22,25};float R[6]={1.1f,.9f,.8f,.7f,.45f,.2f};
        sk_tube(k,X,Y,R,6,mix(fin,0xe4dec0,.45f));
    }
    float X[16],Y[16];int n=0;
    /* Smooth sampled dome for the modern themes; a deliberately simple polygon for Original. */
    int steps=quiet||club?12:8;
    for(int i=0;i<=steps;i++){float a=3.14159f+i*3.14159f/steps;X[n]=17*sx*fast_cos(a);Y[n++]=2+17*sy*fast_sin(a);}
    X[n]=11*sx;Y[n++]=5;X[n]=-11*sx;Y[n++]=5;
    if(quiet){
        sk_poly(k,X,Y,n,body,155);
        /* Nested, translucent highlights preserve water behind the moon jelly. */
        sk_dot(k,-3,-6,10*sx,7*sy,mix(body,0xe8efd8,.6f),55,true);
        sk_dot(k,-5,-8,5*sx,3*sy,0xf0e8ce,55,true);
    }else if(club){
        lagoon_shape(k,X,Y,n,body);
        /* the scalloped hem: a cream crescent under each lobe of the bell */
        for(int i=0;i<6;i++){float x=(-14+i*5.6f)*sx;sk_dot(k,x,5.2f,3.1f,2.5f,rim,255,false);sk_dot(k,x,3.8f,2.9f,2.1f,body,255,false);}
    }else sk_poly(k,X,Y,n,body,255);
    if(!club){float xx[5]={-16*sx,-9*sx,0,9*sx,16*sx},yy[5]={2,4,5,4,2},rr[5]={.5f,.65f,.7f,.65f,.5f};sk_tube(k,xx,yy,rr,5,rim);}
    if(quiet){
        if(!fry)for(int j=0;j<4;j++){                             /* the four petal gonads */
            float xx[9],yy[9],rr[9];
            for(int i=0;i<9;i++){float a=i*TAU/8,dx=2+2*fast_cos(a),dy=1.5f*fast_sin(a),angle=j*TAU/4;
                xx[i]=dx*fast_cos(angle)-dy*fast_sin(angle);yy[i]=-4+dx*fast_sin(angle)+dy*fast_cos(angle);rr[i]=.22f;}
            sk_tube(k,xx,yy,rr,9,mix(rim,accent,.3f));
        }
        sk_line(k,-10,-5,-7,-10,.4f,.35f,0xe5e7cf);
        if(elder)for(int i=0;i<7;i++)sk_dot(k,-12+i*4,3+.5f*(i%2),.4f,.4f,rim,150,false);
    }else if(club){
        if(!fry)for(int i=0;i<3;i++)sk_dot(k,-7+i*7,-8-(i==1?3:0),1.5f,1.3f,mix(accent,0x995333,.5f),230,true);
        sk_line(k,-12,-9,-5,-14,.9f,.7f,rim);                     /* the parasol's shine */
        sk_dot(k,-4,-2,1.1f,1.1f,0x36584b,255,false);sk_dot(k,4,-2,1.1f,1.1f,0x36584b,255,false);
    }else{
        if(!fry)for(int i=0;i<3;i++)sk_mark(k,-7+i*4,-4,1.3f,2.7f,accent,240);
        sk_eye(k,11,-4,1.9f,false);
    }
}
