sample.so: sample.cpp sample.h
	( if [ -f sample.so ]; then mv -f sample.so sample.so~ ; fi )
	g++ -shared -fPIC -o sample.so sample.cpp -L.. -lmux
	( if [ -f sample.so ]; then cd ../../game/bin ; rm -f sample.so; ln -s ../../src/modules/sample.so sample.so ; fi )
