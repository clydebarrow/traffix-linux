#ifndef MAIN_H_
#define MAIN_H_


#include <sys/cdefs.h>
#include <syslog.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>

#define ARRAY_SIZE(arr) ((sizeof(arr)/sizeof(arr[0])))
#define FOREACH(array, ptr, type) for (type * ptr = (array) ; ptr != (array) + ARRAY_SIZE(array) ; ptr++)

extern long getMsTime();



#endif /* MAIN_H_ */

/**************************  Useful Electronics  ****************END OF FILE***/
