#include "camera_math.h"
#include <math.h>
int chase_pivot_shift(const float f[3],const float c[3],float d,float out[3]){
    out[0]=out[1]=out[2]=0;
    if(!isfinite(d)||d<0||d>2)return 0;
    for(int i=0;i<3;i++)if(!isfinite(f[i])||!isfinite(c[i]))return 0;
    float fn=hypotf(f[0],f[2]),cn=hypotf(c[0],c[2]);
    if(fn<.5f||fn>1.1f||cn<.1f)return 0;
    float fx=f[0]/fn,fz=f[2]/fn,cx=c[0]/cn,cz=c[2]/cn;
    float dot=fx*cx+fz*cz;
    /* Let manual look-behind/sideways retain its native framing. */
    if(dot<.15f)return 0;
    float fade=fminf(1,(dot-.15f)/.35f);
    out[0]=d*(fx-cx)*fade;out[2]=d*(fz-cz)*fade;
    return 1;
}
