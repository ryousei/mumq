#define _GNU_SOURCE             
#include <sched.h>
#include <unistd.h>
#include <stdio.h>

#include "cpu.h"

/*----------------------------------------------------------------------------*/
int 
GetNumCPUCores(void)
{
	return (int)sysconf(_SC_NPROCESSORS_ONLN);
}
/*----------------------------------------------------------------------------*/
int 
AffinitizeThreadToCore(int core)
{
	cpu_set_t *cmask;
	int n, ret;
				    
	n = sysconf(_SC_NPROCESSORS_ONLN);

	if (core < 0 || core >= n) {
		fprintf(stderr, "%d: invalid CPU number.\n", core);
		return -1;
	}   

	cmask = CPU_ALLOC(n);
	if (cmask == NULL) {
		fprintf(stderr, "%d: uexpected cmask.\n", n);
		return -1;
	}

	CPU_ZERO_S(n, cmask);
	CPU_SET_S(core, n, cmask);

	ret = sched_setaffinity(0, n, cmask);

	CPU_FREE(cmask);
	return ret;
}
