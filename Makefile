tarball:
	git clean -fdx
	( cd src ; make )
	rm -f n_*.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *

