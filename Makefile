# tmtest Makefile
# Scott Bronson
# 28 Dec 2004

VERSION=1.5

COPTS=-g -Wall
# -Werror

CSRC=qscandir.c qtempfile.c vars.c test.c scan.c main.c
CHDR=qscandir.h qtempfile.h vars.h test.h scan.h

CSRC+=r2read.c r2read-fd.c r2scan.c
CHDR+=r2read.h r2read-fd.h r2scan.h

CSRC+=scan1.c scan2.c scan3.c scan4.c


tmtest: $(CSRC) $(CHDR) tmpl.c Makefile
	$(CC) $(COPTS) $(CSRC) tmpl.c -o tmtest

tmpl.c: tmpl.sh cstrfy Makefile
	./cstrfy -n exec_template < tmpl.sh > tmpl.c

scan.c: scan1.c scan2.c scan3.c scan4.c
	touch scan.c

scan1.c: scan1.re scan.h r2scan.h Makefile
	re2c $(REOPTS) scan1.re > scan1.c

scan2.c: scan2.re scan.h r2scan.h Makefile
	re2c $(REOPTS) scan2.re > scan2.c

scan3.c: scan3.re scan.h r2scan.h Makefile
	re2c $(REOPTS) scan3.re > scan3.c

scan4.c: scan4.re scan.h r2scan.h Makefile
	re2c $(REOPTS) scan4.re > scan4.c

clean:
	rm -f tmtest tmpl.c scan.c scan[1-4].c

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

%.png: %.dot
	dot -Tpng $< -o $@
	qiv -t $@ &
