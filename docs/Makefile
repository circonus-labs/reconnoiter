XSLTPROC=xsltproc
XMLLINT=xmllint
FOP=fop

DOCS=out/html/singlepage.html \
	out/html/index.html \
	out/pdf/manual.pdf

all:	$(DOCS)

out/html:
	mkdir -p out/html

out/pdf:
	mkdir -p out/pdf

html-assets:	out/html
	cp -r assets out/html/

out/html/singlepage.html:	out/html html-assets
	$(XSLTPROC) --xinclude -o out/html/singlepage.html ./docbook-xsl/xhtml5/docbook.xsl index.xml

out/html/index.html:	out/html html-assets
	$(XSLTPROC) --xinclude --stringparam base.dir out/html ./docbook-xsl/html/html-multipage.xsl index.xml

out/pdf/docbook.fo:	out/pdf
	$(XSLTPROC) --xinclude -o out/pdf/docbook.fo ./docbook-xsl/fo/docbook.xsl index.xml

out/pdf/docbook-pretty.fo:	out/pdf/docbook.fo
	$(XMLLINT) -o $@ --pretty 1 $<

out/pdf/manual.pdf:	out/pdf/docbook-pretty.fo
	$(FOP) out/pdf/docbook-pretty.fo -pdf $@
