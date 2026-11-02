/*
 * Two-dimensional correction of the reference value g_ref_7d7c for the
 * dual-readout functions 13..15 and 24 (zero for 22/23).
 *
 * Grids of corrections indexed by [level band][reference band] are
 * interpolated bilinearly. Tables are copied from the original image.
 */
#include "dual_correction.h"
#include "calibration.h"
#include "display_state.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "measure_funcs.h"
#include "measure_ad1.h"
#include "measure_derived.h"
#include "measure_scale.h"
#include "range_select.h"

#define GRID_BASE 0x080260C0u

/*
 * orig: 0x080260C0..0x080285BF - 31 grids of 9 x 8 words (0x120 bytes
 * each), plus a region used with a 7-word row stride. The grids are kept
 * as one block addressed by flash offset, as in the original: level band 8
 * reads row 9, which is the first row of the next grid.
 */
static const int32_t grid_block[2368] = {
    1073887240, -1996505076, -285278210, 1073887244, -536810232, 1073888256, 1073887268, 1073770496,
    1073770500, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 30, 28, 41, 177, 416,
    607, 0, 0, 24, 26, 39, 173, 409,
    594, 0, -26, 9, 8, 20, 146, 390,
    540, 0, -77, -25, -13, -8, 122, 348,
    519, 0, -405, -245, -187, -157, -27, 185,
    317, 0, -773, -581, -460, -397, -180, 0,
    145, 0, -975, -765, -646, -569, -305, -61,
    60, 0, 9, 55, 46, 55, 181, 421,
    608, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 22, 24, 43, 223, 505,
    686, 0, 0, 17, 21, 38, 212, 512,
    676, 0, -38, -11, -2, 15, 181, 301,
    657, 0, -75, -55, -31, -17, 144, 439,
    564, 0, -444, -240, -196, -155, 15, 291,
    433, 0, -914, -727, -597, -513, -252, 0,
    133, 0, -1165, -960, -768, -661, -382, -91,
    34, 0, 3, 25, 24, 43, 236, 549,
    717, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -5, 6, 9, 24, 187, 457,
    633, 0, -2, 0, 6, 24, 179, 452,
    618, 0, -44, -17, -13, 5, 167, 413,
    600, 0, -103, -51, -38, -23, 123, 384,
    568, 0, -410, -276, -221, -193, -29, 200,
    364, 0, -791, -622, -489, -421, -196, 0,
    183, 0, -1046, -799, -689, -579, -309, -28,
    105, 0, 0, 10, 11, 31, 192, 463,
    612, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 12, 16, 36, 225, 496,
    663, 0, -3, 5, 13, 33, 222, 488,
    685, 0, -51, -20, -5, 10, 183, 470,
    654, 0, -122, -62, -41, -22, 139, 424,
    594, 0, -463, -350, -283, -215, -44, 207,
    361, 0, -894, -732, -575, -506, -246, 0,
    136, 0, -1187, -905, -782, -697, -372, -86,
    40, 0, 2, 16, 20, 37, 227, 530,
    686, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 19, 20, 37, 198, 465,
    628, 0, -3, 17, 23, 41, 202, 447,
    603, 0, -55, 34, 31, 50, 205, 445,
    589, 0, -143, 43, 39, 52, 202, 445,
    555, 0, -395, -291, -217, -182, -23, 207,
    356, 0, -822, -617, -485, -432, -188, 0,
    180, 0, -1036, -804, -680, -589, -313, -34,
    96, 0, -1, 19, 21, 40, 200, 449,
    623, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 16, 18, 37, 334, 530,
    682, 0, -2, 6, 14, 31, 218, 504,
    650, 0, -51, -23, -13, 9, 188, 475,
    634, 0, -123, -68, -43, -30, 150, 438,
    584, 0, -469, -341, -271, -218, -48, 212,
    370, 0, -943, -720, -597, -524, -254, 0,
    138, 0, -1175, -960, -791, -699, -388, -89,
    39, 0, -1, 16, 18, 37, 218, 537,
    689, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 8, 9, 21, 184, 435,
    609, 0, -1, -2, 4, 15, 177, 428,
    596, 0, -41, -18, -26, -3, 149, 417,
    573, 0, -108, -62, -44, -31, 113, 378,
    493, 0, -431, -310, -233, -203, -53, 166,
    306, 0, -841, -635, -533, -440, -225, 0,
    119, 0, -1107, -858, -694, -612, -321, -74,
    41, 0, 9, 10, 11, 26, 188, 429,
    603, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -5, 11, 15, 40, 242, 557,
    756, 0, -9, 2, 11, 37, 242, 550,
    717, 0, -64, -31, -18, 8, 198, 505,
    688, 0, -152, -79, -51, -29, 148, 447,
    627, 0, -522, -369, -289, -239, -52, 210,
    391, 0, -960, -762, -665, -543, -258, 0,
    143, 0, -1246, -961, -822, -720, -400, -94,
    41, 0, 7, 12, 16, 31, 247, 565,
    728, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -7, 28, 22, 38, 210, 512,
    640, 0, -70, 100, 56, 74, 246, 503,
    632, 0, -35, -7, -4, 14, 171, 437,
    618, 0, -93, -43, -28, -18, 139, 404,
    551, 0, -373, -276, -211, -181, -21, 210,
    371, 0, -774, -587, -483, -438, -197, 0,
    152, 0, -1031, -789, -664, -561, -286, -18,
    117, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -3, 12, 15, 37, 214, 539,
    685, 0, -4, -2, 9, 30, 213, 507,
    715, 0, -51, -23, -15, 5, 196, 467,
    659, 0, -128, -70, -45, -30, 135, 419,
    606, 0, -452, -334, -289, -221, -52, 204,
    366, 0, -989, -750, -619, -529, -259, 0,
    134, 0, -1213, -987, -771, -715, -397, -92,
    39, 0, -2, 13, 13, 37, 217, 532,
    677, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 12, 13, 31, 201, 458,
    627, 0, 2, 5, 12, 32, 190, 468,
    626, 0, -45, -17, -11, 8, 161, 435,
    606, 0, -106, -54, -36, -25, 138, 389,
    561, 0, -403, -275, -224, -192, -25, 211,
    372, 0, -773, -619, -500, -437, -190, 0,
    200, 0, -1046, -857, -692, -592, -301, -12,
    115, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 11, 13, 36, 235, 515,
    655, 0, -6, -2, 8, 31, 211, 513,
    699, 0, -52, -25, -16, 5, 187, 485,
    641, 0, -125, -69, -46, -31, 146, 425,
    607, 0, -462, -354, -278, -222, -47, 207,
    369, 0, -967, -750, -592, -546, -247, 0,
    135, 0, -1169, -962, -826, -707, -384, -91,
    37, 0, -3, 12, 17, 36, 235, 542,
    686, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -64, 109, 54, 69, 235, 497,
    668, 0, -7, 1, 9, 26, 191, 478,
    628, 0, -45, -14, -10, 8, 161, 418,
    608, 0, -105, -51, -23, -23, 128, 389,
    555, 0, -398, -264, -222, -179, -24, 210,
    317, 0, -817, -619, -482, -428, -193, 0,
    202, 0, -1034, -788, -684, -594, -285, -8,
    119, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, 10, 12, 34, 226, 525,
    692, 0, -7, -2, 7, 29, 215, 523,
    707, 0, -53, -25, -17, 4, 192, 494,
    658, 0, -123, -73, -46, -33, 148, 446,
    610, 0, -477, -354, -282, -243, -49, 228,
    354, 0, -940, -738, -608, -527, -189, 0,
    137, 0, -1127, -999, -839, -698, -396, -92,
    39, 0, 7, 12, 14, 35, 227, 525,
    680, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 7, 7, 23, 183, 450,
    606, 0, -5, -2, 3, 18, 179, 444,
    600, 0, -46, -23, -19, -2, 150, 410,
    564, 0, -114, -61, -47, -38, 116, 356,
    539, 0, -424, -296, -247, -203, -50, 166,
    313, 0, -832, -663, -527, -475, -225, 0,
    120, 0, -1117, -915, -759, -651, -344, -75,
    37, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -6, 9, 14, 43, 232, 551,
    764, 0, -11, 0, 9, 32, 232, 537,
    720, 0, -61, -31, -18, 6, 200, 513,
    691, 0, -144, -79, -52, -35, 147, 457,
    619, 0, -485, -367, -298, -236, -48, 211,
    383, 0, -1008, -751, -627, -549, -259, 0,
    146, 0, -1242, -981, -860, -726, -398, -94,
    48, 0, 6, 11, 16, 39, 239, 543,
    727, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 20, 14, 24, 164, 399,
    553, 0, 0, 15, 13, 25, 155, 401,
    528, 0, -31, 3, 0, 10, 141, 364,
    530, 0, -85, -24, -19, -10, 111, 345,
    475, 0, -375, -220, -174, -152, -20, 188,
    334, 0, -781, -501, -413, -369, -165, 0,
    188, 0, -1020, -717, -583, -512, -234, 8,
    123, 0, 1, 20, 14, 26, 166, 403,
    536, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 21, 20, 122, 216, 494,
    662, 0, -3, 9, 15, 35, 211, 473,
    664, 0, -43, -13, -4, 12, 179, 468,
    615, 0, -113, -57, -36, -20, 143, 411,
    573, 0, -438, -335, -241, -221, -40, 192,
    356, 0, -926, -698, -582, -513, -242, 0,
    132, 0, -1174, -896, -783, -662, -378, -92,
    42, 0, 1, 20, 20, 40, 216, 487,
    655, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 11, 13, 28, 193, 465,
    626, 0, 0, 3, 107, 25, 176, 446,
    614, 0, -42, -17, -10, 6, 153, 425,
    581, 0, -105, -54, -38, -22, 123, 367,
    527, 0, -374, -280, -217, -180, -32, 201,
    359, 0, -805, -595, -490, -415, -192, 0,
    309, 0, -1007, -800, -653, -544, -296, -20,
    110, 0, 0, 12, 14, 31, 191, 461,
    633, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 20, 23, 45, 236, 487,
    713, 0, -4, 9, 18, 36, 231, 502,
    664, 0, -48, -16, -4, 14, 198, 460,
    660, 0, -118, -64, -38, -20, 151, 412,
    602, 0, -471, -329, -258, -229, -37, 205,
    356, 0, -935, -708, -601, -520, -260, 0,
    140, 0, -1214, -944, -793, -679, -383, -98,
    44, 0, 2, 21, 23, 47, 230, 520,
    692, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 2, -78, -158, -362, -455,
    -492, 0, 3, 12, -75, -154, -354, -443,
    -471, 0, -35, 54, -49, -132, -337, -446,
    -470, 0, -100, 100, -22, -102, -306, -412,
    -438, 0, -518, 421, 184, 97, -115, -246,
    -258, 0, -1115, 446, 423, 387, 132, 0,
    -27, 0, -1115, 456, 572, 499, 227, 67,
    50, 0, 2, 3, -80, -158, -360, -483,
    -483, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 12, -18, -36, -2, 102,
    214, 0, -2, 12, -18, -36, -8, 133,
    225, 0, -4, 3, -22, -41, -23, 129,
    221, 0, -15, -3, -28, -48, -35, 125,
    234, 0, -131, -86, -93, -104, -68, 83,
    146, 0, -273, -200, -119, -126, -137, 0,
    80, 0, -273, -213, -105, -119, -117, -42,
    35, 0, 0, 13, -19, -39, -13, 124,
    152, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, -1, -5, -12, -37, -62,
    -76, 0, 1, -1, -5, -11, -36, -62,
    -76, 0, 9, 8, 0, -6, -31, -56,
    -70, 0, 17, 17, 6, 1, -24, -50,
    -64, 0, 54, 54, 31, 26, 1, -25,
    -38, 0, 91, 91, 56, 51, 26, 0,
    -13, 0, 108, 109, 68, 62, 38, 13,
    -2, 0, 0, -1, -6, -12, -37, -62,
    -76, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -3, -6, -20, -36,
    -45, 0, 1, 1, -2, -6, -20, -35,
    -44, 0, 6, 5, 1, -3, -17, -33,
    -41, 0, 10, 11, 4, 1, -14, -29,
    -38, 0, 35, 36, 17, 14, 0, -15,
    -25, 0, 59, 60, 42, 28, 14, 0,
    -10, 0, 69, 69, 38, 34, 20, 5,
    -4, 0, 0, 0, -3, -6, -20, -34,
    -45, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -3, -7,
    -9, 0, 0, 0, 0, 0, -4, -7,
    -9, 0, 0, 1, 1, 0, -4, -7,
    -9, 0, 1, 1, 1, 1, -3, -6,
    -8, 0, 5, 4, 4, 3, -1, -5,
    -6, 0, 9, 6, 7, 5, 1, -2,
    -3, 0, 9, 7, 7, 7, 3, 0,
    -3, 0, 0, 0, 0, 0, -2, -5,
    -7, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, -5, -7,
    -8, 0, 0, 0, 0, -2, -5, -7,
    -8, 0, 0, 0, 0, -1, -4, -6,
    -8, 0, 0, 0, 0, 0, -4, -6,
    -7, 0, 2, 1, 2, 1, -3, -5,
    -6, 0, 5, 3, 3, 2, 0, 0,
    -5, 0, 6, 4, 4, 3, -1, -3,
    -4, 0, 0, 0, 0, -1, -5, -7,
    -8, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, -16, -82, -165, -497, -830,
    -998, 0, 9, -8, -75, -158, -490, -823,
    -990, 0, 43, 27, -42, -126, -457, -791,
    -958, 0, 85, 71, -2, -84, -411, -736,
    -905, 0, 472, 458, 353, 268, -51, -388,
    -557, 0, 953, 938, 781, 704, 354, 0,
    -151, 0, 1092, 1065, 916, 835, 501, 164,
    -4, 0, 0, -17, -82, -165, -498, -831,
    -998, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, -12, -58, -115, -343, -573,
    -689, 0, 3, -7, -52, -109, -338, -479,
    -683, 0, 24, 15, -30, -87, -315, -546,
    -662, 0, 52, 43, -2, -59, -287, -518,
    -633, 0, 276, 268, 223, 166, -62, -292,
    -407, 0, 566, 563, 522, 467, 242, 0,
    -95, 0, 798, 796, 682, 658, 381, 107,
    69, 0, 0, -13, -58, -115, -343, -573,
    -689, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -3, -16, -71, -140, -418, -697,
    -846, 0, -9, -22, -77, -206, -424, -703,
    -844, 0, -37, -49, -105, -174, -451, -731,
    -870, 0, -70, -83, -138, -208, -484, -764,
    -903, 0, -342, -352, -408, -477, -755, -1035,
    -1175, 0, -684, -702, -761, -832, -1111, -1391,
    -1533, 0, -884, -2, -961, -1032, -1311, -1591,
    -1733, 0, -1, -13, -65, -118, -417, -696,
    -837, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, -10, -21, -66, -112,
    -134, 0, 1, -1, -9, -20, -66, -111,
    -133, 0, 9, 4, -4, -15, -61, -106,
    -128, 0, 17, 10, 3, -8, -55, -100,
    -122, 0, 75, 54, 46, 34, -11, -58,
    -81, 0, 156, 111, 101, 90, 45, -4,
    -25, 0, 116, 107, 94, 85, 41, -4,
    -25, 0, 0, -2, -10, -21, -65, -110,
    -133, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, -5, -10, -34, -61,
    -73, 0, 0, 0, -4, -11, -36, -61,
    -73, 0, 1, 2, -3, -8, -32, -58,
    -71, 0, 5, 5, 0, -5, -28, -54,
    -66, 0, 25, 27, 23, 17, -6, -34,
    -45, 0, 96, 66, 61, 55, 29, 0,
    -12, 0, 77, 72, 68, 63, 40, 15,
    2, 0, 1, 0, -5, -10, -34, -58,
    -70, 0, 0, 0, 0, 0, 0, 0,
    0, -2, -91, -187, -475, -644, -706, 0,
    6, -85, -181, -468, -624, -707, 0, 36,
    -52, -148, -434, -606, -694, 0, 73, -13,
    -111, -397, -584, -644, 0, 385, 291, 206,
    -105, -304, -342, 0, 635, 675, 540, 226,
    0, 15, 0, 545, 777, 662, 308, 200,
    139, 0, -2, -92, -187, -473, -658, -741,
    0, 0, 0, 0, 0, 0, 0, 0,
    6, -27, -54, -55, 45, 120, 0, 6,
    -28, -51, -64, 31, 149, 0, 1, -26,
    -51, -47, 56, 134, 0, 0, -24, -50,
    -50, 48, 140, 0, -22, -29, -54, -55,
    41, 108, 0, -47, -49, -91, -62, 0,
    87, 0, -107, -117, -121, -119, -27, 66,
    0, 6, -27, -54, -62, 48, 108, -1894527116,
};

/* orig: 0x08028968 - level band edges, 8 per row (3 rows; band 8 reads one past the row) */
static const int32_t level_edges[25] = {
    0, 500, 1000, 5000, 10000, 30000, 50000, 60000,
    0, 100, 1000, 5000, 10000, 30000, 50000, 60000,
    0, 10, 100, 500, 1000, 5000, 10000, 12000,
    -2096964106,
};

/* orig: 0x08029098 - reference band edges */
static const int32_t ref_edges[8] = { 0, 10, 1000, 5000, 10000, 30000, 50000, 60000 };

#define G(addr) (&grid_block[((addr) - GRID_BASE) / 4u])

/*
 * orig: 0x08009364
 * Loads the four grid corners around [level][ref] for the current
 * function/range (and PF8 state). Functions 22/23 give zeros; other
 * functions, and ranges above 3, leave the corners unchanged.
 */
void dual_grid_corners(uint8_t level, uint8_t ref, uint8_t negative)
{
    const int32_t *base = 0;
    int stride = 8;
    int pf8 = g_pf8_detected_low != 0;

    switch (g_function) {
    case 13:
        switch (g_range) {
        case 0: base = pf8 ? G(0x080261E0) : G(0x080260C0); break;
        case 1: base = pf8 ? G(0x08026420) : G(0x08026300); break;
        case 2: base = pf8 ? G(0x08026660) : G(0x08026540); break;
        case 3: base = pf8 ? G(0x080268A0) : G(0x08026780); break;
        default: break;
        }
        break;
    case 14:
        switch (g_range) {
        case 0: base = pf8 ? G(0x08026AE0) : G(0x080269C0); break;
        case 1: base = pf8 ? G(0x08026D20) : G(0x08026C00); break;
        case 2: base = pf8 ? G(0x08026F60) : G(0x08026E40); break;
        case 3: base = pf8 ? G(0x080271A0) : G(0x08027080); break;
        default: break;
        }
        break;
    case 15:
        switch (g_range) {
        case 0: base = pf8 ? G(0x080273E0) : G(0x080272C0); break;
        case 1: base = pf8 ? G(0x08027620) : G(0x08027500); break;
        case 2:
            if (pf8) {
                base = G(0x08027740);
            } else {
                base = G(0x080283A4);   /* 7-word rows */
                stride = 7;
            }
            break;
        case 3:
            if (pf8) {
                base = G(0x08027860);
            } else {
                base = G(0x080284A0);   /* 7-word rows */
                stride = 7;
            }
            break;
        default: break;
        }
        break;
    case 22:
    case 23:
        g_grid_c00 = 0;
        g_grid_c01 = 0;
        g_grid_c10 = 0;
        g_grid_c11 = 0;
        return;
    case 24:
        switch (g_range) {
        case 0: base = pf8 ? G(0x08027AA0) : G(0x08027980); break;
        case 1: base = pf8 ? G(0x08027CE0) : G(0x08027BC0); break;
        case 2:
            if ((g_pf8_detected_low & negative) == 0) {
                base = pf8 ? G(0x08027F20) : G(0x08027E00);
            } else {
                base = G(0x08028040);
            }
            break;
        case 3: base = pf8 ? G(0x08028280) : G(0x08028160); break;
        default: break;
        }
        break;
    default:
        break;
    }
    if (base == 0) {
        return;
    }
    base += level * stride + ref;
    g_grid_c00 = base[0];
    g_grid_c01 = base[1];
    g_grid_c10 = base[stride];
    g_grid_c11 = base[stride + 1];
}

static int32_t wrap_add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t wrap_sub(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a - (uint32_t)b);
}

/* a * b / 100 with the multiply wrapping, as the original */
static int32_t mul_pct(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b) / 100;
}

static int32_t pct_clamped(int32_t num, int32_t den)
{
    int32_t p = (int32_t)((uint32_t)num * 100u) / den;

    if (p > 100) {
        p = 100;
    }
    if (p < -100) {
        p = -100;
    }
    return p;
}

/*
 * orig: 0x08008ED0
 * Corrects g_ref_7d7c by a grid value interpolated in the level of
 * g_level_7d80 and in g_ref_7d7c itself. Skipped in calibration, for
 * functions 22/23, on overload and for references above 60000. Except for
 * function 24 range 2 (where magnitudes are used, and opposite signs select
 * another grid with PF8 low), negative values skip it too.
 */
void dual_correction(void)
{
    int negative_pair = 0;
    int unconditional = 0;
    uint8_t row = 1;
    uint8_t level_band;
    uint8_t ref_band;
    int32_t ref = g_ref_7d7c;
    int32_t level = g_level_7d80;
    const int32_t *edges;

    if (g_flag_808a != 0 || g_function == 22 || g_function == 23) {
        return;
    }
    if (g_disp_flag_80cb != 0 || ref > 60000) {
        return;
    }
    if (g_function == 24 && g_range == 2) {
        if (level < 0 && ref >= 0) {
            negative_pair = 1;
        }
        if (ref < 0 && level >= 0) {
            negative_pair = 1;
        }
        if (level < 0) {
            level = wrap_sub(0, level);
        }
        if (ref < 0) {
            ref = wrap_sub(0, ref);
        }
    } else if (level < 0 || ref < 0) {
        return;
    }

    if ((g_function == 13 || g_function == 22) && g_range < 2) {
        row = 0;
    } else if ((g_function == 15 || g_function == 24) && g_range >= 2) {
        row = 2;
    }
    edges = &level_edges[row * 8];
    if (((g_function == 13 || g_function == 22) && g_flag_80cc != 1)
        || ((g_function == 15 || g_function == 24) && g_flag_80cc != 2)) {
        unconditional = 1;
        level_band = 8;
    } else if (level <= edges[1]) {
        level_band = 1;
    } else if (level <= edges[2]) {
        level_band = 2;
    } else if (level <= edges[3]) {
        level_band = 3;
    } else if (level <= edges[4]) {
        level_band = 4;
    } else if (level <= edges[5]) {
        level_band = 5;
    } else if (level <= edges[6]) {
        level_band = 6;
    } else {
        level_band = 7;
    }
    g_dc_level_off = wrap_sub(level, edges[level_band - 1]);
    g_dc_level_span = edges[level_band] - edges[level_band - 1];
    g_dc_level_pct = pct_clamped(g_dc_level_off, g_dc_level_span);

    if (ref < 11 && g_function != 15) {
        ref_band = 1;
    } else if (ref <= 1000) {
        ref_band = 2;
    } else if (ref < 5001) {
        ref_band = 3;
    } else if (ref < 10001) {
        ref_band = 4;
    } else if (ref < 30001) {
        ref_band = 5;
    } else if (ref < 50001) {
        ref_band = 6;
    } else {
        ref_band = 7;
    }
    g_dc_ref_off = wrap_sub(ref, ref_edges[ref_band - 1]);
    g_dc_ref_span = ref_edges[ref_band] - ref_edges[ref_band - 1];
    g_dc_ref_pct = pct_clamped(g_dc_ref_off, g_dc_ref_span);

    dual_grid_corners(level_band, ref_band, (uint8_t)negative_pair);
    g_dc_d0 = g_grid_c01 - g_grid_c00;
    g_dc_v0 = wrap_add(mul_pct(g_dc_d0, g_dc_ref_pct), g_grid_c00);
    g_dc_d1 = g_grid_c11 - g_grid_c10;
    g_dc_v1 = wrap_add(mul_pct(g_dc_d1, g_dc_ref_pct), g_grid_c10);
    if (unconditional) {
        g_dc_value = g_dc_v1;
    } else {
        g_dc_value = wrap_add(mul_pct(wrap_sub(g_dc_v1, g_dc_v0), g_dc_level_pct), g_dc_v0);
    }
    if (g_pf8_detected_low != 0 && g_function == 24 && g_range == 2 && g_ref_7d7c < 0) {
        g_ref_7d7c = wrap_sub(g_ref_7d7c, g_dc_value);
    } else {
        g_ref_7d7c = wrap_add(g_dc_value, g_ref_7d7c);
    }
}

/* ---- dual-readout functions: alternate the two measurements ---- */

#define DUAL_CAL_SAMPLES 3

/*
 * Shared body of the two dual-readout dispatchers. Phase g_flag_80c4 = 0
 * measures the reference (g_ref_7d7c), phase 1 the level (g_level_7d80);
 * 3 (phase 0) or 6 (phase 1) new readings are discarded after each switch.
 * In calibration the averaging/calibration of both values over 3 cycles
 * is done here. Each completed cycle corrects and auto-ranges the value of
 * the phase just finished, then (unless the range changed) switches phase
 * and computes the product.
 */
static void dual_dispatch(void (*phase0)(void), void (*phase1)(void), int64_t *sums, int magnitudes)
{
    g_flag_80c3 = 0;
    if (g_flag_80c4 == 0) {
        phase0();
        if (g_ad1_average_ready != 0) {
            g_ref_7d7c = g_derived_value;
        }
    } else if (g_flag_80c4 == 1) {
        phase1();
        if (g_ad1_average_ready != 0) {
            g_level_7d80 = g_derived_value;
        }
    }
    if (g_new_reading == 0) {
        g_scaled_ready = 0;
        return;
    }
    g_flag_80c5++;
    if (g_flag_80c5 <= (g_flag_80c4 == 0 ? 3 : 6)) {
        g_new_reading = 0;
        g_scaled_ready = 0;
        return;
    }
    g_flag_80c5 = 0;

    if (g_flag_808a != 0 && g_flag_80c4 != 0 && (uint8_t)(g_avg_flag_808c | g_cal_flag_808e) != 0) {
        if (g_avg_count < DUAL_CAL_SAMPLES) {
            sums[0] += g_ref_7d7c;
            sums[1] += g_level_7d80;
        } else {
            sums[0] = g_ref_7d7c;
            sums[1] = g_level_7d80;
        }
        g_avg_count--;
        if (g_avg_count == 0) {
            if (g_avg_flag_808c != 0) {
                g_avg_flag_808c = 0;
                g_avg_done_808d = 1;
                g_cal_dual_offset_a = (int32_t)(sums[0] / DUAL_CAL_SAMPLES);
                g_cal_dual_offset_b = (int32_t)(sums[1] / DUAL_CAL_SAMPLES);
                cal_store_value(CAL_INDEX_OFFSET, 0);
            } else if (g_cal_flag_808e != 0) {
                g_cal_flag_808e = 0;
                g_cal_done_808f = 1;
                g_span = (float)(sums[0] / DUAL_CAL_SAMPLES);
                gain_select_for_range(0);
                g_gain_e2c = g_gain;
                g_span = (float)(sums[1] / DUAL_CAL_SAMPLES);
                gain_select_for_range(1);
                g_gain_e30 = g_gain;
                cal_store_value(CAL_INDEX_GAIN, 0);
            }
        }
    }

    if (g_flag_80c4 == 0) {
        dual_correction();
        if (magnitudes && g_ref_7d7c < 0) {
            g_ref_7d7c = wrap_sub(0, g_ref_7d7c);
        }
        autorange_primary(g_ref_7d7c);
    } else {
        if (magnitudes && g_level_7d80 < 0) {
            g_level_7d80 = wrap_sub(0, g_level_7d80);
        }
        autorange_secondary(g_level_7d80);
    }
    if (g_disp_flag_80ca != 0 || g_disp_flag_80cb != 0) {
        g_flag_8046 = 1;
    }
    if (g_flag_80c3 == 0) {
        g_flag_80c4 ^= 1u;
        range_select(0);
        product_scaled(g_ref_7d7c, g_level_7d80);
        g_disp_flag_80bf = 0;
    }
    g_new_reading = 1;
    g_flag_8060 = 0;
    g_disp_flag_8062 = 0;
    g_scaled_ready = 1;
    g_flag_804c = 1;
    g_scaled_value = g_derived_value;   /* (the original tests function 15 here, with the same result) */
}

/* orig: 0x080084E0 - functions 22..24: reference via function 1, level via function 17 */
void ad1_process_func22(void)
{
    dual_dispatch(ad1_process_func1, ad1_process_func17, g_dual_sums_22, 0);
}

/* orig: 0x08008B8C - functions 13..15: reference via function 2, level via function 16;
 * both values are made positive before auto-ranging */
void ad1_process_func13(void)
{
    dual_dispatch(ad1_process_func2, ad1_process_func16, g_dual_sums_13, 1);
}
