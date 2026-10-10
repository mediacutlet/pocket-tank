/* Blackwater (2026-10-10): the castle slot is a sunken MAYAN STEP PYRAMID. Redrawn the same
   day so it reads as one at tank scale (the first cut was dark basalt and came out a dim grey
   mound against the near-black water): pale limestone in five receding tiers with a bright
   3 px ledge along each top and a crisp shadow where the tier above meets it, dark joints in
   big 12 px blocks, one bold carved band per tier (pale squares in a dark field), a central
   stair of lit treads and dark risers bracketed by dark ramp balustrades, a small temple house
   on top (a doorway the water shows through, a slab roof, a pierced roof comb); moss in the
   lower joints, two cracks, two tumbled blocks on the sand. The vaulted passage at the bottom
   centre is the swim-through gate every castle shares: the same opening (|x|<26, vault radius
   26) in a ring of lit voussoirs. Same rule as the other castles: every solid pixel is painted
   in both passes, the holes' dark only behind the fish (!front). */
#define BWC_TIERS 5
static const int8_t bwc_tier_bot[BWC_TIERS + 1] = { 0, 30, 52, 70, 86, 100 };   /* each tier's base row; [5] = the temple's floor */
static const int8_t bwc_tier_hw[BWC_TIERS]      = { 86, 71, 56, 42, 29 };         /* half-width at the base of the face */
/* the face's half-width on row h (a slight batter: 1 px in 8) */
static inline int bwc_hw(int i, int h) { return bwc_tier_hw[i] - (h - bwc_tier_bot[i]) / 8; }
static void draw_blackwater_castle(ctx_t *c,int cx,int front,bool final) {
    cst_t k={c,cx,front,final};
    for(int y=CASTLE_FY-145;y<=CASTLE_FY+3;y++) {
        if(y<c->oy||y>=c->oy+c->h)continue;
        int h=CASTLE_FY-y;
        int left=c->ox-cx;
        if(left< -92)left=-92;
        int right=c->ox+c->w-1-cx;
        if(right>92)right=92;
        for(int x=left;x<=right;x++) {
            int ax=abs(x);
            uint32_t hs=chash(x,h);
            /* the gate: the opening every castle shares, its ring of lit stone */
            bool opening=ax<26&&h>=0&&(h<24||x*x+(h-24)*(h-24)<26*26);
            bool ring=!opening&&ax<30&&h>=0&&(h<24||x*x+(h-24)*(h-24)<30*30);
            /* the temple house, its doorway, the roof slab and the pierced comb */
            bool temple=h>=100&&h<120&&ax<17;
            bool door=temple&&ax<5&&h>=102&&h<115;
            bool slab=h>=120&&h<124&&ax<19;
            bool comb=h>=124&&h<140&&ax<(h<134?13:9);
            bool slot=comb&&h>=127&&h<137&&ax>=4&&ax<8;
            /* the tiers: which face, or which ledge, owns the pixel (the top tier first, so a
               ledge shows only outside the tier above it; faces reach 3 rows under the next) */
            int tier=-1; bool ledge=false;
            for(int i=BWC_TIERS-1;i>=0;i--) {
                int bot=bwc_tier_bot[i], top=bwc_tier_bot[i+1];
                if(h<bot-3||h>=top)continue;
                int hw=bwc_hw(i,h<bot?bot:h);
                if(ax>=hw)continue;
                tier=i; ledge=h>=top-3;
                if(ledge&&i==BWC_TIERS-1&&ax<17)ledge=false;                     /* the temple stands on the top tier */
                break;
            }
            /* the stair up the front and its ramps, over the tiers from the gate's ring to the temple */
            bool stair=ax<=12&&h>=54&&h<100, ramp=ax>12&&ax<=16&&h>=54&&h<100;
            /* the footing in the sand and the two tumbled blocks */
            bool footing=h>=-3&&h<0&&ax<91;
            bool block_a=x>=-92&&x<-78&&h>=0&&h<8, block_b=x>=72&&x<86&&h>=0&&h<(x<79?10:7);
            bool hole=opening||door||slot;
            if(hole){if(!front)castle_put(&k,cx+x,y,rgb565(mix(water_rgb(y),0x05090c,.55f),c->dim));continue;}
            if(!(ring||temple||slab||comb||tier>=0||stair||ramp||footing||block_a||block_b))continue;
            uint32_t rgb;
            /* limestone: 7 px courses of 12 px blocks, dark joints between, three pale tones by hash */
            int course=h/7; int bx=x+400+((course&1)?6:0);
            bool mortar=(h%7)==0||(bx%12)==0;
            uint32_t blk=(chash(course,bx/12)%5); uint32_t stone=blk<2?0x9c988c:blk<4?0x8e8a7e:0xa8a498;
            if(block_a||block_b) {
                rgb=(block_a?(h>=6):(h>=(x<79?8:5)))?0xd0ccc0:(hs%4)?0x9c988c:0x8e8a7e;   /* a lit top on each tumbled block */
                if(block_a&&(x==-92||x==-79||h==0))rgb=0x3a3832;
                if(block_b&&(x==72||x==85||x==78||h==0))rgb=0x3a3832;
            }
            else if(footing) rgb=(hs%3)?0x2a2824:0x3a3832;
            else if(ring) rgb=((ax<28&&h<52)||h<2)?0x3a3832:(((x+h)/4)&1)?0xd0ccc0:0xa8a498;   /* the vault's ring, lit voussoirs over a dark inner edge */
            else if(comb) {
                rgb=((h%4)==0)?0x3a3832:0x9c988c;
                if(ax>=(h<134?11:7))rgb=0x5a564e;
                if(h==139||h==133)rgb=0xe0dcd0;
            }
            else if(slab) rgb=h==123?0xe8e4d8:h==120?0x3a3832:0x5a564e;
            else if(temple) {
                rgb=mortar?0x4a473f:stone;
                if(ax>=5&&ax<7&&h>=102&&h<116)rgb=0xd0ccc0;                             /* the door jambs */
                if(h>=115&&h<117&&ax<7)rgb=0xe0dcd0;                                    /* the lintel */
                if(x<-14)rgb=mix(rgb,0xe8e4d8,.35f);
                if(x>14)rgb=mix(rgb,0x1a1917,.45f);
            }
            else if(stair) {
                int step=(h-54)%4;
                rgb=step==0?0x2a2824:step==1?0x8e8a7e:step==2?0xd0ccc0:0xe0dcd0;        /* a lit tread, a dark riser */
                if(ax==12)rgb=0x2a2824;
            }
            else if(ramp) {
                rgb=((h%7)==0)?0x3a3832:0x5a564e;
                if(ax==16)rgb=x<0?0xb4b0a4:0x2a2824;
                if(ax==13)rgb=0x3a3832;
            }
            else if(ledge) {
                int hw=bwc_hw(tier,h);
                rgb=(ax==hw-1)?0xa8a498:(h==bwc_tier_bot[tier+1]-1)?0xe8e4d8:0xd0ccc0;   /* the lit top face of the tier */
                if(tier<BWC_TIERS-1&&ax<bwc_hw(tier+1,bwc_tier_bot[tier+1])+3)rgb=0x6e6a60;   /* the shadow the tier above casts on it */
                if(x>0&&ax>=hw-2)rgb=0x7a766c;
            }
            else {
                int hw=bwc_hw(tier,h), bot=bwc_tier_bot[tier];
                rgb=mortar?0x4a473f:stone;
                /* the carved band, rows 5..11 above the tier's base: pale squares in a dark field */
                int gb=h-bot-5;
                if(gb>=0&&gb<7) {
                    int gx=(x+400)%8;
                    if(gb==0||gb==6)rgb=0x2e2c28;
                    else rgb=(gb>=2&&gb<=4&&gx>=1&&gx<=4)?0xc6c2b6:0x5a564e;
                }
                /* moss in the lower joints, the sun on the left edges, shade on the right */
                if(h<44&&mortar&&(hs%100)<(unsigned)(60-h))rgb=(hs>>8)&1?0x447a38:0x2f5c2c;
                else if(h<18&&(hs%100)<(unsigned)(14-h/2))rgb=0x3a6a30;
                if(x<0&&ax>=hw-2)rgb=mix(rgb,0xe8e4d8,.5f);
                if(x>0&&ax>=hw-2)rgb=mix(rgb,0x100f0e,.55f);
                if(x<-20&&!mortar)rgb=mix(rgb,0xe8e4d8,.08f);
                /* three cracks: a pixel wide, wandering by hash */
                { int cxk=0, c0=0, c1=0;
                  if(h>=6&&h<48){cxk=-52+(h-6)/3+(int)(chash(h,1)&1u); c0=1;}
                  if(h>=56&&h<84){cxk=38-(h-56)/4-(int)(chash(h,2)&1u); c1=1;}
                  if((c0||c1)&&x==cxk)rgb=0x2a2824;
                  if(h>=10&&h<28&&x==24+(h-10)/2+(int)(chash(h,3)&1u))rgb=0x2a2824; }
                if((hs%37)==0&&!mortar)rgb=mix(rgb,0xe8e4d8,.25f);
            }
            castle_put(&k,cx+x,y,rgb565(rgb,c->dim));
        }
    }
}
