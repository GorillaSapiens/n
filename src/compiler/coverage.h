#ifndef _INCLUDE_COVERAGE_H_
#define _INCLUDE_COVERAGE_H_

#define COVER_LINE(x) cover(x);
#define COVER COVER_LINE(__LINE__)

void cover(int n);
void coverage_report(void);

#endif
