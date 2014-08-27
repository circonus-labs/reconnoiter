XSLTPROC=xsltproc
XMLLINT=xmllint
FOP=fop
PANDOC=pandoc

DOCS=out/html/singlepage.html \
	out/html/index.html \
	out/pdf/manual.pdf

all:	$(DOCS)

out/html:
	mkdir -p out/html

out/pdf:
	mkdir -p out/pdf

out/md:
	mkdir -p out/md

html-assets:	out/html
	cp -r assets out/html/

out/single.xml:
	$(XMLLINT) --xinclude index.xml > $@

out/md/manual.md:	out/md out/single.xml
	$(PANDOC) -f docbook -t markdown -s out/single.xml -o $@

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
