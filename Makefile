# tmtest Makefile
# Scott Bronson
# 28 Dec 2004
#
# This software is distributed under the LGPL.  See COPYING for more.


VERSION=0.91

# override this when installing: "make install prefix=/usr/local"
prefix=$(HOME)
bindir=$(prefix)/bin
libdir=$(prefix)/share/tmtest


COPTS=-g -Wall -Werror

# utilities:
CSRC=curdir.c qscandir.c pcrs.c
CHDR=curdir.h qscandir.h pcrs.h

# scanner files
CSRC+=re2c/read.c re2c/read-fd.c re2c/scan.c
CHDR+=re2c/read.h re2c/read-fd.h re2c/scan.h

# program files:
CSRC+=vars.c test.c compare.c rusage.c tfscan.o stscan.o main.c
CHDR+=vars.h test.h compare.h rusage.h tfscan.h stscan.h matchval.h

# It makes it rather hard to debug when Make deletes the intermediate files.
INTERMED=tfscan.c stscan.c


tmtest: $(CSRC) $(CHDR) tmpl.c Makefile $(INTERMED)
	$(CC) $(COPTS) $(CSRC) -lpcre tmpl.c -o tmtest "-DVERSION=$(VERSION)"

tmpl.c: tmpl.sh cstrfy Makefile
	./cstrfy -n exec_template < tmpl.sh > tmpl.c

%.c: %.re
	re2c $(REOPTS) $< > $@
	perl -pi -e 's/^\#line.*$$//' $@

%.o: %.c
	$(CC) -g -c $< -o $@
	

test: tmtest
	tmtest test

run: tmtest
	./tmtest

install: tmtest
	install -d -m755 $(bindir)
	install tmtest $(bindir)
	install -d -m755 $(libdir)
	install tmlib/* $(libdir)

clean:
	rm -f tmtest tmpl.c stscan.[co] tfscan.[co]

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

%.png: %.dot
	dot -Tpng $< -o $@

rediff:
	diff ../oe/re2c/ re2c
	
reupdate:
	ls re2c/*.[ch] | (ODIR=`pwd`; cd ../oe; xargs cp --target-directory $$ODIR/re2c)
