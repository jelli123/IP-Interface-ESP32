/*
 *  heap_check.h - Diagnosis of heap corruption, only with -DSBIP_HEAP_CHECK=1.
 *
 *  The internal heap is checked at the places marked with HEAP_GUARD (on
 *  entry and on exit) and with sbipHeapCheck(). The first failure is logged
 *  with the place it was found at and the last place it was still intact.
 *
 *  Walking a corrupt heap can itself crash. Both places are therefore kept in
 *  RTC RAM as well, and the next start logs them.
 *
 *  Without the switch the guards compile to nothing.
 */
#pragma once

#ifndef SBIP_HEAP_CHECK
#define SBIP_HEAP_CHECK 0
#endif

#if SBIP_HEAP_CHECK

void sbipHeapCheck(const char* where);

/** After a restart: what the checks before it last saw. */
void sbipHeapReport();

struct SbipHeapGuard
{
    const char* name;

    explicit SbipHeapGuard(const char* n) : name(n) { check(" in"); }
    ~SbipHeapGuard() { check(" out"); }

private:
    void check(const char* suffix);
};

#define HEAP_GUARD(name) SbipHeapGuard sbipHeapGuard_(name)
#define HEAP_CHECK(where) sbipHeapCheck(where)

#else

#define HEAP_GUARD(name) do {} while (0)
#define HEAP_CHECK(where) do {} while (0)
inline void sbipHeapReport() {}

#endif
