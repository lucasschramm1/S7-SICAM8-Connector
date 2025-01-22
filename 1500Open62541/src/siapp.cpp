/*
 * siapp-sdk
 *
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Siemens AG
 *
 * Authors of this file:
 *    Lukas Wimmer <lukas.wimmer@siemens.com> (SIAPP SDK Basic Functions)
 *    Lucas Schramm <lucas.schramm@siemens.com> (Specific Functions for implementation of OPC UA Client)
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
#include <fstream>
#include <map>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <iomanip>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/asn1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/conf.h>
#include <openssl/rand.h>
#include <open62541/client.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_highlevel_async.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/securitypolicy.h>
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

    if (it != event_list.end())
    {
        // Eintrag existiert bereits und wird aktualisiert
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
      // Bei Fehler neuen Verbindungsversuch starten
      edge_data_disconnect();
      pthread_mutex_unlock(&s_mutex);
   }
   return 0;
}

//Funktion zum Aktualisieren der Daten von S7 an SICAM8 via Edge Data API
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

//---EIGENE FUNKTIONEN ZUM VERWENDEN DER EDGEDATA API---//---CUSTOM FUNCTIONS TO USE EDGE DATA API---//

// Funktion zum Extrahieren und Umwandeln der Werte und Typen für Topics
std::map<std::string, std::tuple<float, uint32_t, int, std::string>> processSICAM8toS7Topics(const std::map<std::string, std::pair<int, std::string>>& specificTopics)
{
    // Map zum Speichern des jeweiligen value (float), der Qualität und der nodeID
    std::map<std::string, std::tuple<float, uint32_t, int, std::string>> SICAM8toS7Information;

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

            // NodeID und Datentyp aus der Map lesen
            int nodeId = it->second.first;
            std::string dataType = it->second.second;

            // Speichern des numericValue, der Qualität, NodeID und des Datentyps in der Map
            SICAM8toS7Information[event.topic] = std::make_tuple(numericValue, event.quality, nodeId, dataType);
        }
    }
    return SICAM8toS7Information;
}

//---EIGENE FUNKTIONEN ZUR IMPLEMENTIERUNG DES OPC UA CLIENTS---//---CUSTOM FUNCTIONS TO IMPLEMENT OPC UA CLIENT---//

//Gesicherte Liste für eingelesene Daten generieren
std::shared_mutex dataMutex;
std::vector<std::pair<std::string, float>> dataList;

// Variablen für den Verbindungsstatus
bool verbunden = false;
bool restart = false;

// Variablen zur Überwachung des Abschlusses der Write-Requests
bool boolWriteDone = true;
bool dintWriteDone = true;
bool realWriteDone = true;

// Callback für Abschluss des Schreibvorgangs der Bool-Signale
void boolWriteCallback(UA_Client *client, void *userdata, UA_UInt32 requestId, UA_WriteResponse *wr)
{
    if (wr->responseHeader.serviceResult != UA_STATUSCODE_GOOD)
    {
        std::cerr << "Fehler beim Schreiben der Werte (Error Code: " << UA_StatusCode_name(wr->responseHeader.serviceResult) << "). Request ID: " << requestId << std::endl;
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::cerr << "Zeitpunkt des Fehlers: " << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
        verbunden = false;
        restart = true;
    }
    boolWriteDone = true;
    UA_WriteResponse_clear(wr);
}

// Callback für Abschluss des Schreibvorgangs der DInt-Signale
void dintWriteCallback(UA_Client *client, void *userdata, UA_UInt32 requestId, UA_WriteResponse *wr)
{
    if (wr->responseHeader.serviceResult != UA_STATUSCODE_GOOD)
    {
        std::cerr << "Fehler beim Schreiben der Werte (Error Code: " << UA_StatusCode_name(wr->responseHeader.serviceResult) << "). Request ID: " << requestId << std::endl;
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::cerr << "Zeitpunkt des Fehlers: " << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
        verbunden = false;
        restart = true;
    }
    dintWriteDone = true;
    UA_WriteResponse_clear(wr);
}

// Callback für Abschluss des Schreibvorgangs der Real-Signale
void realWriteCallback(UA_Client *client, void *userdata, UA_UInt32 requestId, UA_WriteResponse *wr)
{
    if (wr->responseHeader.serviceResult != UA_STATUSCODE_GOOD)
    {
        std::cerr << "Fehler beim Schreiben der Werte (Error Code: " << UA_StatusCode_name(wr->responseHeader.serviceResult) << "). Request ID: " << requestId << std::endl;
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::cerr << "Zeitpunkt des Fehlers: " << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
        verbunden = false;
        restart = true;
    }
    realWriteDone = true;
    UA_WriteResponse_clear(wr);
}

// Variable zur Überwachung des Abschlusses der Read-Request
bool readDone = true;

// Callback für Abschluss des Lesevorgangs
void readCallback(UA_Client *client, void *userdata, UA_UInt32 requestId, UA_ReadResponse *response)
{
    if (response->responseHeader.serviceResult == UA_STATUSCODE_GOOD)
    {
        auto S7toSICAM8Nodes = static_cast<std::map<std::string, int>*>(userdata);
        auto it = S7toSICAM8Nodes->begin();

        for (size_t i = 0; i < response->resultsSize; ++i, ++it)
        {
            if (response->results[i].status == UA_STATUSCODE_GOOD)
            {
                UA_Variant value = response->results[i].value;
                float receivedValue = 0.0;

                // Überprüfe den Datentyp des Wertes
                const UA_DataType* type = value.type;
                if (type == &UA_TYPES[UA_TYPES_BOOLEAN])
                {
                    receivedValue = static_cast<float>(*(UA_Boolean*)value.data);
                }
                else if (type == &UA_TYPES[UA_TYPES_INT32])
                {
                    receivedValue = static_cast<float>(*(UA_Int32*)value.data);
                }
                else if (type == &UA_TYPES[UA_TYPES_FLOAT])
                {
                    receivedValue = *(UA_Float*)value.data;
                }
                // Verwenden des Topics aus S7toSICAM8Nodes
                const std::string& topic = it->first;

                // Daten in die globale Liste aktualisieren
                std::unique_lock<std::shared_mutex> lock(dataMutex);
                auto it = std::find_if(dataList.begin(), dataList.end(), [&topic](const std::pair<std::string, float>& entry)
                {return entry.first == topic;});

                if (it == dataList.end())
                {
                    dataList.emplace_back(topic, receivedValue);
                }
                else
                {
                    it->second = receivedValue;
                }
            }
            else
            {
                std::cerr << "Fehler beim Lesen des Werts: " << UA_StatusCode_name(response->results[i].status) << std::endl;
            }
        }
    }
    else
    {
        std::cerr << "Fehler bei der Leseanfrage: " << UA_StatusCode_name(response->responseHeader.serviceResult) << std::endl;
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::cerr << "Zeitpunkt des Fehlers: " << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
        verbunden = false;
        restart = true;
    }
    readDone = true;
    UA_ReadResponse_clear(response);
}

//---EIGENE FUNKTIONEN ZUR ZERTIFIKATSVERWENDUNG---//---CUSTOM FUNCTIONS TO USE CERTIFICATES---//

// Funktion zum Verifizieren ob Client-Zertifikat und Key zusammenpassen (Security = 3)
bool verifyCertificateAndKey(const char* certPath, const char* keyPath)
{
    // Lade Zertifikat und Key
    FILE* certFile = fopen(certPath, "rb");
    FILE* keyFile = fopen(keyPath, "rb");
    if (!certFile || !keyFile)
    {
        std::cout << "Fehler beim Öffnen der Zertifikat- oder Schlüsseldatei!" << std::endl;
        return false;
    }
    // Konvertiere in X509 und EVP_PKEY
    X509* cert = d2i_X509_fp(certFile, NULL);
    EVP_PKEY* pkey = d2i_PrivateKey_fp(keyFile, NULL);
    fclose(certFile);
    fclose(keyFile);

    if (!cert || !pkey) 
    {
        std::cout << "Fehler beim Lesen der Zertifikat- oder Schlüsseldatei!" << std::endl;
        if (cert) X509_free(cert);
        if (pkey) EVP_PKEY_free(pkey);
        return false;
    }
    // Überprüfe Zertifikat und Key
    bool result = X509_check_private_key(cert, pkey);
    if (!result)
    {
        std::cout << "Zertifikat und privater Schlüssel stimmen nicht überein!" << std::endl;
    }
    else
    {
        std::cout << "Zertifikat und privater Schlüssel stimmen überein." << std::endl;
    }
    // Speicher freigeben
    X509_free(cert);
    EVP_PKEY_free(pkey);

    return result;
}

// Funktion zum Verifizieren ob Client-Zertifikat und CA zusammenpassen (Security = 3)
bool verifyCACertificate(const char* caCertPath, const char* clientCertPath)
{
    // Lade CA und Zertifikat
    FILE* caFile = fopen(caCertPath, "rb");
    FILE* clientFile = fopen(clientCertPath, "rb");
    if (!caFile || !clientFile)
    {
        std::cout << "Fehler beim Öffnen der CA- oder Client-Zertifikatdatei!" << std::endl;
        return false;
    }
    // Konvertiere in X509
    X509* caCert = d2i_X509_fp(caFile, NULL);
    X509* clientCert = d2i_X509_fp(clientFile, NULL);
    fclose(caFile);
    fclose(clientFile);

    if (!caCert || !clientCert)
    {
        std::cout << "Fehler beim Lesen der CA- oder Client-Zertifikatdatei!" << std::endl;
        if (caCert) X509_free(caCert);
        if (clientCert) X509_free(clientCert);
        ERR_print_errors_fp(stderr);
        return false;
    }
    // Extrahiere den öffentlichen Schlüssel aus dem CA-Zertifikat
    EVP_PKEY* caPublicKey = X509_get_pubkey(caCert);
    if (!caPublicKey)
    {
        std::cout << "Fehler beim Extrahieren des öffentlichen Schlüssels aus der CA!" << std::endl;
        X509_free(caCert);
        X509_free(clientCert);
        return false;
    }
    // Überprüfe die Signatur des Client-Zertifikats mit dem öffentlichen Schlüssel der CA
    bool result = (X509_verify(clientCert, caPublicKey) == 1);
    if (!result)
    {
        std::cout << "Client-Zertifikat wurde nicht von der CA signiert!" << std::endl;
        ERR_print_errors_fp(stderr);
    }
    else
    {
        std::cout << "Client-Zertifikat wurde von der CA signiert." << std::endl;
    }
    // Speicher freigeben
    EVP_PKEY_free(caPublicKey);
    X509_free(caCert);
    X509_free(clientCert);

    return result;
}

//Funktion um ApplicationURI aus Client-Zertifikat auszulesen (Security = 3)
std::string getApplicationURI(const char* clientCertPath)
{
    // Lese Client-Zertifikat
    FILE* clientFile = fopen(clientCertPath, "rb");
    X509* clientCert = d2i_X509_fp(clientFile, NULL);
    fclose(clientFile);

    // Lesen der Extensions des Zertifikats
    STACK_OF(GENERAL_NAME) *san_names = NULL;
    san_names = (STACK_OF(GENERAL_NAME)*) X509_get_ext_d2i(clientCert, NID_subject_alt_name, NULL, NULL);
    if (!san_names) 
    {
        std::cout << "Keine SAN-Erweiterungen im Zertifikat gefunden." << std::endl;
        return "";
    }
    // Durchsuchen der SAN-Namen nach einem URI
    for (int i = 0; i < sk_GENERAL_NAME_num(san_names); ++i) 
    {
        GENERAL_NAME* san_name = sk_GENERAL_NAME_value(san_names, i);

        // Überprüfen, ob der SAN-Typ ein URI ist
        if (san_name->type == GEN_URI) 
        {
            // URI als ASN1_STRING extrahieren
            const char* uri = (const char*)ASN1_STRING_get0_data(san_name->d.uniformResourceIdentifier);
            std::string applicationURI(uri);
            std::cout << "Gefundene Application URI: " << applicationURI << std::endl;

            //Speicher freigeben und Application URI zurückgeben
            sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
            return applicationURI;
        }
    }
    //Speicher freigeben
    sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
    std::cout << "Kein URI in den SAN-Erweiterungen gefunden." << std::endl;
    return "";
}

//Funktion zum Laden der Zertifikate (Entnommen aus Open62541 Beispielen)
static UA_INLINE UA_ByteString loadFile(const char* const path)
{
    UA_ByteString fileContents = UA_STRING_NULL;
    //Datei öffnen
    FILE* fp = fopen(path, "rb");
    if (!fp)
    {
        errno = 0;
        return fileContents;
    }
    //Dateilänge bestimmen und Datei einlesen
    fseek(fp, 0, SEEK_END);
    fileContents.length = (size_t)ftell(fp);
    fileContents.data = (UA_Byte*)UA_malloc(fileContents.length * sizeof(UA_Byte));
    if (fileContents.data)
    {
        fseek(fp, 0, SEEK_SET);
        size_t read = fread(fileContents.data, sizeof(UA_Byte), fileContents.length, fp);
        if (read != fileContents.length)
        {
            UA_ByteString_clear(&fileContents);
        }
    }
    else
    {
        fileContents.length = 0;
    }
    fclose(fp);

    return fileContents;
}

// Hilfsfunktion, um einen OpenSSL RSA-Schlüssel in ein UA_ByteString zu konvertieren
UA_ByteString evpKeyToUAByteString(EVP_PKEY* evpKey)
{
    unsigned char* keyData = NULL;
    int keyLength = i2d_PrivateKey(evpKey, &keyData);

    UA_ByteString byteString;
    byteString.data = keyData;
    byteString.length = keyLength;

    return byteString;
}

// Hilfsfunktion, um ein X509 Zertifikat in ein UA_ByteString zu konvertieren
UA_ByteString x509ToUAByteString(X509* cert)
{
    unsigned char* certData = NULL;
    int certLength = i2d_X509(cert, &certData);

    UA_ByteString byteString;
    byteString.data = certData;
    byteString.length = certLength;

    return byteString;
}

//Funktion zum Erstellen eines selbstsignierten Zertifikats für die Verbindung mit Username und Passwort ohne Zertifikate (Security = 2)
void generateSelfSignedCertificate(UA_ByteString& certificate, UA_ByteString& privateKey, std::string applicationURI)
{
    // Initialisiere OpenSSL
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, NULL);
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
    RAND_poll();

    // Erstelle EVP_PKEY für Private Key
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey)
    {
        std::cerr << "Fehler beim Erstellen von EVP_PKEY." << std::endl;
        return;
    }
    // Generiere RSA Schlüssel mit EVP_PKEY
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx)
    {
        std::cerr << "Fehler beim Erstellen von EVP_PKEY_CTX." << std::endl;
        EVP_PKEY_free(pkey);
        return;
    }
    if (EVP_PKEY_keygen_init(pctx) <= 0)
    {
        std::cerr << "Fehler beim Initialisieren des Keygen." << std::endl;
        EVP_PKEY_CTX_free(pctx);
        EVP_PKEY_free(pkey);
        return;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) <= 0)
    {
        std::cerr << "Fehler beim Setzen der RSA Schlüssellänge." << std::endl;
        EVP_PKEY_CTX_free(pctx);
        EVP_PKEY_free(pkey);
        return;
    }
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0)
    {
        std::cerr << "Fehler beim Generieren des RSA Schlüssels." << std::endl;
        EVP_PKEY_CTX_free(pctx);
        EVP_PKEY_free(pkey);
        return;
    }
    EVP_PKEY_CTX_free(pctx);

    // Erstelle X509 Zertifikat
    X509* x509 = X509_new();
    if (!x509)
    {
        std::cerr << "Fehler beim Erstellen des X509 Zertifikats: " << ERR_error_string(ERR_get_error(), NULL) << std::endl;
        EVP_PKEY_free(pkey);
        return;
    }
    //Setze Seriennummer und Gültigkeiten
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 3153600000L);

    //Ordner pkey dem Zertifikat zu
    X509_set_pubkey(x509, pkey);

    //Setze den SubjectName
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
        (unsigned char*)"DE", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
        (unsigned char*)"Siemens", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (unsigned char*)"OPCua", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Ertstelle GENERAL_NAMES Struktur für SAN
    GENERAL_NAMES* san_names = sk_GENERAL_NAME_new_null();
    if (!san_names)
    {
        return;
    }
    // Erstelle einen GENERAL_NAME für den URI SAN-Eintrag
    GENERAL_NAME* san_name = GENERAL_NAME_new();
    if (!san_name)
    {
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }
    //Setze den SAN-Eintrag auf URI
    san_name->type = GEN_URI;
    san_name->d.uniformResourceIdentifier = ASN1_IA5STRING_new();
    if (!san_name->d.uniformResourceIdentifier)
    {
        GENERAL_NAME_free(san_name);
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }
    ASN1_STRING_set(san_name->d.uniformResourceIdentifier, applicationURI.c_str(), applicationURI.length());

    // Füge den GENERAL_NAME zu GENERAL_NAMES hinzu
    sk_GENERAL_NAME_push(san_names, san_name);

    // Erstelle X509 Extension für den SAN
    X509_EXTENSION* extension_san = X509V3_EXT_i2d(NID_subject_alt_name, 0, san_names);

    if (!extension_san)
    {
        std::cerr << "Error creating X509 extension: " << ERR_error_string(ERR_get_error(), NULL) << std::endl;
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }
    // Füge die Extension dem Zertifikat hinzu
    if (X509_add_ext(x509, extension_san, -1) != 1)
    {
        std::cerr << "Error adding X509 extension: " << ERR_error_string(ERR_get_error(), NULL) << std::endl;
        X509_EXTENSION_free(extension_san);
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }
    //Speicher freigeben
    X509_EXTENSION_free(extension_san);
    sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);

    //Signieren des Zertifikats
    if (X509_sign(x509, pkey, EVP_sha256()) <= 0)
    {
        std::cerr << "X509_sign failed: " << ERR_error_string(ERR_get_error(), NULL) << std::endl;
        ERR_print_errors_fp(stderr);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return;
    }
    //Konvertieren der Zertifikate in UA_ByteStrings
    certificate = x509ToUAByteString(x509);
    privateKey = evpKeyToUAByteString(pkey);

    //Speicher freigeben
    EVP_PKEY_free(pkey);
    X509_free(x509);
}

//---EIGENE FUNKTIONEN ZUR PASSWORTENTSCHLÜSSELUNG---//---CUSTOM FUNCTIONS TO DECRYPT PASSWORD---//

// Funktion zum Einlesen des privaten Schlüssels aus einer Datei
EVP_PKEY* loadPrivateKey(const char* filePath)
{
    FILE* fp = fopen(filePath, "r");
    if (!fp)
    {
        std::cerr << "Fehler beim Öffnen der Datei: " << filePath << std::endl;
        return nullptr;
    }
    EVP_PKEY* pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    return pkey;
}

// Funktion zum Dekodieren eines Base64-Strings
std::vector<unsigned char> base64Decode(const std::string& encoded)
{
    BIO* bio = BIO_new_mem_buf(encoded.data(), encoded.size());
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    std::vector<unsigned char> decoded(encoded.size());
    int decodedLength = BIO_read(bio, decoded.data(), encoded.size());
    decoded.resize(decodedLength);

    BIO_free_all(bio);
    return decoded;
}

// Funktion zum Entschlüsseln des Passworts
std::string decryptPassword(const std::string& encryptedPassword, EVP_PKEY* privateKey)
{
    std::vector<unsigned char> encryptedBytes = base64Decode(encryptedPassword);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(privateKey, nullptr);
    if (!ctx)
    {
        std::cerr << "Fehler beim Erstellen des Kontextes: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        return "";
    }
    if (EVP_PKEY_decrypt_init(ctx) <= 0)
    {
        std::cerr << "Fehler beim Initialisieren der Entschlüsselung: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        EVP_PKEY_CTX_free(ctx);
        return "";
    }
    size_t outLen;
    if (EVP_PKEY_decrypt(ctx, nullptr, &outLen, encryptedBytes.data(), encryptedBytes.size()) <= 0)
    {
        std::cerr << "Fehler beim Bestimmen der Ausgabelänge: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        EVP_PKEY_CTX_free(ctx);
        return "";
    }
    std::vector<unsigned char> decrypted(outLen);
    if (EVP_PKEY_decrypt(ctx, decrypted.data(), &outLen, encryptedBytes.data(), encryptedBytes.size()) <= 0)
    {
        std::cerr << "Fehler beim Entschlüsseln: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        EVP_PKEY_CTX_free(ctx);
        return "";
    }
    EVP_PKEY_CTX_free(ctx);
    return std::string(decrypted.begin(), decrypted.end());
}

//Ablauf in Main-Methode
int main(int argc, char** argv)
{
    printf("Programm gestartet\n");
    pthread_mutex_init(&s_mutex, NULL);
    pthread_mutex_init(&s_mutex_event, NULL);
    pthread_t edgedata_thread_nr;
    int ret;

    //Thread erstellen der edgedata_task aufruft
    if (pthread_create(&edgedata_thread_nr, NULL, edgedata_task, &ret))
    {
        printf("Fehler beim Erstellen des Threads\n");
        return 1;
    }

    // Deklaration eines Open62541 Clients inklusive Default-Config, Timeout von 30s und SCL von 1h
    UA_Client* client = UA_Client_new();
    UA_ClientConfig* config = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(config);
    config->timeout = 30000;
    config->noReconnect = true;
    config->secureChannelLifeTime = 3600000;
    config->requestedSessionTimeout = 30000;

    // Standardmäßig keine Sicherheit verwenden
    config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#None");

    //OPC UA Securityeinstellungen für Nutzung von Username und Passwort
    if (Info::Security == 2)
    {
            // Setzen einer DefaultApplicationURI
            config->clientDescription.applicationUri = UA_String_fromChars("urn:SIMATIC.S7-1500.OPC-UA.Application:Default");

            // Generiere ein selbstsigniertes Zertifikat mit Key
            UA_ByteString certificate = UA_STRING_NULL;
            UA_ByteString privateKey = UA_STRING_NULL;
            std::string applicationURI = "urn:SIMATIC.S7-1500.OPC-UA.Application:Default";
            generateSelfSignedCertificate(certificate, privateKey, applicationURI);

            // Setzen des selbstsignierten Zertifikats und Keys
            UA_ClientConfig_setDefaultEncryption(config, certificate, privateKey, nullptr, 0, nullptr, 0);
    }

    //OPC UA Securityeinstellungen für Nutzung von Zertifikaten und Username und Passwort
    if (Info::Security == 3)
    {
        // Laden und umwandeln der Dateien in UA_ByteStrings
        std::string certPath = "/cert/ClientCert.der";
        std::string keyPath = "/cert/ClientKey.der";
        std::string caPath = "/cert/CertAuth.der";
        UA_ByteString certificate = loadFile(certPath.c_str());
        UA_ByteString privateKey = loadFile(keyPath.c_str());
        UA_ByteString trustListArray[] = { loadFile(caPath.c_str()) };
        size_t trustListSize = sizeof(trustListArray) / sizeof(trustListArray[0]);
        UA_ByteString* revocationList = NULL;

        // Verifiziere ob Client-Zertifikat und Key zusammenpassen
        if (!verifyCertificateAndKey(certPath.c_str(), keyPath.c_str()))
        {
            std::cout << "Zertifikat und privater Schlüssel stimmen nicht überein! Es wird versucht ohne Verschlüsselung eine Verbindung aufzubauen." << std::endl;
        }
        // Verifiziere ob Client-Zertifikat und CA zusammenpassen
        else if (!verifyCACertificate(caPath.c_str(), certPath.c_str()))
        {
            std::cout << "CA-Zertifikat ist ungültig! Es wird versucht ohne Verschlüsselung eine Verbindung aufzubauen." << std::endl;
        }
        //Falls ja, Encryption mit Zertifikaten, ApplicationUri wie im Client-Zertifikat und SecurityPolicyURI je nach Auswahl setzen
        else
        {
            std:: string applicationURI = getApplicationURI(certPath.c_str());
            config->clientDescription.applicationUri =  UA_String_fromChars(applicationURI.c_str());
            config->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;

            if (Info::Encryption == 1)
            {config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic128Rsa15");}
            else if (Info::Encryption == 2)
            {config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256");}
            else if (Info::Encryption == 3)
            {config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");}
            else if (Info::Encryption == 4)
            {config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Aes128_Sha256_RsaOaep");}
            else if (Info::Encryption == 5)
            {config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Aes256_Sha256_RsaPss");}

            // Updaten der Verschlüsselung
            UA_StatusCode retval = UA_ClientConfig_setDefaultEncryption(config, certificate, privateKey, trustListArray, trustListSize, revocationList, 0);
            if (retval != UA_STATUSCODE_GOOD)
            {
                std::cout << "Fehler beim Setzen der Verschlüsselung: " << retval << std::endl;
            }
        }
    }

    // Ausgabe der eingelesenen Informationen zu IP und DB Nummern
    std::cout << "\nName DBS8anS7: " << Info::DBS8anS7 << std::endl;
    std::cout << "Name DBS7anS8: " << Info::DBS7anS8 << std::endl;
    std::cout << "IP-Adresse: " << Info::IPadresse << std::endl;

    // Ausgabe aller Signale von SICAM8 an S7
    printf("\nSignale von SICAM 8 an S7\n");

    for (const auto& signal : writeSignals)
    {
            std::cout << "Name: " << signal.name << ", Datentyp: " << signal.dataType << std::endl;
    }

    // Ausgabe aller Signale von S7 an SICAM8
    printf("\nSignale von S7 an SICAM8\n");

    for (const auto& signal : readSignals)
    {
        std::cout << "Name: " << signal.name << ", Datentyp: " << signal.dataType << std::endl;
    }

    // Map für Topics und nodeIds von Signalen von SICAM8 an S7
    std::map<std::string, std::pair<int, std::string>> SICAM8toS7Topics;

    // Map für registrierte Handle von Signalen von SICAM8 an S7
    std::map<std::string, int> S7toSICAM8Nodes;

    // Warte bei erstem Verbinden, bis EdgeDataAPI verbunden und synchronisiert ist
    while (!edge_data_sync)
    {
        usleep(1000000);
    }

    // While-Schleife damit bei Verbindungsabbruch erneut versucht wird eine Verbindung herzustellen
    while (true)
    {
        // While-Schleife zum Verbindung herstellen
        while (!verbunden)
        {
            if (restart == true)
            {
                UA_Client_disconnect(client);
                restart = false;
            }
            // Versuche Verbindung zum OPC UA Server herzustellen
            UA_StatusCode status;
            // Falls keine Security (Security = 1) ausgewählt:
            if (Info::Security == 1)
            {
                // Verbindung ohne Username und Passwort herstellen
                std::string opcUrl = "opc.tcp://" + Info::IPadresse + ":4840";
                status = UA_Client_connect(client, opcUrl.c_str());
            }
            // Sonst (Security = 2 oder 3):
            else
            {
                // Adresse und Username einlesen und Passworr entschlüsseln
                std::string opcUrl = "opc.tcp://" + Info::IPadresse + ":4840";
                std::string username = Info::Username;

                // Pfad zum privaten Schlüssel
                std::string KeyPath = "/cert/Key.pem";
                // Verschlüsseltes Passwort aus Header laden
                std::string encryptedPassword = Info::Passwort;
                // Privaten SChlüssel aus Datei laden
                EVP_PKEY* privateKey = loadPrivateKey(KeyPath.c_str());
                //Passwort entschlüsseln
                std::string decryptedPassword = decryptPassword(encryptedPassword, privateKey);
                // Speicher freigeben
                EVP_PKEY_free(privateKey);

                // Verbindung mit Benutzername und Passwort herstellen
                status = UA_Client_connectUsername(client, opcUrl.c_str(), username.c_str(), decryptedPassword.c_str());
            }

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
                    nodeId.namespaceIndex = 3; // Setze den Namespace auf 3 für Standard-SIMATIC-Server-Schnittstelle
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

                            // Speicher die registrierte NodeIDs zusammen mit Signalnamen und Datentyp in Map
                            int numericNodeId = static_cast<int>(registeredNodeId.identifier.numeric);
                            std::string dataType = signal.dataType;
                            SICAM8toS7Topics[signal.name] = std::make_pair(numericNodeId, dataType);
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

                // Iteriere durch die Liste, extrahiere die Topics der Signale von S7 an SICAM8 und generiere die Nodes am Server für Registered Read
                for (const auto& signal : readSignals)
                {
                    //Generiere die NodeID aus dem Signal- und Bausteinnamen
                    UA_NodeId nodeId;
                    nodeId.namespaceIndex = 3; // Setze den Namespace auf 3 für Standard-SIMATIC-Server-Schnittstelle
                    nodeId.identifierType = UA_NODEIDTYPE_STRING; // Setze den Typ auf STRING
                    std::string fullSymbolicName = "\""+Info::DBS7anS8+"\".\"" + signal.name + "\"";
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
                    if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD)
                    {
                        for (size_t i = 0; i < response.registeredNodeIdsSize; i++)
                        {
                            UA_NodeId registeredNodeId = response.registeredNodeIds[i];
                            printf("Registrierte NodeID: ns=%u;i=%u;Name=%s\n", registeredNodeId.namespaceIndex, registeredNodeId.identifier.numeric, fullSymbolicName.c_str());

                            // Speicher die registrierte NodeIDs zusammen mit Signalnamen und Datentyp in Map
                            int numericNodeId = static_cast<int>(registeredNodeId.identifier.numeric);
                            S7toSICAM8Nodes[signal.name] = numericNodeId;
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
            }
            else
            {
                // Bei Misserfolg nach 10 Sekunden erneut versuchen eine Verbindung aufzubauen
                std::cerr << "Verbindung zum OPC UA Server fehlgeschlagen. Erneuter Versuch in 10 Sekunden..." << std::endl;
                usleep(10000000);
            }
        }

        //While-Schleife zum Lesen und Schreiben von Daten während die Verbindung besteht
        while (verbunden)
        {
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

            //Aktualisieren der Werte von SICAM8 an S7 auf OPC UA Server
            if (!SICAM8toS7Topics.empty())
            {
                //Aufrufen der Funktion zum Einlesen der Daten der extrahierten Topics
                std::map<std::string, std::tuple<float, uint32_t, int, std::string>> SICAM8toS7Data = processSICAM8toS7Topics(SICAM8toS7Topics);

                // Erstellen der Datenvektoren von SICAM8 an S7
                std::vector<UA_WriteValue> boolWriteValues;
                std::vector<UA_WriteValue> dintWriteValues;
                std::vector<UA_WriteValue> realWriteValues;

                // Datenvektoren befüllen
                for (const auto& entry : SICAM8toS7Data)
                {
                    const std::string& topic = entry.first;
                    float value = std::get<0>(entry.second);
                    uint32_t quality = std::get<1>(entry.second);
                    int nodeId = std::get<2>(entry.second);
                    std::string dataType = std::get<3>(entry.second);

                    UA_WriteValue wv;
                    UA_WriteValue_init(&wv);
                    wv.nodeId = UA_NODEID_NUMERIC(3, nodeId);
                    wv.attributeId = UA_ATTRIBUTEID_VALUE;

                    if (dataType == "Bool")
                    {
                        UA_Boolean boolValue = static_cast<bool>(value);
                        UA_Variant_setScalarCopy(&wv.value.value, &boolValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
                        wv.value.hasValue = true;
                        boolWriteValues.push_back(wv);
                    }
                    else if (dataType == "DInt")
                    {
                        UA_Int32 dintValue = static_cast<UA_Int32>(value);
                        UA_Variant_setScalarCopy(&wv.value.value, &dintValue, &UA_TYPES[UA_TYPES_INT32]);
                        wv.value.hasValue = true;
                        dintWriteValues.push_back(wv);
                    }
                    else if (dataType == "Real")
                    {
                        UA_Float realValue = static_cast<UA_Float>(value);
                        UA_Variant_setScalarCopy(&wv.value.value, &realValue, &UA_TYPES[UA_TYPES_FLOAT]);
                        wv.value.hasValue = true;
                        realWriteValues.push_back(wv);
                    }
                }
                // Asynchrone Schreibanfragen erstellen und senden
                if (!boolWriteValues.empty())
                {
                    boolWriteDone = false;
                    UA_WriteRequest boolWriteRequest;
                    UA_WriteRequest_init(&boolWriteRequest);
                    boolWriteRequest.nodesToWrite = boolWriteValues.data();
                    boolWriteRequest.nodesToWriteSize = boolWriteValues.size();
                    UA_UInt32 boolRequestId;
                    UA_StatusCode statusCode = UA_Client_sendAsyncWriteRequest(client, &boolWriteRequest, boolWriteCallback, nullptr, &boolRequestId);
                    if (statusCode != UA_STATUSCODE_GOOD)
                    {
                        std::cerr << "Fehler beim Senden der asynchronen Schreibanfrage (Error Code: " << UA_StatusCode_name(statusCode) << ")" << std::endl;
                        verbunden = false;
                        UA_Client_disconnect(client);
                        break;
                    }
                }
                if (!dintWriteValues.empty())
                {
                    dintWriteDone = false;
                    UA_WriteRequest dintWriteRequest;
                    UA_WriteRequest_init(&dintWriteRequest);
                    dintWriteRequest.nodesToWrite = dintWriteValues.data();
                    dintWriteRequest.nodesToWriteSize = dintWriteValues.size();
                    UA_UInt32 dintRequestId;
                    UA_StatusCode statusCode = UA_Client_sendAsyncWriteRequest(client, &dintWriteRequest, dintWriteCallback, nullptr, &dintRequestId);
                    if (statusCode != UA_STATUSCODE_GOOD)
                    {
                        std::cerr << "Fehler beim Senden der asynchronen Schreibanfrage (Error Code: " << UA_StatusCode_name(statusCode) << ")" << std::endl;
                        verbunden = false;
                        UA_Client_disconnect(client);
                        break;
                    }
                }
                if (!realWriteValues.empty())
                {
                    realWriteDone = false;
                    UA_WriteRequest realWriteRequest;
                    UA_WriteRequest_init(&realWriteRequest);
                    realWriteRequest.nodesToWrite = realWriteValues.data();
                    realWriteRequest.nodesToWriteSize = realWriteValues.size();
                    UA_UInt32 realRequestId;
                    UA_StatusCode statusCode = UA_Client_sendAsyncWriteRequest(client, &realWriteRequest, realWriteCallback, nullptr, &realRequestId);
                    if (statusCode != UA_STATUSCODE_GOOD)
                    {
                        std::cerr << "Fehler beim Senden der asynchronen Schreibanfrage (Error Code: " << UA_StatusCode_name(statusCode) << ")" << std::endl;
                        verbunden = false;
                        UA_Client_disconnect(client);
                        break;
                    }
                }
                // Speicher freigeben
                for (auto& wv : boolWriteValues)
                {
                    UA_Variant_clear(&wv.value.value);
                }
                for (auto& wv : dintWriteValues)
                {
                    UA_Variant_clear(&wv.value.value);
                }
                for (auto& wv : realWriteValues)
                {
                    UA_Variant_clear(&wv.value.value);
                }
            }

            if (!S7toSICAM8Nodes.empty())
            {
                // Erstellen der Leseanfragen für die Elemente aus S7toSICAM8Nodes
                std::vector<UA_ReadValueId> readValues;

                for (const auto& entry : S7toSICAM8Nodes)
                {
                    const std::string& signalName = entry.first;
                    int nodeId = entry.second;

                    UA_ReadValueId rv;
                    UA_ReadValueId_init(&rv);
                    rv.nodeId = UA_NODEID_NUMERIC(3, nodeId);
                    rv.attributeId = UA_ATTRIBUTEID_VALUE;
                    readValues.push_back(rv);
                }
                // Asynchrone Leseanfrage senden
                if (!readValues.empty())
                {
                    readDone = false;
                    UA_ReadRequest readRequest;
                    UA_ReadRequest_init(&readRequest);
                    readRequest.nodesToRead = readValues.data();
                    readRequest.nodesToReadSize = readValues.size();
                    UA_UInt32 readRequestId;
                    UA_StatusCode statusCode = UA_Client_sendAsyncReadRequest(client, &readRequest, readCallback, (void*)&S7toSICAM8Nodes, &readRequestId);
                    if (statusCode != UA_STATUSCODE_GOOD)
                    {
                        std::cerr << "Fehler beim Senden der asynchronen Leseanfrage (Error Code: " << UA_StatusCode_name(statusCode) << ")" << std::endl;
                        verbunden = false;
                        UA_Client_disconnect(client);
                        break;
                    }
                }
                // Speicher freigeben
                for (auto& rv : readValues)
                {
                    UA_NodeId_clear(&rv.nodeId);
                }
            }

            // Auf Bestätigung aller Anfragen warten
            while (!boolWriteDone || !dintWriteDone || !realWriteDone || !readDone)
            {
                // Abfrage der asynchronen Schreib- und Leseanfragen
                UA_StatusCode retval = UA_Client_run_iterate(client, 10);
                if (retval != UA_STATUSCODE_GOOD)
                {
                    auto now = std::chrono::system_clock::now();
                    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
                    std::cerr << "Zeitpunkt des Fehlers: " << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
                    verbunden = false;
                    UA_Client_disconnect(client);
                    break;
                }
            }

            // Liste gelesener Daten in S7toSICAM8Topics schreiben
            std::vector<std::pair<std::string, float>> S7toSICAM8Topics;
            {
                std::unique_lock<std::shared_mutex> lock(dataMutex);
                S7toSICAM8Topics.swap(dataList);
            }

            // Aktualiseren der Signale von S7 an SICAM8 auf EdgeDataAPI
            if (!S7toSICAM8Topics.empty())
            {
                std::vector<T_EDGE_DATA*> S7toSICAM8Data;

                for (const auto& entry : S7toSICAM8Topics)
                {
                    const std::string& topic = entry.first;
                    float value = entry.second;

                    // Generiere einen aktuellen Zeitstempel
                    auto currentTime = std::chrono::system_clock::now();
                    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();

                    // Erhalte das Handle für das Topic
                    T_EDGE_DATA_HANDLE handle = edge_data_get_writeable_handle(topic.c_str());

                    if (handle != 0)
                    {
                        // Anlegen eines neuen Eintrags
                        T_EDGE_DATA new_entry;
                        E_EDGE_DATA_TYPE type = E_EDGE_DATA_TYPE_UNKNOWN;
                        (void)memset(&new_entry, 0, sizeof(new_entry));
                        new_entry.handle = handle;
                        new_entry.timestamp64 = timestamp;

                        // E_EDGE_DATA_TYPE aus s_write_list auslesen
                        for (unsigned int y = 0; y < s_write_list.size(); y++)
                        {
                            if (s_write_list[y]->handle == new_entry.handle)
                            {
                                type = s_write_list[y]->type;
                                break;
                            }
                        }
                        // Konvertieren der Daten und Ablegen in new_entry
                        new_entry.type = type;
                        std::string valueStr = std::to_string(value);
                        convert_str_to_value(type, valueStr.c_str(), &new_entry);
                        convert_quality_str_to_value("", &new_entry); // Qualität hier aktuell immer leer = gültig

                        S7toSICAM8Data.push_back(new T_EDGE_DATA(new_entry));
                    }
                    else
                    {
                        std::cerr << "Kein Handle für Topic: " << topic << std::endl;
                    }
                }
                // Aufruf der Funktion zum Aktualisieren der Daten
                processS7toSICAM8Data(S7toSICAM8Data);

                // Speicher freigeben
                for (size_t i = 0; i < S7toSICAM8Data.size(); ++i)
                {
                    delete S7toSICAM8Data[i];
                }
            }
        }
    }
}
