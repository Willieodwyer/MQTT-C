#ifndef MQTT_C_INCLUDE_SSLPUBLISHER_H_
#define MQTT_C_INCLUDE_SSLPUBLISHER_H_

#include "mqtt.h"
#include <mutex>

namespace MQTT {
    class SSLPublisher
    {
      public:
        SSLPublisher(std::string addr,
                     std::string port,
                     void (*publish_response_callback)(void** state, struct mqtt_response_publish* publish),
                     std::string ca_file   = "",
                     std::string ca_path   = "",
                     std::string cert_path = "",
                     std::string key_path  = "");

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
        const std::string addr;
        const std::string port;
        const std::string ca_file;
        const std::string ca_path;
        const std::string cert_path;
        const std::string key_path;
        std::string       error;

        uint8_t sendbuf[200]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
        uint8_t recvbuf[1024]; /* recvbuf should be large enough any whole mqtt message expected to be received */

        static void* client_refresher(void* client);
    };
} // namespace MQTT

#endif // MQTT_C_INCLUDE_SSLPUBLISHER_H_
