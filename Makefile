all:
	$(MAKE) -C src
report:
	$(MAKE) -C report
clean:
	$(MAKE) -C src clean
.PHONY: all report clean
