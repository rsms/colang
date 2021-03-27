#pragma once
ASSUME_NONNULL_BEGIN

typedef struct Chan Chan;

Chan* ChanNew(Mem nullable mem, size_t elemsize, u32 cap);
void ChanFree(Chan*);
bool ChanSend(Chan*, const void* elem);
void* ChanRecv(Chan*);

ASSUME_NONNULL_END
