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
#include <cstring>
#include <snap7.h>
#include "ReadSignals.h"
#include "WriteSignals.h"
#include "Info.h"

using namespace std;

static void edgedata_callback(T_EDGE_DATA* event);

static pthread_mutex_t s_mutex;
static pthread_mutex_t s_mutex_event;

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

// Konvertiere String in einen EDGE_DATA_VALUE
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

// Konvertiere EDGE_DATA_QUALITY in einen Integer
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

// Konvertiere EDGE_DATA_VALUE in einen String
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

// EdgeDataCallback für abonnierte Topics
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
    else
    {
        // Kein Eintrag mit dem gleichen Topic, also einen neuen Eintrag hinzufügen
        T_EDGE_EVENT event_entry = { event->topic, event->handle, event->type, event->quality, event->value, event->timestamp64 };
        event_list.push_back(event_entry);
    }
    pthread_mutex_unlock(&s_mutex_event);
}

// Variable zum Warten auf Verbindung mit EdgeDataAPI
bool edge_data_sync = false;

// Synchronisieren mit der EdgeDataAPI
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
      edge_data_sync = true;
      while (1)
      {
         if (edge_data_sync_read(discover_info->read_handle_list, discover_info->read_handle_list_len) != E_EDGE_DATA_RETVAL_OK)
         {
            break;
         }
         // Daten einmal pro Sekunde updaten
         pthread_mutex_unlock(&s_mutex);
         usleep(1000000);
         pthread_mutex_lock(&s_mutex);
      }
      edge_data_disconnect();
      /* error -> reconnect */
      pthread_mutex_unlock(&s_mutex);
   }
   return 0;
}

// Funktion zum Aktualisieren der Daten von S7 an SICAM8 auf dem Edge Data API
bool processS7toSICAM8Data(vector<T_EDGE_DATA*> parsed_values)
{
   vector<T_EDGE_DATA*> parsed_data = parsed_values;
   for (uint32_t i = 0; i < parsed_data.size(); i++)
   {
      // Anfang kritischer Abschnitt
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
      // Verlasse kritischen Abschnitt
      pthread_mutex_unlock(&s_mutex);
   }
   return true;
}

//---EIGENE FUNKTIONEN ZUM VERWENDEN DER EDGEDATA API---//---CUSTOM FUNCTIONS TO USE EDGE DATA API---//

// Funktion zum Extrahieren und Umwandeln der Werte und Typen für Topics
std::map<std::string, std::tuple<float, uint32_t, std::string, int, int>> processSICAM8toS7Topics(const std::map<std::string, std::tuple<std::string, int, int>>& specificTopics)
{
   // Map zum Speichern des jeweiligen value (float), der Qualität, des Datentyps, des Byte-Offsets und des Bit-Offsets
   std::map<std::string, std::tuple<float, uint32_t, std::string, int, int>> SICAM8toS7Information;

   // Eventliste nach Topics durchsuchen und relevante Informationen extrahieren
   for (const auto& event : event_list)
   {
      // Prüfen, ob das Topic im specificTopics vorhanden ist
      auto it = specificTopics.find(event.topic);
      if (it != specificTopics.end())
      {
         // Umwandeln des T_EDGE_DATA_VALUE in einen String basierend auf dem Typ
         std::string out_value = convert_value_to_str(event.value, event.type);

         // Umwandeln des Strings in einen float-Wert
         float numericValue = std::stof(out_value);

         // Auslesen des Datentyps, Byte-Offsets und Bit-Offsets
         std::string dataType = std::get<0>(it->second);
         int byteOffset = std::get<1>(it->second);
         int bitOffset = std::get<2>(it->second);

         // Speichern des numericValue, der Qualität, des Datentyps, des Byte-Offsets und des Bit-Offsets in der Map
         SICAM8toS7Information[event.topic] = std::make_tuple(numericValue, event.quality, dataType, byteOffset, bitOffset);
      }
   }
   return SICAM8toS7Information;
}

// Ablauf in Main-Methode
int main(int argc, char** argv)
{
   printf("Programm gestartet\n");
   pthread_mutex_init(&s_mutex, NULL);
   pthread_mutex_init(&s_mutex_event, NULL);

   pthread_t edgedata_thread_nr;
   int ret;

   // Thread erstellen der edgedata_task aufruft
   if (pthread_create(&edgedata_thread_nr, NULL, edgedata_task, &ret))
   {
      printf("Fehler beim Erstellen des Threads\n");
      return 1;
   }

   //Deklaration eines Snap7 Clients
   TS7Client Client;

   //Ausgabe der eingelesenen Informationen zu IP und DB Nummern
   std::cout << "\nNrDBS8anS7: " << Info::DBS8anS7 << std::endl;
   std::cout << "NrDBS7anS8: " << Info::DBS7anS8 << std::endl;
   std::cout << "IP-Adresse: " << Info::IPadresse << std::endl;
   std::cout << "Rack: " << Info::RackNr << std::endl;
   std::cout << "Slot: " << Info::SlotNr << std::endl;

   //Ausgabe aller Signale von SICAM8 an S7
   printf("\nSignale von SICAM 8 an S7\n");

   for (const auto& signal : writeSignals)
   {
      std::cout << "Name: " << signal.name << ", Datentyp: " << signal.dataType << ", ByteOffset: " << signal.byteOffset << ", BitOffset: " << signal.bitOffset << std::endl;
   }

   //Ausgabe aller Signale von S7 an SICAM8
   printf("\nSignale von S7 an SICAM8\n");

   for (const auto& signal : readSignals)
   {
        std::cout << "Name: " << signal.name << ", Datentyp: " << signal.dataType << ", ByteOffset: " << signal.byteOffset << ", BitOffset: " << signal.bitOffset << std::endl;
   }

   // Variable für den Verbindungsstatus
   bool verbunden = false;

   // Lege Liste mit Topics, Datentypen und Adressen der Signale von SICAM8 an S7 an
   std::map<std::string, std::tuple<std::string, int, int>> SICAM8toS7Topics;

   // Iteriere durch die übergebene writeSignals-Liste und extrahiere die Topics, Datentypen und Adressen der Signale von SICAM8 an S7
   for (const auto& signal : writeSignals)
   {
      SICAM8toS7Topics[signal.name] = std::make_tuple(signal.dataType, signal.byteOffset, signal.bitOffset);
   }

   // Lege Liste mit Topics, Datentypen und Adressen der Signale von S7 an SICAM8 an
   std::map<std::string, std::tuple<std::string, int, int>> S7toSICAM8Topics;

   // Iteriere durch die readSignals-Liste und extrahiere die die Topics, Datentypen und Adressen der Signale von S7 an SICAM8
   for (const auto& signal : readSignals)
   {
       S7toSICAM8Topics[signal.name] = std::make_tuple(signal.dataType, signal.byteOffset, signal.bitOffset);
   }

   // Bestimme den maximalen Byte-Offset für die writeSignals
   int maxWriteByteOffset = 0;
   for (const auto& entry : SICAM8toS7Topics)
   {
      int byteOffset = std::get<1>(entry.second);
      int additionalDataSize = 0;
      if (std::get<0>(entry.second) == "Bool")
      {
         additionalDataSize = 0;
      }
      else if (std::get<0>(entry.second) == "DInt" || std::get<0>(entry.second) == "Real")
      {
         additionalDataSize = 3;
      }
      maxWriteByteOffset = std::max(maxWriteByteOffset, byteOffset + additionalDataSize);
   }

   // Bestimme  den maximalen Byte-Offset für die readSignals
   int maxReadByteOffset = 0;
   for (const auto& entry : S7toSICAM8Topics)
   {
      int byteOffset = std::get<1>(entry.second);
      int additionalDataSize = 0;
      if (std::get<0>(entry.second) == "Bool")
      {
            additionalDataSize = 0;
      }
      else if (std::get<0>(entry.second) == "DInt" || std::get<0>(entry.second) == "Real")
      {
         additionalDataSize = 3;
      }
      maxReadByteOffset = std::max(maxReadByteOffset, byteOffset + additionalDataSize);
   }

   std::cout << maxWriteByteOffset << std::endl;
   std::cout << maxReadByteOffset << std::endl;

   // Nummer des Datenbausteins für Signale von SICAM8 an S7
   int dbNumber1 = std::stoi(Info::DBS8anS7);

   // Nummer des Datenbausteins für Signale von SICAM8 an S7
   int dbNumber2 = std::stoi(Info::DBS7anS8);

   // Warte bei erstem Verbinden, bis EdgeDataAPI verbunden und synchronisiert ist
    while (!edge_data_sync)
    {
        usleep(1000000);
    }

   // While-Schleife damit bei Verbindungsabbruch erneut versucht wird eine Verbindung herzustellen
   while(true)
   {
      // While-Schleife zum Verbindung herstellen
      while (!verbunden)
      {
         // Versuche Verbindung zur SPS herzustellen
         if (Client.ConnectTo(Info::IPadresse.c_str(), Info::RackNr, Info::SlotNr) == 0)
         {
            // Bei Erfolg verbunden auf true setzt und Rest des Programms fortsetzen
            std::cout << "Verbindung zur SPS hergestellt!" << std::endl;
            verbunden = true;
         }
         else
         {
            // Bei Misserfolg nach 10 Sekunden erneut versuchen
            std::cerr << "Verbindung zur SPS fehlgeschlagen. Erneuter Versuch in 10 Sekunden..." << std::endl;
            usleep(10000000);
         }
      }

      // While-Schleife zum Lesen und Schreiben von Daten während die SPS verbunden ist
      while (verbunden)
      {
         // Überprüfen, ob die Verbindung zur SPS besteht
         if (Client.Connected() == false)
         {
            //Falls nicht, Schleife verlassen
            std::cerr << "Verbindung zur SPS abgebrochen!" << std::endl;
            verbunden = false;
            break;
         }

         // Daten an SPS senden wenn vorhanden
         if (!SICAM8toS7Topics.empty())
         {     
            // Aufrufen der Funktion zum Einlesen der Daten der extrahierten Topics
            std::map<std::string, std::tuple<float, uint32_t, std::string, int, int>> SICAM8toS7Data = processSICAM8toS7Topics(SICAM8toS7Topics);

            // Erstelle einen Puffer für die gesammelten Daten
            std::vector<uint8_t> writeDataBuffer(maxWriteByteOffset+1, 0);

            // Fülle den WritePuffer mit den Werten aus SICAM8toS7Data
            for (const auto &entry : SICAM8toS7Data)
            {
               float value = std::get<0>(entry.second);
               std::string dataType = std::get<2>(entry.second);
               int byteOffset = std::get<3>(entry.second);
               int bitOffset = std::get<4>(entry.second);

               if (dataType == "Bool")
               {
                  bool boolValue = static_cast<bool>(value);
                  if (boolValue)
                  {
                        writeDataBuffer[byteOffset] |= (1 << bitOffset);
                  } 
                  else
                  {
                        writeDataBuffer[byteOffset] &= ~(1 << bitOffset);
                  }
               }
               else if (dataType == "DInt")
               {
                  uint32_t dintValue = static_cast<uint32_t>(value);
                  writeDataBuffer[byteOffset] = (dintValue >> 24) & 0xFF;
                  writeDataBuffer[byteOffset + 1] = (dintValue >> 16) & 0xFF;
                  writeDataBuffer[byteOffset + 2] = (dintValue >> 8) & 0xFF;
                  writeDataBuffer[byteOffset + 3] = dintValue & 0xFF;
               }
               else if (dataType == "Real")
               {
                  uint32_t intValue = *reinterpret_cast<uint32_t*>(&value);
                  writeDataBuffer[byteOffset] = (intValue >> 24) & 0xFF;
                  writeDataBuffer[byteOffset + 1] = (intValue >> 16) & 0xFF;
                  writeDataBuffer[byteOffset + 2] = (intValue >> 8) & 0xFF;
                  writeDataBuffer[byteOffset + 3] = intValue & 0xFF;
               }
            }

            // Schreibe den Write Puffer in einem Aufruf in die SPS
            if (Client.DBWrite(dbNumber1, 0, writeDataBuffer.size(), writeDataBuffer.data()) !=0)
            {
               std::cerr << "Fehler beim Schreiben der Daten in die SPS" << std::endl;
               verbunden = false;
               break;
            }
            // Gebe den WriteBuffer frei
            writeDataBuffer.clear();
         }

         // Daten aus SPS lesen wenn vorhanden
         if (!S7toSICAM8Topics.empty())
         { 
            // Erstellen eines Vektors mit Datenpaketen, die ausgegeben werden sollen
            vector<T_EDGE_DATA*> S7toSICAM8Data;

            // Erstellen Sie einen Puffer für die eingelesenen Daten
            std::vector<uint8_t> readDataBuffer(maxReadByteOffset+1, 0);

            // Lese alle Daten in einem Aufruf aus der SPS in den ReadBuffer
            if (Client.DBRead(dbNumber2, 0, readDataBuffer.size(), readDataBuffer.data()) != 0)
            {
               std::cerr << "Fehler beim Lesen der Daten aus der SPS" << std::endl;
               verbunden = false;
               break;
            }

            // for-Schleife zum Einlesen der Werte in S7toSICAM8Data
            for (const auto& entry : S7toSICAM8Topics)
            {
               std::string topic = entry.first;
               //Funktion zum Erhalten des zugehoerigen Handles zum jeweligen Topic
               if(edge_data_get_writeable_handle(topic.c_str())!=0)
               {
                  T_EDGE_DATA_HANDLE handle = edge_data_get_writeable_handle(topic.c_str());

                  //Umwandlung in einen String
                  std::string handleStr = std::to_string(handle);

                  //Generieren eines aktuellen Zeitstempels fuer das Ausgabesignal
                  auto currentTime = std::chrono::system_clock::now();
                  auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();
                  //Initialisieren des Wertes
                  float value = 0.0;
                  //Einlesen von Datentyp und Bit- und Byte-Offset
                  std::string dataType = std::get<0>(entry.second);
                  int byteOffset = std::get<1>(entry.second);
                  int bitOffset = std::get<2>(entry.second);

                  if (dataType == "Bool")
                  {
                     value = static_cast<float>((readDataBuffer[byteOffset] & (1 << bitOffset)) != 0);
                  }
                  else if (dataType == "DInt")
                  {
                     uint32_t dintValue = (static_cast<uint32_t>(readDataBuffer[byteOffset]) << 24) |
                                          (static_cast<uint32_t>(readDataBuffer[byteOffset + 1]) << 16) |
                                          (static_cast<uint32_t>(readDataBuffer[byteOffset + 2]) << 8) |
                                          (static_cast<uint32_t>(readDataBuffer[byteOffset + 3]));
                     value = static_cast<float>(dintValue);
                  }
                  else if (dataType == "Real")
                  {
                     uint32_t intValue =  (static_cast<uint32_t>(readDataBuffer[byteOffset]) << 24) |
                                          (static_cast<uint32_t>(readDataBuffer[byteOffset + 1]) << 16) |
                                          (static_cast<uint32_t>(readDataBuffer[byteOffset + 2]) << 8) |
                                          (static_cast<uint32_t>(readDataBuffer[byteOffset + 3]));
                  value = *reinterpret_cast<float*>(&intValue);
                  }
                  else
                  {
                     std::cerr << "Unbekannter Datentyp für " << entry.first << std::endl;
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
                  printf("Kein Handle fuer Topic %s gefunden\n", entry.first.c_str());
               }
            }
            //Gebe den ReadBuffer frei
            readDataBuffer.clear();

            //Aufruf der Funktion zum Aktualisieren der Daten
            processS7toSICAM8Data(S7toSICAM8Data);

            //Freigeben des Speichers von parsed_values
            for (size_t i = 0; i < S7toSICAM8Data.size(); ++i)
            {
               delete S7toSICAM8Data[i];
            }
         }
      }
   }
   return 0;
}
