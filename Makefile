# tmtest Makefile
# Scott Bronson
# 28 Dec 2004
#
# This software is distributed under the LGPL.  See COPYING for more.


VERSION=0.92

# override this when installing: "make install prefix=/usr/local"
#prefix=/usr
prefix=$(HOME)

bindir=$(prefix)/bin
libdir=$(prefix)/share/tmtest
stdlib=$(libdir)/tmlib.sh

ifeq ($(prefix), $(HOME))
	global_conf=$(HOME)/.tmtestrc
	conf_file=tmtestrc
else
	global_conf=/etc/tmtest.conf
	conf_file=tmtestetc
endif


COPTS=-g -Wall -Werror

# utilities:
CSRC+=curdir.c qscandir.c pcrs.c rel2abs.c
CHDR+=curdir.h qscandir.h pcrs.h rel2abs.h
# scanner files
CSRC+=re2c/read.c re2c/read-fd.c re2c/scan.c
CHDR+=re2c/read.h re2c/read-fd.h re2c/scan.h
# program files:
CSRC+=vars.c test.c compare.c rusage.c tfscan.o stscan.o main.c
CHDR+=vars.h test.h compare.h rusage.h tfscan.h stscan.h matchval.h

# It makes it rather hard to debug when Make deletes the intermediate files.
INTERMED=tfscan.c stscan.c


tmtest: $(CSRC) $(CHDR) template.c Makefile $(INTERMED)
	$(CC) $(COPTS) $(CSRC) -lpcre template.c -o tmtest "-DVERSION=$(VERSION)"

template.c: template.sh cstrfy Makefile
	./cstrfy -n exec_template < template.sh > template.c

%.c: %.re
	re2c $(REOPTS) $< > $@
	perl -pi -e 's/^\#line.*$$//' $@

%.o: %.c
	$(CC) -g -c $< -o $@


test: tmtest
	tmtest --config=./tmtest.conf test

run: tmtest
	./tmtest

install: tmtest
	install -d -m755 $(bindir)
	install tmtest $(bindir)
	install -d -m755 $(libdir)
	install tmlib.sh $(libdir)
	install $(conf_file) $(global_conf)
	perl -pi -e 's/USER/$(shell whoami)/g' $(global_conf)
	perl -pi -e 's:STDLIB:$(stdlib):g' $(global_conf)

uninstall: tmtest
	rm $(bindir)/tmtest
	rm $(stdlib)
ifeq ($(prefix), $(HOME))
	rm $(HOME)/.tmtestrc
else
	rm /etc/tmtest.conf
endif

clean:
	rm -f tmtest template.c

# Ensure re2c is installed to regenerate the scanners before making distclean
distclean: clean
	rm -f stscan.[co] tfscan.[co] tags

dist: stscan.c tfscan.c

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
