#include "SSLPublisher.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <thread>
#include <utility>

MQTT::SSLPublisher::SSLPublisher(std::string addr,
                                 std::string port,
                                 void (*publish_response_callback)(void** state, struct mqtt_response_publish* publish),
                                 std::string ca_file /*= ""*/,
                                 std::string ca_path /*= ""*/,
                                 std::string cert_path /*= ""*/,
                                 std::string key_path /*= ""*/)
    : m_ssl_ctx(nullptr),
      m_sockfd(nullptr),
      m_client(nullptr),
      m_addr(std::move(addr)),
      m_port(std::move(port)),
      m_ca_file(std::move(ca_file)),
      m_ca_path(std::move(ca_path)),
      m_cert_path(std::move(cert_path)),
      m_key_path(std::move(key_path)),
      m_error("None"),
      m_publish_response_callback(publish_response_callback),
      m_client_daemon(nullptr)
{
    /* Load OpenSSL */
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
    SSL_library_init();
}

MQTT::SSLPublisher::~SSLPublisher()
{
    std::lock_guard<std::mutex> lk(m_client_mutex);
    Cleanup();
}

bool MQTT::SSLPublisher::ConnectSocket()
{
    m_ssl_ctx = SSL_CTX_new(SSLv23_client_method());
    SSL* ssl;

    /* load certificate */
    if (!SSL_CTX_load_verify_locations(m_ssl_ctx, m_ca_file.empty() ? nullptr : m_ca_file.c_str(), m_ca_path.empty() ? nullptr : m_ca_path.c_str()))
    {
        m_error = "MQTT::SSLPublisher::ConnectSocket: failed to load ca certificate.";
        return false;
    }

    if (!m_cert_path.empty() && !m_key_path.empty())
    {
        if (!SSL_CTX_use_certificate_file(m_ssl_ctx, m_cert_path.c_str(), SSL_FILETYPE_PEM))
        {
            m_error = "MQTT::SSLPublisher::ConnectSocket: failed to load client certificate.";
            return false;
        }

        if (!SSL_CTX_use_PrivateKey_file(m_ssl_ctx, m_key_path.c_str(), SSL_FILETYPE_PEM))
        {
            m_error = "MQTT::SSLPublisher::ConnectSocket: error: failed to load client key";
            return false;
        }
    }

    /* open BIO socket */
    m_sockfd = BIO_new_ssl_connect(m_ssl_ctx);
    BIO_get_ssl(m_sockfd, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    BIO_set_conn_hostname(m_sockfd, m_addr.c_str());
    BIO_set_nbio(m_sockfd, 1);
    BIO_set_conn_port(m_sockfd, m_port.c_str());

    /* wait for connect with 10 second timeout */
    time_t start_time    = time(nullptr);
    int    do_connect_rv = BIO_do_connect(m_sockfd);

    while (do_connect_rv <= 0 && BIO_should_retry(m_sockfd) && (int)time(nullptr) - start_time < 10)
    {
        do_connect_rv = BIO_do_connect(m_sockfd);
    }

    if (do_connect_rv <= 0)
    {
        m_error = "MQTT::SSLPublisher::ConnectSocket: " + std::string(ERR_reason_error_string(ERR_get_error()));
        BIO_free_all(m_sockfd);
        SSL_CTX_free(m_ssl_ctx);
        m_sockfd  = nullptr;
        m_ssl_ctx = nullptr;
        return false;
    }

    /* verify certificate */
    if (SSL_get_verify_result(ssl) != X509_V_OK)
    {
        /* Handle the failed verification */
        m_error = "MQTT::SSLPublisher::ConnectSocket: error: x509 certificate verification failed";
        return false;
    }
    return true;
}

void MQTT::SSLPublisher::Cleanup()
{
    if (m_client_daemon)
    {
        m_daemon_cv.notify_all();
        m_client_daemon->join();
    }

    if (m_ssl_ctx)
    {
        SSL_CTX_free(m_ssl_ctx);
        m_ssl_ctx = nullptr;
    }

    if (m_sockfd)
    {
        BIO_free_all(m_sockfd);
        m_sockfd = nullptr;
    }

    if (m_client)
    {
        delete (m_client);
        m_client = nullptr;
    }

    if (m_sendbuf)
    {
        delete (m_sendbuf);
        m_sendbuf = nullptr;
    }
    
    if (m_recvbuf)
    {   
        delete (m_recvbuf);
        m_recvbuf = nullptr;
    } 
}

void* MQTT::SSLPublisher::client_refresher(void* client)
{
    while (true)
    {
        auto* ssl_publisher = static_cast<SSLPublisher*>(client);
        {
            std::unique_lock<std::mutex> lk(ssl_publisher->m_client_mutex);
            ssl_publisher->m_daemon_cv.wait_until(
                lk, std::chrono::system_clock::now() + std::chrono::seconds(1), [] { return true; });
            if (ssl_publisher->m_client)
                mqtt_sync((mqtt_client*)ssl_publisher->m_client);
            else
                break;
        }
    }
    return nullptr;
}

bool MQTT::SSLPublisher::InitClient()
{
    if (!m_client)
    {
        /* setup a client */

        m_client = new mqtt_client();

        m_sendbuf = new uint8_t[256 * 1024]; // Max IOT Core payload size x 2
        m_recvbuf = new uint8_t[256 * 1024]; // Max IOT Core payload size x 2

        mqtt_init(m_client, m_sockfd, m_sendbuf, 1024, m_recvbuf, 1024, m_publish_response_callback);
        mqtt_connect(m_client, "publishing_client", nullptr, nullptr, 0, nullptr, nullptr, 0, 400);

        /* check that we don't have any errors */
        if (m_client->error != MQTT_OK)
        {
            m_error = "MQTT::SSLPublisher::InitClient: " + std::string(mqtt_error_str(m_client->error));
            Cleanup();
            return false;
        }

        /* start a thread to refresh the client (handle egress and ingree client traffic) */
        if (!(m_client_daemon = new std::thread(client_refresher, this)))
        {
            m_error = "MQTT::SSLPublisher::InitClient: Failed to start client daemon.";
            Cleanup();
            return false;
        }
    }
    return true;
}

bool MQTT::SSLPublisher::Publish(const char* topic, const char* message)
{
    std::lock_guard<std::mutex> lk(m_client_mutex);
    if (!m_ssl_ctx && !m_sockfd)
    {
        if (!(ConnectSocket() && InitClient()))
        {
            Cleanup();
            return false;
        }
    }

    /* check for errors */
    auto er = mqtt_publish(m_client, topic, message, strlen(message) + 1, MQTT_PUBLISH_QOS_2);
    if (er != MQTT_OK)
    {
        m_error = "MQTT::SSLPublisher::Publish: " + std::string(mqtt_error_str(m_client->error));
        Cleanup();
        return false;
    }
    return true;
}

std::string MQTT::SSLPublisher::GetError() const { return m_error; }
