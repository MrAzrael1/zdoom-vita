# Offline atlas generation using the system FreeType library; no Vita font dependency.
import ctypes as C, ctypes.util, sys
class Generic(C.Structure): _fields_=[("data",C.c_void_p),("finalizer",C.c_void_p)]
class Box(C.Structure): _fields_=[(n,C.c_long) for n in ("xmin","ymin","xmax","ymax")]
class Vector(C.Structure): _fields_=[("x",C.c_long),("y",C.c_long)]
class Bitmap(C.Structure):
    _fields_=[("rows",C.c_uint),("width",C.c_uint),("pitch",C.c_int),("buffer",C.POINTER(C.c_ubyte)),("num_grays",C.c_ushort),("pixel_mode",C.c_ubyte),("palette_mode",C.c_ubyte),("palette",C.c_void_p)]
class Slot(C.Structure):
    _fields_=[("library",C.c_void_p),("face",C.c_void_p),("next",C.c_void_p),("index",C.c_uint),("generic",Generic),("metrics",C.c_long*8),("linear_hori",C.c_long),("linear_vert",C.c_long),("advance",Vector),("format",C.c_uint),("bitmap",Bitmap),("left",C.c_int),("top",C.c_int)]
class Face(C.Structure):
    _fields_=[("num_faces",C.c_long),("face_index",C.c_long),("face_flags",C.c_long),("style_flags",C.c_long),("num_glyphs",C.c_long),("family",C.c_char_p),("style",C.c_char_p),("num_fixed",C.c_int),("sizes",C.c_void_p),("num_charmaps",C.c_int),("charmaps",C.c_void_p),("generic",Generic),("bbox",Box),("units",C.c_ushort),("ascender",C.c_short),("descender",C.c_short),("height",C.c_short),("max_advance_width",C.c_short),("max_advance_height",C.c_short),("underline_position",C.c_short),("underline_thickness",C.c_short),("glyph",C.POINTER(Slot))]
ft=C.CDLL(ctypes.util.find_library("freetype"))
lib=C.c_void_p(); assert ft.FT_Init_FreeType(C.byref(lib))==0
face=C.POINTER(Face)(); assert ft.FT_New_Face(lib,sys.argv[1].encode(),0,C.byref(face))==0
assert ft.FT_Set_Pixel_Sizes(face,0,24)==0
atlas=bytearray(512*192)
for i in range(96):
    assert ft.FT_Load_Char(face,i+32,4)==0
    s=face.contents.glyph.contents; b=s.bitmap
    assert b.pixel_mode==2 or not b.rows
    for y in range(b.rows):
        for x in range(b.width):
            dx=max(0,s.left)+x; dy=26-s.top+y
            if 0<=dx<32 and 0<=dy<32:
                atlas[((i//16)*32+dy)*512+(i%16)*32+dx]=b.buffer[y*b.pitch+x]
assert sum(atlas)>0
open(sys.argv[2],"wb").write(atlas)
print("Rasterized",face.contents.family.decode(),len(atlas),"bytes")
ft.FT_Done_Face(face);ft.FT_Done_FreeType(lib)