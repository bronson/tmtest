# tmtest Makefile
# Scott Bronson
# 28 Dec 2004

VERSION=1.5

COPTS=-g -Wall
# -Werror

CSRC=qscandir.c qtempfile.c vars.c test.c status.c main.c
CHDR=qscandir.h qtempfile.h vars.h test.h status.h

CSRC+=r2read.c r2read-fd.c r2scan.c
CHDR+=r2read.h r2read-fd.h r2scan.h

CSRC+=status1.c status2.c status3.c status4.c


tmtest: $(CSRC) $(CHDR) tmpl.c Makefile
	$(CC) $(COPTS) $(CSRC) tmpl.c -o tmtest

tmpl.c: tmpl.sh cstrfy Makefile
	./cstrfy -n exec_template < tmpl.sh > tmpl.c

status.c: status1.c status2.c status3.c status4.c
	touch status.c

status1.c: status1.re status.h r2scan.h Makefile
	re2c $(REOPTS) status1.re > status1.c
	perl -pi -e 's/^\#line.*$$//' status1.c

status2.c: status2.re status.h r2scan.h Makefile
	re2c $(REOPTS) status2.re > status2.c
	perl -pi -e 's/^\#line.*$$//' status2.c

status3.c: status3.re status.h r2scan.h Makefile
	re2c $(REOPTS) status3.re > status3.c
	perl -pi -e 's/^\#line.*$$//' status3.c

status4.c: status4.re status.h r2scan.h Makefile
	re2c $(REOPTS) status4.re > status4.c
	perl -pi -e 's/^\#line.*$$//' status4.c

clean:
	rm -f tmtest tmpl.c status.c status[1-4].c

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

%.png: %.dot
	dot -Tpng $< -o $@
	qiv -t $@ &
