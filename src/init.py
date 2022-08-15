async def init():
  import micropip
  import sys
  from js import console
  console.log(f"hello from pyodide, python version is {sys.version}")
  await micropip.install("numpy")
