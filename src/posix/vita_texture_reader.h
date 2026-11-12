// Short-lived prefix cache for texture format detection. The underlying lump
// stays alive and retains ownership. Large reads fall back to the original reader.
#ifndef VITA_TEXTURE_READER_H
#define VITA_TEXTURE_READER_H
#include "files.h"
#include <algorithm>
#include <cstring>
#include <limits>
class FVitaTextureReader : public FileReader {
 FileReader &source;
 unsigned char prefix[4096];
 long cached;
public:
 explicit FVitaTextureReader(FileReader &reader):source(reader),cached(0) {
  File=NULL;CloseOnDestruct=false;StartPos=FilePos=0;Length=reader.GetLength();
  // Cached PK3 lumps already have cheap memory seeks; leave them alone.
  if(source.GetFile()!=NULL && Length>0 && source.Seek(0,SEEK_SET)==0)
   cached=std::max(0L,source.Read(prefix,std::min(Length,long(sizeof(prefix)))));
 }
 long Tell() const {return FilePos;}
 long Seek(long offset,int origin) {
  long base=0;
  if(origin==SEEK_CUR)base=FilePos;
  else if(origin==SEEK_END)base=Length;
  else if(origin!=SEEK_SET)return -1;
  if(offset>0 && base>std::numeric_limits<long>::max()-offset)return -1;
  long target=base+offset;
  if(target<0)return -1;
  FilePos=long(target);return 0;
 }
 long Read(void *out,long length) {
  if(length<=0 || FilePos>=Length)return 0;
  length=std::min(length,Length-FilePos);
  long copied=0;
  if(FilePos<cached) {
   copied=std::min(length,cached-FilePos);
   memcpy(out,prefix+FilePos,copied);FilePos+=copied;
  }
  if(copied<length && source.Seek(FilePos,SEEK_SET)==0) {
   long got=source.Read(static_cast<unsigned char*>(out)+copied,length-copied);
   if(got>0){FilePos+=got;copied+=got;}
  }
  return copied;
 }
 char *Gets(char *out,int size) {
  if(size<=0 || FilePos>=Length)return NULL;
  if(size==1){out[0]=0;return out;}
  int n=0;
  while(n<size-1 && Read(out+n,1)==1){if(out[n++]=='\n')break;}
  out[n]=0;return n?out:NULL;
 }
};
#endif
