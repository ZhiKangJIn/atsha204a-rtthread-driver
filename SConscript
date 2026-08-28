from building import *
import os

cwd = GetCurrentDir()
src = Glob(os.path.join(cwd, 'src', '*.c'))
group = DefineGroup('ATSHA204A_Driver', src, CPPPATH=[os.path.join(cwd, 'include')])

Return('group')
