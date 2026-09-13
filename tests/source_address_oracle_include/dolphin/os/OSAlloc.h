#ifndef MELEE_SOURCE_ADDRESS_ORACLE_OSALLOC_H
#define MELEE_SOURCE_ADDRESS_ORACLE_OSALLOC_H
typedef int OSHeapHandle;
extern volatile int __OSCurrHeap;
void* OSAllocFromHeap(int, unsigned long);
void OSFreeToHeap(int, void*);
long OSCheckHeap(int);
#endif
