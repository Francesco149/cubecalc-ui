from common import *

@global_enum
class CalcParam(IntEnum):
  WANTS = auto()
  CUBE = auto()
  TIER = auto()
  CATEGORY = auto()
  LEVEL = auto()
  REGION = auto()
  MATCHING = auto()

@global_enum
class CalcOperator(IntEnum):
  OR = auto()
  AND = auto()
