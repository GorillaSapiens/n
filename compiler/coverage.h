#ifndef _INCLUDE_COVERAGE_H_
#define _INCLUDE_COVERAGE_H_

#define COVER_LINE(x) cover(x);
#define COVER COVER_LINE(__LINE__)

// register that a rule has been covered
void cover(int n);

// print a report.
// prints nothing if 100% coverage
// calls exit if not 100% coverage
void coverage_report(void);

#endif
