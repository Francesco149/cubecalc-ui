from pyodide.ffi import to_js

from cubecalc import cube_calc
from datautils import find_probabilities
from functools import reduce
from operator import or_

# note: global_enum makes the enum global within common.py
#       so if there's any files that use @global_enum they must be imported before common
#       if you want the enum to be global within this file as well
from glue_common import *
from common import *
from kms import cubes as kms
from tms import event as tms
from familiars import familiars, red_card_estimate
from js import console

calc_param_to_key = {
  WANTS: "wants",
  CUBE: "type",
  TIER: "tier",
  CATEGORY: "category",
  LEVEL: "level",
  REGION: "region",
}

bitenum_to_str = lambda e, v: " | ".join([x.name for x in e if x & v])

calc_param_stringify = {
  WANTS: lambda x: "[" + ", ".join(["{ " + ", ".join([f"{bitenum_to_str(Line, k)}: {v}"
                                                      for k, v in want.items()]) + " }"
                                    for want in x]) + "]",
  CUBE: lambda x: Cube(x).name,
  TIER: lambda x: Tier(x).name,
  CATEGORY: lambda x: Category(x).name,
  REGION: lambda x: Region(x).name,
  LEVEL: lambda x: f"{x}",
}

calcs = {}


def calc_free(i):
  if i in calcs:
    calcs.pop(i)


def calc_ensure(i):
  if i not in calcs:
    calcs[i] = {}


def calc_debug_print(pre, i):
  if i not in calcs:
    return "(not present)"
  console.log(
    f"{pre} calc {i} = " +
    "; ".join([f"{CalcParam(k).name}: {calc_param_stringify[k](v)}" for k, v in calcs[i].items()])
  )


def calc_set(i, k, v):
  calc_ensure(i)
  calcs[i][k] = v
  calc_debug_print("set", i)


def calc_push_want(i):
  c = calcs[i]
  if WANTS not in c:
    c[WANTS] = []
  c[WANTS].append({})


def calc_want(i, k, v):
  calc_ensure(i)
  c = calcs[i]
  if WANTS not in c or not len(c[WANTS]):
    calc_push_want(i)
  c[WANTS][-1][k] = v
  calc_debug_print("want", i)


def find_lines(cube, category):
  r = [x for x in [kms, tms, familiars, red_card_estimate] if cube & reduce(or_, x.keys())]
  if len(r) <= 0:
    return {}
  return find_probabilities(r[0], cube, category)


def calc(i):
  calc_debug_print("calc", i)
  c = calcs[i]
  params = {calc_param_to_key[k]: v for k, v in c.items() if k in calc_param_to_key}
  params["lines"] = find_lines(c[CUBE], c[CATEGORY])
  console.log(str(params["lines"]))
  if not params["lines"]:
    return 0
  res, tier = cube_calc(**params)
  console.log(f"result: {res}")
  console.log(f"resulting tier: {Tier(tier)}")
  return to_js(res)
