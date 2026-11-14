// MP22: replay of the ray +z from (-25,0,-3) against the tetrahedron of s4_star3_000 with Geogram's double* orient_3d_SOS in
// SOS_ADDRESS mode (the mode classify() runs in). The ray passes exactly through the tetra vertex V2=(-25,0,5).
// For every memory order (permutation) of the six points {V0..V3,q1,q2} it counts the facets the segment "hits". A closed tetra must give an even count.
#include <geogram/basic/common.h>
#include <geogram/numerics/predicates.h>
#include <algorithm>
#include <cstdio>
#include <map>
using namespace GEO;
static bool seg_tri(const double* q1,const double* q2,const double* p1,const double* p2,const double* p3){
    Sign o1=PCK::orient_3d_SOS(q1,p1,p2,p3), o2=PCK::orient_3d_SOS(q2,p1,p2,p3);
    if(o1==o2) return false;
    Sign s1=PCK::orient_3d_SOS(q1,q2,p1,p2), s2=PCK::orient_3d_SOS(q1,q2,p2,p3);
    if(s1*s2<0) return false;
    Sign s3=PCK::orient_3d_SOS(q1,q2,p3,p1);
    return (s2*s3>0 && s3*s1>0);
}
int main(){
    GEO::initialize();
    double P[6][3]={{-5,0,0},{0,0,0},{-25,0,5},{0,1,0},{-25,0,-3},{-25,0,1e6-3}}; // V0 V1 V2 V3 q1 q2
    int F[4][3]={{0,1,2},{3,1,0},{2,1,3},{2,3,0}};  // facets as in the Linux trace
    std::map<int,int> hist; int perm[6]={0,1,2,3,4,5}; int total=0;
    int bad_q_outside=0, tot_q_outside=0;
    do{
        double mem[6][3]; for(int i=0;i<6;i++) for(int k=0;k<3;k++) mem[i][k]=P[perm[i]][k]; // mem[slot] holds point perm[slot]
        const double* ad[6]; for(int i=0;i<6;i++) ad[perm[i]]=mem[i];
        int hits=0; for(auto&f:F) hits+=seg_tri(ad[4],ad[5],ad[f[0]],ad[f[1]],ad[f[2]]);
        hist[hits]++; total++;
        // realistic case: q1,q2 both below all tetra vertices or both above (stack vs heap)
        int pos4=0,pos5=0; for(int i=0;i<6;i++){ if(perm[i]==4)pos4=i; if(perm[i]==5)pos5=i; }
        bool below=(pos4<4&&pos5<4&&std::max(pos4,pos5)<=1), above=(std::min(pos4,pos5)>=4);
        if(below||above){ tot_q_outside++; if(hits%2) bad_q_outside++; }
    }while(std::next_permutation(perm,perm+6));
    {   // the two memory orders observed by the instrumented runs (ascending address; point ids: 0..3 = V0(-5,0,0) V1(0,0,0) V2(-25,0,5) V3(0,1,0), 4 = q1, 5 = q2)
        struct O{const char* name; int ord[6];} obs[2]={{"M1 (clang/libc++): V1<V0<V2<V3<q2<q1",{1,0,2,3,5,4}},{"Linux (gcc/libstdc++): V0<V2<V3<V1<q1<q2",{0,2,3,1,4,5}}};
        for(auto&o:obs){ double mem[6][3]; const double* ad[6]; for(int i=0;i<6;i++){ for(int k=0;k<3;k++) mem[i][k]=P[o.ord[i]][k]; ad[o.ord[i]]=mem[i]; }
            int hits=0; for(auto&f:F) hits+=seg_tri(ad[4],ad[5],ad[f[0]],ad[f[1]],ad[f[2]]); printf("observed order %s: %d hits\n",o.name,hits); }
    }
    printf("permutations %d; hit-count histogram:",total); for(auto&h:hist) printf(" %d hits: %d;",h.first,h.second); printf("\n");
    printf("q1,q2 both below or both above the four tetra vertices in memory: %d orders, %d with an ODD hit count\n",tot_q_outside,bad_q_outside);
}
