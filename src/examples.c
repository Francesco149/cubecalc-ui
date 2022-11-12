int examplesCommonCubeTier(int* succ, int category, int cube, int tier) {
  struct nk_vec2 s = nk_vec2(20, 20);
  int ncategory;
  if (category >= 0) {
    ncategory = uiTreeAddChk(s, NCATEGORY, 0, 0, succ);
  }
  int ncube = uiTreeAddChk(s, NCUBE, 310, 0, succ);
  int ntier = uiTreeAddChk(s, NTIER, 520, 0, succ);
  int nlevel = uiTreeAddChk(s, NLEVEL, 730, 0, succ);
  int nregion = uiTreeAddChk(s, NREGION, 940, 0, succ);
  int nsplit = uiTreeAddChk(s, NSPLIT, 650, 90, succ);

  if (*succ) {
    uiTreeDataByNode(nsplit)->value = ntier;
    uiTreeDataByNode(nlevel)->value = 200;
    uiTreeDataByNode(ntier)->value = tier;
    uiTreeDataByNode(ncube)->value = cube;
    if (category >= 0) {
      uiTreeDataByNode(ncategory)->value = category;
      uiTreeLink(ncategory, ncube);
    }
    uiTreeLink(ncube, ntier);
    uiTreeLink(nlevel, ntier);
    uiTreeLink(nlevel, nregion);
  }

  return nsplit;
}

int examplesCommonCube(int* succ, int category, int cube) {
  return examplesCommonCubeTier(succ, category, cube, LEGENDARY_IDX);
}

int examplesCommon(int* succ, int category) {
  return examplesCommonCube(succ, category, RED_IDX);
}

void examplesWSE() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, WEAPON_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %att", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }

  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 20+ %att and 30+ %boss", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 210, 140, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = BOSS_IDX;
      uiTreeDataByNode(namt2)->value = treeDefaultValue(NAMOUNT, BOSS_IDX);
      uiTreeDataByNode(namt)->value = 20;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(nstat2, namt2);
      uiTreeLink(namt, nres);
      uiTreeLink(namt2, nres);
    }
  }

  {
    s.x = 230;
    s.y += 350;
    uiTreeAddComment(s, 0, 0, 200, 220, "example: bpot 23+ %att", &succ);
    int nbpot = uiTreeAddChk(s, NCUBE, 0, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 140, &succ);
    int nsplit2 = uiTreeAddChk(s, NSPLIT, -100, -20, &succ);

    if (succ) {
      uiTreeDataByNode(nbpot)->value = BONUS_IDX;
      uiTreeDataByNode(nsplit2)->value = nprevres;
      uiTreeLink(nbpot, nsplit2);
      uiTreeLink(nbpot, nres);
    }
  }

  {
    s.x += 240;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: any 3l combo of %att or %boss", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat3 = uiTreeAddChk(s, NSTAT, 210, 140, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = BOSS_ONLY_IDX;
      uiTreeDataByNode(nstat3)->value = LINES_IDX;
      uiTreeDataByNode(namt2)->value = 3;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstat3);
      uiTreeLink(nstat, nstat2);
      uiTreeLink(nstat3, namt2);
      uiTreeLink(namt2, nres);
    }
  }
}

void examplesAccessory() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, FACE_EYE_RING_EARRING_PENDANT_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %stat accessory", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: drop and 10+ %stat accessory", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(ndrop)->value = DROP_IDX;
      uiTreeDataByNode(namt)->value = 10;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
      uiTreeLink(ndrop, nres);
    }
  }
  {
    s.y += 350;
    s.x -= 450;
    uiTreeAddComment(s, 0, 0, 410, 400,
        "example: any 2l combo drop/meso w/ black cubes", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 140, &succ);
    int nstat3 = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 320, &succ);
    int ncube = uiTreeAddChk(s, NCUBE, 210, 320, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = DROP_IDX;
      uiTreeDataByNode(nstat2)->value = MESO_IDX;
      uiTreeDataByNode(nstat3)->value = LINES_IDX;
      uiTreeDataByNode(ncube)->value = BLACK_IDX;
      uiTreeDataByNode(namt)->value = 2;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat2, nstat);
      uiTreeLink(nstat3, nstat2);
      uiTreeLink(nstat3, namt);
      uiTreeLink(namt, ncube);
      uiTreeLink(ncube, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: 40+ %drop accessory w/ black cubes", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ncube = uiTreeAddChk(s, NCUBE, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = DROP_IDX;
      uiTreeDataByNode(ncube)->value = BLACK_IDX;
      uiTreeDataByNode(namt)->value = 40;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, ncube);
      uiTreeLink(ncube, nres);
    }
  }
}

void examplesGlove() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, GLOVE_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %stat glove", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: 8+ %critdmg and and 10+ %stat glove", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(ndrop)->value = CRITDMG_IDX;
      uiTreeDataByNode(namt)->value = 10;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
      uiTreeLink(ndrop, nres);
    }
  }
  {
    s.x -= 450;
    s.y += 350;
    uiTreeAddComment(s, 0, 0, 410, 400,
        "example: (sharp eyes or crit dmg) and 10+ %stat glove", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int ncritdmg = uiTreeAddChk(s, NSTAT, 0, 320, &succ);
    int nor = uiTreeAddChk(s, NOR, 210, 320, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(ndrop)->value = DECENT_SHARP_EYES_IDX;
      uiTreeDataByNode(ncritdmg)->value = CRITDMG_IDX;
      uiTreeDataByNode(namt)->value = 10;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
      uiTreeLink(ndrop, nor);
      uiTreeLink(ncritdmg, nor);
      uiTreeLink(nres, nor);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: 16+ %critdmg glove w/ black cubes", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ncube = uiTreeAddChk(s, NCUBE, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = CRITDMG_IDX;
      uiTreeDataByNode(ncube)->value = BLACK_IDX;
      uiTreeDataByNode(namt)->value = 16;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, ncube);
      uiTreeLink(ncube, nres);
    }
  }
}

void examplesHat() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, HAT_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %stat hat", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: 2+s cooldown and and 10+ %stat hat", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(ndrop)->value = COOLDOWN_IDX;
      uiTreeDataByNode(namt)->value = 10;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
      uiTreeLink(ndrop, nres);
    }
  }
  {
    s.y += 350;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: 4+s cooldown hat w/ black cubes", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ncube = uiTreeAddChk(s, NCUBE, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = COOLDOWN_IDX;
      uiTreeDataByNode(ncube)->value = BLACK_IDX;
      uiTreeDataByNode(namt)->value = 4;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, ncube);
      uiTreeLink(ncube, nres);
    }
  }
}

void examplesTopOverall() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, TOP_OVERALL_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %stat top/overall", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
      "example: 2+s invincibility top/overall", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = INVIN_IDX;
      uiTreeDataByNode(namt)->value = 2;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
}

void examplesShoe() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, SHOE_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 23+ %stat shoe", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: combat orders and and 10+ %stat shoe", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(ndrop)->value = DECENT_COMBAT_ORDERS_IDX;
      uiTreeDataByNode(namt)->value = 10;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
      uiTreeLink(ndrop, nres);
    }
  }
}

void examplesCapeBeltShoulder() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommon(&succ, CAPE_BELT_SHOULDER_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310,
      "example: 23+ %stat cape/belt/shoulder", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 23;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
}

void examplesOperators() {
  int succ = 1;
  struct nk_vec2 s = nk_vec2(20, 20);
  int nsplit = examplesCommon(&succ, FACE_EYE_RING_EARRING_PENDANT_IDX);

  {
    s = nk_vec2(20, 130);
    struct nk_vec2 s0 = s;
    int nmeso = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int ndrop = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nor = uiTreeAddChk(s, NOR, 210 + 210 / 2 - 80 / 2, 140, &succ);
    s.y += 140 + 40;
    int n23stat = uiTreeAddChk(s, NSTAT, 0, 0, &succ);
    int n9stat = uiTreeAddChk(s, NSTAT, 210, 0, &succ);
    s.y += 90;
    int n23amt = uiTreeAddChk(s, NAMOUNT, 0, 0, &succ);
    int n9amt = uiTreeAddChk(s, NAMOUNT, 210, 0, &succ);
    s.y += 90;
    int nor2 = uiTreeAddChk(s, NOR, 210 - 80 / 2, 0, &succ);
    s.y += 60;
    int nres = uiTreeAddChk(s, NRESULT, 210/2, 0, &succ);
    s.y += 90;
    uiTreeAddComment(s0, 0, 0, 410, s.y - s0.y,
        "example: ((meso or drop) and 10+ stat) or 23+ stat", &succ);

    if (succ) {
      uiTreeDataByNode(n23stat)->value = uiTreeDataByNode(n9stat)->value = STAT_IDX;
      uiTreeDataByNode(n23amt)->value = 23;
      uiTreeDataByNode(n9amt)->value = 10;
      uiTreeDataByNode(ndrop)->value = DROP_IDX;
      uiTreeDataByNode(nmeso)->value = MESO_IDX;

      uiTreeLink(ndrop, nor);
      uiTreeLink(nmeso, nor);
      uiTreeLink(nor, n9stat);
      uiTreeLink(n9stat, n9amt);
      uiTreeLink(n9amt, nor2);

      uiTreeLink(n23stat, n23amt);
      uiTreeLink(n23amt, nor2);

      uiTreeLink(nor2, nres);

      uiTreeLink(nsplit, nres);
    }
  }
}

void examplesFamiliars() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1, nprevres;

  {
    uiTreeAddComment(s, 0, 0, 410, 400, "example: unique fam 30+ boss reveal", &succ);
    int nfamcat = uiTreeAddChk(s, NCATEGORY, 0, 50, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 140, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 230, &succ);
    int nfamtier = uiTreeAddChk(s, NTIER, 210, 140, &succ);
    int nfamcube = uiTreeAddChk(s, NCUBE, 210, 230, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 320, &succ);
    int nsplit = nprevres = uiTreeAddChk(s, NSPLIT, 330, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nsplit)->value = nfamcat;
      uiTreeDataByNode(namt)->value = 30;
      uiTreeDataByNode(nfamcat)->value = FAMILIAR_STATS_IDX;
      uiTreeDataByNode(nfamcube)->value = FAMILIAR_IDX;
      uiTreeDataByNode(nfamtier)->value = UNIQUE_IDX;
      uiTreeDataByNode(nstat)->value = BOSS_IDX;
      uiTreeLink(nsplit, nfamtier);
      uiTreeLink(nstat, namt);
      uiTreeLink(nfamcube, namt);
      uiTreeLink(nfamtier, nfamcube);
      uiTreeLink(nfamcube, nres);
    }
  }

  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: red cards 40+ boss", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nfamtier = uiTreeAddChk(s, NTIER, 210, 50, &succ);
    int nfamcube = uiTreeAddChk(s, NCUBE, 210, 140, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 230, &succ);

    if (succ) {
      uiTreeDataByNode(namt)->value = 40;
      uiTreeDataByNode(nfamcube)->value = RED_FAM_CARD_IDX;
      uiTreeDataByNode(nfamtier)->value = LEGENDARY_IDX;
      uiTreeDataByNode(nstat)->value = BOSS_IDX;
      uiTreeLink(nprevres, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(nfamcube, namt);
      uiTreeLink(nfamtier, nfamcube);
      uiTreeLink(nfamcube, nres);
    }
  }
}

void examplesBonus() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommonCube(&succ, FACE_EYE_RING_EARRING_PENDANT_IDX, BONUS_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 480, 400,
        "example: L bpot any 2l combo of stat, stat per 10 lvls", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int nstatperlvl = uiTreeAddChk(s, NSTAT, 0, 140, &succ);
    int nlines = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 320, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = MAINSTAT_IDX;
      uiTreeDataByNode(nstatperlvl)->value = MAINSTAT_PER_10_LVLS_IDX;
      uiTreeDataByNode(nlines)->value = LINES_IDX;
      uiTreeDataByNode(namt)->value = 2;
      uiTreeDataByNode(nres)->bounds.w = 270;
      uiTreeDataByNode(nres)->bounds.h = 350;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstatperlvl);
      uiTreeLink(nlines, nstatperlvl);
      uiTreeLink(nlines, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 530;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: rare 11 att bpot", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ntier = uiTreeAddChk(s, NTIER, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = FLAT_ATT_IDX;
      uiTreeDataByNode(ntier)->value = RARE_IDX;
      uiTreeDataByNode(namt)->value = 11;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(ntier, namt);
      uiTreeLink(ntier, nres);
    }
  }
}

void examplesHatBonus() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommonCube(&succ, HAT_IDX, BONUS_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 480, 400,
        "example: L bpot any 2l combo of stat, stat per 10 lvls", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int nstatperlvl = uiTreeAddChk(s, NSTAT, 0, 140, &succ);
    int nlines = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 320, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = MAINSTAT_IDX;
      uiTreeDataByNode(nstatperlvl)->value = MAINSTAT_PER_10_LVLS_IDX;
      uiTreeDataByNode(nlines)->value = LINES_IDX;
      uiTreeDataByNode(namt)->value = 2;
      uiTreeDataByNode(nres)->bounds.w = 270;
      uiTreeDataByNode(nres)->bounds.h = 350;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstatperlvl);
      uiTreeLink(nlines, nstatperlvl);
      uiTreeLink(nlines, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 530;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: rare 11 att bpot", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int ntier = uiTreeAddChk(s, NTIER, 0, 230, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = FLAT_ATT_IDX;
      uiTreeDataByNode(ntier)->value = RARE_IDX;
      uiTreeDataByNode(namt)->value = 11;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(ntier, namt);
      uiTreeLink(ntier, nres);
    }
  }
  {
    s.y += 440;
    s.x -= 530;
    uiTreeAddComment(s, 0, 0, 480, 490,
        "example: L bpot any 2l cooldown, stat, stat per 10 lvls", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int nstatperlvl = uiTreeAddChk(s, NSTAT, 0, 140, &succ);
    int ncooldown = uiTreeAddChk(s, NSTAT, 0, 230, &succ);
    int nlines = uiTreeAddChk(s, NSTAT, 0, 320, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 410, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = MAINSTAT_IDX;
      uiTreeDataByNode(nstatperlvl)->value = MAINSTAT_PER_10_LVLS_IDX;
      uiTreeDataByNode(nlines)->value = LINES_IDX;
      uiTreeDataByNode(ncooldown)->value = COOLDOWN_IDX;
      uiTreeDataByNode(namt)->value = 2;
      uiTreeDataByNode(nres)->bounds.w = 270;
      uiTreeDataByNode(nres)->bounds.h = 440;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstatperlvl);
      uiTreeLink(ncooldown, nstatperlvl);
      uiTreeLink(ncooldown, nlines);
      uiTreeLink(nlines, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.y -= 90;
    s.x += 530;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: L bpot 2+s cooldown", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = COOLDOWN_IDX;
      uiTreeDataByNode(namt)->value = 2;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
}

void examplesOccultStat() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit =
    examplesCommonCubeTier(&succ, FACE_EYE_RING_EARRING_PENDANT_IDX, OCCULT_IDX, EPIC_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 11+ %stat", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 11;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
}

void examplesOccultWSE() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommonCubeTier(&succ, WEAPON_IDX, OCCULT_IDX, EPIC_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 7+ %att", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(namt)->value = 7;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: any 2l combo of %att or %ied", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat3 = uiTreeAddChk(s, NSTAT, 210, 140, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = IED_IDX;
      uiTreeDataByNode(nstat3)->value = LINES_IDX;
      uiTreeDataByNode(namt2)->value = 2;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstat3);
      uiTreeLink(nstat, nstat2);
      uiTreeLink(nstat3, namt2);
      uiTreeLink(namt2, nres);
    }
  }
}

void examplesMasterStat() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit =
    examplesCommonCubeTier(&succ, FACE_EYE_RING_EARRING_PENDANT_IDX, MASTER_IDX, UNIQUE_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 17+ %stat", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat)->value = STAT_IDX;
      uiTreeDataByNode(namt)->value = 17;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
}

void examplesMasterWSE() {
  struct nk_vec2 s = nk_vec2(20, 20);
  int succ = 1;
  int nprevres;
  int nsplit = examplesCommonCubeTier(&succ, WEAPON_IDX, MASTER_IDX, UNIQUE_IDX);
  {
    s.y += 150;
    uiTreeAddComment(s, 0, 0, 410, 310, "example: 17+ %att", &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nres = nprevres = uiTreeAddChk(s, NRESULT, 210, 50, &succ);

    if (succ) {
      uiTreeDataByNode(namt)->value = 17;
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, namt);
      uiTreeLink(namt, nres);
    }
  }
  {
    s.x += 450;
    uiTreeAddComment(s, 0, 0, 410, 310,
        "example: any 2l combo of %att or %boss", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 0, 50, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 0, 140, &succ);
    int nstat3 = uiTreeAddChk(s, NSTAT, 210, 140, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 210, 230, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = BOSS_ONLY_IDX;
      uiTreeDataByNode(nstat3)->value = LINES_IDX;
      uiTreeDataByNode(namt2)->value = 2;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat, nstat3);
      uiTreeLink(nstat, nstat2);
      uiTreeLink(nstat3, namt2);
      uiTreeLink(namt2, nres);
    }
  }
  {
    s.x -= 450;
    s.y += 350;
    uiTreeAddComment(s, 0, 0, 620, 310,
        "example: any 2l combo of %att, %ied, %boss", &succ);
    int nstat2 = uiTreeAddChk(s, NSTAT, 420, 230, &succ);
    int namt2 = uiTreeAddChk(s, NAMOUNT, 210, 140, &succ);
    int nstat3 = uiTreeAddChk(s, NSTAT, 420, 140, &succ);
    int nstat4 = uiTreeAddChk(s, NSTAT, 420, 50, &succ);
    int nstat = uiTreeAddChk(s, NSTAT, 210, 50, &succ);
    int nres = uiTreeAddChk(s, NRESULT, 0, 50, &succ);

    if (succ) {
      uiTreeDataByNode(nstat2)->value = BOSS_ONLY_IDX;
      uiTreeDataByNode(nstat4)->value = IED_IDX;
      uiTreeDataByNode(nstat3)->value = LINES_IDX;
      uiTreeDataByNode(namt2)->value = 2;
      uiTreeLink(nsplit, nstat);
      uiTreeLink(nstat3, nstat2);
      uiTreeLink(nstat4, nstat3);
      uiTreeLink(nstat4, nstat);
      uiTreeLink(nstat3, namt2);
      uiTreeLink(namt2, nres);
      uiTreeDataByNode(nres)->bounds.h = 260;
      uiTreeResultByNode(nres)->perPage = 100;
    }
  }
}

#define examplesFile(x) \
  examplesFile_(#x, examples##x)

void examplesFile_(char* path, void (* func)()) {
  if (!presetExists(path)) {
    uiTreeClear();
    func();
    presetSave(path);
  }
}

void examples() {
  examplesFile(WSE);
  examplesFile(Accessory);
  examplesFile(Glove);
  examplesFile(Hat);
  examplesFile(TopOverall);
  examplesFile(Shoe);
  examplesFile(CapeBeltShoulder);
  examplesFile(Operators);
  examplesFile(Familiars);
  examplesFile(Bonus);
  examplesFile(HatBonus);
  examplesFile(OccultStat);
  examplesFile(OccultWSE);
  examplesFile(MasterStat);
  examplesFile(MasterWSE);
  examplesFile_("zzz_Preset1", examplesWSE);
  examplesFile_("zzz_Preset2", examplesWSE);
  examplesFile_("zzz_Preset3", examplesWSE);
  examplesFile_("zzz_Preset4", examplesWSE);
  examplesFile_("zzz_Preset5", examplesWSE);
}
