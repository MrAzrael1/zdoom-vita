// Harness appended to the generated test translation unit (see
// vita_draw_threads_test.py). O_* are the original single-threaded drawers;
// the un-prefixed functions are the converted stubs, which queue to workers.

static void O_DrawParticle(BYTE *dest, int ycount, int countbase, int spacing, DWORD fg, DWORD *bg2rgb)
{
	do
	{
		int count = countbase;
		do
		{
			DWORD bg = bg2rgb[*dest];
			bg = (fg+bg) | 0x1f07c1f;
			*dest++ = RGB32k.All[bg & (bg>>15)];
		} while (--count);
		dest += spacing;
	} while (--ycount);
}

namespace {
#ifndef TEST_W
#define TEST_W 67	// odd pitch by default
#define TEST_H 150
#endif
const int W = TEST_W, H = TEST_H, GUARD = 2;	// guard rows catch stray writes
std::vector<BYTE> fbA, fbB;
std::mt19937 rng(20261114);
BYTE colormaps[8][256], translations[4][256], shademap[256];	// shade tables hold alpha 0..64
DWORD blend[8][256];
std::vector<BYTE> sources[8];
unsigned failures;

int R(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
DWORD RD() { return rng(); }

typedef void (*Plain)();
typedef DWORD (*Vline1Fn)();
typedef fixed_t (*Vline1Fixed)();

struct Pair { const char *name; Plain orig, queued; int kind; };
#define P(n, k) { #n, (Plain)O_##n, (Plain)n, k }
enum { COL, COLV1, COLV1F, V4, SPN };
const Pair pairs[] = {
	P(R_DrawColumnP_C,COL), P(R_FillColumnP,COL), P(R_FillAddColumn,COL), P(R_FillAddClampColumn,COL),
	P(R_FillSubClampColumn,COL), P(R_FillRevSubClampColumn,COL), P(R_DrawAddColumnP_C,COL),
	P(R_DrawTranslatedColumnP_C,COL), P(R_DrawTlatedAddColumnP_C,COL), P(R_DrawShadedColumnP_C,COL),
	P(R_DrawAddClampColumnP_C,COL), P(R_DrawAddClampTranslatedColumnP_C,COL), P(R_DrawSubClampColumnP_C,COL),
	P(R_DrawSubClampTranslatedColumnP_C,COL), P(R_DrawRevSubClampColumnP_C,COL),
	P(R_DrawRevSubClampTranslatedColumnP_C,COL),
	P(vlinec1,COLV1), P(mvlinec1,COLV1), P(tmvline1_add,COLV1F), P(tmvline1_addclamp,COLV1F),
	P(tmvline1_subclamp,COLV1F), P(tmvline1_revsubclamp,COLV1F),
	P(vlinec4,V4), P(mvlinec4,V4), P(tmvline4_add,V4), P(tmvline4_addclamp,V4),
	P(tmvline4_subclamp,V4), P(tmvline4_revsubclamp,V4),
	P(R_DrawSpanP_C,SPN), P(R_DrawSpanMaskedP_C,SPN), P(R_DrawSpanTranslucentP_C,SPN),
	P(R_DrawSpanMaskedTranslucentP_C,SPN), P(R_DrawSpanAddClampP_C,SPN), P(R_DrawSpanMaskedAddClampP_C,SPN),
	P(R_FillSpan,SPN),
};
const int NUMPAIRS = sizeof(pairs)/sizeof(pairs[0]);
int usage[64];

struct Op
{
	int pair;	// -1: particle
	int x, y, count, bits;
	DWORD iscale, frac;
	int cmap, tl, src, sb, db, color;
	DWORD srccolor;
	DWORD vpl[4], vin[4]; int cm4[4], src4[4];
	int x2, xbits, ybits; DWORD xf, yf, xs, ys;
	int countbase; DWORD fg;
	const BYTE *customsource;
};

BYTE *Base(std::vector<BYTE> &fb) { return fb.data() + GUARD * W; }

void SetCanvas(std::vector<BYTE> &fb)
{
	dc_destorg = Base(fb);
	dc_pitch = W;
	for (int i = 0; i < H; ++i) ylookup[i] = i * W;
}

Op RandomOp()
{
	Op o = {};
	o.pair = R(0, NUMPAIRS); // NUMPAIRS -> particle
	if (o.pair == NUMPAIRS) o.pair = -1;
	o.cmap = R(0,7); o.tl = R(0,3); o.src = R(0,7); o.sb = R(0,7); o.db = R(0,7);
	o.color = R(0,255); o.srccolor = RD();
	if (o.pair < 0)
	{
		o.y = R(0, H-1); o.count = R(1, H - o.y);
		o.x = R(0, W-1); o.countbase = R(1, W - o.x); o.fg = RD();
		return o;
	}
	switch (pairs[o.pair].kind)
	{
	case COL:
		o.x = R(0, W-1); o.y = R(0, H-1); o.count = R(0, H - o.y);
		if (R(0,1)) { o.iscale = R(0, 0x30000); o.frac = R(0, 1<<23); }
		else { o.iscale = DWORD(-R(0, 0x10000)); o.frac = (1u<<24) + R(0, 1<<20); } // flipped sprites
		break;
	case COLV1: case COLV1F:
		o.x = R(0, W-1); o.y = R(0, H-1); o.count = R(1, H - o.y);
		o.bits = R(16, 24); o.iscale = RD(); o.frac = RD();
		break;
	case V4:
		o.x = R(0, W-4); o.y = R(0, H-1); o.count = R(1, H - o.y); o.bits = R(16, 24);
		for (int z = 0; z < 4; ++z) { o.vpl[z] = RD(); o.vin[z] = RD(); o.cm4[z] = R(0,7); o.src4[z] = R(0,7); }
		break;
	case SPN:
		o.y = R(0, H-1); o.x = R(0, W-1); o.x2 = R(o.x, W-1);
		if (R(0,1)) { o.xbits = o.ybits = 6; } else { o.xbits = R(1,8); o.ybits = R(1,8); }
		o.xf = RD(); o.yf = RD(); o.xs = RD(); o.ys = RD();
		break;
	}
	return o;
}

// Returns the chained wall result (frac / vplce) for comparison.
void Apply(const Op &o, std::vector<BYTE> &fb, bool queued, DWORD out[4])
{
	SetCanvas(fb);
	if (o.pair < 0)
	{
		BYTE *dest = ylookup[o.y] + o.x + dc_destorg;
		if (!queued) { O_DrawParticle(dest, o.count, o.countbase, W - o.countbase, o.fg, blend[o.db]); return; }
		FRectArgs a; a.dest = dest; a.ycount = o.count; a.countbase = o.countbase; a.spacing = W - o.countbase;
		a.fg = o.fg; a.bg2rgb = blend[o.db];
		R_SubmitRect(K_DrawParticle, a, o.y);
		return;
	}
	const Pair &p = pairs[o.pair];
	++usage[o.pair];
	dc_colormap = (p.orig == (Plain)O_R_DrawShadedColumnP_C) ? shademap : colormaps[o.cmap];
	dc_translation = translations[o.tl];
	dc_source = o.customsource ? o.customsource : sources[o.src].data();
	dc_srcblend = blend[o.sb]; dc_destblend = blend[o.db]; dc_color = o.color; dc_srccolor = o.srccolor;
	switch (p.kind)
	{
	case COL: case COLV1: case COLV1F:
		dc_dest = ylookup[o.y] + o.x + dc_destorg; dc_count = o.count;
		dc_iscale = fixed_t(o.iscale); dc_texturefrac = fixed_t(o.frac);
		vlinebits = mvlinebits = tmvlinebits = o.bits;
		if (p.kind == COL) (queued ? p.queued : p.orig)();
		else if (p.kind == COLV1) out[0] = ((Vline1Fn)(queued ? p.queued : p.orig))();
		else out[0] = DWORD(((Vline1Fixed)(queued ? p.queued : p.orig))());
		break;
	case V4:
		dc_dest = ylookup[o.y] + o.x + dc_destorg; dc_count = o.count;
		vlinebits = mvlinebits = tmvlinebits = o.bits;
		for (int z = 0; z < 4; ++z)
		{
			vplce[z] = o.vpl[z]; vince[z] = o.vin[z];
			palookupoffse[z] = colormaps[o.cm4[z]]; bufplce[z] = sources[o.src4[z]].data();
		}
		(queued ? p.queued : p.orig)();
		for (int z = 0; z < 4; ++z) out[z] = vplce[z];
		break;
	case SPN:
		ds_y = o.y; ds_x1 = o.x; ds_x2 = o.x2; ds_colormap = colormaps[o.cmap];
		ds_xfrac = o.xf; ds_yfrac = o.yf; ds_xstep = o.xs; ds_ystep = o.ys;
		ds_xbits = o.xbits; ds_ybits = o.ybits; ds_source = sources[o.src].data(); ds_color = o.color;
		(queued ? p.queued : p.orig)();
		break;
	}
}

// A random sprite drawn both ways: the engine's per-column R_DrawMaskedColumn
// with an original drawer as colfunc, and one queued whole-sprite command.
struct SpriteTex
{
	int width, height;
	std::vector<std::vector<BYTE>> pixels;
	std::vector<std::vector<FTexture::Span>> spans;
};
std::vector<SpriteTex> spriteTextures;
unsigned spritesDrawn;

void MakeSpriteTextures()
{
	for (int t = 0; t < 6; ++t)
	{
		SpriteTex tex;
		tex.width = R(1, 48);
		tex.height = R(1, 160);
		for (int x = 0; x < tex.width; ++x)
		{
			std::vector<BYTE> col(tex.height);
			for (auto &v : col) v = BYTE(rng());
			std::vector<FTexture::Span> sp;
			int y = 0;
			while (true)
			{
				int top = y + R(0, 10), len = R(1, 40);
				if (top + len > tex.height) break;
				sp.push_back({ WORD(top), WORD(len) });
				y = top + len;
			}
			sp.push_back({ 0, 0 });
			tex.pixels.push_back(col);
			tex.spans.push_back(sp);
		}
		spriteTextures.push_back(tex);
	}
}

void ApplySprite(std::vector<BYTE> &fb, bool queued, int pairIndex, int texIndex, unsigned seed)
{
	std::mt19937 local(seed);	// identical parameters for both paths
	auto LR = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(local); };
	const Pair &p = pairs[pairIndex];
	const SpriteTex &tex = spriteTextures[texIndex];
	SetCanvas(fb);
	dc_colormap = (p.orig == (Plain)O_R_DrawShadedColumnP_C) ? shademap : colormaps[LR(0,7)];
	dc_translation = translations[LR(0,3)];
	dc_srcblend = blend[LR(0,7)]; dc_destblend = blend[LR(0,7)];
	dc_color = LR(0,255); dc_srccolor = DWORD(local());
	// R_DrawVisSprite's setup
	fixed_t yscale = LR(12000, 260000);
	fixed_t texturemid = LR(-60, 220) * FRACUNIT;
	centeryfrac = (H / 2) << FRACBITS;
	spryscale = yscale;
	dc_iscale = fixed_t(0xffffffffu / unsigned(yscale));
	dc_texturemid = texturemid;
	if (LR(0, 3) == 0)
	{
		sprflipvert = true;
		spryscale = -spryscale;
		dc_iscale = -dc_iscale;
		dc_texturemid -= tex.height << FRACBITS;
		sprtopscreen = centeryfrac + FixedMul(dc_texturemid, spryscale);
	}
	else
	{
		sprflipvert = false;
		sprtopscreen = centeryfrac - FixedMul(dc_texturemid, spryscale);
	}
	static short floorclip[TEST_W], ceilingclip[TEST_W];
	// Often put the clip limits within a couple of rows of the sprite's own top
	// and bottom, where the post-end adjustments in R_DrawMaskedColumn apply.
	int spriteTop = (sprtopscreen >> FRACBITS), spriteBottom = ((sprtopscreen + spryscale * tex.height) >> FRACBITS);
	if (spriteTop > spriteBottom) std::swap(spriteTop, spriteBottom);
	const bool nearEdges = LR(0, 2) != 0;
	for (int x = 0; x < W; ++x)
	{
		if (nearEdges)
		{
			ceilingclip[x] = short(std::max(0, std::min(H, spriteTop + LR(-2, 2))));
			floorclip[x] = short(std::max(0, std::min(H, spriteBottom + LR(-3, 3))));
			continue;
		}
		ceilingclip[x] = short(LR(0, H / 2));
		floorclip[x] = short(LR(0, 5) ? LR(ceilingclip[x], H) : LR(0, H));	// sometimes crossed
	}
	mfloorclip = floorclip; mceilingclip = ceilingclip;
	int x1 = LR(0, W - 1), x2 = LR(x1 + 1, W);
	fixed_t frac = LR(0, tex.width << FRACBITS), xiscale = LR(1 << 13, 3 << FRACBITS);
	auto column = [&](fixed_t f) { return int((unsigned(f) >> FRACBITS) % unsigned(tex.width)); };
	if (!queued)
	{
		colfunc = p.orig;
		for (dc_x = x1; dc_x < x2; ++dc_x, frac += xiscale)
		{
			int c = column(frac);
			R_DrawMaskedColumn(tex.pixels[c].data(), tex.spans[c].data());
		}
		return;
	}
	FColumnKernel kernel = R_GetColumnKernel(p.queued);
	if (!kernel) { fprintf(stderr, "no kernel for %s\n", p.name); fflush(stderr); _Exit(1); }
	static std::vector<FSpriteColumn> inlineCols;
	FSpriteColumn *cols = (FSpriteColumn *)R_DrawerFrameAlloc((x2 - x1) * sizeof(FSpriteColumn));
	if (!cols) { inlineCols.resize(x2 - x1); cols = inlineCols.data(); }	// threads off: drawn immediately
	for (int i = 0; i < x2 - x1; ++i, frac += xiscale)
	{
		int c = column(frac);
		cols[i].pixels = tex.pixels[c].data();
		cols[i].spans = tex.spans[c].data();
		cols[i].floorclip = floorclip[x1 + i];
		cols[i].ceilingclip = ceilingclip[x1 + i];
	}
	FSpriteCmd cmd;
	cmd.kernel = kernel;
	R_CaptureColumnArgs(cmd.base);
	cmd.cols = cols; cmd.destorg = dc_destorg; cmd.x1 = x1; cmd.ncols = x2 - x1;
	cmd.sprtopscreen = sprtopscreen; cmd.spryscale = spryscale; cmd.iscale = dc_iscale;
	cmd.texturemid = dc_texturemid; cmd.centeryfrac = centeryfrac; cmd.flip = sprflipvert;
	R_SubmitBanded(K_DrawSpriteColumns, &cmd, sizeof(cmd));
	++spritesDrawn;
}

void Compare(const char *where, int window)
{
	if (fbA == fbB) return;
	size_t i = 0;
	while (fbA[i] == fbB[i]) ++i;
	int row = int(i / W) - GUARD, col = int(i % W);
	fprintf(stderr, "MISMATCH %s window %d at row %d col %d: %d vs %d\n", where, window, row, col, fbA[i], fbB[i]);
	if (++failures > 5) { fflush(stdout); fflush(stderr); _Exit(1); }
	fbB = fbA;
}

void RunWindow(int window, int numops, int mainshare, bool threads)
{
	vita_drawthreads = threads;
	vita_drawmainshare = mainshare;
	// Mostly the adaptive balance; sometimes extreme or random worker splits,
	// and sometimes a view shorter than the canvas (rows below go to worker 2).
	int pick = R(0, 9);
	R_DrawerForceSplitForTest(pick == 0 ? 32 : pick == 1 ? 992 : pick == 2 ? R(32, 992) : -1);
	viewheight = R(0, 3) ? H : R(1, H);
	R_BeginDrawerQueue();
	for (int n = 0; n < numops; ++n)
	{
		int event = R(0, 199);
		if (event == 0)
		{	// mid-window sync: everything queued so far must be on screen
			R_DrawerSync();
			Compare("after R_DrawerSync", window);
		}
		else if (event == 1)
		{	// fuzz-like direct access: copy neighbouring rows inline
			FDrawerInlineScope scope;
			int y = R(1, H-2), x = R(0, W-1), len = R(1, W - x);
			memcpy(Base(fbA) + y*W + x, Base(fbA) + (y-1)*W + x, len);
			memcpy(Base(fbB) + y*W + x, Base(fbB) + (y-1)*W + x, len);
			Op o = RandomOp();	// drawers inside the scope run inline
			DWORD a[4] = {}, b[4] = {};
			Apply(o, fbA, false, a); Apply(o, fbB, true, b);
			Compare("inside FDrawerInlineScope", window);
		}
		else if (event == 2 && threads)
		{	// queued columns read arena memory, then the arena is recycled
			BYTE *mem = R_DrawerFrameAlloc(65536);
			if (!mem) { fprintf(stderr, "arena unavailable\n"); { fflush(stdout); fflush(stderr); _Exit(1); } }
			for (int i = 0; i < 65536; ++i) mem[i] = BYTE(rng());
			std::vector<BYTE> copy(mem, mem + 65536);
			Op o = RandomOp();
			while (o.pair < 0 || pairs[o.pair].kind != COL) o = RandomOp();
			DWORD a[4] = {}, b[4] = {};
			o.customsource = copy.data(); Apply(o, fbA, false, a);
			o.customsource = mem; Apply(o, fbB, true, b);
			for (int k = 0; k < 9; ++k)	// > 512 KiB: forces a sync + arena reset
			{
				BYTE *junk = R_DrawerFrameAlloc(65536);
				memset(junk, 0xEE, 65536);
			}
		}
		if (R(0, 9) == 0)
		{	// a whole sprite (any queueable column drawer)
			int pair;
			do pair = R(0, NUMPAIRS - 1); while (pairs[pair].kind != COL);
			int t = R(0, int(spriteTextures.size()) - 1);
			unsigned seed = rng();
			ApplySprite(fbA, false, pair, t, seed);
			ApplySprite(fbB, true, pair, t, seed);
		}
		Op o = RandomOp();
		DWORD a[4] = {}, b[4] = {};
		Apply(o, fbA, false, a);
		Apply(o, fbB, true, b);
		if (memcmp(a, b, sizeof(a)) != 0)
		{
			fprintf(stderr, "chained position mismatch for %s: %08x vs %08x\n", pairs[o.pair].name, a[0], b[0]);
			{ fflush(stdout); fflush(stderr); _Exit(1); }
		}
	}
	R_EndDrawerQueue();
	Compare("end of window", window);
}
// Parallel rows: a palette conversion split between the game thread and both
// workers must equal the serial result and touch every row exactly once.
struct ConvertCtx { const BYTE *src; DWORD *dst; const DWORD *pal; int width; int *hits; };
void ConvertJob(void *p, int y0, int y1)
{
	ConvertCtx *c = (ConvertCtx *)p;
	for (int y = y0; y < y1; ++y)
	{
		++c->hits[y];
		for (int x = 0; x < c->width; ++x) c->dst[y * c->width + x] = c->pal[c->src[y * c->width + x]];
	}
}

void CheckParallelRows(int rows, bool threads)
{
	vita_drawthreads = threads;
	std::vector<BYTE> src(size_t(rows) * W);
	for (auto &v : src) v = BYTE(rng());
	std::vector<DWORD> serial(src.size()), parallel(src.size(), 0xDEADBEEF);
	std::vector<int> hits(rows, 0);
	DWORD pal[256];
	for (auto &v : pal) v = rng();
	for (size_t i = 0; i < src.size(); ++i) serial[i] = pal[src[i]];
	ConvertCtx ctx = { src.data(), parallel.data(), pal, W, hits.data() };
	R_DrawerParallelRows(ConvertJob, &ctx, rows);
	if (parallel != serial) { fprintf(stderr, "parallel rows mismatch (%d rows)\n", rows); fflush(stderr); _Exit(1); }
	for (int y = 0; y < rows; ++y)
		if (hits[y] != 1) { fprintf(stderr, "row %d converted %d times\n", y, hits[y]); fflush(stderr); _Exit(1); }
}
} // namespace

int main()
{
	for (int i = 0; i < (1 << 17); ++i) RGB32k.All[i] = BYTE(rng());
	for (auto &m : colormaps) for (int i = 0; i < 256; ++i) m[i] = BYTE(rng());
	for (auto &t : translations) for (int i = 0; i < 256; ++i) t[i] = BYTE(rng());
	for (int i = 0; i < 256; ++i) shademap[i] = BYTE(rng() % 65);
	for (auto &b : blend) for (int i = 0; i < 256; ++i) b[i] = rng();
	for (auto &b : Col2RGB8) for (int i = 0; i < 256; ++i) b[i] = rng();
	for (auto &s : sources)
	{
		s.resize(65536);
		for (auto &v : s) v = (rng() % 5) ? BYTE(rng() | 1) : 0;	// holes for masked drawers
	}
	MakeSpriteTextures();
	if (R_GetColumnKernel((Plain)O_DrawParticle) != NULL) { fprintf(stderr, "unexpected kernel for a non-drawer\n"); return 1; }
	fbA.resize(W * (H + 2*GUARD));
	for (auto &v : fbA) v = BYTE(rng());
	fbB = fbA;

	int window = 0;
	for (int round = 0; round < 60; ++round)
		for (int share = 0; share <= 4; ++share)
		{
			RunWindow(window++, R(1, 300), share, true);
			CheckParallelRows(R(1, 2*H), true);	// like the per-frame conversion
		}
	for (int i = 0; i < 20; ++i) RunWindow(window++, R(1, 300), R(0,4), false);	// threads off: inline
	RunWindow(window++, 30000, 0, true);	// ring wrap-around (8192 slots), workers only
	RunWindow(window++, 30000, 3, true);	// ring wrap-around with a game-thread share
	for (int rows : {1, 5, 8, 9, 10, 272, 544}) { CheckParallelRows(rows, true); CheckParallelRows(rows, false); }
	R_ShutdownDrawerThreads();

	for (int i = 0; i < NUMPAIRS; ++i)
	{
		if (!usage[i]) { fprintf(stderr, "drawer never exercised: %s\n", pairs[i].name); return 1; }
	}
	if (failures) return 1;
	if (spritesDrawn < 500) { fprintf(stderr, "too few sprites exercised (%u)\n", spritesDrawn); return 1; }
	printf("PASS (%dx%d canvas): %d drawers, particles and %u whole sprites identical to the original across %d windows, shares 0-4, syncs, inline scopes, arena resets, adaptive/forced worker splits, short views, parallel-row jobs and ring wrap\n", W, H, NUMPAIRS, spritesDrawn, window);
	return 0;
}
