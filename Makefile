# tmtest Makefile
# Scott Bronson
# 28 Dec 2004

VERSION=0.91

COPTS=-g -Wall -Werror

# utilities:
CSRC=qscandir.c qtempfile.c
CHDR=qscandir.h qtempfile.h

# scanner files
CSRC+=r2read.c r2read-fd.c r2scan.c r2scan-dyn.c
CHDR+=r2read.h r2read-fd.h r2scan.h r2scan-dyn.h

# program files:
CSRC+=vars.c test.c compare.c status.c tfscan.o main.c
CHDR+=vars.h test.h compare.h status.h tfscan.h matchval.h

# arg. re2c should allow multiple scanners per file.
# and produce code that isn't riddled with warnings...
CSRC+=status1.o status2.o status3.o status4.o


tmtest: $(CSRC) $(CHDR) tmpl.c Makefile
	$(CC) $(COPTS) $(CSRC) tmpl.c -o tmtest

tmpl.c: tmpl.sh cstrfy Makefile
	./cstrfy -n exec_template < tmpl.sh > tmpl.c

status.c: status1.o status2.o status3.o status4.o
	touch status.c

# re2c scanners have the re suffix.
%.c: %.re
	re2c $(REOPTS) $< > $@
	perl -pi -e 's/^\#line.*$$//' $@

# C files that don't emit warnings are compiled in the tmtest rule.
# The rest (unfixable because they're generated) get handled by this rule.
%.o: %.c
	$(CC) -g -c $< -o $@
	
test: tmtest
	./tmtest

clean:
	rm -f tmtest tmpl.c status.c tfscan.c status[1-4].c *.o

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

%.png: %.dot
	dot -Tpng $< -o $@

