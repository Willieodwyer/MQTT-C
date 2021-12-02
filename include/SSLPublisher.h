#ifndef MQTT_C_INCLUDE_SSLPUBLISHER_H_
#define MQTT_C_INCLUDE_SSLPUBLISHER_H_

#include "mqtt.h"
#include <mutex>

namespace MQTT {
    class SSLPublisher
    {
      public:
        SSLPublisher(const char* addr,
                     const char* port,
                     void (*publish_response_callback)(void** state, struct mqtt_response_publish* publish),
                     const char* ca_file   = nullptr,
                     const char* ca_path   = nullptr,
                     const char* cert_path = nullptr,
                     const char* key_path  = nullptr);

        virtual ~SSLPublisher();

        bool Publish(const char* topic, const char* message);
        std::string GetError() const;

      protected:
        bool ConnectSocket();
        bool InitClient();
        void Cleanup();

        SSL_CTX* ssl_ctx;
        BIO*     sockfd;

        mqtt_client* client;
        std::mutex   client_mutex;

        pthread_t client_daemon;

        void (*publish_response_callback)(void**, struct mqtt_response_publish*);
        const char* addr;
        const char* port;
        const char* ca_file;
        const char* ca_path;
        const char* cert_path;
        const char* key_path;
        std::string error;

        uint8_t sendbuf[2048]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
        uint8_t recvbuf[1024]; /* recvbuf should be large enough any whole mqtt message expected to be received */

        static void* client_refresher(void* client);
    };
} // namespace MQTT

#endif // MQTT_C_INCLUDE_SSLPUBLISHER_H_
