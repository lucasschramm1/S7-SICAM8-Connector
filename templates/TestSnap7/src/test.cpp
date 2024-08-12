/* 
 * siapp-sdk
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Siemens AG
 *
 * Authors:
 *    Lukas Wimmer <lukas.wimmer@siemens.com>
 *    Lucas Schramm <lucas.schramm@siemens.com>
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

#define EVENTS_LENGTH 50

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

static FILE* log_file_p = NULL;

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

std::string convert_quality_to_str(uint32_t quality)
{
   std::string out;
   if (quality & EDGE_QUALITY_FLAG_NOT_TOPICAL) {
        out += "NT|";
    }
    if (quality & EDGE_QUALITY_FLAG_OVERFLOW) {
        out += "OV|";
    }
    if (quality & EDGE_QUALITY_FLAG_OPERATOR_BLOCKED) {
        out += "OB|";
    }
    if (quality & EDGE_QUALITY_FLAG_SUBSITUTED) {
        out += "SB|";
    }
    if (quality & EDGE_QUALITY_FLAG_TEST) {
        out += "T|";
    }
    if (quality & EDGE_QUALITY_FLAG_INVALID) {
        out += "IV|";
    }

    // Entferne '|' am Ende
    if (!out.empty() && out.back() == '|') {
        out.pop_back();
    }

    return out;
}

static void edgedata_callback(T_EDGE_DATA* event) 
{
   pthread_mutex_lock(&s_mutex_event);
   //printf("Eingehendes Event unter Topic: %s, Wert: %s, Timestamp: %lld, Qualitaet: %u\n", event->topic, convert_value_to_str(event->value,event->type).c_str(), event->timestamp64, event->quality);
   if (event_list.size() >= EVENTS_LENGTH) 
   {
      event_list.erase(event_list.begin());
   }
   T_EDGE_EVENT event_entry = {event->topic, event->handle, event->type, event->quality, event->value, event->timestamp64};
   event_list.push_back(event_entry);
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

         //Testweise Ausgabe der Read-Handle und Topics
         /*cout << "Inhalt der s_read_list:" << endl;
         for (const auto& entry : s_read_list) 
         {
            cout << "Handle: " << entry->handle << " Topic: " << entry->topic << endl;
         }*/
      }
      for (int i = 0; i < discover_info->write_handle_list_len; i++) 
      {
         s_write_list.push_back(edge_data_get_data(discover_info->write_handle_list[i]));

         //Testweise Ausgabe der Write-Handle und Topics
         /*cout << "Inhalt der s_write_list:" << endl;
         for (const auto& entry : s_write_list) 
         {
            cout << "Handle: " << entry->handle << " Topic: " << entry->topic << endl;
         }*/
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

//Funktion zum Extrahieren des Values und Types fuer eingestellte Topics
std::map<std::string, std::tuple<T_EDGE_DATA_VALUE, E_EDGE_DATA_TYPE, uint32_t>> processSpecificTopics(const std::vector<std::string>& specificTopics) 
{
   //Map zum Speichern des jeweiligen Values, des Typs und der Qualitaet
   std::map<std::string, std::tuple<T_EDGE_DATA_VALUE, E_EDGE_DATA_TYPE, uint32_t>> topicValues;

   // Eventliste nach Topics durchsuchen und relevante Informationen extrahieren
   for (const auto& event : event_list) 
   {
      if (std::find(specificTopics.begin(), specificTopics.end(), event.topic) != specificTopics.end()) 
      {
         topicValues[event.topic] = std::make_tuple(event.value, event.type, event.quality);  
      }
    }
    return topicValues;
}

//Funktion zum Aktualisieren der Write-List
bool processSetDataRequest(vector<T_EDGE_DATA*> parsed_values) 
{
   vector<T_EDGE_DATA*> parsed_data = parsed_values;
   for (uint32_t i = 0; i < parsed_data.size(); i++) 
   {
      //Testweise Ausgabe der aktualisierenden Daten
      //printf("Handle detektiert: %u (Neuer Wert: %s, Timestamp: %lld, Qualitaet: %u)\n", parsed_data[i]->handle, convert_value_to_str(parsed_data[i]->value,parsed_data[i]->type).c_str(), (long long)parsed_data[i]->timestamp64, parsed_data[i]->quality);
      /* enter critical section */
      pthread_mutex_lock(&s_mutex);
      for (int w = 0; w < s_write_list.size(); w++) 
      {
         if (s_write_list[w]->handle == parsed_data[i]->handle) 
         {
            (void)memcpy(&s_write_list[w]->value, &parsed_data[i]->value, sizeof(T_EDGE_DATA_VALUE));
            s_write_list[w]->timestamp64 = parsed_data[i]->timestamp64;
            s_write_list[w]->quality = parsed_data[i]->quality;
            edge_data_sync_write(&s_write_list[w]->handle, 1);
            //printf("Synchronisieren der Write-List abgeschlossen : Neuer Wert %lf\n", s_write_list[w]->value);
         }
      }
      /* leave critical section */
      pthread_mutex_unlock(&s_mutex);
   }
   return true;
}

//Funktion zur Berechnung der Ausgabewerte Value und Quality pro Topic, Modus v gibt Value und Modus q Qualitaet zurueck
std::string calculateOutput(const std::vector<std::string>& OutputTopics, const std::map<std::string, std::pair<double, uint32_t>>& InputMap, char mode)
{
   for (const std::string& topic : OutputTopics)
   {
         if (topic == "Ausgabe1")
         {
            //Beispielweise Addition der beiden Testsignale
            double output = InputMap.at("Test1").first + InputMap.at("Test2").first;
            //Bewertung der Qualtitaet des Ausgangssignals anhand der Eingangssignale
            uint32_t quality = std::max(InputMap.at("Test1").second, InputMap.at("Test2").second);
            if(mode == 'v')
            {
               return std::to_string(output);
            }
            if(mode == 'q')
            {
               return convert_quality_to_str(quality);
            }
         }
         else if (topic == "Ausgabe2")
         {
            //Ausgabe2 ist als Beispiel gleich Test1
            double output = InputMap.at("Test1").first;
            //Qualitaet wird daher auch uebernommen
            uint32_t quality = InputMap.at("Test1").second;
            if(mode == 'v')
            {
               return std::to_string(output);
            }
            if(mode == 'q')
            {
               return convert_quality_to_str(quality);
            }
         }
         //Fuer jedes Ausgabe-Topic hier eine Bedingung einfuegen und die Berechnungen durchfuehren
   }

   //Ausgabe falls kein Topic gefunden wurde
   return "Topic nicht gefunden";
};

//Ablauf in Main-Methode
int main(int argc, char** argv) 
{
   pthread_mutex_init(&s_mutex, NULL);
   pthread_mutex_init(&s_mutex_event, NULL);
   pthread_mutex_init(&s_mutex_log, NULL);

   pthread_t edgedata_thread_nr;
   int ret;

   TS7Client Client;
   int result;

   // Verbindung herstellen
   if (Client.ConnectTo("192.168.0.1", 0, 1) == 0) 
   {  // IP-Adresse des PLCs
        std::cout << "Verbindung hergestellt!" << std::endl;
        
   } 
   else 
   {
      std::cerr << "Verbindung fehlgeschlagen!" << std::endl;
   }

    //Thread erstellen der edgedata_task aufruft
   if (pthread_create(&edgedata_thread_nr, NULL, edgedata_task, &ret)) 
   {
      printf("Fehler beim Erstellen des Threads\n");
      return 1;
   }

   printf("Programm gestartet\n");

   while (1) 
   {     
      //Wartezeit um CPU Auslastung zu begrenzen(kann angepasst werden)
      usleep(5000000); 

      // Lesen des ersten Bytes von DB1
      uint8_t buffer[1];
      result = Client.DBRead(1, 0, 1, buffer); // DB-Nummer, Startadresse, Länge, Puffer
         if (result != 0) 
         {
            std::cerr << "Lesen des Datenbausteins fehlgeschlagen!" << std::endl;
            Client.Disconnect();
            return 1;
         }

         // Auslesen des ersten Bits
         bool firstBit = buffer[0] & 0x01;
         std::cout << "Erstes Bit von DB1 ist: " << firstBit << std::endl;

         if (!firstBit) 
            {
               buffer[0] |= 0x01;
               // Schreiben des geänderten Bytes zurück in DB1
               result = Client.DBWrite(1, 0, 1, buffer); // DB-Nummer, Startadresse, Länge, Puffer
               if (result != 0) 
                  {
                     std::cerr << "Schreiben des Datenbausteins fehlgeschlagen!" << std::endl;
                     Client.Disconnect();
                     return 1;
                  }
               std::cout << "Das erste Bit von DB1 wurde auf true gesetzt." << std::endl;
            } 
            else 
               {
                  buffer[0] &= ~0x01;
                  // Schreiben des geänderten Bytes zurück in DB1
                  result = Client.DBWrite(1, 0, 1, buffer); // DB-Nummer, Startadresse, Länge, Puffer
                  if (result != 0) 
                     {
                     std::cerr << "Schreiben des Datenbausteins fehlgeschlagen!" << std::endl;
                     Client.Disconnect();
                     return 1;
                     }
                  std::cout << "Das erste Bit von DB1 wurde auf false gesetzt." << std::endl;
               }

      //Auswahl der einzulesenden Signale bzw. Topics
      std::vector<std::string> specificTopics = {"Test1", "Test2"};

      //Aufrufen der Funktion zum Einlesen der Daten aus Topics
      std::map<std::string, std::tuple<T_EDGE_DATA_VALUE, E_EDGE_DATA_TYPE, uint32_t>> topicValues = processSpecificTopics(specificTopics);

      //Anlegen einer Map als Speicher der eingelesenen Daten und Qualitaeten
      std::map<std::string, std::pair<double, uint32_t>> InputMap;

      //for-Schleife zum Extrahieren des Zahlenwerts T_EDGE_DATA_VALUE und der Qualitaet
      for (const auto& entry : topicValues) 
      {
         const std::string& topic = entry.first;
         const T_EDGE_DATA_VALUE& value = std::get<0>(entry.second);
         const E_EDGE_DATA_TYPE& type = std::get<1>(entry.second);
         const uint32_t& quality = std::get<2>(entry.second);
      
         //Umwandeln des T_EDGE_DATA_VALUE in einen String
         std::string out_value = convert_value_to_str(value, type);

         // Umwandeln des Strings in eine Double-Zahl
         double numericValue = std::stod(out_value);

         //Speichern in den Maps
         InputMap[topic] = std::make_pair(numericValue, quality);
      }

      //Testweise Ausgabe der Werte(.first)/Qualitaet(.second) fuer die Topics Test1 und Test2
      /*std::cout << "Topic: " << "Test1" << ", Qualitaet: " << InputMap["Test1"].second << std::endl;
      std::cout << "Topic: " << "Test2" << ", Qualitaet: " << InputMap["Test2"].second << std::endl;*/
   
      //Erstellen eines Vektors mit Datenpaketen, die ausgegeben werden sollen
      vector<T_EDGE_DATA*> parsed_values;

      //Auswahl der auszugebenen Signale bzw. Topics
      std::vector<std::string> OutputTopics = {"Ausgabe1","Ausgabe2"};
      
      //for-Schleife zur Ausgabe der berechneten Werte
      for (const std::string& topic : OutputTopics)
      {
         //Funktion zum Erhalten des zugehoerigen Handles zum jeweligen Topic
         if(edge_data_get_writeable_handle(topic.c_str())!=0)
         {
            T_EDGE_DATA_HANDLE handle = edge_data_get_writeable_handle(topic.c_str());
            
            //Umwandlung in einen String
            std::string handleStr = std::to_string(handle);

            // Testweise Ausgabe des Handles
            //std::cout << "Handle fuer: '" << topic << "': " << handleStr << std::endl;

            //Generieren eines aktuellen Zeitstempels fuer das Ausgabesignal
            auto currentTime = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();

            //Anlegen eines neuen Eintrags
            T_EDGE_DATA new_entry;
               E_EDGE_DATA_TYPE type = E_EDGE_DATA_TYPE_UNKNOWN;
               (void)memset(&new_entry, 0, sizeof(new_entry));
               //Uebergabe des Handles und Zeitstempels, sowie Berechnung des Values und der Qualitaet durch Funktion calculateOutput
               vector<string> EdgeDataEntryTag_Handle = {handleStr};
               std::string resultv = calculateOutput({topic}, InputMap, 'v');
               std::vector<std::string> EdgeDataEntryTag_Value = {resultv};
               std::string resultq = calculateOutput({topic}, InputMap, 'q');
               std::vector<std::string> EdgeDataEntryTag_Quality = {resultq};
               //vector<string> EdgeDataEntryTag_Quality = {"IV"};
               vector<string> EdgeDataEntryTag_Timestamp = {std::to_string(timestamp)};
               new_entry.handle = strtoul(EdgeDataEntryTag_Handle[0].c_str(), NULL, 10);

               //E_EDGE_DATA_TYPE aus write_list auslesen
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
               parsed_values.push_back(new T_EDGE_DATA(new_entry));
         }
         else
         {
            printf("Kein Handle fuer Topic %s gefunden\n", topic.c_str());
         }
      }
      //Aufruf der Funktion zum Aktualisieren der Daten
      processSetDataRequest(parsed_values);

      //Freigeben des Speichers von parsed_values
      for (size_t i = 0; i < parsed_values.size(); ++i) 
      {
         delete parsed_values[i];
      }
   }
   return 0;
}