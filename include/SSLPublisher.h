#ifndef MQTT_C_INCLUDE_SSLPUBLISHER_H_
#define MQTT_C_INCLUDE_SSLPUBLISHER_H_

#include "mqtt.h"
#include <condition_variable>
#include <mutex>
#include <thread>

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

        SSL_CTX* m_ssl_ctx;
        BIO*     m_sockfd;

        mqtt_client*            m_client;
        std::mutex              m_client_mutex;
        std::thread*            m_client_daemon;
        std::condition_variable m_daemon_cv;

        void (*m_publish_response_callback)(void**, struct mqtt_response_publish*);
        const std::string m_addr;
        const std::string m_port;
        const std::string m_ca_file;
        const std::string m_ca_path;
        const std::string m_cert_path;
        const std::string m_key_path;
        std::string       m_error;

        uint8_t* m_sendbuf; /* sendbuf should be large enough to hold multiple whole mqtt messages */
        uint8_t* m_recvbuf; /* recvbuf should be large enough any whole mqtt message expected to be received */

        /**
         * @brief The client's refresher. This function triggers back-end routines to
         *        handle ingress/egress traffic to the broker.
         */
        static void* client_refresher(void* client);
    };
} // namespace MQTT

#endif // MQTT_C_INCLUDE_SSLPUBLISHER_H_
