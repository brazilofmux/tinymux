FROM alpine:latest AS builder
RUN apk add --no-cache clang g++ make openssl openssl-dev pcre2-dev pkgconf
COPY mux-2.13.0.11.unix.tar.gz .
RUN tar xzf mux-2.13.0.11.unix.tar.gz
WORKDIR /mux2.13/src
RUN CC=clang CXX=clang++ ./configure --enable-stubslave
RUN CC=clang CXX=clang++ CXXFLAGS=-O3 make -j$(nproc)
RUN make install
RUN cp libmux.so ../game/bin/
RUN cp modules/*.so ../game/bin/
#
# Startmux requires adjustment
#
WORKDIR /mux2.13/game
RUN sed -e '49s/(nohup /exec /g' -e '49s/ &)//' Startmux > Startmux.new
RUN mv Startmux.new Startmux
RUN chmod u+x Startmux

FROM alpine:latest
RUN apk add --no-cache libstdc++ libc6-compat openssl pcre2 sqlite
COPY --from=builder mux2.13/game /game/
RUN (cd /game/bin;rm -f dbconvert;ln -s netmux dbconvert)
WORKDIR /game
VOLUME ["/game"]
EXPOSE 2860
ENTRYPOINT ["./Startmux"]
CMD [""]
