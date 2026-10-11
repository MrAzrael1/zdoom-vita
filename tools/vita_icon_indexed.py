# Convert a 128x128 RGB raster to Vita's 8-bit indexed PNG, using stdlib only.
import collections, struct, zlib, sys
raw=open(sys.argv[1],"rb").read()
assert len(raw)==128*128*3
colors=collections.Counter(tuple(raw[i:i+3]) for i in range(0,len(raw),3))
boxes=[list(colors)]
while len(boxes)<256:
    candidates=[b for b in boxes if len(b)>1]
    if not candidates: break
    def ranges(b): return [max(c[k] for c in b)-min(c[k] for c in b) for k in range(3)]
    box=max(candidates,key=lambda b:max(ranges(b))*sum(colors[c] for c in b))
    axis=max(range(3),key=lambda k:ranges(box)[k])
    box.sort(key=lambda c:c[axis]); total=sum(colors[c] for c in box); acc=0; cut=1
    for i,c in enumerate(box[:-1]):
        acc+=colors[c]; cut=i+1
        if acc>=total/2: break
    boxes.remove(box); boxes.extend([box[:cut],box[cut:]])
palette=[]
for box in boxes:
    total=sum(colors[c] for c in box)
    palette.append(tuple(round(sum(c[k]*colors[c] for c in box)/total) for k in range(3)))
lookup={c:min(range(len(palette)),key=lambda i:sum((c[k]-palette[i][k])**2 for k in range(3))) for c in colors}
indices=bytes(lookup[tuple(raw[i:i+3])] for i in range(0,len(raw),3))
palette.extend([(0,0,0)]*(256-len(palette)))
def chunk(tag,data):
    return struct.pack(">I",len(data))+tag+data+struct.pack(">I",zlib.crc32(tag+data)&0xffffffff)
png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",128,128,8,3,0,0,0))
png+=chunk(b"PLTE",bytes(v for c in palette for v in c))
png+=chunk(b"IDAT",zlib.compress(b"".join(b"\0"+indices[y*128:(y+1)*128] for y in range(128)),9))+chunk(b"IEND",b"")
open(sys.argv[2],"wb").write(png)
print("Wrote 128x128 8-bit indexed PNG:",sys.argv[2])
