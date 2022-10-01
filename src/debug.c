void WantPrint(Want const* wantBuf) {
  BufEach(Want const, wantBuf, w) {
    switch (w->type) {
      case WANT_STAT: {
        char* line = LineToStr(w->lineHi, w->lineLo);
        printf("%d %s\n", w->value, line);
        BufFree(&line);
        break;
      }
      case WANT_OP:
        printf("<%s %d>\n", WantOpNames[w->op], w->opCount);
        break;
      case WANT_MASK:
      case WANT_NULLTYPE:
        puts(WantTypeNames[w->type]);
        break;
    }
  }
}

static
void DataPrint(LineData const* ld, int tier, int* values) {
  if (!ld) {
    puts("(null)");
    return;
  }
  Align* al = AlignInit();
  BufEachi(ld->lineHi, i) {
    char* s = LineToStr(ld->lineHi[i], ld->lineLo[i]);
    AlignFeed(al, "%d %s 1", " in %g", values[i], s, ld->onein[i]);
    BufFree(&s);
  }
  AlignPrint(al, stdout);
  AlignFree(al);
}

static
void LinesPrint(Lines* l) {
  Align* al = AlignInit();
  BufEachi(l->lineHi, i) {
    char* s = LineToStr(l->lineHi[i], l->lineLo[i]);
    if (!(i % l->comboSize)) {
      AlignFeed(al, "%s", "", "");
    }
    float p = l->onein[i];
    if (p < 1) {
      p = 1/p;
    }
    AlignFeed(al, "%d %s %s 1", " in %g",
      l->value[i], s, ArrayBit(l->prime, i) ? "P" : " ", p);
    BufFree(&s);
  }
  AlignPrint(al, stdout);
  AlignFree(al);
}
