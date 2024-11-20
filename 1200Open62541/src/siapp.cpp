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
static pthread_mutex_t s_mutex_log;

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

//Konvertiere String in einen EDGE_DATA_VALUE
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

//Konvertiere EDGE_DATA_QUALITY in einen Integer
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

//Konvertiere EDGE_DATA_VALUE in einen String
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

//EdgeDataCallback für abonnierte Topics
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
      //Bei Fehler neuen Verbindungsversuch starten
      edge_data_disconnect();
      pthread_mutex_unlock(&s_mutex);
   }
   return 0;
}

//---EIGENE FUNKTIONEN ZUM VERWENDEN DER EDGEDATA API---//---CUSTOM FUNCTIONS TO USE EDGE DATA API---//

// Funktion zum Extrahieren und Umwandeln der Werte und Typen für Topics
std::map<std::string, std::tuple<float, uint32_t, std::string>> processSICAM8toS7Topics(const std::map<std::string, std::string>& specificTopics)
{
    // Map zum Speichern des jeweiligen value (float), der Qualität und der nodeID
    std::map<std::string, std::tuple<float, uint32_t, std::string>> SICAM8toS7Information;

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
            std::string nodeId = it->second;

            // Speichern des numericValue, der Qualität und der nodeID in der Map
            SICAM8toS7Information[event.topic] = std::make_tuple(numericValue, event.quality, nodeId);
        }
    }
    return SICAM8toS7Information;
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

//---EIGENE FUNKTIONEN ZUR IMPLEMENTIERUNG DES OPC UA CLIENTS---//---CUSTOM FUNCTIONS TO IMPLEMENT OPC UA CLIENT---//

//Liste für eingelesene Daten generieren
std::vector<std::pair<std::string, float>> dataList;
std::mutex dataMutex;

//Funktion zum Aktualisieren der Liste eingelesener Daten wenn Änderung vorliegt
void dataChangeNotificationCallback(UA_Client* client, UA_UInt32 subId, void* subContext, UA_UInt32 monId, void* monContext, UA_DataValue* value)
{
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
        auto it = std::find_if(dataList.begin(), dataList.end(), [&topic](const std::pair<std::string, float>& entry) {
            return entry.first == topic;
            });
        if (it == dataList.end()) 
        {
            dataList.emplace_back(topic, receivedValue);
        }
        else 
        {
            it->second = receivedValue;
        }
    }
}

// Funktion zum Verifizieren ob Client-Zertifikat und Key zusammenpassen
bool verifyCertificateAndKey(const char* certPath, const char* keyPath) {
    FILE* certFile = fopen(certPath, "rb");
    FILE* keyFile = fopen(keyPath, "rb");
    if (!certFile || !keyFile) 
    {
        std::cout << "Fehler beim Öffnen der Zertifikat- oder Schlüsseldatei!" << std::endl;
        return false;
    }

    X509* cert = d2i_X509_fp(certFile, NULL);
    EVP_PKEY* pkey = d2i_PrivateKey_fp(keyFile, NULL);
    fclose(certFile);
    fclose(keyFile);

    if (!cert || !pkey) {
        std::cout << "Fehler beim Lesen der Zertifikat- oder Schlüsseldatei!" << std::endl;
        if (cert) X509_free(cert);
        if (pkey) EVP_PKEY_free(pkey);
        return false;
    }

    bool result = X509_check_private_key(cert, pkey);
    if (!result) 
    {
        std::cout << "Zertifikat und privater Schlüssel stimmen nicht überein!" << std::endl;
    }
    else 
    {
        std::cout << "Zertifikat und privater Schlüssel stimmen überein." << std::endl;
    }

    X509_free(cert);
    EVP_PKEY_free(pkey);

    return result;
}

// Funktion zum Verifizieren ob Client-Zertifikat und CA zusammenpassen
bool verifyCACertificate(const char* caCertPath, const char* clientCertPath) 
{
    FILE* caFile = fopen(caCertPath, "rb");
    FILE* clientFile = fopen(clientCertPath, "rb");
    if (!caFile || !clientFile) 
    {
        std::cout << "Fehler beim Öffnen der CA- oder Client-Zertifikatdatei!" << std::endl;
        return false;
    }

    X509* caCert = d2i_X509_fp(caFile, NULL);
    X509* clientCert = d2i_X509_fp(clientFile, NULL);
    fclose(caFile);
    fclose(clientFile);

    if (!caCert || !clientCert) 
    {
        std::cout << "Fehler beim Lesen der CA- oder Client-Zertifikatdatei!" << std::endl;
        if (caCert) X509_free(caCert);
        if (clientCert) X509_free(clientCert);
        ERR_print_errors_fp(stderr); // Print OpenSSL errors
        return false;
    }

    // Neues X509_STORE erstellen und CA hinzufügen
    X509_STORE* store = X509_STORE_new();
    X509_STORE_add_cert(store, caCert);

    // Neues X509_STORE_CTX erstellen und initialisieren mit Client-Zertifikat und CA
    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_init(ctx, store, clientCert, NULL);

    // Überprüfung des Client-Zertifikats
    bool result = (X509_verify_cert(ctx) == 1);
    if (!result) 
    {
        std::cout << "Client-Zertifikat wurde nicht von der CA signiert!" << std::endl;
    }
    else 
    {
        std::cout << "Client-Zertifikat wurde von der CA signiert." << std::endl;
    }
    // Speicher freigeben
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(caCert);
    X509_free(clientCert);

    return result;
}

//Funktion zum Laden der Zertifikate (Entnommen aus Open62541 Beispielen)
static UA_INLINE UA_ByteString loadFile(const char* const path) 
{
    UA_ByteString fileContents = UA_STRING_NULL;

    //Datei öffnen
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        errno = 0;
        return fileContents;
    }

    //Dateilänge bestimmen und Datei einlesen
    fseek(fp, 0, SEEK_END);
    fileContents.length = (size_t)ftell(fp);
    fileContents.data = (UA_Byte*)UA_malloc(fileContents.length * sizeof(UA_Byte));
    if (fileContents.data) {
        fseek(fp, 0, SEEK_SET);
        size_t read = fread(fileContents.data, sizeof(UA_Byte), fileContents.length, fp);
        if (read != fileContents.length)
            UA_ByteString_clear(&fileContents);
    }
    else {
        fileContents.length = 0;
    }
    fclose(fp);

    return fileContents;
}

// Hilfsfunktion, um einen OpenSSL RSA-Schlüssel in ein UA_ByteString zu konvertieren
UA_ByteString evpKeyToUAByteString(EVP_PKEY* evpKey) {
    unsigned char* keyData = NULL;
    int keyLength = i2d_PrivateKey(evpKey, &keyData);

    UA_ByteString byteString;
    byteString.data = keyData;
    byteString.length = keyLength;

    return byteString;
}

// Hilfsfunktion, um ein X509 Zertifikat in ein UA_ByteString zu konvertieren
UA_ByteString x509ToUAByteString(X509* cert) {
    unsigned char* certData = NULL;
    int certLength = i2d_X509(cert, &certData);

    UA_ByteString byteString;
    byteString.data = certData;
    byteString.length = certLength;

    return byteString;
}

//Funktion zum Erstellen eines selbstsignierten Zertifikats für die Verbindung mit Username und Passwort (Security = 2)
void generateSelfSignedCertificate(UA_ByteString& certificate, UA_ByteString& privateKey, std::string applicationURI) {
    // Initialisiere OpenSSL
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, NULL);
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
    RAND_poll();

    // Erstelle EVP_PKEY für Private Key
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) {
        std::cerr << "Fehler beim Erstellen von EVP_PKEY." << std::endl;
        return;
    }
    // Generiere RSA Schlüssel
    RSA* rsa = RSA_new();
    BIGNUM* bn = BN_new();
    BN_set_word(bn, RSA_F4);

    if (RSA_generate_key_ex(rsa, 2048, bn, NULL) != 1) {
        std::cerr << "Fehler beim Erstellen des RSA Keys." << std::endl;
        BN_free(bn);
        RSA_free(rsa);
        return;
    }
    BN_free(bn);
    EVP_PKEY_assign_RSA(pkey, rsa);

    // Erstelle X509 Zertifikat
    X509* x509 = X509_new();
    if (!x509) {
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
    if (!san_names) {
        return;
    }
    // Erstelle einen GENERAL_NAME für den URI SAN-Eintrag
    GENERAL_NAME* san_name = GENERAL_NAME_new();
    if (!san_name) {
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }
    //Setze den SAN-Eintrag auf URI
    san_name->type = GEN_URI;
    san_name->d.uniformResourceIdentifier = ASN1_IA5STRING_new();
    if (!san_name->d.uniformResourceIdentifier) {
        GENERAL_NAME_free(san_name);
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }
    ASN1_STRING_set(san_name->d.uniformResourceIdentifier, applicationURI.c_str(), applicationURI.length());

    // Füge den GENERAL_NAME zu GENERAL_NAMES hinzu
    sk_GENERAL_NAME_push(san_names, san_name);

    // Erstelle X509 Extension für den SAN
    X509_EXTENSION* extension_san = X509V3_EXT_i2d(NID_subject_alt_name, 0, san_names);
    if (!extension_san) {
        std::cerr << "Error creating X509 extension: " << ERR_error_string(ERR_get_error(), NULL) << std::endl;
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }

    // Füge die Extensio dem Zertifikat hinzu
    if (X509_add_ext(x509, extension_san, -1) != 1) {
        std::cerr << "Error adding X509 extension: " << ERR_error_string(ERR_get_error(), NULL) << std::endl;
        X509_EXTENSION_free(extension_san);
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        return;
    }

    //Speicher freigeben
    X509_EXTENSION_free(extension_san);
    sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
 

    //Signieren des Zertifikats
    if (X509_sign(x509, pkey, EVP_sha256()) <= 0) {
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

//Ablauf in Main-Methode
int main(int argc, char** argv)
{
   printf("Programm gestartet\n");
   pthread_mutex_init(&s_mutex, NULL);
   pthread_mutex_init(&s_mutex_event, NULL);
   pthread_mutex_init(&s_mutex_log, NULL);
   pthread_t edgedata_thread_nr;
   int ret;

   // Deklaration eines Open62541 Clients inklusive Default-Config
   UA_Client* client = UA_Client_new();
   UA_ClientConfig* config = UA_Client_getConfig(client);
   UA_ClientConfig_setDefault(config);

    // Standardmäßig keine Sicherheit verwenden
   config->securityMode = UA_MESSAGESECURITYMODE_NONE;
   config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#None");

   //OPC UA Securityeinstellungen für Nutzung von Username und Passwort
   if (Info::Security == 2) 
   {
       config->securityMode = UA_MESSAGESECURITYMODE_SIGN;
       config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
       config->clientDescription.applicationUri = UA_String_fromChars("urn:SIMATIC.S7-1500.OPC-UA.Application:Default");
       // Generiere ein selbstsigniertes Zertifikat
       UA_ByteString certificate = UA_STRING_NULL;
       UA_ByteString privateKey = UA_STRING_NULL;
       std::string applicationURI = "urn:SIMATIC.S7-1500.OPC-UA.Application:Default";
       generateSelfSignedCertificate(certificate, privateKey, applicationURI);
       UA_ClientConfig_setDefaultEncryption(config, certificate, privateKey, nullptr, 0, nullptr, 0);
   }

   //OPC UA Securityeinstellungen für Nutzung von Zertifikaten und Username und Passwort
   if (Info::Security == 3) 
   {
        //Setzen der ApplicationURI
        config->clientDescription.applicationUri = UA_String_fromChars(Info::ApplicationURI.c_str());
        // Umwandeln der Dateipfade in UA_ByteStrings
        std::string certPath = "/cert/" + Info::NameClientCert;
        std::string keyPath = "/cert/" + Info::NameClientKey;
        std::string caPath = "/cert/" + Info::NameCertAuth;
        UA_ByteString certificate = loadFile(certPath.c_str());
        UA_ByteString privateKey = loadFile(keyPath.c_str());
        UA_ByteString trustListArray[] = { loadFile(caPath.c_str()) };
        size_t trustListSize = sizeof(trustListArray) / sizeof(trustListArray[0]);
        UA_ByteString* revocationList = NULL;

        // Verifiziere ob Client-Zertifikat und Key zusammenpassen
        if (!verifyCertificateAndKey(certPath.c_str(), keyPath.c_str()))
        {
            std::cout << "Zertifikat und privater Schlüssel stimmen nicht überein! Es wird versuhcht ohne Verschlüsselung eine Verbindung aufzubauen." << std::endl;
        }
        // Verifiziere ob Client-Zertifikat und CA zusammenpassen
        else if (!verifyCACertificate(caPath.c_str(), certPath.c_str())) {
            std::cout << "CA-Zertifikat ist ungültig! Es wird versuhcht ohne Verschlüsselung eine Verbindung aufzubauen." << std::endl;
        }
        //Falls ja, Encryption mit Zertifikaten setzen
        else 
        {
            config->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
            config->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");

            // Setzen der Verschlüsselung
            UA_StatusCode retval = UA_ClientConfig_setDefaultEncryption(config, certificate, privateKey, trustListArray, trustListSize, revocationList, 0);
            if (retval != UA_STATUSCODE_GOOD) {
                std::cout << "Fehler beim Setzen der Verschlüsselung: " << retval << std::endl;
            }
        }    
   }

   //Thread erstellen der edgedata_task aufruft
   if (pthread_create(&edgedata_thread_nr, NULL, edgedata_task, &ret))
   {
      printf("Fehler beim Erstellen des Threads\n");
      return 1;
   }

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

    //Map für Topics und nodeIds von Signalen von SICAM8 an S7
    std::map<std::string, std::string> SICAM8toS7Topics;

    for (const auto& signal : writeSignals)
    {
        std::string symbolicNodeID = "\"" + Info::DBS8anS7 + "\".\"" + signal.name + "\"";
        SICAM8toS7Topics[signal.name] = symbolicNodeID;
    }

    //Kurze Wartezeit vor dem ersten Verbindungsaufbau (1s)
    usleep(1000000);

    //While-Schleife damit bei Verbindungsabbruch erneut versucht wird eine Verbindung herzustellen
    while (true)
    {
        //While-Schleife zum Verbindung herstellen
        while (!verbunden)
        {
            // Versuche Verbindung zum OPC UA Server herzustellen
            UA_StatusCode status;
            //Falls keine Security ausgewählt:
            if (Info::Security == 1) 
            {
                //Verbindung ohne Username und Passwort herstellen
                std::string opcUrl = "opc.tcp://" + Info::IPadresse + ":4840";
                status = UA_Client_connect(client, opcUrl.c_str());
            }
            //Sonst:
            else 
            {
                // Verbindung mit Benutzername und Passwort herstellen
                std::string opcUrl = "opc.tcp://" + Info::IPadresse + ":4840";
                std::string username = Info::Username;
                std::string password = Info::Passwort;

                status = UA_Client_connectUsername(client, opcUrl.c_str(), username.c_str(), password.c_str());
            }

            if (status == UA_STATUSCODE_GOOD)
            {
                // Bei Erfolg wird 'verbunden' auf true gesetzt und der Rest des Programms fortgesetzt
                std::cout << "Verbindung zum OPC UA Server hergestellt!" << std::endl;
                verbunden = true;

                // Erstelle Subscriptions für alle Signale von S7 an SICAM8
                UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
                request.requestedPublishingInterval = 200.0; // 200ms
                request.requestedLifetimeCount = 60000; // Lebensdauer erhöhen
                request.requestedMaxKeepAliveCount = 50; // Maximale Anzahl von Keep-Alive erhöhen
                request.maxNotificationsPerPublish = 0; // Unbegrenzt
                UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);

                if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD) 
                {
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
                // Bei Misserfolg nach 10 Sekunden erneut versuchen eine Verbindung aufzubauen
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
                std::map<std::string, std::tuple<float, uint32_t, std::string>> SICAM8toS7Data = processSICAM8toS7Topics(SICAM8toS7Topics);

                // Für jedes Element von SICAM8toS7Data die Werte an SPS übertragen
                for (const auto& entry : SICAM8toS7Data)
                {
                    const std::string& topic = entry.first;
                    float value = std::get<0>(entry.second);
                    uint32_t quality = std::get<1>(entry.second);
                    std::string nodeIdStr = std::get<2>(entry.second);

                    std::cout << "Topic: " << topic << ", Value: " << value << ", Quality: " << quality << ", NodeID: " << nodeIdStr << std::endl;

                    // Suche den entsprechenden Signal-Eintrag zum Topic
                    auto it = std::find_if(writeSignals.begin(), writeSignals.end(),
                        [&topic](const Signal& signal) { return signal.name == topic; });

                    if (it != writeSignals.end())
                    {
                        const Signal& signal = *it;

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
                        char* tempNodeIdStr = strdup(nodeIdStr.c_str());
                        UA_NodeId nodeIdObj = UA_NODEID_STRING(3, tempNodeIdStr); // NamespaceIndex = 3, tempnodeIdStr ist die symbolische NodeID
                        //UA_NodeId nodeIdObj = UA_NODEID_STRING(3, nodeIdStr.c_str());

                        // Schreibe den Wert in die SPS über OPC UA
                        UA_StatusCode statusCode = UA_Client_writeValueAttribute(client, nodeIdObj, &valueAttribute);

                        //Speicher freigeben
                        free(tempNodeIdStr);

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
                S7toSICAM8Topics.swap(dataList); //Liste eingelesener Daten in S7toSICAM8Topics schreiben
            }

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
                        convert_quality_str_to_value("", &new_entry); // Qualität hier leer, anpassen falls benötigt

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
                for (size_t i = 0; i < S7toSICAM8Data.size(); ++i) {
                    delete S7toSICAM8Data[i];
                }
            }
        }
    }
}