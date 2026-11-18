all:
	$(MAKE) -C src
strace: all
	cd src && strace -f -yy -s 160 -o ../strace.log ./parent
report:
	$(MAKE) -C report
clean:
	$(MAKE) -C src clean
.PHONY: all strace report clean
