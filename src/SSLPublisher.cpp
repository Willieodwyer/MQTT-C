#include "SSLPublisher.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <thread>

MQTT::SSLPublisher::SSLPublisher(const char* addr,
                                 const char* port,
                                 void (*publish_response_callback)(void**, struct mqtt_response_publish*),
                                 const char* ca_file /*= nullptr*/,
                                 const char* ca_path /*= nullptr*/,
                                 const char* cert_path /*= nullptr*/,
                                 const char* key_path /*= nullptr*/)
    : ssl_ctx(nullptr),
      sockfd(nullptr),
      client(nullptr),
      addr(addr),
      port(port),
      ca_file(ca_file),
      ca_path(ca_path),
      cert_path(cert_path),
      key_path(key_path),
      error("None"),
      publish_response_callback(publish_response_callback),
      client_daemon(-1)
{
    /* Load OpenSSL */
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
    SSL_library_init();
}

MQTT::SSLPublisher::~SSLPublisher() { Cleanup(); }

bool MQTT::SSLPublisher::ConnectSocket()
{
    ssl_ctx = SSL_CTX_new(SSLv23_client_method());
    SSL* ssl;

    /* load certificate */
    if (!SSL_CTX_load_verify_locations(ssl_ctx, ca_file, ca_path))
    {
        Cleanup();
        error = "MQTT::SSLPublisher::ConnectSocket: failed to load ca certificate.";
        return false;
    }

    if (cert_path && key_path)
    {
        if (!SSL_CTX_use_certificate_file(ssl_ctx, cert_path, SSL_FILETYPE_PEM))
        {
            error = "MQTT::SSLPublisher::ConnectSocket: failed to load client certificate.";
            return false;
        }

        if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path, SSL_FILETYPE_PEM))
        {
            error = "MQTT::SSLPublisher::ConnectSocket: error: failed to load client key";
            return false;
        }
    }

    /* open BIO socket */
    sockfd = BIO_new_ssl_connect(ssl_ctx);
    BIO_get_ssl(sockfd, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    BIO_set_conn_hostname(sockfd, addr);
    BIO_set_nbio(sockfd, 1);
    BIO_set_conn_port(sockfd, port);

    /* wait for connect with 10 second timeout */
    int start_time    = time(NULL);
    int do_connect_rv = BIO_do_connect(sockfd);

    while (do_connect_rv <= 0 && BIO_should_retry(sockfd) && (int)time(NULL) - start_time < 10)
    {
        do_connect_rv = BIO_do_connect(sockfd);
    }

    if (do_connect_rv <= 0)
    {
        error = "MQTT::SSLPublisher::ConnectSocket: " + std::string(ERR_reason_error_string(ERR_get_error()));
        BIO_free_all(sockfd);
        SSL_CTX_free(ssl_ctx);
        sockfd  = NULL;
        ssl_ctx = NULL;
        return false;
    }

    /* verify certificate */
    if (SSL_get_verify_result(ssl) != X509_V_OK)
    {
        /* Handle the failed verification */
        error = "MQTT::SSLPublisher::ConnectSocket: error: x509 certificate verification failed";
        return false;
    }
    return true;
}

void MQTT::SSLPublisher::Cleanup()
{
    std::lock_guard<std::mutex> lk(client_mutex);
    if (ssl_ctx)
    {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }

    if (sockfd)
    {
        BIO_free_all(sockfd);
        sockfd = nullptr;
    }

    if (client)
    {
        delete (client);
        client = nullptr;
    }

    if (client_daemon != -1)
    {
        pthread_cancel(client_daemon);
        client_daemon = -1;
    }
}

void* MQTT::SSLPublisher::client_refresher(void* client)
{
    while (true)
    {
        auto* ssl_publisher = static_cast<SSLPublisher*>(client);
        {
            std::lock_guard<std::mutex> lk(ssl_publisher->client_mutex);
            if (ssl_publisher->client)
                mqtt_sync((mqtt_client*)ssl_publisher->client);
            else
                break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return nullptr;
}

bool MQTT::SSLPublisher::InitClient()
{
    if (!client)
    {
        /* setup a client */
        client = new mqtt_client();

        mqtt_init(client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_response_callback);
        mqtt_connect(client, "publishing_client", NULL, NULL, 0, NULL, NULL, 0, 400);

        /* check that we don't have any errors */
        if (client->error != MQTT_OK)
        {
            error = "MQTT::SSLPublisher::InitClient: " + std::string(mqtt_error_str(client->error));
            Cleanup();
            return false;
        }

        /* start a thread to refresh the client (handle egress and ingree client traffic) */
        if (pthread_create(&client_daemon, NULL, client_refresher, this))
        {
            error = "MQTT::SSLPublisher::InitClient: Failed to start client daemon.";
            Cleanup();
            return false;
        }
    }
    return true;
}

bool MQTT::SSLPublisher::Publish(const char* topic, const char* message)
{
    std::lock_guard<std::mutex> lk(client_mutex);
    if (!ssl_ctx && !sockfd)
    {
        if (!(ConnectSocket() && InitClient())) return false;
    }

    /* check for errors */
    if (mqtt_publish(client, topic, message, strlen(message) + 1, MQTT_PUBLISH_QOS_2) != MQTT_OK)
    {
        error = "MQTT::SSLPublisher::Publish: " + std::string(mqtt_error_str(client->error));
        Cleanup();
        return false;
    }
    return true;
}

std::string MQTT::SSLPublisher::GetError() const { return error; }
