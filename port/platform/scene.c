/*
 * The 3-D scene as the software pass draws it, for the GPU's view of the world (M7). R3D's
 * producers hand over each face before the near clip (port/asm/r3d.c), actorrender_entity each
 * object (ACTRENDR.C), and the sinks the polygon each face became.
 *
 * For now it only checks what it is handed, when BAK_SCENE_CHECK is set. A face that reached its
 * sink whole (no near clip, no viewport clip, as many corners) must be its corners projected, in
 * order, with the zone's focal length and the viewport's centre; a clipped one must keep those of
 * its corners in front of the view plane that project inside the clip rectangle. Both within two
 * pixels: the original truncates its divisions, and LOD 2's long scaler drops bits (a pixel and a
 * bit off). And every corner, its level of detail's units made world units, must lie near its
 * object's origin (within twice the object's bounding half extent), which a wrong unit would
 * break. The counts and the largest deviations go to stderr at exit.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "bak_port.h"

#define TOLERANCE 2.0 /* pixels */

static int s_capturing = -1; /* not looked up yet */
static struct {
    double x, y, z, extent;
    int unitShift;
    bool known;
} s_object;
static BakSceneFace s_face;
static bool s_facePending;
static struct {
    unsigned long long faces, whole, partly, mismatched, unchecked, strayCorners;
} s_counts;
static double s_worst[4]; /* the largest deviation within the tolerance, by level of detail */
static int s_shown;       /* mismatches described so far */

static void report(void) {
    fprintf(stderr,
            "scene: %llu faces; %llu drawn whole and %llu clipped compared with their polygons, "
            "%llu of them more than two pixels off; %llu clipped with no corner to compare; %llu "
            "with a corner far from their object; the largest deviation by LOD: %.2f %.2f %.2f "
            "%.2f\n",
            s_counts.faces, s_counts.whole, s_counts.partly, s_counts.mismatched,
            s_counts.unchecked, s_counts.strayCorners, s_worst[0], s_worst[1], s_worst[2],
            s_worst[3]);
}

bool bak_scene_capturing(void) {
    if (s_capturing < 0) {
        const char *env = SDL_getenv("BAK_SCENE_CHECK");
        s_capturing = env != NULL && env[0] != '\0' && env[0] != '0';
        if (s_capturing)
            atexit(report);
    }
    return s_capturing == 1;
}

void bak_scene_object(int32_t x, int32_t y, int32_t z, int32_t halfExtent, int unitShift) {
    if (!bak_scene_capturing())
        return;
    s_object.x = x;
    s_object.y = y;
    s_object.z = z;
    s_object.extent = halfExtent;
    s_object.unitShift = unitShift;
    s_object.known = true;
}

void bak_scene_face(const BakSceneFace *face) {
    if (!bak_scene_capturing())
        return;
    s_face = *face;
    s_facePending = true;
    s_counts.faces++;
    if (!s_object.known)
        return;
    double scale = ldexp(1.0, s_object.unitShift);
    for (unsigned i = 0; i < face->corners; i++) {
        double dx = face->cam[i][0] * scale - s_object.x;
        double dy = face->cam[i][1] * scale - s_object.y;
        double dz = face->cam[i][2] * scale - s_object.z;
        /* The extent bounds the mesh about its origin; the corners' units round them. */
        if (sqrt(dx * dx + dy * dy + dz * dz) > 2.0 * s_object.extent + 2.0 * scale + 16.0) {
            s_counts.strayCorners++;
            break;
        }
    }
}

static void mismatch(const BakSceneFace *f, unsigned i, double px, double py, unsigned n,
                     const int16_t *xs, const int16_t *ys) {
    s_counts.mismatched++;
    if (s_shown++ >= 8)
        return;
    fprintf(stderr,
            "scene: a LOD %u face (%s%s) of %u corners, corner %u (%ld, %ld, %ld) projects to "
            "(%.1f, %.1f); its polygon:",
            f->lod, f->nearClipped ? "near-clipped" : "in front",
            f->viewportClipped ? ", viewport-clipped" : "", f->corners, i, (long)f->cam[i][0],
            (long)f->cam[i][1], (long)f->cam[i][2], px, py);
    for (unsigned k = 0; k < n && k < 10; k++)
        fprintf(stderr, " (%d, %d)", xs[k], ys[k]);
    fprintf(stderr, "\n");
}

/* How far a projected corner is from a polygon corner, in pixels (the larger axis). */
static double deviation(double px, double py, int16_t x, int16_t y) {
    return SDL_max(fabs(px - x), fabs(py - y));
}

void bak_scene_polygon(unsigned n, const int16_t *xs, const int16_t *ys) {
    if (!bak_scene_capturing() || !s_facePending)
        return;
    s_facePending = false;
    const BakSceneFace *f = &s_face;
    double focal = ldexp(1.0, f->screenShift);
    bool whole = !f->nearClipped && !f->viewportClipped && n == f->corners;
    unsigned checked = 0;
    for (unsigned i = 0; i < f->corners; i++) {
        if (!(f->frontMask & 1u << i) || f->cam[i][1] <= 0)
            continue;
        double depth = f->cam[i][1];
        double px = f->centerX + f->cam[i][0] * focal / depth;
        double py = f->centerY - f->cam[i][2] * focal / depth;
        double off;
        if (whole) {
            off = deviation(px, py, xs[i], ys[i]);
        } else {
            if (px < f->clip[0] + 1 || px > f->clip[2] - 1 || py < f->clip[1] + 1 ||
                py > f->clip[3] - 1)
                continue;
            off = 1e9;
            for (unsigned k = 0; k < n; k++)
                off = SDL_min(off, deviation(px, py, xs[k], ys[k]));
        }
        if (off > TOLERANCE) {
            mismatch(f, i, px, py, n, xs, ys);
            return;
        }
        if (off > s_worst[f->lod & 3])
            s_worst[f->lod & 3] = off;
        checked++;
    }
    if (whole)
        s_counts.whole++;
    else if (checked)
        s_counts.partly++;
    else
        s_counts.unchecked++;
}
