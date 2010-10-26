# tmtest Makefile
# Scott Bronson
# 28 Dec 2004
#
# This software is distributed under the LGPL.  See COPYING for more.


VERSION=0.96

# override this for make install.  "make install prefix=/usr/local"
#prefix=/usr
prefix=$(HOME)

# figure out where to install the software:
bindir=$(prefix)/bin
lib_src=tmlib.sh

ifeq ($(prefix), $(HOME))
	libdir=$(HOME)
	stdlib=$(HOME)/.tmlib.sh
	conf_dst=$(HOME)/.tmtestrc
else
	libdir=$(prefix)/share/tmtest
	stdlib=$(libdir)/tmlib.sh
	conf_dst=/etc/tmtest.conf
endif


COPTS=-g -Wall -Werror

# scanner files
SCANC=re2c/read.c re2c/read-fd.c re2c/read-mem.c re2c/read-rand.c re2c/scan.c re2c/scan-dyn.c
SCANH=re2c/read.h re2c/read-fd.h re2c/read-mem.h re2c/read-rand.h re2c/scan.h re2c/scan-dyn.h

# utilities:
CSRC+=qscandir.c pathstack.c compare.c pathconv.c
CHDR+=qscandir.h pathstack.h compare.h pathconv.h
# program files:
CSRC+=vars.c test.c rusage.c tfscan.c stscan.o main.c template.c
CHDR+=vars.h test.h rusage.h tfscan.h stscan.h

# It makes it rather hard to debug when Make deletes the intermediate files.
INTERMED=stscan.c

all: tmtest

tmtest: $(CSRC) $(SCANH) $(SCANC) $(CHDR) $(INTERMED)
	$(CC) $(COPTS) $(CSRC) $(SCANC) -o tmtest -DVERSION="$(VERSION)"

template.c: template.sh cstrfy
	./cstrfy -n exec_template < template.sh > template.c

%.c: %.re
	re2c $(REOPTS) $< > $@
	perl -pi -e 's/^\#line.*$$//' $@

%.o: %.c
	$(CC) -g -c $< -o $@

.PHONY: test
test: tmtest
	tmtest test

install: tmtest
	install -d -m755 $(bindir)
	install tmtest $(bindir)
ifneq ($(libdir),$(HOME))
	install -d -m755 $(libdir)
endif
	install tmlib.sh $(stdlib)
ifeq ($(wildcard $(conf_dst)),$(conf_dst))
	# configuration already exists, don't overwrite it.
	@echo "---> Not installing new config file over '$(conf_dst).'"
	@echo "---> Please merge changes in 'sample.conf' by hand."
	@echo "--->   (run 'diff -u $(conf_dst) sample.conf' to show differences)"
else
	# global configuration file doesn't exist so install it
	install sample.conf $(conf_dst)
	@perl -pi -e 's/USER/$(shell whoami)/g' $(conf_dst)
	@perl -pi -e 's:STDLIB:$(stdlib):g' $(conf_dst)
endif

# NOTE: This will remove the configuration file too!
uninstall: tmtest
	rm $(bindir)/tmtest
	rm $(stdlib)
ifeq ($(prefix), $(HOME))
	rm $(HOME)/.tmtestrc
else
	rm /etc/tmtest.conf
endif

clean:
	rm -f tmtest template.c tags

distclean: clean
	rm -f stscan.[co]

dist: stscan.c

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

%.png: %.dot
	dot -Tpng $< -o $@
