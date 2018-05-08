FROM alpine:latest
RUN apk add --no-cache libstdc++ libc6-compat openssl
COPY build/mux2.12/game /game/
WORKDIR /game
RUN (cd /game/bin;rm -f dbconvert;ln -s netmux dbconvert)
VOLUME ["/game"]
EXPOSE 2860
ENTRYPOINT ["./Startmux"]
CMD [""]
