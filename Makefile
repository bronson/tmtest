# tmtest Makefile
# Scott Bronson
# 28 Dec 2004

VERSION=1.5

COPTS=-g -Wall -Werror

CSRC=qscandir.c qtempfile.c vars.c main.c
CHDR=qscandir.h qtempfile.h vars.h test.h


tmtest: $(CSRC) $(CHDR) tmpl.c Makefile
	$(CC) $(COPTS) $(CSRC) tmpl.c -o tmtest

tmpl.c: tmpl.sh cstrfy Makefile
	./cstrfy -n exec_template < tmpl.sh > tmpl.c

clean:
	rm -f tmtest tmpl.c

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

