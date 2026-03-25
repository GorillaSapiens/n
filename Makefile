tarball:
	( cd src ; make )
	rm -f *.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *

