#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "rusage.h"


void print_rusage()
{
	// struct rusage self;
	struct rusage child;

	/*	we don't care about self -- it disappears in the noise.

	if(getrusage(RUSAGE_SELF, &self) != 0) {
		printf("rusage self error: %s", strerror(errno));
		return;
	}
	*/

	if(getrusage(RUSAGE_CHILDREN, &child) != 0) {
		printf("rusage children error: %s", strerror(errno));
		return;
	}

//	double uself = self.ru_utime.tv_sec + self.ru_utime.tv_usec / 1000000.0;
	double uchild = child.ru_utime.tv_sec + child.ru_utime.tv_usec / 1000000.0;
//	double sself = self.ru_stime.tv_sec + self.ru_stime.tv_usec / 1000000.0;
	double schild = child.ru_stime.tv_sec + child.ru_stime.tv_usec / 1000000.0;
	double total = uchild + schild;

	
	/*
	printf("%2.4fs testing, %02d%% user %02d%% sys",
			uchild+schild,
			(int)(100.0*uchild/total+0.5),
			(int)(100.0*schild/total+0.5));
			*/

	printf("Tests took %.2fs (%02d%% sys)",
			uchild+schild,
			(int)(100.0*schild/total+0.5));
}

