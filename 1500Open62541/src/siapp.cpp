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
#include <mutex>
#include <open62541/client.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include "ReadSignals.h"
#include "WriteSignals.h"
#include "Info.h"

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

// Funktion zum Extrahieren und Umwandeln der Werte und Typen für Topics
//std::map<std::string, std::pair<float, uint32_t>> processSICAM8toS7Topics(const std::vector<std::string>& specificTopics)
std::map<std::string, std::tuple<float, uint32_t, int>> processSICAM8toS7Topics(const std::map<std::string, int>& specificTopics)
{
    // Map zum Speichern des jeweiligen value (float), der Qualität und der nodeID
    std::map<std::string, std::tuple<float, uint32_t, int>> SICAM8toS7Information;

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

            // NodeID aus der Map lesen
            int nodeId = it->second;

            // Speichern des numericValue, der Qualität und der nodeID in der Map
            SICAM8toS7Information[event.topic] = std::make_tuple(numericValue, event.quality, nodeId);
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
         }
      }
      /* leave critical section */
      pthread_mutex_unlock(&s_mutex);
   }
   return true;
}

// Funktion zum Schreiben eines Bool-Wertes in Datenbaustein der SPS
void writeBoolToSPS()
{
    
}

// Funktion zum Schreiben eines DInt-Wertes in Datenbaustein der SPS
void writeDIntToSPS()
{
    
}

// Funktion zum Schreiben eines Real-Wertes in Datenbaustein der SPS
void writeRealToSPS()
{
   
}
// Funktion zum Lesen eines Bool-Wertes aus Datenbaustein der SPS
void readBoolFromSPS()
{
  
}

// Funktion zum Lesen eines DInt-Wertes aus der SPS
void readDIntFromSPS()
{
    
}

// Funktion zum Lesen eines Real-Wertes aus der SPS
void readRealFromSPS()
{
    
}
//Liste für eingelesen Daten generieren
std::vector<std::pair<std::string, float>> dataList;
std::mutex dataMutex;

//Funktion zum Aktualisieren der Daten Liste wenn Änderung vorliegt
void dataChangeNotificationCallback(UA_Client* client, UA_UInt32 subId, void* subContext,
    UA_UInt32 monId, void* monContext, UA_DataValue* value) {
    if (value->hasValue) 
    {
        float receivedValue = 0.0;
        // Überprüfe den Datentyp des Wertes
        const UA_DataType* type = value->value.type;
        if (type == &UA_TYPES[UA_TYPES_BOOLEAN]) 
        {
            receivedValue = static_cast<float>(*(UA_Boolean*)value->value.data);
            std::cout << "Subscription ID: " << subId << ", Monitored Item ID: " << monId << ", Neuer Wert (Bool): " << (receivedValue ? "true" : "false") << std::endl;
        }
        else if (type == &UA_TYPES[UA_TYPES_INT32]) 
        {
            receivedValue = static_cast<float>(*(UA_Int32*)value->value.data);
            std::cout << "Subscription ID: " << subId << ", Monitored Item ID: " << monId << ", Neuer Wert (DInt): " << receivedValue << std::endl;
        }
        else if (type == &UA_TYPES[UA_TYPES_FLOAT]) 
        {
            receivedValue = *(UA_Float*)value->value.data;
            std::cout << "Subscription ID: " << subId << ", Monitored Item ID: " << monId << ", Neuer Wert (Float): " << receivedValue << std::endl;
        }
        else 
        {
            std::cerr << "Unbekannter Datentyp für Monitored Item ID: " << monId << std::endl;
        }
        // Erhalte den Topic-Namen aus monContext
        std::string topic = static_cast<char*>(monContext);

        // Daten in die globale Liste aktualisieren
        std::lock_guard<std::mutex> lock(dataMutex);
        dataList.emplace_back(topic, receivedValue);
    }
}
//Ablauf in Main-Methode
int main(int argc, char** argv)
{
   pthread_mutex_init(&s_mutex, NULL);
   pthread_mutex_init(&s_mutex_event, NULL);
   pthread_mutex_init(&s_mutex_log, NULL);

   pthread_t edgedata_thread_nr;
   int ret;

   //Deklaration eines Open62541 Clients mit Statusvariablen
   UA_Client* client = UA_Client_new();
   UA_ClientConfig* config = UA_Client_getConfig(client);
   UA_ClientConfig_setDefault(UA_Client_getConfig(client));

   //Thread erstellen der edgedata_task aufruft
   if (pthread_create(&edgedata_thread_nr, NULL, edgedata_task, &ret))
   {
      printf("Fehler beim Erstellen des Threads\n");
      return 1;
   }

   printf("Programm gestartet\n");

   //Ausgabe der eingelesenen Informationen zu IP und DB Nummern
   std::cout << "\nName DBS8anS7: " << Info::DBS8anS7 << std::endl;
   std::cout << "Name DBS7anS8: " << Info::DBS7anS8 << std::endl;
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

    //Map für Topics und nodeIds
    std::map<std::string, int> SICAM8toS7Topics;

    //Kurze Wartezeit vor dem ersten Verbindungsaufbau (1s)
    usleep(1000000);

    //While-Schleife damit bei Verbindungsabbruch erneut versucht wird eine Verbindung herzustellen
    while (true)
    {
        //While-Schleife zum Verbindung herstellen
        while (!verbunden)
        {
            // Versuche Verbindung zum OPC UA Server herzustellen
            std::string opcUrl = "opc.tcp://" + Info::IPadresse + ":4840";
            UA_StatusCode status = UA_Client_connect(client, opcUrl.c_str());

            if (status == UA_STATUSCODE_GOOD)
            {
                // Bei Erfolg wird 'verbunden' auf true gesetzt und der Rest des Programms fortgesetzt
                std::cout << "Verbindung zum OPC UA Server hergestellt!" << std::endl;
                verbunden = true;

                // Iteriere durch die Liste, extrahiere die Topics der Signale von SICAM8 an S7 und generiere die Nodes am Server für Registered Write
                for (const auto& signal : writeSignals)
                {
                    //Generiere die NodeID aus dem Signal- und Bausteinnamen
                    UA_NodeId nodeId;
                    nodeId.namespaceIndex = 3; // Setze den Namespace auf 3
                    nodeId.identifierType = UA_NODEIDTYPE_STRING; // Setze den Typ auf STRING
                    std::string fullSymbolicName = "\""+Info::DBS8anS7+"\".\"" + signal.name + "\"";
                    nodeId.identifier.string = UA_STRING_ALLOC(fullSymbolicName.c_str());

                    // Registriere die symbolischen NodeIDs
                    UA_NodeId nodeIds[1] = { nodeId };
                    UA_RegisterNodesRequest request;
                    UA_RegisterNodesRequest_init(&request);
                    request.nodesToRegister = nodeIds;
                    request.nodesToRegisterSize = 1;

                    //Registrierungsanfrage
                    UA_RegisterNodesResponse response = UA_Client_Service_registerNodes(client, request);

                    // Auswertung der Antwort
                    if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                        for (size_t i = 0; i < response.registeredNodeIdsSize; i++) {
                            UA_NodeId registeredNodeId = response.registeredNodeIds[i];
                            printf("Registrierte NodeID: ns=%u;i=%u;Name=%s\n", registeredNodeId.namespaceIndex, registeredNodeId.identifier.numeric, fullSymbolicName.c_str());

                            //Speicher die registrierte NodeIDs zusammen mit Signalnamen in Map
                            int numericNodeId = static_cast<int>(registeredNodeId.identifier.numeric);
                            SICAM8toS7Topics[signal.name] = numericNodeId;
                        }
                    }
                    else 
                    {
                        printf("Node konnte nicht registriert werden!\n");
                    }
                    // Speicher freigeben
                    UA_RegisterNodesResponse_clear(&response);
                    UA_String_clear(&nodeId.identifier.string);
                }
                // Erstelle Subscriptions für alle Signale von S7 an SICAM8
                UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
                request.requestedPublishingInterval = 200.0; // 200ms
                request.requestedLifetimeCount = 60000; // Lebensdauer erhöhen
                request.requestedMaxKeepAliveCount = 50; // Maximale Anzahl von Keep-Alive erhöhen
                request.maxNotificationsPerPublish = 0; // Unbegrenzt
                UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);

                if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                    UA_UInt32 subscriptionId = response.subscriptionId;
                    std::cout << "Subscription erfolgreich erstellt, ID: " << subscriptionId << std::endl;

                    for (const auto& signal : readSignals) 
                    {
                        std::string fullSymbolicName2 = "\"" + Info::DBS7anS8 + "\".\"" + signal.name + "\"";
                        char* symbolicName = const_cast<char*>(fullSymbolicName2.c_str());
                        UA_MonitoredItemCreateRequest monRequest = UA_MonitoredItemCreateRequest_default(UA_NODEID_STRING(3, symbolicName));
                        UA_MonitoredItemCreateResult monResponse = UA_Client_MonitoredItems_createDataChange(
                            client, subscriptionId, UA_TIMESTAMPSTORETURN_BOTH, monRequest, (void*)signal.name.c_str(), dataChangeNotificationCallback, NULL);

                        if (monResponse.statusCode == UA_STATUSCODE_GOOD) 
                        {
                            std::cout << "Monitored Item erfolgreich erstellt für Signal: " << signal.name << std::endl;
                        }
                        else 
                        {
                            std::cerr << "Fehler beim Erstellen des Monitored Items für Signal: " << signal.name << std::endl;
                        }
                    }
                }
                else 
                {
                    std::cerr << "Fehler beim Erstellen der Subscription!" << std::endl;
                }
            }
            else
            {
                // Bei Misserfolg nach 10 Sekunden erneut versuchen
                std::cerr << "Verbindung zum OPC UA Server fehlgeschlagen. Erneuter Versuch in 10 Sekunden..." << std::endl;
                usleep(10000000);
            }
        }

        //While-Schleife zum Lesen und Schreiben von Daten während die Verbindung besteht
        while (verbunden)
        {
            // Wartezeit, um CPU-Auslastung zu begrenzen (aktuell: 2s)
            usleep(2000000);

            // Überprüfe den Client-Status
            UA_SecureChannelState secureChannelState;
            UA_SessionState sessionState;
            UA_StatusCode statusCode;

            UA_Client_getState(client, &secureChannelState, &sessionState, &statusCode);

            if (statusCode != UA_STATUSCODE_GOOD) 
            {
                std::cerr << "Verbindung zum OPC UA Server verloren!" << std::endl;
                verbunden = false;
                UA_Client_disconnect(client);
                break; // Beende die Schleife, wenn der Client nicht mehr verbunden ist
            }

            if (!SICAM8toS7Topics.empty())
            {
                // Variablen initialisieren für OPC UA Write
                UA_Variant valueAttribute;
                UA_Variant_init(&valueAttribute);

                //Aufrufen der Funktion zum Einlesen der Daten der extrahierten Topics
                std::map<std::string, std::tuple<float, uint32_t, int>> SICAM8toS7Data = processSICAM8toS7Topics(SICAM8toS7Topics);

                for (const auto& entry : SICAM8toS7Data)
                {
                    const std::string& topic = entry.first;
                    float value = std::get<0>(entry.second);
                    uint32_t quality = std::get<1>(entry.second);
                    int nodeId = std::get<2>(entry.second);

                    std::cout << "Topic: " << topic << ", Value: " << value << ", Quality: " << quality << ", NodeID: " << nodeId << std::endl;

                    // Suche den entsprechenden Signal-Eintrag zum Topic
                    auto it = std::find_if(writeSignals.begin(), writeSignals.end(),
                        [&topic](const Signal& signal) { return signal.name == topic; });

                    if (it != writeSignals.end())
                    {
                        const Signal& signal = *it;

                        // Umwandeln der übergebenen DB Nr zu einem Int
                        int nodeId = std::get<2>(entry.second);  // NodeID aus der map holen

                        // Schreibe Daten in den Node basierend auf dem Datentyp
                        if (signal.dataType == "Bool") {
                            UA_Boolean boolValue = static_cast<bool>(value);
                            UA_Variant_setScalar(&valueAttribute, &boolValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
                        }
                        else if (signal.dataType == "DInt") {
                            UA_Int32 dintValue = static_cast<UA_Int32>(value);
                            UA_Variant_setScalar(&valueAttribute, &dintValue, &UA_TYPES[UA_TYPES_INT32]);
                        }
                        else if (signal.dataType == "Real") {
                            UA_Float realValue = static_cast<UA_Float>(value);
                            UA_Variant_setScalar(&valueAttribute, &realValue, &UA_TYPES[UA_TYPES_FLOAT]);
                        }

                        // NodeID erstellen
                        UA_NodeId nodeIdObj = UA_NODEID_NUMERIC(3, nodeId); // NamespaceIndex = 3, nodeId ist die numerische ID

                        // Schreibe den Wert in die SPS über OPC UA
                        UA_StatusCode statusCode = UA_Client_writeValueAttribute(client, nodeIdObj, &valueAttribute);

                        // Auswertung des Ergebnisses
                        if (statusCode == UA_STATUSCODE_GOOD)
                        {
                            std::cout << "Wert erfolgreich in die SPS geschrieben: " << topic << std::endl;
                        }
                        else
                        {
                            std::cerr << "Fehler beim Schreiben des Wertes für Topic: " << topic << " (Error Code: " << UA_StatusCode_name(statusCode) << ")" << std::endl;
                        }
                    }
                    else
                    {
                        std::cerr << "Kein Signal gefunden für Topic: " << topic << std::endl;
                    }
                }
            }
            UA_Client_run_iterate(client, 100);

            // Daten aus der globalen Liste verarbeiten
            std::vector<std::pair<std::string, float>> S7toSICAM8Topics;
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                S7toSICAM8Topics.swap(dataList); // Schnelles Austauschen der Listen
            }

            if (!S7toSICAM8Topics.empty()) 
            {
                std::vector<T_EDGE_DATA*> S7toSICAM8Data;

                for (const auto& entry : S7toSICAM8Topics) {
                    const std::string& topic = entry.first;
                    float value = entry.second;

                    // Generiere einen aktuellen Zeitstempel
                    auto currentTime = std::chrono::system_clock::now();
                    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();

                    // Erhalte den Handle für das Topic
                    T_EDGE_DATA_HANDLE handle = edge_data_get_writeable_handle(topic.c_str());

                    if (handle != 0) {
                        // Anlegen eines neuen Eintrags
                        T_EDGE_DATA new_entry;
                        E_EDGE_DATA_TYPE type = E_EDGE_DATA_TYPE_UNKNOWN;
                        (void)memset(&new_entry, 0, sizeof(new_entry));
                        new_entry.handle = handle;
                        new_entry.timestamp64 = timestamp;

                        // E_EDGE_DATA_TYPE aus s_write_list auslesen
                        for (unsigned int y = 0; y < s_write_list.size(); y++) {
                            if (s_write_list[y]->handle == new_entry.handle) {
                                type = s_write_list[y]->type;
                                break;
                            }
                        }

                        // Konvertieren der Daten und Ablegen in new_entry
                        new_entry.type = type;
                        std::string valueStr = std::to_string(value);
                        convert_str_to_value(type, valueStr.c_str(), &new_entry);
                        convert_quality_str_to_value("", &new_entry); // Qualität hier leer, anpassen falls benötigt

                        S7toSICAM8Data.push_back(new T_EDGE_DATA(new_entry));
                    }
                    else {
                        std::cerr << "Kein Handle für Topic: " << topic << std::endl;
                    }
                }

                // Aufruf der Funktion zum Aktualisieren der Daten
                processS7toSICAM8Data(S7toSICAM8Data);

                // Speicher freigeben
                for (size_t i = 0; i < S7toSICAM8Data.size(); ++i) {
                    delete S7toSICAM8Data[i];
                }
            }
        }
    }
}