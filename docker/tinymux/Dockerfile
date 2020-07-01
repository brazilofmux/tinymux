FROM alpine:latest as builder
RUN apk add --no-cache clang g++ make openssl openssl-dev
COPY mux-2.12.0.9.unix.tar.gz .
RUN tar xzf mux-2.12.0.9.unix.tar.gz
WORKDIR /mux2.12/src
RUN CC=clang CXX=clang++ ./configure --enable-ssl
RUN sed -e '80s/-lssl/-lssl -lcrypto/g' Makefile > Makefile.new
RUN mv Makefile.new Makefile
RUN CC=clang CXX=clang++ CXXFLAGS=-O3 make depend
RUN CC=clang CXX=clang++ CXXFLAGS=-O3 make -j12
#
# Startmux requires adjustment
#
WORKDIR /mux2.12/game
RUN sed -e '82s/(nohup /exec /g' -e '82s/ &)//' Startmux > Startmux.new
RUN mv Startmux.new Startmux
RUN chmod u+x Startmux

FROM alpine:latest
RUN apk add --no-cache libstdc++ libc6-compat openssl
COPY --from=builder mux2.12/game /game/
COPY --from=builder mux2.12/src/netmux /game/bin
COPY --from=builder mux2.12/src/libmux.so /game/bin
COPY --from=builder mux2.12/src/slave /game/bin
RUN (cd /game/bin;rm -f dbconvert;ln -s netmux dbconvert)
WORKDIR /game
VOLUME ["/game"]
EXPOSE 2860
ENTRYPOINT ["./Startmux"]
CMD [""]
