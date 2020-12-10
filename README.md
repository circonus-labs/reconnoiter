This is Reconnoiter
===================

To build, check out the instructions in [BUILDING](./BUILDING.md).

Documentation is automatically generated from the source tree (`cd docs` then
run one or more of the following):
* HTML, single-page: `make out/html/singlepage.html` (requires `xsltproc`)
* HTML, indexed: `make out/html/index.html` (requires `xsltproc`)
* Markdown: `make out/md/manual.md` (requires `pandoc`, `xmllint`)
* PDF: `make out/pdf/manual.pdf` (requires `fop`, `xmllint`, `xsltproc`)

Enjoy.
