inventory.lst:	modules/*
	ls modules/* | sort | \
		grep -v _driver.xml | grep -v _ingestor.xml | \
		sed -e 's^\(.*\)^<xi:include xmlns:xi="http://www.w3.org/2001/XInclude" href="\1"/>^g;' > $@
