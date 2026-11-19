all:
	$(MAKE) -C src
run: all
	cd src && ./parent
strace: all
	cd src && strace -f -yy -s 160 -o ../strace.log ./parent
report:
	$(MAKE) -C report
clean:
	$(MAKE) -C src clean
.PHONY: all run strace report clean
