tarball:
	rm *.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *
