SUBDIRS="lib src"

# try and optimize build on SMP machines
PARALLEL_MAKE+=$(shell if [ X$(OSTYPE) = XFreeBSD ] ; then echo -j `sysctl -n hw.ncpu` ; fi)
PARALLEL_MAKE+=$(shell if [ X$(OSTYPE) = Xlinux ] ; then echo -j `grep -c '^processor' /proc/cpuinfo` ; fi)
PARALLEL_MAKE+=$(shell if [ `uname` = Linux ] ; then echo -j `grep -c '^processor' /proc/cpuinfo` ; fi)

OPTFLAG?=-O4 -fno-rtti

# redhat gcc version 4.1.2 20070626 doesn't support this flag.
# if important, figure out the right way to detect support for this flag:
# OPTFLAG+=$(shell if [ `uname` = Linux ] ; then echo '-march=native' ; fi)

VERSION=0.1.0

#
# see http://www.gnu.org/software/make/manual/make.html#Phony-Targets
# for a way of improving the following:
#
all test:
	@for i in "$(SUBDIRS)"; do \
		if [ "XXX$$i" = XXX ] ;\
		then \
		    continue; \
		fi;\
		if [ \! -d $$i ] ; \
		then \
		    echo "directory $$i does not exist, skipping." ; \
		    continue ; \
		fi ; \
		(echo "building in directory $$i";cd $$i; $(MAKE) $(PARALLEL_MAKE) OPTFLAG="$(OPTFLAG)" --no-print-directory $@) ; \
		if [ $$? -ne 0 ] ; \
		then \
		    echo "make stopped because of errors." ; \
		    break ; \
		fi \
	done

release:
	(make clean)
	tar cvz --exclude="*~" --exclude="statGen.*.tgz" --exclude-vcs -f statGen.$(VERSION).tgz ../statgen

clean:
	@for i in "$(SUBDIRS)"; do \
		if [ "XXX$$i" = XXX ] ;\
		then \
		    continue; \
		fi;\
		if [ \! -d $$i ] ; \
		then \
		    echo "directory $$i does not exist, skipping." ; \
		    continue ; \
		fi ; \
		(echo "building in directory $$i";cd $$i; $(MAKE) $(PARALLEL_MAKE) --no-print-directory $@) ; \
		if [ $$? -ne 0 ] ; \
		then \
		    echo "make stopped because of errors." ; \
		    break ; \
		fi \
	done
