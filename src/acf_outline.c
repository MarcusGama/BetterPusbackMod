/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2022 Saso Kiselkov. All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <acfutils/assert.h>
#include <acfutils/acf_file.h>
#include <acfutils/perf.h>
#include <acfutils/safe_alloc.h>

#include "xplane.h"
#include "acf_outline.h"

#define    READ_PROP(x, func, ...) \
    do { \
        char path[64]; \
        const char *str; \
        VERIFY3S(snprintf(path, sizeof (path), __VA_ARGS__), <=, \
            sizeof (path)); \
        str = acf_prop_find(acf, path); \
        if (str == NULL) { \
            logMsg(BP_ERROR_LOG "Error parsing acf file: property %s not found",\
                path); \
            goto errout; \
        } \
        x = func(str); \
    } while (0)

#define    READ_INT(x, ...)    READ_PROP(x, atoi, __VA_ARGS__)
#define    READ_FLOAT(x, ...)    READ_PROP(x, atof, __VA_ARGS__)
#define    READ_FEET(x, offset, ...) \
    do { \
        READ_FLOAT(x, __VA_ARGS__); \
        x = FEET2MET(x) - offset; \
    } while (0)

typedef enum {
    WING_OUTLINE_LEADING_EDGE,
    WING_OUTLINE_TRAILING_EDGE,
    WING_OUTLINE_FULL
} wing_outline_type_t;

/*
 * Reads the geometric shape of an aircraft part outline. This is used to
 * extract the fuselage shape. part_nbr denotes the part number to read and
 * s_dim is the number of longitudinal part rings. The resulting points are
 * stored in `pts' (must have space for at least s_dim points). Returns
 * B_TRUE if successful, B_FALSE otherwise.
 */
bool_t
part_outline_read(const acf_file_t *acf, const char *part_name, vect2_t *pts,
                  int s_dim, float z_ref) {
    int r_dim;
    double part_z;
    const char *prop_val;

    ASSERT(acf != NULL);

    if (acf_file_get_version(acf) >= 1200)
        prop_val = acf_prop_find(acf, "_body/0/_part_z");
    else
        prop_val = acf_prop_find(acf, "_part/56/_part_z");

    part_z = (prop_val != NULL ? FEET2MET(atof(prop_val)) : 0);
    z_ref -= part_z;
    READ_INT(r_dim, "_%s/_r_dim", part_name);

    for (int s = 0; s < s_dim; s++) {
        vect2_t p = VECT2(-1e10, 0);

        for (int r = 0; r < r_dim; r++) {
            vect2_t p2;
            READ_FEET(p2.x, 0, "_%s/_geo_xyz/%d,%d,0",
                      part_name, s, r);
            READ_FEET(p2.y, z_ref, "_%s/_geo_xyz/%d,%d,2",
                      part_name, s, r);
            if (p2.x > p.x && (s > 0 || p2.y < p.y) &&
                (s + 1 < s_dim || p2.y > p.y))
                p = p2;
        }
        pts[s] = p;
    }
    return (B_TRUE);
    errout:
    return (B_FALSE);
}

/*
 * Determines the outline of a wing segment. The part number of the wing
 * segment is wing_nbr. The resulting points (relative to z_ref) are stored
 * in pts. If tip_p is not NULL, it is filled with the location of the tip
 * chord center point.
 * This function can store 2 or 4 points depending on `type. If type is
 * WING_OUTLINE_FULL, all 4 outline points are stored (ordered as: leading
 * edge root, leading edge tip, trailing edge tip and trailing edge root).
 * If type is WING_OUTLINE_LEADING_EDGE or WING_OUTLINE_TRAILING_EDGE, only
 * the respective 2 edge points are stored (in the same order as above).
 */
bool_t
wing_seg_outline_read(const acf_file_t *acf, int wing_nbr, vect2_t pts[4],
                      vect2_t *tip_p, double z_ref, wing_outline_type_t type) {
    vect2_t root, tip;
    double sweep, semilen, dihed, root_chord, tip_chord;
    int i = 0;

    READ_FLOAT(sweep, "_wing/%d/_sweep_design", wing_nbr);
    READ_FEET(semilen, 0, "_wing/%d/_semilen_SEG", wing_nbr);
    READ_FLOAT(dihed, "_wing/%d/_dihed_design", wing_nbr);
    READ_FEET(root_chord, 0, "_wing/%d/_Croot", wing_nbr);
    READ_FEET(tip_chord, 0, "_wing/%d/_Ctip", wing_nbr);
    if (acf_file_get_version(acf) >= 1200) {
        READ_FEET(root.x, 0, "_wing/%d/_part_x", wing_nbr);
        READ_FEET(root.y, z_ref, "_wing/%d/_part_z", wing_nbr);
    } else {
        READ_FEET(root.x, 0, "_wing/%d/_crib_x_arm/0", wing_nbr);
        READ_FEET(root.y, z_ref, "_wing/%d/_crib_z_arm/0", wing_nbr);
    }

    tip = vect2_add(root, vect2_rot(VECT2(semilen, 0), -sweep));
    tip.x = (tip.x - root.x) * cos(DEG2RAD(dihed)) + root.x;
    if (tip_p != NULL)
        *tip_p = tip;

    if (type == WING_OUTLINE_LEADING_EDGE || type == WING_OUTLINE_FULL) {
        pts[i++] = vect2_add(root, VECT2(0, -root_chord * 0.25));
        pts[i++] = vect2_add(tip, VECT2(0, -tip_chord * 0.25));
    }
    if (type == WING_OUTLINE_TRAILING_EDGE || type == WING_OUTLINE_FULL) {
        pts[i++] = vect2_add(tip, VECT2(0, tip_chord * 0.75));
        pts[i++] = vect2_add(root, VECT2(0, root_chord * 0.75));
    }

    return (B_TRUE);
    errout:
    return (B_FALSE);
}

/*
 * Reads the outline of a wing, potentially consisting of multiple segments.
 * The number of wing segments is n_wing_nbrs and wing_nbrs are the individual
 * segment numbers. `pts' will be filled the individual outline points, from
 * leading edge root to leading edge tip, trailing edge tip to trailing edge
 * root. tip_p will be populated with the position of the wing tip. This is
 * first checked to make sure that the wing tip position being stored is
 * further away from the aircraft centerline (X coord) than what's already
 * stored in there.
 */
int
wing_outline_read(const acf_file_t *acf, int n_wing_nbrs,
                  const int *wing_nbrs, vect2_t *pts, vect2_t *tip_p, double z_ref) {
    int p = 0;

    for (int i = 0; i < n_wing_nbrs; i++) {
        vect2_t tip;
        bool_t last_wing = (i + 1 == n_wing_nbrs);
        wing_seg_outline_read(acf, wing_nbrs[i], &pts[p], &tip, z_ref,
                              last_wing ? WING_OUTLINE_FULL : WING_OUTLINE_LEADING_EDGE);
        p += 2;
        if (last_wing) {
            p += 2;
            if (tip_p->x < tip.x)
                *tip_p = tip;
        }
    }
    for (int i = n_wing_nbrs - 2; i >= 0; i--) {
        wing_seg_outline_read(acf, wing_nbrs[i], &pts[p], NULL, z_ref,
                              WING_OUTLINE_TRAILING_EDGE);
        p += 2;
    }

    return (p);
}

/*
 * Since not all aircraft use all wing segments, we need to cut down the
 * actual segment list we build the outline from. This function takes an
 * array of wing numbers from root to tip and determines which segments
 * (if any) are in use in the aircraft model. Number of initial elements
 * is n_wing_nbrs. The wing_nbrs array is modified so as to contain only
 * the wing segments that are in use and the function returns the number
 * of wing segments left in wing_nbrs (0 if none are used in the model).
 */
static int
count_wings(const acf_file_t *acf, int *wing_nbrs, int n_wing_nbrs) {
    double prev_x_arm = 0;
    int n = 0;

    ASSERT(acf != NULL);
    ASSERT(wing_nbrs != NULL || n_wing_nbrs == 0);

    for (n = 0; n < n_wing_nbrs; n++) {
        double root_chord = 0, x_arm = 0;

        /*
         * For a wing segment to make sense it must have a non-zero
         * root chord and its X offset must be greater than the X
         * offset of the previous segment.
         */
        READ_FEET(root_chord, 0, "_wing/%d/_Croot", wing_nbrs[n]);

        if (acf_file_get_version(acf) >= 1200) {
            READ_FEET(x_arm, 0, "_wing/%d/_part_x", wing_nbrs[n]);
        } else {
            READ_FEET(x_arm, 0, "_wing/%d/_crib_x_arm/0",
                      wing_nbrs[n]);
        }
        if (root_chord == 0 || x_arm < prev_x_arm)
            goto errout;
        prev_x_arm = x_arm;

        continue;
        errout:
        for (int i = n + 1; i < n_wing_nbrs; i++)
            wing_nbrs[i - 1] = wing_nbrs[i];
        n--;
        n_wing_nbrs--;
    }
    return (n);
}

#define    N_MAIN_WING_IDS        4
#define    MAIN_WING_IDS_XP11    ((int[N_MAIN_WING_IDS]){ 9, 11, 13, 15 })
#define    MAIN_WING_IDS_XP12    ((int[N_MAIN_WING_IDS]){ 1, 3, 5, 7 })

#define    N_STAB_WING_IDS        1
#define    STAB_WING_IDS_XP11    ((int[N_STAB_WING_IDS]){ 17 })
#define    STAB_WING_IDS_XP12    ((int[N_STAB_WING_IDS]){ 9 })

acf_outline_t *
acf_outline_read(const char *filename) {
    ASSERT(filename != NULL);
    acf_outline_t *outline = NULL;
    acf_file_t *acf = acf_file_read(filename);
    vect2_t *pts = NULL;
    int p, s_dim_fus;
    double z_ref;
    const char *prop_val;

    enum {
        MAX_WINGS = 4
    };
    /* even wing numbers = left side, +1 is right side */
    int main_wings[MAX_WINGS], stab_wings[MAX_WINGS];
    int n_main_wings = N_MAIN_WING_IDS, n_stab_wings = N_STAB_WING_IDS;

    if (acf == NULL)
        goto errout;

    CTASSERT(ARRAY_NUM_ELEM(main_wings) >= N_MAIN_WING_IDS);
    CTASSERT(ARRAY_NUM_ELEM(stab_wings) >= N_STAB_WING_IDS);
    if (acf_file_get_version(acf) >= 1200) {
        memcpy(main_wings, MAIN_WING_IDS_XP12,
               sizeof(MAIN_WING_IDS_XP12));
        memcpy(stab_wings, STAB_WING_IDS_XP12,
               sizeof(STAB_WING_IDS_XP12));
    } else {
        memcpy(main_wings, MAIN_WING_IDS_XP11,
               sizeof(MAIN_WING_IDS_XP11));
        memcpy(stab_wings, STAB_WING_IDS_XP11,
               sizeof(STAB_WING_IDS_XP12));
    }
    outline = safe_calloc(1, sizeof(*outline));
    /*
     * Zibo 737 workaround: they ship with no fuselage body and instead
     * replace the fuselage by a series of weird criss-crossing wings.
     */
    if (acf_file_get_version(acf) >= 1200)
        prop_val = acf_prop_find(acf, "_body/0/_s_dim");
    else
        prop_val = acf_prop_find(acf, "_part/56/_s_dim");
    s_dim_fus = (prop_val != NULL ? atoi(prop_val) : 0);
    READ_FEET(z_ref, 0, "acf/_cgZ");

    n_main_wings = count_wings(acf, main_wings, n_main_wings);
    n_stab_wings = count_wings(acf, stab_wings, n_stab_wings);

    outline->num_pts = s_dim_fus + n_main_wings * 4 + n_stab_wings * 4 + 2;
    outline->pts = safe_calloc(outline->num_pts, sizeof(*pts));

    if (acf_file_get_version(acf) >= 1200) {
        part_outline_read(acf, "body/0", outline->pts, s_dim_fus,
                          z_ref);
    } else {
        part_outline_read(acf, "part/56", outline->pts, s_dim_fus,
                          z_ref);
    }
    p = s_dim_fus;
    outline->pts[p++] = NULL_VECT2;
    p += wing_outline_read(acf, n_main_wings, main_wings,
                           &outline->pts[p], &outline->wingtip, z_ref);
    outline->pts[p++] = NULL_VECT2;
    p += wing_outline_read(acf, n_stab_wings, stab_wings,
                           &outline->pts[p], &outline->wingtip, z_ref);

    if (acf_prop_find(acf, "acf/_size_x") == NULL) {
        double x_dim[2] = {1e10, 0};
        double y_dim[2] = {1e10, 0};

        /*
         * XP 11.10 removed the size parameters, so we'll have to
         * guess them from the maximum X and Y offsets of the
         * individual points.
         */
        for (size_t i = 0; i < outline->num_pts; i++) {
            const vect2_t *vectPtr = &outline->pts[i];
            if (vectPtr->x < x_dim[0])
                x_dim[0] = vectPtr->x;
            if (vectPtr->x > x_dim[1])
                x_dim[1] = vectPtr->x;
            if (vectPtr->y < y_dim[0])
                y_dim[0] = vectPtr->y;
            if (vectPtr->y > y_dim[1])
                y_dim[1] = vectPtr->y;
        }
        outline->semispan = MAX(x_dim[1] - x_dim[0], 0);
        outline->length = MAX(y_dim[1] - y_dim[0], 0);
    } else {
        READ_FEET(outline->semispan, 0, "acf/_size_x");
        READ_FEET(outline->length, 0, "acf/_size_z");
    }

    acf_file_free(acf);

    return (outline);

    errout:
    if (outline != NULL)
        acf_outline_free(outline);
    if (acf != NULL)
        acf_file_free(acf);
    return (NULL);
}

void
acf_outline_free(acf_outline_t *outline) {
    free(outline->pts);
    free(outline);
}
