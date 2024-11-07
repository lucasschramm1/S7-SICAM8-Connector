/*
 * siapp-sdk
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Siemens AG
 *
 * Authors of this file:
 *    Lukas Wimmer <lukas.wimmer@siemens.com> (SIAPP SDK Basic Functions)
 *    Lucas Schramm <lucas.schramm@siemens.com> (Specific Functions for implementation of Snap7)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <alloca.h>
#include <fcgiapp.h>
#include <edgedata.h>
#include <vector>
#include <string>
#include <list>
#include <iterator>
#include <pthread.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <algorithm>
#include <chrono>
#include <snap7.h>
#include "ReadSignals.h"
#include "WriteSignals.h"
#include "Info.h"

using namespace std;

static void edgedata_callback(T_EDGE_DATA* event);

static pthread_mutex_t s_mutex;
static pthread_mutex_t s_mutex_event;
static pthread_mutex_t s_mutex_log;

static bool s_connected = false;
static vector<T_EDGE_DATA*> s_read_list, s_write_list;

struct T_EDGE_EVENT{
public:
   string                        topic;         /* value name    */
   uint32_t                      handle;        /* value handle  */
   E_EDGE_DATA_TYPE              type;          /* value type    */
   uint32_t                      quality;       /* see EDGE_QUALITY_FLAG_ defines ... for details */
   T_EDGE_DATA_VALUE             value;         /* value         */
   int64_t                       timestamp64;   /* timestamp     */
};

static vector<T_EDGE_EVENT> event_list;

//---FUNKTIONEN AUS SIAPP SDK BEISPIELEN---//---FUNCTIONS OUT OF SIAPP SDK EXAMPLES---//

static void convert_str_to_value(E_EDGE_DATA_TYPE type, const char* in, T_EDGE_DATA* out)
{
   switch (type)
   {
   case E_EDGE_DATA_TYPE_INT32:
      out->value.int32 = strtol(in, NULL, 10);
      break;
   case E_EDGE_DATA_TYPE_UINT32:
      out->value.uint32 = strtoul(in, NULL, 10);
      break;
   case E_EDGE_DATA_TYPE_INT64:
      out->value.int64 = strtoll(in, NULL, 10);
      break;
   case E_EDGE_DATA_TYPE_UINT64:
      out->value.uint64 = strtoull(in, NULL, 10);
      break;
   case E_EDGE_DATA_TYPE_FLOAT32:
      out->value.float32 = strtof(in, NULL);
      break;
   case E_EDGE_DATA_TYPE_DOUBLE64:
      out->value.double64 = strtod(in, NULL);
      break;
   case E_EDGE_DATA_TYPE_UNKNOWN:
   default:
      out->value.uint64 = 0;
      break;
   }
}

static void convert_quality_str_to_value(const char* in, T_EDGE_DATA* out)
{
   uint32_t quality = EDGE_QUALITY_VALID_VALUE;
   if ((strstr(in, "|NT") != NULL) || (strncmp(in, "NT", 2) == 0))
   {
      quality += EDGE_QUALITY_FLAG_NOT_TOPICAL;
   }
   if ((strstr(in, "|OV") != NULL) || (strncmp(in, "OV", 2) == 0))
   {
      quality += EDGE_QUALITY_FLAG_OVERFLOW;
   }
   if ((strstr(in, "|OB") != NULL) || (strncmp(in, "OB", 2) == 0))
   {
      quality += EDGE_QUALITY_FLAG_OPERATOR_BLOCKED;
   }
   if ((strstr(in, "|SB") != NULL) || (strncmp(in, "SB", 2) == 0))
   {
      quality += EDGE_QUALITY_FLAG_SUBSITUTED;
   }
   if ((strstr(in, "|T") != NULL) || (strncmp(in, "T", 1) == 0))
   {
      quality += EDGE_QUALITY_FLAG_TEST;
   }
   if ((strstr(in, "|IV") != NULL) || (strncmp(in, "IV", 2) == 0))
   {
      quality += EDGE_QUALITY_FLAG_INVALID;
   }
   out->quality = quality;
}

static std::string convert_value_to_str(T_EDGE_DATA_VALUE value, E_EDGE_DATA_TYPE type)
{
    std::string out_value;

    switch (type) {
        case E_EDGE_DATA_TYPE_INT32:
            out_value = std::to_string(value.int32);
            break;
        case E_EDGE_DATA_TYPE_UINT32:
            out_value = std::to_string(value.uint32);
            break;
        case E_EDGE_DATA_TYPE_INT64:
            out_value = std::to_string(value.int64);
            break;
        case E_EDGE_DATA_TYPE_UINT64:
            out_value = std::to_string(value.uint64);
            break;
        case E_EDGE_DATA_TYPE_FLOAT32:
            out_value = std::to_string(value.float32);
            break;
        case E_EDGE_DATA_TYPE_DOUBLE64:
            out_value = std::to_string(value.double64);
            break;
        case E_EDGE_DATA_TYPE_UNKNOWN:
        default:
            out_value = "0";
            break;
    }

    return out_value;
}

static void edgedata_callback(T_EDGE_DATA* event)
{
    pthread_mutex_lock(&s_mutex_event);

    // Überprüfen, ob bereits ein Eintrag mit demselben Topic existiert
    auto it = std::find_if(event_list.begin(), event_list.end(),
        [&](const T_EDGE_EVENT& e) { return e.topic == event->topic; });

    if (it != event_list.end()) {
        // Eintrag existiert bereits, du kannst ihn hier ggf. aktualisieren, wenn nötig
        it->handle = event->handle;
        it->type = event->type;
        it->quality = event->quality;
        it->value = event->value;
        it->timestamp64 = event->timestamp64;
    }
    else {
        // Kein Eintrag mit dem gleichen Topic, also einen neuen Eintrag hinzufügen
        T_EDGE_EVENT event_entry = { event->topic, event->handle, event->type, event->quality, event->value, event->timestamp64 };
        event_list.push_back(event_entry);
    }
    pthread_mutex_unlock(&s_mutex_event);
}

//Synchronisieren mit der EdgeDataAPI
void* edgedata_task(void* void_ptr)
{
   sleep(1);
   while (1)
   {
      pthread_mutex_lock(&s_mutex);
      s_read_list.clear();
      s_write_list.clear();

      if (edge_data_connect() != E_EDGE_DATA_RETVAL_OK)
      {
         printf("Fehler beim Verbinden mit der EdgeDataApi\n");
         pthread_mutex_unlock(&s_mutex);
         sleep(1);
         continue;
      }
      printf("EdgeDataAPI erfolgreich verbunden\n");

      const T_EDGE_DATA_LIST* discover_info = edge_data_discover();
      for (int i = 0; i < discover_info->read_handle_list_len; i++)
      {
         s_read_list.push_back(edge_data_get_data(discover_info->read_handle_list[i]));
         edge_data_subscribe_event(discover_info->read_handle_list[i], &edgedata_callback);
      }
      for (int i = 0; i < discover_info->write_handle_list_len; i++)
      {
         s_write_list.push_back(edge_data_get_data(discover_info->write_handle_list[i]));
      }
      s_connected = true;

      while (1)
      {
         if (edge_data_sync_read(discover_info->read_handle_list, discover_info->read_handle_list_len) != E_EDGE_DATA_RETVAL_OK)
         {
            break;
         }
         //Daten einmal pro Sekunde updaten
         pthread_mutex_unlock(&s_mutex);
         usleep(1000000);
         pthread_mutex_lock(&s_mutex);
      }
      s_connected = false;
      edge_data_disconnect();
      /* error -> reconnect */
      pthread_mutex_unlock(&s_mutex);
   }
   return 0;
}

//---EIGENE FUNKTIONEN ZUM VERWENDEN DER EDGEDATA API---//---CUSTOM FUNCTIONS TO USE EDGE DATA API---//

// Funktion zum Extrahieren und Umwandeln der Werte und Typen für Topics
std::map<std::string, std::pair<float, uint32_t>> processSICAM8toS7Topics(const std::vector<std::string>& specificTopics)
{
   // Map zum Speichern des jeweiligen value (float) und der Qualitaet
   std::map<std::string, std::pair<float, uint32_t>> SICAM8toS7Information;

   // Eventliste nach Topics durchsuchen und relevante Informationen extrahieren
   for (const auto& event : event_list)
   {
      if (std::find(specificTopics.begin(), specificTopics.end(), event.topic) != specificTopics.end())
      {
         // Umwandeln des T_EDGE_DATA_VALUE in einen String basierend auf dem Typ
         std::string out_value = convert_value_to_str(event.value, event.type);

         // Umwandeln des Strings in einen float-Wert
         float numericValue = std::stof(out_value);

         // Speichern des numericValue und der Qualität in der Map
         SICAM8toS7Information[event.topic] = std::make_pair(numericValue, event.quality);
      }
    }
    return SICAM8toS7Information;
}

//Funktion zum Aktualisieren der Daten von S7 an SICAM8 auf dem Edge Data API
bool processS7toSICAM8Data(vector<T_EDGE_DATA*> parsed_values)
{
   vector<T_EDGE_DATA*> parsed_data = parsed_values;
   for (uint32_t i = 0; i < parsed_data.size(); i++)
   {
      //Anfang kritischer Abschnitt
      pthread_mutex_lock(&s_mutex);
      for (int w = 0; w < s_write_list.size(); w++)
      {
         if (s_write_list[w]->handle == parsed_data[i]->handle)
         {
            (void)memcpy(&s_write_list[w]->value, &parsed_data[i]->value, sizeof(T_EDGE_DATA_VALUE));
            s_write_list[w]->timestamp64 = parsed_data[i]->timestamp64;
            s_write_list[w]->quality = parsed_data[i]->quality;
            edge_data_sync_write(&s_write_list[w]->handle, 1);
         }
      }
      //Verlasse kritischen Abschnitt
      pthread_mutex_unlock(&s_mutex);
   }
   return true;
}

//---EIGENE FUNKTIONEN ZUR IMPLEMENTIERUNG VON SNAP7---//---CUSTOM FUNCTIONS TO IMPLEMENT SNAP7---//

// Funktion zum Schreiben eines Bool-Wertes in Datenbaustein der SPS
void writeBoolToSPS(TS7Client& Client, int dbNumber, const Signal& signal, bool value, const std::string& topic)
{
    uint8_t buffer[1] = { 0 };

    // Lesen des Bytes aus der SPS
    if (Client.DBRead(dbNumber, signal.byteOffset, 1, buffer) != 0) 
    {
        std::cerr << "Fehler beim Lesen des Bytes für " << topic << std::endl;
        return;
    }
    // Setzen des spezifischen Bits im Byte
    int bitPosition = signal.bitOffset;
    if (bitPosition < 0 || bitPosition >= 8) 
    {
        std::cerr << "Ungültige Bit-Position: " << bitPosition << std::endl;
        return;
    }

    if (value) 
    {
        buffer[0] |= (1 << bitPosition); // Setze das Bit auf 1
    } 
    else 
    {
        buffer[0] &= ~(1 << bitPosition); // Setze das Bit auf 0
    }

    // Schreiben des aktualisierten Bytes zurück in die SPS
    if (Client.DBWrite(dbNumber, signal.byteOffset, 1, buffer) != 0) 
    {
        std::cerr << "Fehler beim Schreiben des Bool-Werts für " << topic << std::endl;
    }
}

// Funktion zum Schreiben eines DInt-Wertes in Datenbaustein der SPS
void writeDIntToSPS(TS7Client& Client, int dbNumber, const Signal& signal, uint32_t value, const std::string& topic)
{
    //Puffer für 4 Byte
    uint8_t buffer[4] = { 0 };

    // Direkte Zuweisung der Bytes in den Puffer im Big-Endian-Format
    buffer[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    buffer[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[3] = static_cast<uint8_t>(value & 0xFF);

    // Schreibe den Puffer in die SPS
    if (Client.DBWrite(dbNumber, signal.byteOffset, sizeof(buffer), buffer) != 0) 
    {
        std::cerr << "Fehler beim Schreiben des DInt-Werts für " << topic << std::endl;
    }
}

// Funktion zum Schreiben eines Real-Wertes in Datenbaustein der SPS
void writeRealToSPS(TS7Client& Client, int dbNumber, const Signal& signal, float value, const std::string& topic)
{
   // Puffer für 4 Byte
    uint8_t buffer[4] = { 0 };

    // Konvertiere den float-Wert in einen 32-Bit Integer (nur um static_cast nutzen zu können)
    uint32_t intValue = *reinterpret_cast<uint32_t*>(&value);

    // Direkte Zuweisung der Bytes in den Puffer im Big-Endian-Format
    buffer[0] = static_cast<uint8_t>((intValue >> 24) & 0xFF);
    buffer[1] = static_cast<uint8_t>((intValue >> 16) & 0xFF);
    buffer[2] = static_cast<uint8_t>((intValue >> 8) & 0xFF);
    buffer[3] = static_cast<uint8_t>(intValue & 0xFF);

    // Schreibe den Buffer in die SPS
    if (Client.DBWrite(dbNumber, signal.byteOffset, sizeof(buffer), buffer) != 0) 
    {
        std::cerr << "Fehler beim Schreiben des Real-Werts für " << topic << std::endl;
    }
}

// Funktion zum Lesen eines Bool-Wertes aus Datenbaustein der SPS
uint32_t readBoolFromSPS(TS7Client& Client, int dbNumber, const Signal& signal)
{
    uint8_t buffer[1] = { 0 };

    // Lesen des Bytes aus der SPS
    if (Client.DBRead(dbNumber, signal.byteOffset, 1, buffer) != 0) 
    {
        std::cerr << "Fehler beim Lesen des Bytes für " << signal.name << std::endl;
        return 0;
    }
    // Extrahiere das Bit an der angegebenen Position
    int bitPosition = signal.bitOffset;
    if (bitPosition < 0 || bitPosition >= 8) 
    {
        std::cerr << "Ungültige Bit-Position: " << bitPosition << std::endl;
        return 0;
    }
    // Bit extrahieren und als unsigned int zurückgeben
    return (buffer[0] >> bitPosition) & 0x01;
}

// Funktion zum Lesen eines DInt-Wertes aus der SPS
uint32_t readDIntFromSPS(TS7Client& Client, int dbNumber, const Signal& signal)
{
    uint8_t buffer[4] = { 0 };
    if (Client.DBRead(dbNumber, signal.byteOffset, sizeof(buffer), buffer) != 0)
    {
        std::cerr << "Fehler beim Lesen des DInt-Werts für " << signal.name << std::endl;
        return 0;
    }
    uint32_t value = (static_cast<uint32_t>(buffer[0]) << 24) |
                     (static_cast<uint32_t>(buffer[1]) << 16) |
                     (static_cast<uint32_t>(buffer[2]) << 8) |
                     (static_cast<uint32_t>(buffer[3]));
    return value;
}

// Funktion zum Lesen eines Real-Wertes aus der SPS
float readRealFromSPS(TS7Client& Client, int dbNumber, const Signal& signal)
{
    uint8_t buffer[4] = { 0 };
    if (Client.DBRead(dbNumber, signal.byteOffset, sizeof(buffer), buffer) != 0) 
    {
        std::cerr << "Fehler beim Lesen des Real-Werts für " << signal.name << std::endl;
        return 0.0f;
    }
    uint32_t intValue = (static_cast<uint32_t>(buffer[0]) << 24) |
                        (static_cast<uint32_t>(buffer[1]) << 16) |
                        (static_cast<uint32_t>(buffer[2]) << 8) |
                        (static_cast<uint32_t>(buffer[3]));
    float value = *reinterpret_cast<float*>(&intValue);
    return value;
}

//Ablauf in Main-Methode
int main(int argc, char** argv)
{
   pthread_mutex_init(&s_mutex, NULL);
   pthread_mutex_init(&s_mutex_event, NULL);
   pthread_mutex_init(&s_mutex_log, NULL);

   pthread_t edgedata_thread_nr;
   int ret;

   //Deklaration eines Snap7 Clients
   TS7Client Client;

   //Thread erstellen der edgedata_task aufruft
   if (pthread_create(&edgedata_thread_nr, NULL, edgedata_task, &ret))
   {
      printf("Fehler beim Erstellen des Threads\n");
      return 1;
   }

   printf("Programm gestartet\n");

   //Ausgabe der eingelesenen Informationen zu IP und DB Nummern
   std::cout << "\nNrDBS8anS7: " << Info::DBS8anS7 << std::endl;
   std::cout << "NrDBS7anS8: " << Info::DBS7anS8 << std::endl;
   std::cout << "IP-Adresse: " << Info::IPadresse << std::endl;

   //Ausgabe aller Signale von SICAM8 an S7
   printf("\nSignale von SICAM 8 an S7\n");

   for (const auto& signal : writeSignals)
   {
        std::cout << "Name: " << signal.name
                  << ", Datentyp: " << signal.dataType
                  << ", ByteOffset: " << signal.byteOffset
                  << ", BitOffset: " << signal.bitOffset
                  << std::endl;
    }

   //Ausgabe aller Signale von S7 an SICAM8
    printf("\nSignale von S7 an SICAM8\n");

    for (const auto& signal : readSignals)
   {
        std::cout << "Name: " << signal.name
                  << ", Datentyp: " << signal.dataType
                  << ", ByteOffset: " << signal.byteOffset
                  << ", BitOffset: " << signal.bitOffset
                  << std::endl;
    }

   // Variable für den Verbindungsstatus
   bool verbunden = false;

   //Lege Liste mit Topics der Signale von SICAM8 an S7 an
   std::vector<std::string> SICAM8toS7Topics;

   // Iteriere durch die übergebene writeSignals-Liste und extrahiere die Topics der Signale von SICAM8 an S7
   for (const auto& signal : writeSignals)
   {
       SICAM8toS7Topics.push_back(signal.name);
   }

   //Lege Liste mit Topics der Signale von S7 an SICAM8
   std::vector<std::string> S7toSICAM8Topics;

   // Iteriere durch die readSignals-Liste und extrahiere die Topics der Signale von S7 an SICAM8
   for (const auto& signal : readSignals)
   {
       S7toSICAM8Topics.push_back(signal.name);
   }

   //Kurze Wartezeit vor dem ersten Verbindungsaufbau (1s)
   usleep(1000000);

   //While-Schleife damit bei Verbindungsabbruch erneut versucht wird eine Verbindung herzustellen
   while(true)
   {
      //While-Schleife zum Verbindung herstellen
      while (!verbunden)
      {
         // Versuche Verbindung zur SPS herzustellen
         if (Client.ConnectTo(Info::IPadresse.c_str(), 0, 1) == 0)
         {
            //Bei Erfolg verbunden auf true setzt und Rest des Programms fortsetzen
            std::cout << "Verbindung zur SPS hergestellt!" << std::endl;
            verbunden = true;
         }
         else
         {
            //Bei Misserfolg nach 10 Sekunden erneut versuchen
            std::cerr << "Verbindung zur SPS fehlgeschlagen. Erneuter Versuch in 10 Sekunden..." << std::endl;
            usleep(10000000);
         }
      }

      //While-Schleife zum Lesen und Schreiben von Daten während die SPS verbunden ist
      while (verbunden)
      {
         //Wartezeit um CPU Auslastung zu begrenzen(kann angepasst werden) aktuell: 10s
         usleep(10000000);

         // Überprüfen, ob die Verbindung zur SPS besteht
         if (Client.Connected() == false)
         {
            //Falls nicht, Schleife verlassen
            std::cerr << "Verbindung zur SPS abgebrochen!" << std::endl;
            verbunden = false;
            break;
         }

         //Aufrufen der Funktion zum Einlesen der Daten der extrahierten Topics
         std::map<std::string, std::pair<float, uint32_t>> SICAM8toS7Data = processSICAM8toS7Topics(SICAM8toS7Topics);

         //Für jedes Element von SICAM8toS7Data die Werte an SPS übertragen
         for (const auto& entry : SICAM8toS7Data)
         {
            const std::string& topic = entry.first;
            float value = entry.second.first;
            uint32_t quality = entry.second.second;

            std::cout << "Topic: " << topic << ", Value: " << value << ", Quality: " << quality << std::endl;
         
            // Suche den entsprechenden Signal-Eintrag zum Topic
            auto it = std::find_if(writeSignals.begin(), writeSignals.end(),
               [&topic](const Signal& signal) { return signal.name == topic; });

            if (it != writeSignals.end())
            {
               const Signal& signal = *it;

               //Umwandeln der übergebenen DB Nr zu einem Int
               int dbNumber = std::stoi(Info::DBS8anS7);

               //Schreibe Daten in den Datenbaustein basierend auf dem Datentyp
               if (signal.dataType == "Bool") {
                     writeBoolToSPS(Client, dbNumber, signal, static_cast<bool>(value), topic);
               }
               else if (signal.dataType == "DInt") {
                     writeDIntToSPS(Client, dbNumber, signal, static_cast<uint32_t>(value), topic);
               }
               else if (signal.dataType == "Real") {
                     writeRealToSPS(Client, dbNumber, signal, value, topic);
               }
            }
            else
            {
               std::cerr << "Kein Signal gefunden für Topic: " << topic << std::endl;
            }
         }

         //Erstellen eines Vektors mit Datenpaketen, die ausgegeben werden sollen
         vector<T_EDGE_DATA*> S7toSICAM8Data;

         //for-Schleife zur Ausgabe der berechneten Werte
         for (const std::string& topic : S7toSICAM8Topics)
         {
            //Funktion zum Erhalten des zugehoerigen Handles zum jeweligen Topic
            if(edge_data_get_writeable_handle(topic.c_str())!=0)
            {
               T_EDGE_DATA_HANDLE handle = edge_data_get_writeable_handle(topic.c_str());

               //Umwandlung in einen String
               std::string handleStr = std::to_string(handle);

               //Generieren eines aktuellen Zeitstempels fuer das Ausgabesignal
               auto currentTime = std::chrono::system_clock::now();
               auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();
               float value = 0.0;

               // Suche den entsprechenden Signal-Eintrag
               auto it = std::find_if(readSignals.begin(), readSignals.end(),
                  [&topic](const Signal& signal) { return signal.name == topic; });

               if (it != readSignals.end())
               {
                  const Signal& signal = *it;

                  //Umwandeln der übergebenen DB Nr zu einem Int
                  int dbNumber = std::stoi(Info::DBS7anS8);

                  // Lese Daten aus dem Datenbaustein basierend auf dem Datentyp
                  if (signal.dataType == "Bool")
                  {
                     value = static_cast<float>(readBoolFromSPS(Client, dbNumber, signal));
                  }
                  else if (signal.dataType == "DInt")
                  {
                     value = static_cast<float>(readDIntFromSPS(Client, dbNumber, signal));
                  }
                  else if (signal.dataType == "Real")
                  {
                     value = readRealFromSPS(Client, dbNumber, signal);
                  }
                  else
                  {
                     std::cerr << "Unbekannter Datentyp für " << topic << std::endl;
                  }
               }
               else
               {
                  std::cerr << "Kein Signal gefunden für Topic: " << topic << std::endl;
               }
               //Anlegen eines neuen Eintrags
               T_EDGE_DATA new_entry;
               E_EDGE_DATA_TYPE type = E_EDGE_DATA_TYPE_UNKNOWN;
               (void)memset(&new_entry, 0, sizeof(new_entry));

               //Anlegen eines Eintrags für Handle, Value, Timestamp und Qualität
               vector<string> EdgeDataEntryTag_Handle = {handleStr};
               std::string result = std::to_string(value);
               std::vector<std::string> EdgeDataEntryTag_Value = {result};
               vector<string> EdgeDataEntryTag_Quality = {""};
               vector<string> EdgeDataEntryTag_Timestamp = {std::to_string(timestamp)};
               new_entry.handle = strtoul(EdgeDataEntryTag_Handle[0].c_str(), NULL, 10);

                  //E_EDGE_DATA_TYPE aus s_write_list auslesen
                  for (unsigned int y = 0; y < s_write_list.size(); y++)
                  {
                     if (s_write_list[y]->handle == new_entry.handle)
                     {
                        type = s_write_list[y]->type;
                     break;
                     }
                  }

               //Konvertieren der Daten und Ablegen in new_entry
               new_entry.type = type;
               convert_str_to_value(type, EdgeDataEntryTag_Value[0].c_str(), &new_entry);
               convert_quality_str_to_value(EdgeDataEntryTag_Quality[0].c_str(), &new_entry);
               new_entry.timestamp64 = strtoll(EdgeDataEntryTag_Timestamp[0].c_str(), NULL, 10);
               S7toSICAM8Data.push_back(new T_EDGE_DATA(new_entry));
            }
            else
            {
               printf("Kein Handle fuer Topic %s gefunden\n", topic.c_str());
            }
         }
         //Aufruf der Funktion zum Aktualisieren der Daten
         processS7toSICAM8Data(S7toSICAM8Data);

         //Freigeben des Speichers von parsed_values
         for (size_t i = 0; i < S7toSICAM8Data.size(); ++i)
         {
            delete S7toSICAM8Data[i];
         }
      }
   }
   return 0;
}