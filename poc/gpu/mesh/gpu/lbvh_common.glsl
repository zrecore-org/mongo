// MP10 shared definitions: node layout, box overlap, mutants.
// Boxes are 6 floats per box: lo.xyz, hi.xyz (outward-rounded binary32 of the binary64 triangle boxes; rounding is done on the host).
// Node ids: internal nodes 0..n-2, leaves n-1 .. 2n-2 (leaf k = node n-1+k, k = position in the Morton-sorted order).
#ifndef MUT
#define MUT 0     // planted mutants: 1 = strict overlap (touching boxes missed), 2 = no index tie-break for equal Morton codes, 3 = right child only after a left-child hit
#endif
bool boxOverlap(vec3 alo, vec3 ahi, vec3 blo, vec3 bhi) {
#if MUT == 1
    return all(lessThan(alo, bhi)) && all(lessThan(blo, ahi));
#else
    return all(lessThanEqual(alo, bhi)) && all(lessThanEqual(blo, ahi));
#endif
}
