# tmtest Makefile
# Scott Bronson
# 28 Dec 2004

VERSION=1.5

COPTS=-g -Wall -Werror

CSRC=qscandir.c main.c
CHDR=qscandir.h


tmtest: $(CSRC) $(CHDR) exec.c
	$(CC) $(COPTS) $(CSRC) exec.c -o tmtest

exec.c: exec.tmpl cstrfy
	./cstrfy -n exec < exec.tmpl > exec.c

clean:
	rm -f tmtest

doc:
	doxygen
	which pods2html > /dev/null || echo "You must install Pod::Tree off CPAN"
	pods2html . docs/html
	$(MAKE) graphs

