TEX = xelatex
all: report.pdf
report.pdf: report.tex preamble.tex titlepage.tex inc/demo.txt inc/ipc-excerpt.txt ../src/mapping.c ../src/parent_input.c
	$(TEX) -interaction=nonstopmode -halt-on-error report.tex > build.log
	$(TEX) -interaction=nonstopmode -halt-on-error report.tex >> build.log
clean:
	rm -f *.aux *.log *.out *.toc
.PHONY: all clean
