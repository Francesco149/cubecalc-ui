from pyodide.ffi import to_js

from cubecalc import cube_calc
from common import *
import kms
import tms

@global_enum
class CalcParam(IntEnum):
  WANTS = auto()
  CUBE = auto()
  TIER = auto()
  CATEGORY = auto()

calc_param_to_key = {
  WANTS: "wants",
  CUBE: "type",
  TIER: "tier",
}

calcs = {}

def calc_free(i):
  if i in calcs:
    calcs.pop(i)

def calc_ensure(i):
  if i not in calcs:
    calcs[i] = {}

def calc_set(i, k, v):
  calc_ensure(i)
  calcs[i][k] = v

def calc_want_clear(i):
  calc_ensure(i)
  c = calcs[i]
  if WANTS in c:
    c[WANTS] = {}

def calc_want(i, k, v):
  calc_ensure(i)
  c = calcs[i]
  if WANTS not in c:
    c[WANTS] = {}
  c[WANTS][k] = v

def find_lines(cube, category):
  data = tms if cube & reduce(or_, tms.keys()) else kms
  return find_probabilities(data, cube, category)

def calc(i):
  c = calcs[i]
  params = {calc_param_to_key[x] for x in c if x in calc_param_to_key}
  params["lines"] = find_lines(c[CUBE], c[CATEGORY])
  return to_js(cube_calc(**params))
