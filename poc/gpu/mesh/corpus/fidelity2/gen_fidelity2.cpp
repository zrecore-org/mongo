// MP24b acceptance families (written after the pipeline v2 was frozen: lib/geogram/mp24b/FROZEN_SHA256.txt; fresh seed, not used by any earlier generator).
//   gen_fidelity2 <outdir>    writes <outdir>/ as ONE corpus in the G1 layout (manifest.tsv + adversarial/<id>_{A,B}.msh); the class is the second '_' field of the id (f2_<class>_...)
// Classes (PLAN.md "MP24b amended rule"):
//   cnear   smooth full-mantissa icospheres (L1, L2, L3, L3r = level 3 radius 1.1) at the origin and at "x1" (far in x only) / "x2" (far in x and y) offsets with bbox-centre / side ratio <= about 2^11.8:
//           NOT exactly re-centrable (nonzero c on the near axes needs more than 53 bits) so they take the c = 0 path; identical pairs have exact volumes, shifted pairs are scored by exact point tests
//   cfar    the same far configurations with ratio about 2^12.8 .. 2^30: the hard-limit class (REFUSE expected, reason "hard limit (far from the origin)"); x2 at ratio >= 2^25 is where c = 0 fails (MP24b dev sweep)
//   cfarrc  full-mantissa spheres far from the origin on all axes (offsets 2^14 .. 2^41): exactly re-centrable, must PASS through the re-centred path
//   nest    nested pairs at container half-widths 2^15 .. 2^40 (random boxes, inner box at the bottom of a container of half-width 31 * 2^(k-5) (just under the 2^19 scaling bound x 2^(k-19)), nested octahedron, nested sphere); exact volumes except the sphere
//   off     unit features at offsets 2^0 .. 2^50 (uniform and odd per-axis vectors; overlap, nested, edge touch, sliver shift; face and vertex touch every tenth t); exact volumes
//   thin    slivers, gaps, overlaps, shells, flush plates of width 2^-30 .. 2^-50 of the extent (extents 1, 2^12, 2^-10, 2^25), plus spheres cut by a plate; exact volumes (sphere: point tests)
//   touch   face, edge, vertex, coplanar, identical box contacts at scales 2^-20 .. 2^40; octahedra on a face, vertex to vertex, inscribed, half cut; curved vertex-on-face contact (exact volumes)
//   uflow   two pairs whose exact scaling would underflow a nonzero coordinate (REFUSE expected, reason "hard limit (underflow)")
// Every rnd() call is in its own statement; coordinates come only from + - * / sqrt and ldexp; build with -O2 -ffp-contract=off (corpus/fidelity2/build_gen_fidelity2.sh).
#define main gen_fidelity_main
#include "../fidelity/gen_fidelity.cpp"
#undef main

static Mesh makeOcta(const double c[3], double r) {   // same triangulation as corpus/magnitude/gen_magnitude.cpp
    Mesh m; const double d[6][3] = {{r,0,0},{-r,0,0},{0,r,0},{0,-r,0},{0,0,r},{0,0,-r}};
    for (auto& p : d) m.addV(c[0] + p[0], c[1] + p[1], c[2] + p[2]);
    static const uint32_t T[8][3] = {{0,2,4},{0,5,2},{0,4,3},{0,3,5},{1,4,2},{1,2,5},{1,3,4},{1,5,3}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
static std::string idn(const char* cls, const std::string& rest) { return std::string("f2_") + cls + "_" + rest; }
// exact expectations from 6 x volumes: va, vb, inter
static void emitExact(const std::string& id, const char* cls, const std::string& tags, const Mesh& A, const Mesh& B, const Dyadic& va, const Dyadic& vb, const Dyadic& inter) {
    if (!mp21b::validate(A).empty() || !mp21b::validate(B).empty()) { g_invalid++; return; }
    std::string ex[5] = {toString(va), toString(vb), toString(subExact(addExact(va, vb), inter)), toString(inter), toString(subExact(va, inter))};
    emit(id, cls, tags, A, B, ex);
}
static Mesh scaledBy(const Mesh& m, double k) { Mesh r = m; for (double& x : r.v) x *= k; return r; }   // k a power of two: exact
static Box ubox(const long long lo[3], const long long hi[3], int e) { Box b; for (int k = 0; k < 3; k++) { b.lo[k] = std::ldexp((double)lo[k], e); b.hi[k] = std::ldexp((double)hi[k], e); } return b; }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: gen_fidelity2 <outdir>\n"); return 2; }
    g_s = 0x4D5032344246494Dull;   // "MP24BFIM", not used before
    std::string out = argv[1]; mkdirp(out);
    openCorpus(out);
    struct Shape { const char* n; int lvl; double r; };
    const Shape SH[4] = {{"L1", 1, 1.0}, {"L2", 2, 1.0}, {"L3", 3, 1.0}, {"L3r", 3, 1.1}};
    auto jstr = [](int j) { return std::string(j < 10 ? "0" : "") + std::to_string(j); };
    auto offsetFor = [](int mode, int j, double o[3]) {   // x1: far in x only; x2: far in x and y; all: far on all axes (exactly re-centrable)
        double s = std::ldexp(1.0, j + 1);
        if (mode == 1) { o[0] = s; o[1] = 0.3; o[2] = -0.2; } else if (mode == 2) { o[0] = s; o[1] = -0.625 * s; o[2] = -0.2; } else { o[0] = s; o[1] = -0.625 * s; o[2] = 0.5 * s; }
    };
    const double dS[3] = {0.125, 0.0625, -0.09375};
    // ---------------- cnear ----------------
    for (const Shape& sh : SH) {
        Mesh base = makeIcosphere(sh.lvl, sh.r);
        meshPair(idn("cnear", std::string(sh.n) + "_origin_ident"), "cnear", "valid,curved,identical", base, base, true);
        double d1[3] = {0.125 * sh.r, 0.0625, -0.09375}, d2[3] = {-0.25 * sh.r, 0.1875, 0.03125};
        meshPair(idn("cnear", std::string(sh.n) + "_origin_shiftA"), "cnear", "valid,curved,overlap", base, translated(base, d1), false);
        meshPair(idn("cnear", std::string(sh.n) + "_origin_shiftB"), "cnear", "valid,curved,overlap", base, translated(base, d2), false);
        for (int mode = 1; mode <= 2; mode++) for (int j = 0; j <= 12; j += 2) {
            double o[3]; offsetFor(mode, j, o); Mesh A = translated(base, o); double d[3] = {dS[0] * sh.r, dS[1], dS[2]};
            std::string r = std::string(sh.n) + "_x" + std::to_string(mode) + "_j" + jstr(j);
            meshPair(idn("cnear", r + "_ident"), "cnear", "valid,curved,identical", A, A, true);
            meshPair(idn("cnear", r + "_shift"), "cnear", "valid,curved,overlap", A, translated(A, d), false);
        }
    }
    // ---------------- cfar (hard limit expected) ----------------
    const int farJ[12] = {13, 15, 17, 19, 21, 23, 25, 26, 27, 28, 29, 30};
    for (const Shape& sh : SH) for (int mode = 1; mode <= 2; mode++) {
        if (mode == 1 && !(sh.lvl == 1 || sh.r != 1.0)) continue;   // x1: L1 and L3r only
        Mesh base = makeIcosphere(sh.lvl, sh.r);
        for (int j : farJ) {
            double o[3]; offsetFor(mode, j, o); Mesh A = translated(base, o); double d[3] = {dS[0] * sh.r, dS[1], dS[2]};
            meshPair(idn("cfar", std::string(sh.n) + "_x" + std::to_string(mode) + "_j" + jstr(j) + "_shift"), "cfar", "valid,curved,overlap,far", A, translated(A, d), false);
            if (sh.lvl == 3 && sh.r == 1.0 && (j == 21 || j == 30)) meshPair(idn("cfar", std::string(sh.n) + "_x" + std::to_string(mode) + "_j" + jstr(j) + "_ident"), "cfar", "valid,curved,identical,far", A, A, true);
        }
    }
    // ---------------- cfarrc (far, exactly re-centrable: must pass) ----------------
    for (int si = 1; si < 4; si += 2) { const Shape& sh = SH[si]; Mesh base = makeIcosphere(sh.lvl, sh.r);
        for (int j : {13, 17, 20, 25, 30, 35, 40}) {
            double o[3]; offsetFor(3, j, o); Mesh A = translated(base, o); double d[3] = {dS[0] * sh.r, dS[1], dS[2]};
            std::string r = std::string(sh.n) + "_j" + jstr(j);
            meshPair(idn("cfarrc", r + "_ident"), "cfarrc", "valid,curved,identical,far", A, A, true);
            meshPair(idn("cfarrc", r + "_shift"), "cfarrc", "valid,curved,overlap,far", A, translated(A, d), false);
        }
    }
    // ---------------- nest: container half-widths 2^15 .. 2^40 ----------------
    for (int k = 15; k <= 40; k++) {
        const int e = k - 21; const long long H = 1LL << 21; std::string ks = std::to_string(k);
        const long long Cl[3] = {-H, -H, -H}, Ch[3] = {H, H, H};
        for (int r = 0; r < 3; r++) {
            long long lo[3], hi[3];
            for (int a = 0; a < 3; a++) { uint32_t w = rnd(); long long hw = 1 + (long long)(w % 64); uint32_t c = rnd(); long long cc = ((long long)(c % (uint32_t)(H / 1024)) - H / 2048) * 1024; lo[a] = cc - hw; hi[a] = cc + hw; }
            boxPair(idn("nest", "k" + ks + "_rand" + std::to_string(r)), "nest", "valid,nested", ubox(Cl, Ch, e), ubox(lo, hi, e));
        }
        {   // inner box at the bottom of a container whose half-width is 31 * 2^16 units = 31 * 2^(k-5): the planted-bound trap (scale bound 2^19 leaves a span above 1e6)
            const long long h = 31LL << 16; const long long cl[3] = {-h, -h, -h}, ch[3] = {h, h, h}, il[3] = {-10, -10, -h + 100}, ih[3] = {10, 10, -h + 120};
            boxPair(idn("nest", "k" + ks + "_bottom"), "nest", "valid,nested,bottom", ubox(cl, ch, e), ubox(il, ih, e));
        }
        {   // inscribed octahedron (vertices on the face centres): union = box, intersection = octahedron
            const double c0[3] = {0, 0, 0}; Mesh O = makeOcta(c0, std::ldexp((double)H, e)); Mesh Bx = makeBox(ubox(Cl, Ch, e));
            Dyadic va = exactVolumeTimes6(Bx), vo = exactVolumeTimes6(O); emitExact(idn("nest", "k" + ks + "_octa"), "nest", "valid,nested,touch", Bx, O, va, vo, vo);
        }
        {   // sphere (full mantissa) of radius half the half-width nested in the container: point tests
            Mesh S = makeIcosphere(2, std::ldexp((double)H / 2, e)); Mesh Bx = makeBox(ubox(Cl, Ch, e));
            meshPair(idn("nest", "k" + ks + "_sphere"), "nest", "valid,nested,curved", Bx, S, false);
        }
    }
    // ---------------- off: unit features at offsets 2^0 .. 2^50 ----------------
    {
        const int tpls[6] = {7, 5, 3, 13, 1, 4};
        for (int t = 0; t <= 50; t++) {
            double P = std::ldexp(1.0, t); double o[3] = {P, P, P};
            if (t % 2 == 1) { o[0] = P + 1; o[1] = -(P + 3); o[2] = P + 7; }
            double u = 0; for (int k = 0; k < 3; k++) u = std::max(u, ulpOf(o[k]));
            double E = std::max(1.0, 8 * u);
            for (int ti = 0; ti < 6; ti++) {
                if (ti >= 4 && t % 10 != 0) continue;
                Box a, b; tpl(tpls[ti], E, u, a, b); Box ta = a, tb = b; bool exact = true;
                for (int k = 0; k < 3; k++) {
                    ta.lo[k] = a.lo[k] + o[k]; ta.hi[k] = a.hi[k] + o[k]; tb.lo[k] = b.lo[k] + o[k]; tb.hi[k] = b.hi[k] + o[k];
                    if (twoSumErr(a.lo[k], o[k]) != 0 || twoSumErr(a.hi[k], o[k]) != 0 || twoSumErr(b.lo[k], o[k]) != 0 || twoSumErr(b.hi[k], o[k]) != 0) exact = false;
                }
                if (!exact) { g_skipped++; continue; }
                boxPair(idn("off", "t" + jstr(t) + "_" + kTplName[tpls[ti]]), "off", std::string("valid,offset,") + kTplName[tpls[ti]], ta, tb);
            }
        }
    }
    // ---------------- thin: widths 2^-30 .. 2^-50 of the extent ----------------
    {
        const double Es[4] = {1.0, 4096.0, std::ldexp(1.0, -10), std::ldexp(1.0, 25)}; const char* en[4] = {"E1", "E4096", "Em10", "E2p25"}; const int ks[5] = {30, 35, 40, 45, 50};
        const int thinT[7] = {11, 12, 15, 16, 13, 17, 100}; const char* thinName[7] = {"sliver_inside", "sliver_crossing", "gap_w", "overlap_w", "shift_w", "shell_w", "flush_plate"};
        for (int ei = 0; ei < 4; ei++) for (int ki = 0; ki < 5; ki++) for (int ti = 0; ti < 7; ti++) {
            double E = Es[ei], w = std::ldexp(E, -ks[ki]); Box a, b; int t = thinT[ti];
            if (t < 100) tpl(t, E, w, a, b); else { a = {{0,0,0},{E,E,E}}; b = {{0,0,0},{E,E,w}}; }
            boxPair(idn("thin", std::string(thinName[ti]) + "_" + en[ei] + "_k" + std::to_string(ks[ki])), "thin", std::string("valid,thin,") + thinName[ti], a, b);
        }
        for (int si = 1; si < 3; si++) for (int ei = 0; ei < 4; ei += 3) for (int ki = 0; ki < 5; ki += 2) {   // sphere cut by a plate of width w (L2, L3)
            double E = Es[ei], w = std::ldexp(E, -ks[ki]); Mesh S = makeIcosphere(SH[si].lvl, E); Box pl = {{-2*E, -2*E, 0}, {2*E, 2*E, w}};
            meshPair(idn("thin", std::string("sphere_plate_") + SH[si].n + "_" + en[ei] + "_k" + std::to_string(ks[ki])), "thin", "valid,thin,curved,sliver", S, makeBox(pl), false);
        }
    }
    // ---------------- touch ----------------
    {
        const int es[4] = {-20, 0, 22, 40}; const int tt[7] = {0, 1, 2, 3, 4, 9, 10};
        for (int e : es) {
            double E = std::ldexp(1.0, 20 + e);   // 2^20 units of 2^e
            for (int t : tt) { Box a, b; tpl(t, E, 0, a, b); boxPair(idn("touch", std::string(kTplName[t]) + "_e" + std::to_string(e)), "touch", std::string("valid,touch,") + kTplName[t], a, b); }
            const double H = std::ldexp(1.0, 20 + e); const double c0[3] = {0, 0, 0};
            const long long Cl[3] = {-(1LL << 20), -(1LL << 20), -(1LL << 20)}, Ch[3] = {1LL << 20, 1LL << 20, 1LL << 20};
            Mesh Bx = makeBox(ubox(Cl, Ch, e)); Dyadic vb = exactVolumeTimes6(Bx); std::string es_ = std::to_string(e);
            { Mesh O = makeOcta(c0, H); Dyadic vo = exactVolumeTimes6(O); emitExact(idn("touch", "octa_inscribed_e" + es_), "touch", "valid,touch,nested,curved", Bx, O, vb, vo, vo); }
            { double c[3] = {0, 0, 2 * H}; Mesh O = makeOcta(c, H); Dyadic vo = exactVolumeTimes6(O); Dyadic z{BigInt(0), 0}; emitExact(idn("touch", "octa_on_face_e" + es_), "touch", "valid,touch,vertex_face", Bx, O, vb, vo, z); }
            { double c[3] = {2 * H, 0, 0}; Mesh O1 = makeOcta(c0, H), O2 = makeOcta(c, H); Dyadic vo = exactVolumeTimes6(O1); Dyadic z{BigInt(0), 0}; emitExact(idn("touch", "octa_vertex_vertex_e" + es_), "touch", "valid,touch,vertex_vertex", O1, O2, vo, vo, z); }
            { const long long hl[3] = {-(2LL << 20), -(2LL << 20), -(2LL << 20)}, hh[3] = {0, 2LL << 20, 2LL << 20}; Mesh Hb = makeBox(ubox(hl, hh, e)); Mesh O = makeOcta(c0, H);
              Dyadic vh = exactVolumeTimes6(Hb), vo = exactVolumeTimes6(O); Dyadic half{vo.num / 2, vo.exp2}; emitExact(idn("touch", "octa_half_cut_e" + es_), "touch", "valid,touch,plane_through", Hb, O, vh, vo, half); }
        }
        for (int si = 1; si < 4; si++) for (int ax = 0; ax < 3; ax++) {   // curved vertex-on-face: box face plane at the maximum coordinate of the sphere along the axis
            Mesh S = makeIcosphere(SH[si].lvl, SH[si].r); double zmax = -1e300; for (size_t i = 0; i < S.nv(); i++) zmax = std::max(zmax, S.v[3 * i + ax]);
            Box bx = {{-2, -2, -2}, {2, 2, 2}}; bx.lo[ax] = zmax; bx.hi[ax] = zmax + 2; Mesh Bx = makeBox(bx);
            Dyadic va = exactVolumeTimes6(S), vb = exactVolumeTimes6(Bx); Dyadic z{BigInt(0), 0};
            emitExact(idn("touch", std::string("curved_vertex_face_") + SH[si].n + "_ax" + std::to_string(ax)), "touch", "valid,touch,vertex_face,curved", S, Bx, va, vb, z);
        }
    }
    // ---------------- uflow: scaling by 2^-s would underflow the sliver coordinate ----------------
    {
        double E = std::ldexp(1.0, 500), w = std::ldexp(1.0, -600); Box a = {{0,0,0},{E,E,E}}, b = {{0,0,0},{E,E,w}};
        boxPair(idn("uflow", "E2p500_w2m600"), "uflow", "valid,uflow", a, b);
        E = std::ldexp(1.0, 1000); w = std::ldexp(1.0, -100); a = {{0,0,0},{E,E,E}}; b = {{0,0,w},{E,E,E}}; boxPair(idn("uflow", "E2p1000_w2m100"), "uflow", "valid,uflow", a, b);
    }
    closeCorpus("fidelity2");
    return 0;
}
