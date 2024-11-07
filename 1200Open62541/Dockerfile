# Stage 1: Build
FROM alpine AS build

# Notwendige Pakete installieren
RUN apk add --no-cache g++ make wget cmake libtool python3 fcgi-dev libconfig-dev openssl-dev linux-headers

# Arbeitsverzeichnis erstellen
WORKDIR /usr/src/app

# Kopieren und Kompilieren von Open62541
COPY open62541 /open62541
RUN mkdir -p /open62541/build && cd /open62541/build \
    && cmake -DUA_ENABLE_SUBSCRIPTIONS=ON -DUA_ENABLE_GENERATE_NAMESPACE_ZERO=MINIMAL -DUA_ENABLE_CLIENT=ON \
		-DUA_ENABLE_ENCRYPTION=ON -DUA_ENABLE_ENCRYPTION_OPENSSL=ON .. \
    && make install

# Kopieren des Quellcodes und der Projektdateien in das Image
COPY src /src
COPY edgedataapi /edgedataapi
COPY cert /cert

# Umgebungsvariablen setzen, damit die Bibliotheken und Header gefunden werden
ENV LD_LIBRARY_PATH=/usr/local/lib
ENV CPLUS_INCLUDE_PATH=/usr/local/include

# Kompilieren des Projekts mit Open62541
RUN g++ -std=c++11 -Wcpp -Wno-psabi -Wno-format \
    -I /usr/local/include -I /edgedataapi/include -I /open62541/include -I /cert \
    -L /usr/local/lib -o /siapp /src/siapp.cpp /edgedataapi/src/edgedata.cpp -lopen62541 \
    -lfcgi -lpthread -lssl -lcrypto

# Stage 2: Runtime
FROM alpine

# Notwendige Pakete installieren
RUN apk add --no-cache libconfig-dev fcgi-dev openssl-dev

# Kopieren der Dateien aus Stage 1
COPY --from=build /siapp /siapp
COPY --from=build /cert /cert

RUN if [ -f /cert/*.der ]; then chmod 644 /cert/*.der; fi

# Datei ausführbar machen
RUN chmod +x /siapp

# Startpunkt für den Container
ENTRYPOINT ["/siapp"]