# Integration test actual SDL LoadSound and mixer using privately supplied mod assets.
from pathlib import Path
import zipfile,tempfile,subprocess,sys
root=Path(__file__).resolve().parents[1]
mod=Path(sys.argv[1]) if len(sys.argv)>1 else root/'build-vita/brutalv20b.pk3'
with tempfile.TemporaryDirectory(prefix='vita-ogg-test-') as folder:
 p=Path(folder);manifest=[]
 with zipfile.ZipFile(mod) as z:
  for name in z.namelist():
   if not name.lower().startswith('sounds/') or name.endswith('/'):continue
   data=z.read(name)
   if data[:4]!=b'OggS':continue
   at=data.find(b'\x01vorbis')
   if at<0:continue
   channels=data[at+11];rate=int.from_bytes(data[at+12:at+16],'little')
   path=p/(str(len(manifest))+'.ogg');path.write_bytes(data)
   basename=name.rsplit('/',1)[-1].split('.')[0].upper()
   play=int(basename in {'DSRIFLE','CHGNSHOT','DSRLAUN','RLAUNCH1','DSMINIGN','DSRIFL1','DSRIFL2'})
   manifest.append(f'{path} {rate} {channels} {play}\n')
 (p/'manifest.txt').write_text(''.join(manifest))
 args=['g++','-std=c++17','-fno-rtti','-Dstricmp=strcasecmp','-Dstrnicmp=strncasecmp','-include','strings.h',
       '-Isrc','-Isrc/sound','-Izlib','-Ibzip2','-I/usr/local/vitasdk/arm-vita-eabi/include/SDL2',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer',
       'tests/vita_sdl_audio_test.cpp','src/sound/vita_vorbis.cpp','-o',str(p/'test')]
 subprocess.run(args,cwd=root,check=True)
 subprocess.run([str(p/'test'),str(p/'manifest.txt')],check=True)