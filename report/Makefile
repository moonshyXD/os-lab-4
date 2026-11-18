LATEX = xelatex
SOURCES = ../Makefile ../src/Makefile $(wildcard ../src/*.c ../src/*.h)
TRACE = inc/demo.txt inc/ipc-excerpt.txt inc/ipc-trace.txt

all: report.pdf
report.pdf: report.tex preamble.tex titlepage.tex $(TRACE) $(SOURCES)
	$(LATEX) -interaction=nonstopmode -halt-on-error report.tex > build.log
	$(LATEX) -interaction=nonstopmode -halt-on-error report.tex >> build.log
clean:
	rm -f *.aux *.log *.out *.toc
.PHONY: all clean
