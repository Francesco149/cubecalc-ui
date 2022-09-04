from pyodide.ffi import to_js

from cubecalc import cube_calc
from datautils import find_probabilities
from functools import reduce
from operator import or_, and_

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

def stringify_want(want):
  if isinstance(want, dict):
    return "{ " + ", ".join([f"{bitenum_to_str(Line, k)}: {v}" for k, v in want.items()]) + " }"
  if isinstance(want, tuple):
    op, n = want
  else:
    op = want
    n = -1
  return f"<{op.__name__}, {n}>"

def stringify_wants(x):
  return "[" + ", ".join([stringify_want(want) for want in x]) + "]"

calc_param_stringify = {
  WANTS: stringify_wants,
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
  return calcs[i]


noop = lambda *args, **kwargs: None
calc_debug_print = noop
dbg = noop
dbgcall = noop

def calc_debug_print_(pre, i):
  if i not in calcs:
    return "(not present)"
  dbg(
    f"{pre} calc {i} = " +
    "; ".join([f"{CalcParam(k).name}: {calc_param_stringify[k](v)}" for k, v in calcs[i].items()])
  )


def dbgcall_(f):
  return f()


def calc_debug(enabled):
  global calc_debug_print
  global dbg
  global dbgcall
  calc_debug_print = calc_debug_print_ if enabled else noop
  dbg = console.log if enabled else noop
  dbgcall = dbgcall_ if enabled else noop


def calc_set(i, k, v):
  c = calc_ensure(i)
  c[k] = v
  calc_debug_print("set", i)


def calc_want_push(i):
  c = calc_ensure(i)
  if WANTS not in c:
    c[WANTS] = []
  res = 0
  if (not len(c[WANTS])) or c[WANTS][-1]:
    c[WANTS].append({})
    res = 1
  calc_debug_print("push", i)
  return to_js(res)


def calc_want_len(i):
  c = calc_ensure(i)
  return to_js(len(c[WANTS]) if WANTS in c else 0)


def calc_want_current_len(i):
  def doit():
    c = calc_ensure(i)
    if WANTS not in c:
      return 0
    if not len(c[WANTS]):
      return 0
    if isinstance(c[WANTS][-1], dict):
      return len(c[WANTS][-1])
    return -1
  return to_js(doit())


def calc_want_ensure(i):
  c = calc_ensure(i)
  if WANTS not in c or not len(c[WANTS]):
    calc_want_push(i)
  return c


def calc_want(i, k, v):
  c = calc_want_ensure(i)
  c[WANTS][-1][k] = v
  calc_debug_print("want", i)


op_conversion = {
  OR: or_,
  AND: and_,
}


def calc_want_op(i, op, n):
  c = calc_want_ensure(i)
  if c[WANTS][-1]:
    calc_want_push(i)
  if n < 0:
    c[WANTS][-1] = op_conversion[op]
  else:
    c[WANTS][-1] = (op_conversion[op], n)
  calc_debug_print("want", i)


def find_lines(cube, category):
  r = [x for x in [kms, tms, familiars, red_card_estimate] if cube & reduce(or_, x.keys())]
  if len(r) <= 0:
    return {}
  return find_probabilities(r[0], cube, category)


def calc(i):
  calc_debug_print("calc", i)
  c = calc_ensure(i)
  c[WANTS] = [x for x in c[WANTS] if x]
  if not len(c[WANTS]):
    return to_js(0.0) # just in case something goes wrong
  params = {calc_param_to_key[k]: v for k, v in c.items() if k in calc_param_to_key}
  params["lines"] = find_lines(c[CUBE], c[CATEGORY])
  dbgcall(lambda: dbg(str(params["lines"])))
  if not params["lines"]:
    return 0
  res, tier, good = cube_calc(**params)
  c[MATCHING] = [x.tolist() for x in good.lines]
  def dbgprint():
    dbg(f"result: {res}")
    dbg(f"resulting tier: {Tier(tier)}")
  dbgcall(dbgprint)
  return to_js(res)


def calc_matching_len(i):
  c = calcs[i]
  return to_js(len(c[MATCHING][0]))


def calc_matching_combo_len(i):
  c = calcs[i]
  return to_js(len(c[MATCHING][0][0]))


def calc_matching_lines(i):
  c = calcs[i]
  types, values, onein, is_prime = c[MATCHING]
  return to_js(types)


def calc_matching_values(i):
  c = calcs[i]
  types, values, onein, is_prime = c[MATCHING]
  return to_js(values)


def calc_matching_probabilities(i):
  c = calcs[i]
  types, values, onein, is_prime = c[MATCHING]
  return to_js(onein)


def calc_matching_is_primes(i):
  c = calcs[i]
  types, values, onein, is_prime = c[MATCHING]
  return to_js(is_prime)
