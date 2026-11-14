// embree_runner: Embree v4.4.1 fit test. float32 internally (NOT exact): see REPORT.md.
//   embree_runner pip <mesh.msh> <points.txt>      0/1 per point on stdout; count and seconds on stderr
//   embree_runner closest <mesh.msh> <points.txt>  distance per point (%.17g) on stdout; seconds on stderr
#include <embree4/rtcore.h>
#include "mesh_io.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
using namespace std;

static vector<float> gV;          // float32 vertices (what Embree sees)
static vector<uint32_t> gT;
static long gHits = 0;

static void countFilter(const RTCFilterFunctionNArguments* a) { gHits++; a->valid[0] = 0; }   // count every hit, reject it

static bool closestCb(RTCPointQueryFunctionArguments* a) {
    const uint32_t id = a->primID;
    const float* p0 = &gV[3 * (size_t)gT[3 * id]]; const float* p1 = &gV[3 * (size_t)gT[3 * id + 1]]; const float* p2 = &gV[3 * (size_t)gT[3 * id + 2]];
    double P[3] = {a->query->x, a->query->y, a->query->z};
    double A[3], B[3], C[3];
    for (int k = 0; k < 3; k++) { A[k] = p0[k]; B[k] = p1[k]; C[k] = p2[k]; }
    // Ericson, Real-Time Collision Detection, closest point on triangle (double arithmetic on float32 vertices)
    auto sub = [](const double* a, const double* b, double* r) { for (int k = 0; k < 3; k++) r[k] = a[k] - b[k]; };
    auto dot = [](const double* a, const double* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
    double ab[3], ac[3], ap[3]; sub(B, A, ab); sub(C, A, ac); sub(P, A, ap);
    double d1 = dot(ab, ap), d2 = dot(ac, ap), q[3];
    auto setq = [&](double u, double v) { for (int k = 0; k < 3; k++) q[k] = A[k] + u * ab[k] + v * ac[k]; };
    double bp[3]; sub(P, B, bp); double d3 = dot(ab, bp), d4 = dot(ac, bp);
    double cp[3]; sub(P, C, cp); double d5 = dot(ab, cp), d6 = dot(ac, cp);
    double vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
    if (d1 <= 0 && d2 <= 0) setq(0, 0);
    else if (d3 >= 0 && d4 <= d3) { for (int k = 0; k < 3; k++) q[k] = B[k]; }
    else if (vc <= 0 && d1 >= 0 && d3 <= 0) setq(d1 / (d1 - d3), 0);
    else if (d6 >= 0 && d5 <= d6) { for (int k = 0; k < 3; k++) q[k] = C[k]; }
    else if (vb <= 0 && d2 >= 0 && d6 <= 0) setq(0, d2 / (d2 - d6));
    else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { double w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); for (int k = 0; k < 3; k++) q[k] = B[k] + w * (C[k] - B[k]); }
    else { double den = 1.0 / (va + vb + vc); setq(vb * den, vc * den); }
    double d[3]; sub(P, q, d); double dist = sqrt(dot(d, d));
    double* best = (double*)a->userPtr;
    if (dist < *best) { *best = dist; a->query->radius = (float)dist * 1.0000002f; return true; }   // shrink the search radius
    return false;
}

int main(int argc, char** argv) {
    if (argc != 4 || (strcmp(argv[1], "pip") && strcmp(argv[1], "closest"))) { fprintf(stderr, "usage: embree_runner pip|closest mesh.msh points.txt\n"); return 2; }
    bool pip = !strcmp(argv[1], "pip");
    mesh::Mesh m; if (!mesh::readMesh(argv[2], m)) { fprintf(stderr, "cannot read mesh\n"); return 1; }
    vector<double> pts; { FILE* f = fopen(argv[3], "r"); if (!f) return 1; double x, y, z; while (fscanf(f, "%lf %lf %lf", &x, &y, &z) == 3) { pts.push_back(x); pts.push_back(y); pts.push_back(z); } fclose(f); }
    size_t np = pts.size() / 3;
    gV.resize(m.v.size()); for (size_t i = 0; i < m.v.size(); i++) gV[i] = (float)m.v[i];   // float32 conversion: inexact
    gT = m.t;
    RTCDevice dev = rtcNewDevice("");
    RTCScene sc = rtcNewScene(dev);
    RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_TRIANGLE);
    float* vb = (float*)rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3 * sizeof(float), m.nv());
    memcpy(vb, gV.data(), gV.size() * 4);
    unsigned* ib = (unsigned*)rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(unsigned), m.nt());
    memcpy(ib, gT.data(), gT.size() * 4);
    if (pip) rtcSetGeometryIntersectFilterFunction(g, countFilter);
    else rtcSetGeometryPointQueryFunction(g, closestCb);
    rtcCommitGeometry(g); rtcAttachGeometry(sc, g); rtcReleaseGeometry(g);
    auto t0 = chrono::steady_clock::now();
    rtcCommitScene(sc);
    double tb = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    t0 = chrono::steady_clock::now();
    vector<double> out(np); long inside = 0;
    double dl = sqrt(1 + 0.001 * 0.001 + 0.0003 * 0.0003);
    float dir[3] = {(float)(1 / dl), (float)(0.001 / dl), (float)(0.0003 / dl)};
    for (size_t i = 0; i < np; i++) {
        if (pip) {
            RTCRayHit rh; memset(&rh, 0, sizeof rh);
            rh.ray.org_x = (float)pts[3*i]; rh.ray.org_y = (float)pts[3*i+1]; rh.ray.org_z = (float)pts[3*i+2];
            rh.ray.dir_x = dir[0]; rh.ray.dir_y = dir[1]; rh.ray.dir_z = dir[2];
            rh.ray.tnear = 0; rh.ray.tfar = numeric_limits<float>::infinity(); rh.ray.mask = -1;
            rh.hit.geomID = RTC_INVALID_GEOMETRY_ID; rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
            gHits = 0;
            rtcIntersect1(sc, &rh);       // the filter rejects every hit, so this one call visits all hits along the ray
            out[i] = (gHits & 1); inside += (gHits & 1);
        } else {
            double best = numeric_limits<double>::infinity();
            RTCPointQuery q; q.x = (float)pts[3*i]; q.y = (float)pts[3*i+1]; q.z = (float)pts[3*i+2]; q.radius = numeric_limits<float>::infinity(); q.time = 0;
            RTCPointQueryContext ctx; rtcInitPointQueryContext(&ctx);
            rtcPointQuery(sc, &q, &ctx, nullptr, &best);
            out[i] = best;
        }
    }
    double tq = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    for (size_t i = 0; i < np; i++) { if (pip) printf("%d\n", (int)out[i]); else printf("%.17g\n", out[i]); }
    if (pip) fprintf(stderr, "points %zu inside %ld seconds %.6f (bvh build %.6f)\n", np, inside, tq, tb);
    else fprintf(stderr, "points %zu seconds %.6f (bvh build %.6f)\n", np, tq, tb);
    rtcReleaseScene(sc); rtcReleaseDevice(dev);
}
