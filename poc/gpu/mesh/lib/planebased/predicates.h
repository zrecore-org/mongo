// MP17 step 1: integer plane predicates on fixed-width integers (fixedint.h). Inputs are L-bit signed integer points, |x| <= v = 2^L - 1.
// Clean-room: from the maths only. No division anywhere.
//
// Plane through a, b, c:   n = (b-a) x (c-a),  d = -(n . a);   the plane is { x : n.x + d = 0 }, positive side n.x + d > 0.
// Point side:              sign(n.p + d).
// Vertex of planes P0,P1,P2 (the point x with N x = -dvec, N = rows n0,n1,n2) against a fourth plane Q = (q, dq):
//   Cramer: x = X / W with W = det N (3x3 of the normals). Then q.x + dq = (q.X + dq W) / W.
//   q.X + dq W equals the 4x4 determinant Num of the matrix with rows (n0,d0), (n1,d1), (n2,d2), (q,dq):
//     det [ N d ; q dq ] = W * (dq - q N^-1 d) = W * (dq + q.x).
//   So side(vertex wrt Q) = sign(Num) * sign(W): the sign correction by the 3x3 determinant sign. W == 0 means the three planes do not meet
//   in a single point (returns 2). Num == 0 with W != 0 means the vertex lies on Q (returns 0).
//   The sign of the answer does not depend on the orientation of P0..P2 (flipping a plane flips both Num and W).
//   Evaluation: Laplace expansion of the 4x4 along its last row; the 3x3 minors of rows 0-2 share the six 2x2 minors of rows 1-2:
//     6 x 2 + 4 x 3 + 4 = 28 multiplications (W is the minor that excludes column 3, so it comes free).
//
// Bit growth, derived by me for |coordinate| <= v = 2^L - 1 (magnitude bounds from the simple coefficient count; "bits" = floor(log2 bound)+1;
// a signed two's-complement type needs one more bit):
//   edge e = b-a                      |e| <= 2v                         L+1
//   n component (cross of 2 edges)    2 products of 2v*2v:   8v^2         2L+3   (tighter: 4v^2, twice a triangle area in a 2v x 2v square; 2L+2)
//   d = -(n.a)                        3 products n*v:        24v^3        3L+5   (tighter: |d| = |det(a,b,c)| <= 3 sqrt3 v^3, 3L+3; the naive
//                                                                               evaluation order n.a has partial sums up to 24v^3)
//   2x2 minor, two n columns          2*(8v^2)^2 = 128 v^4               4L+7
//   2x2 minor, one n and the d column 2*8v^2*24v^3 = 384 v^5             5L+9
//   3x3 W (three n columns)           3*8v^2*128v^4 = 3072 v^6           6L+12
//   3x3 with the d column             2*8v^2*384v^5 + 24v^3*128v^4 = 9216 v^7   7L+14
//   one term q_j * M_j                8v^2 * 9216 v^7 = 24v^3 * 3072 v^6 = 73728 v^9   9L+17
//   Num (four terms)                  294912 v^9 = 2^18.17 v^9           9L+19 magnitude, 9L+20 signed
//   point side n.p+d                  3*8v^2*v + 24v^3 = 48 v^3          3L+6 magnitude, 3L+7 signed
// For v = 2^L - 1 the constant 2^18.17 is exact to the digit shown: Num magnitude bits = ceil(18.17 + 9 log2 v) = 9L+19 for large L (9L+18.17 rounds
// up to a whole number of bits). Minimum signed widths: L=26 -> 254 (fits 256), L=40 -> 380 (fits 384), L=53 -> 497 (fits 512). Survey's 9L+18.6 is the
// octree-BSP bound with their eq. (7)-(9) constants; mine is the same order and differs by the constant only.
// Overrides (harness only): planes built as small integer combinations of planes add about 3.2 bits to q and are checked separately.
#pragma once
#include "fixedint.h"

namespace pb {

template <int N> struct Plane {
    FixedInt<N> c[4];   // n_x, n_y, n_z, d
};

template <int N, bool TRIM> inline FixedInt<N> M(const FixedInt<N>& a, const FixedInt<N>& b) {
    if (TRIM) return mulMag(a, b);
    return mul(a, b);
}

template <int N, bool TRIM> inline Plane<N> planeFromPoints(const int64_t* a, const int64_t* b, const int64_t* c) {
    using F = FixedInt<N>;
    F e1[3], e2[3], A[3];
    for (int k = 0; k < 3; k++) {
        e1[k] = F::fromI64(b[k] - a[k]);
        e2[k] = F::fromI64(c[k] - a[k]);
        A[k] = F::fromI64(a[k]);
    }
    Plane<N> p;
    p.c[0] = sub(M<N, TRIM>(e1[1], e2[2]), M<N, TRIM>(e1[2], e2[1]));
    p.c[1] = sub(M<N, TRIM>(e1[2], e2[0]), M<N, TRIM>(e1[0], e2[2]));
    p.c[2] = sub(M<N, TRIM>(e1[0], e2[1]), M<N, TRIM>(e1[1], e2[0]));
    F dot = add(add(M<N, TRIM>(p.c[0], A[0]), M<N, TRIM>(p.c[1], A[1])), M<N, TRIM>(p.c[2], A[2]));
    p.c[3] = MUT(10) ? dot : neg(dot);
    return p;
}

// n.p + d for an integer point p. Optional value output.
template <int N, bool TRIM> inline int pointSide(const Plane<N>& pl, const int64_t* p, FixedInt<N>* valueOut = nullptr) {
    using F = FixedInt<N>;
    F v = add(add(add(M<N, TRIM>(pl.c[0], F::fromI64(p[0])), M<N, TRIM>(pl.c[1], F::fromI64(p[1]))), M<N, TRIM>(pl.c[2], F::fromI64(p[2]))), pl.c[3]);
    if (valueOut) *valueOut = v;
    return v.sign();
}

// Side of the vertex P0^P1^P2 with respect to Q: +1, -1, 0 (on Q), 2 (the three planes do not meet in one point: W == 0).
template <int N, bool TRIM> inline int vertexSide(const Plane<N>& P0, const Plane<N>& P1, const Plane<N>& P2, const Plane<N>& Q,
                                                  FixedInt<N>* Wout = nullptr, FixedInt<N>* Numout = nullptr) {
    using F = FixedInt<N>;
    const F *A0 = P0.c, *A1 = P1.c, *A2 = P2.c, *q = Q.c;
    // 2x2 minors of rows 1 and 2, columns (c,d)
    F m01 = sub(M<N, TRIM>(A1[0], A2[1]), M<N, TRIM>(A1[1], A2[0]));
    F m02 = sub(M<N, TRIM>(A1[0], A2[2]), M<N, TRIM>(A1[2], A2[0]));
    F m03 = sub(M<N, TRIM>(A1[0], A2[3]), M<N, TRIM>(A1[3], A2[0]));
    F m12 = sub(M<N, TRIM>(A1[1], A2[2]), M<N, TRIM>(A1[2], A2[1]));
    F m13 = sub(M<N, TRIM>(A1[1], A2[3]), M<N, TRIM>(A1[3], A2[1]));
    F m23 = sub(M<N, TRIM>(A1[2], A2[3]), M<N, TRIM>(A1[3], A2[2]));
    // 3x3 minors of rows 0..2 excluding column j, expanded along row 0
    F M0 = add(sub(M<N, TRIM>(A0[1], m23), M<N, TRIM>(A0[2], m13)), M<N, TRIM>(A0[3], m12));
    F M1 = add(sub(M<N, TRIM>(A0[0], m23), M<N, TRIM>(A0[2], m03)), M<N, TRIM>(A0[3], m02));
    F M2 = add(sub(M<N, TRIM>(A0[0], m13), M<N, TRIM>(A0[1], m03)), M<N, TRIM>(A0[3], m01));
    F W = add(sub(M<N, TRIM>(A0[0], m12), M<N, TRIM>(A0[1], m02)), M<N, TRIM>(A0[2], m01));
    // Laplace along row 3: signs (-1)^(3+j) = -, +, -, +
    F t0 = M<N, TRIM>(q[0], M0), t1 = M<N, TRIM>(q[1], M1), t2 = M<N, TRIM>(q[2], M2), t3 = M<N, TRIM>(q[3], W);
    F Num;
    if (MUT(8)) Num = sub(add(t3, t0), add(t1, t2));          // wrong sign on the j = 1 cofactor
    else Num = sub(add(t1, t3), add(t0, t2));
    if (Wout) *Wout = W;
    if (Numout) *Numout = Num;
    int sw = W.sign();
    if (sw == 0) return 2;
    int s = Num.sign() * sw;
    return MUT(7) ? -s : s;
}

}  // namespace pb
