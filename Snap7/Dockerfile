# Basis-Image verwenden
FROM alpine

# Notwendige Pakete installieren
RUN apk add --no-cache g++ make wget fcgi-dev fcgi++ libtool

# Arbeitsverzeichnis erstellen
WORKDIR /usr/src/app

# Snap7 kopieren und bauen
COPY snap7 /snap7
RUN cd /snap7/build/unix && make -f /snap7/build/unix/arm_v7_linux.mk

# Kopieren des Quellcodes und der Projektdateien in das Image
COPY src /src
COPY edgedataapi /edgedataapi

# Snap7-Bibliothek an den richtigen Ort verschieben
RUN cp /snap7/build/bin/arm_v7-linux/libsnap7.so /usr/local/lib/libsnap7.so

# Umgebungsvariablen setzen
ENV LD_LIBRARY_PATH=/usr/local/lib

# Kompilieren der C++-Beispiele mit dem vorhandenen Makefile
RUN cd /snap7/examples/cpp/arm_v7-linux && make all

# Kompilieren des Projekts mit Snap7
RUN g++ -std=c++11 -Wcpp -Wno-psabi -Wno-format \
    -I /snap7/examples/cpp -I /usr/local/include -I /edgedataapi/include \
    -L /snap7/build/bin/arm_v7-linux -o /siapp \
    /src/siapp.cpp /edgedataapi/src/edgedata.cpp /snap7/examples/cpp/snap7.cpp \
    -lsnap7 -lfcgi -lpthread

# Compiler und andere Entwicklungswerkzeuge entfernen für ein kleineres Image
RUN apk del g++ make wget libtool

# Ausführbare Datei ausführbar machen
RUN chmod +x /siapp

# Startbefehl für den Container
ENTRYPOINT ["/siapp"]