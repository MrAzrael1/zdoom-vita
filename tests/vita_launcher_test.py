from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='vita-launcher-test-') as folder:
 p=Path(folder)
 subprocess.run(['g++','-std=c++17','-Isrc','-idirafter','/usr/local/vitasdk/arm-vita-eabi/include','-I/usr/local/vitasdk/arm-vita-eabi/include/SDL2','-fsanitize=address,undefined','-fno-omit-frame-pointer','tests/vita_launcher_test.cpp','-o',str(p/'launcher')],cwd=root,check=True)
 subprocess.run([str(p/'launcher'),str(p)],cwd=p,check=True)
