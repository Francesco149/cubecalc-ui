#!/usr/bin/env python3

import sys
sys.path.append("./cubecalc/src/")
from common import *
from glue_common import CalcParam

print(f"/* generated by {sys.argv[0]}, do not edit */")
for en in [Cube, Category, Line, CalcParam, Tier]:
  n = en.__name__.lower()
  print(f"char const* {n}Names[] = {{")
  vals = [x for x in en]
  if en == Line:
    vals = [x for x in LineMasks] + vals
    exclude = {MAINSTAT, BOSS_30, BOSS_35, BOSS_40, IED_15, IED_30, IED_35, IED_40, ANY,
               COOLDOWN_1, COOLDOWN_2, FLAT_MAINSTAT, FLAT_ALLSTAT, FLAT_HP}
    vals = [x for x in vals if x not in exclude]
  if en == Category:
    for x in vals:
      print(f"  \"{category_name(x)}\",")
  else:
    for x in vals:
      vn = x.name.lower().replace("_", " ")
      print(f"  \"{vn}\",")
  print("};")
  print(f"int {n}Values[] = {{")
  for x in vals:
    print(f"  {x.value},")
  print("};")
  for i, x in enumerate(vals):
    print(f"#define {x.name} {i}")

  if en == Cube:
    print("int tierLimits[] = {")
    for x in vals:
      limit = tier_limits[x] if x in tier_limits else LEGENDARY
      print(f"  {limit},")
    print("};")

print("char const* const disclaimer[] = {")
with open("./cubecalc/cubechances.txt") as f:
  f.readline()
  f.readline()
  for line in f:
    if line.startswith("="):
      break
    s = line.strip()
    print(f"\"{s}\",")
print("};")
