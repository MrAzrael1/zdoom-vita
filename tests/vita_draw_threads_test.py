#!/usr/bin/env python3
"""Differential test for multicore drawing (r_draw_mt.cpp).

Builds the actual queue/worker code, the actual converted drawer kernels and
submit stubs from src/r_draw.cpp, and the ORIGINAL single-threaded drawers from
git HEAD~ (or a supplied reference file), then draws thousands of random
batches of columns, wall slices, spans and particles both ways. Every pixel
and every chained wall texture position must match exactly, for every
game-thread row share, across ring wrap-around, mid-window syncs and frame
arena resets. Runs under ASan/UBSan and under ThreadSanitizer.

Usage: tests/vita_draw_threads_test.py [reference_r_draw.cpp]
"""
import os, re, subprocess, sys, tempfile, textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDL_INC = os.environ.get('SDL2_INCLUDE', '/usr/local/vitasdk/arm-vita-eabi/include/SDL2')

COLUMN = ['R_DrawColumnP_C', 'R_FillColumnP', 'R_FillAddColumn', 'R_FillAddClampColumn',
          'R_FillSubClampColumn', 'R_FillRevSubClampColumn', 'R_DrawAddColumnP_C',
          'R_DrawTranslatedColumnP_C', 'R_DrawTlatedAddColumnP_C', 'R_DrawShadedColumnP_C',
          'R_DrawAddClampColumnP_C', 'R_DrawAddClampTranslatedColumnP_C', 'R_DrawSubClampColumnP_C',
          'R_DrawSubClampTranslatedColumnP_C', 'R_DrawRevSubClampColumnP_C',
          'R_DrawRevSubClampTranslatedColumnP_C']
VLINE1 = ['vlinec1', 'mvlinec1', 'tmvline1_add', 'tmvline1_addclamp', 'tmvline1_subclamp', 'tmvline1_revsubclamp']
VLINE4 = ['vlinec4', 'mvlinec4', 'tmvline4_add', 'tmvline4_addclamp', 'tmvline4_subclamp', 'tmvline4_revsubclamp']
SPAN = ['R_DrawSpanP_C', 'R_DrawSpanMaskedP_C', 'R_DrawSpanTranslucentP_C', 'R_DrawSpanMaskedTranslucentP_C',
        'R_DrawSpanAddClampP_C', 'R_DrawSpanMaskedAddClampP_C', 'R_FillSpan']

def norm(text):
    return text.replace('\r\n', '\n')

def function(src, name):
    m = re.search(r'^([^\n;{}]*\b' + re.escape(name) + r'\s*\([^)]*\))\s*\n\{', src, re.M)
    if not m:
        raise SystemExit('missing function ' + name)
    i = src.index('{', m.end() - 1)
    depth = 0
    j = i
    while True:
        if src[j] == '{': depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0: break
        j += 1
    return src[m.start():j + 1]

def strip_rangecheck(t):
    return re.sub(r'#ifdef RANGECHECK.*?#endif\s*\n', '', t, flags=re.S)

def main():
    if len(sys.argv) > 1:
        ref = norm(Path(sys.argv[1]).read_text())
    else:
        ref = norm(subprocess.run(['git', 'show', 'HEAD~:src/r_draw.cpp'], cwd=ROOT,
                                  capture_output=True, text=True, check=True).stdout)
        if 'R_SubmitColumn' in ref:
            raise SystemExit('HEAD~ already has queued drawers; pass the original r_draw.cpp')
    new = norm((ROOT / 'src/r_draw.cpp').read_text())
    things = norm((ROOT / 'src/r_things.cpp').read_text())
    names = COLUMN + VLINE1 + VLINE4 + SPAN

    originals, kernels = [], []
    for n in names:
        o = strip_rangecheck(function(ref, n))
        o = re.sub(r'\b' + n + r'\b', 'O_' + n, o, count=1)
        o = o.replace('STACK_ARGS ', '')
        originals.append(o)
        kernels.append(strip_rangecheck(function(new, 'K_' + n)))
        kernels.append(function(new, n).replace('STACK_ARGS ', ''))
    kernels.append(function(things, 'K_DrawParticle'))
    # (function pointer parameter: take it from its definition to the end of file)
    kernels.append(new[new.index('FColumnKernel R_GetColumnKernel(void (*drawer)(void))'):])
    # Whole-sprite commands: the queued kernel block and, as the reference,
    # the engine's own per-column R_DrawMaskedColumn (calling colfunc).
    m = re.search(r'// ---- queued sprite columns \(begin\) ----.*?// ---- queued sprite columns \(end\) ----', things, re.S)
    if not m:
        raise SystemExit('missing queued sprite block in r_things.cpp')
    kernels.append(m.group(0))
    kernels.append(function(things, 'R_DrawMaskedColumn'))

    prelude = textwrap.dedent(r'''
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>
    #include <cstdint>
    #include <vector>
    #include <random>
    #include "r_draw.h"
    #define FRACBITS 16
    typedef int fixed_t;
    typedef DWORD dsfixed_t;
    typedef BYTE lighttable_t;
    extern "C" {
    int ylookup[MAXHEIGHT]; BYTE *dc_destorg; int dc_pitch;
    lighttable_t *dc_colormap; int dc_x, dc_yl, dc_yh; fixed_t dc_iscale, dc_texturemid, dc_texturefrac;
    int dc_color; DWORD dc_srccolor; DWORD *dc_srcblend, *dc_destblend; const BYTE *dc_source;
    BYTE *dc_dest; int dc_count; DWORD vplce[4], vince[4]; BYTE *palookupoffse[4]; const BYTE *bufplce[4];
    int ds_y, ds_x1, ds_x2; lighttable_t *ds_colormap; dsfixed_t ds_xfrac, ds_yfrac, ds_xstep, ds_ystep;
    int ds_xbits, ds_ybits; fixed_t ds_alpha; const BYTE *ds_source; int ds_color;
    int viewheight;
    }
    BYTE *dc_translation;
    int vlinebits, mvlinebits, tmvlinebits;
    // (fg & (fg>>15)) stays below 2^17, so a 2^17 table keeps random blend
    // tables in bounds while exercising the same arithmetic as RGB32k.
    struct { BYTE All[1 << 17]; } RGB32k;
    typedef uint16_t WORD;
    struct FTexture { struct Span { WORD TopOffset; WORD Length; }; };
    #define FRACUNIT (1 << FRACBITS)
    static inline fixed_t FixedMul(fixed_t a, fixed_t b) { return fixed_t((int64_t(a) * b) >> FRACBITS); }
    template<class T> static inline void swapvalues(T &a, T &b) { T t = a; a = b; b = t; }
    fixed_t sprtopscreen, spryscale, centeryfrac; bool sprflipvert;
    short *mfloorclip, *mceilingclip;
    void (*colfunc)(void);
    DWORD Col2RGB8[65][256];	// read-only blend table (shaded columns)
    extern int vita_drawthreads, vita_drawshare;
    ''')
    body = prelude + '\n'.join(originals) + '\n' + '\n'.join(kernels) + '\n'
    body += (ROOT / 'tests/vita_draw_threads_harness.inc').read_text()

    stubs = {
        'r_local.h': '',
        'r_draw.h': textwrap.dedent('''
            #include "r_draw_mt.h"
            #define MAXHEIGHT 3600
            typedef BYTE lighttable_t;
            extern "C" { extern int ylookup[MAXHEIGHT]; extern BYTE *dc_destorg; extern int dc_pitch;
            extern lighttable_t *dc_colormap; extern int dc_iscale, dc_texturefrac, dc_color; extern DWORD dc_srccolor;
            extern DWORD *dc_srcblend, *dc_destblend; extern const BYTE *dc_source; extern BYTE *dc_dest; extern int dc_count;
            extern DWORD vplce[4], vince[4]; extern BYTE *palookupoffse[4]; extern const BYTE *bufplce[4];
            extern int ds_y, ds_x1, ds_x2; extern lighttable_t *ds_colormap; extern DWORD ds_xfrac, ds_yfrac, ds_xstep, ds_ystep;
            extern int ds_xbits, ds_ybits; extern const BYTE *ds_source; extern int ds_color;
            extern int viewheight; }
            extern BYTE *dc_translation;
            '''),
        'c_cvars.h': textwrap.dedent('''
            #define CVAR(type,name,def,flags) int name = def;
            #define CUSTOM_CVAR(type,name,def,flags) int name = def; static void name##_changed(int &self)
            '''),
        'i_system.h': '#define atterm(f) ((void)0)\n',
        'doomtype.h': '#include <cstddef>\n#include <cstdint>\ntypedef unsigned char BYTE;\ntypedef uint32_t DWORD;\n',
    }
    keep = os.environ.get('KEEP_TEST_DIR')
    with tempfile.TemporaryDirectory(prefix='vita-draw-mt-') as d:
        d = Path(keep) if keep else Path(d)
        d.mkdir(exist_ok=True)
        (d / 'inc').mkdir(exist_ok=True)
        for k, v in stubs.items():
            (d / 'inc' / k).write_text(v)
        (d / 'inc' / 'r_draw_mt.h').write_text(norm((ROOT / 'src/r_draw_mt.h').read_text()))
        (d / 'test.cpp').write_text(body)
        (d / 'mt.cpp').write_text(norm((ROOT / 'src/r_draw_mt.cpp').read_text()))
        (d / 'sdl_stub.cpp').write_text((ROOT / 'tests/vita_draw_threads_sdl.inc').read_text())
        base = ['g++', '-std=c++17', '-g', '-O1', '-fno-omit-frame-pointer', '-I' + str(d / 'inc'),
                '-I' + SDL_INC, '-pthread', str(d / 'test.cpp'), str(d / 'mt.cpp'), str(d / 'sdl_stub.cpp')]
        # R_DRAW_MT_VERIFY cross-checks the divide-free row lookup against a real
        # division for every queued command; 480x272 is the Vita canvas.
        runs = (('ASan/UBSan', '-fsanitize=address,undefined', 67, 150),
                ('ASan/UBSan', '-fsanitize=address,undefined', 480, 272),
                ('ThreadSanitizer', '-fsanitize=thread', 67, 150))
        for label, san, w, h in runs:
            exe = d / f't_{label[:4]}_{w}'
            subprocess.run(base + [san, '-DR_DRAW_MT_VERIFY', f'-DTEST_W={w}', f'-DTEST_H={h}', '-o', str(exe)], check=True)
            env = dict(os.environ, TSAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            r = subprocess.run([str(exe)], env=env)
            if r.returncode:
                raise SystemExit(f'{label} run failed ({r.returncode})')
            print(f'PASS under {label} ({w}x{h})')

if __name__ == '__main__':
    main()
