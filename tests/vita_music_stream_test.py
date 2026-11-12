from pathlib import Path
import tempfile,subprocess,struct
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='vita-music-test-') as d:
 p=Path(d)
 wad=r/'build-vita/labs/DoomMetalVol5_44100.wad'
 if not wad.exists():raise SystemExit('Missing private fixture')
 with wad.open('rb') as f:
  _,n,off=struct.unpack('<4sii',f.read(12));f.seek(off)
  lumps=[struct.unpack('<ii8s',f.read(16)) for _ in range(n)]
  paths=[]
  for pos,size,name in lumps:
   if name.startswith(b'D_') and size:
    f.seek(pos);out=p/(name.rstrip(b'\0').decode()+'.bin');out.write_bytes(f.read(size));paths.append(str(out))
 (p/'manifest').write_text('\n'.join(paths))
 flags=['g++','-std=c++17','-fno-rtti','-Dstricmp=strcasecmp','-Dstrnicmp=strncasecmp','-include','strings.h',
 '-Isrc','-Isrc/sound','-Izlib','-Ibzip2','-I/usr/local/vitasdk/arm-vita-eabi/include/SDL2',
 '-fsanitize=address,undefined','-fno-omit-frame-pointer','-O1']
 subprocess.run(flags+['tests/vita_sdl_audio_test.cpp','src/sound/vita_vorbis.cpp','-o',str(p/'audio')],cwd=r,check=True)
 subprocess.run([str(p/'audio'),'--music',str(p/'manifest')],check=True)
