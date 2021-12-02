
/**
 * @file
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <SSLPublisher.h>
#include <iostream>
#include <mqtt.h>

#include "templates/openssl_sockets.h"


/**
 * @brief The function that would be called whenever a PUBLISH is received.
 * 
 * @note This function is not used in this example. 
 */
void publish_callback(void** unused, struct mqtt_response_publish *published);

/**
 * @brief The client's refresher. This function triggers back-end routines to 
 *        handle ingress/egress traffic to the broker.
 * 
 * @note All this function needs to do is call \ref __mqtt_recv and 
 *       \ref __mqtt_send every so often. I've picked 100 ms meaning that 
 *       client ingress/egress traffic will be handled every 100 ms.
 */
void* client_refresher(void* client);

/**
 * @brief Safelty closes the \p sockfd and cancels the \p client_daemon before \c exit. 
 */
void exit_example(int status, BIO* sockfd, pthread_t *client_daemon);

/**
 * A simple program to that publishes the current time whenever ENTER is pressed. 
 */
int main(int argc, const char *argv[]) 
{
    const char* addr;
    const char* port;
    const char* topic;
    const char* ca_file;
    const char* cert_path;
    const char* key_path;

    if (argc > 1) {
        ca_file = argv[1];
    } else {
        printf("error: path to the CA certificate to use\n");
        exit(1);
    }

    /* get address (argv[2] if present) */
    if (argc > 2) {
        addr = argv[2];
    } else {
        addr = "test.mosquitto.org";
    }

    /* get port number (argv[3] if present) */
    if (argc > 3) {
        port = argv[3];
    } else {
        port = "8883";
    }

    /* get the topic name to publish */
    if (argc > 4) {
        topic = argv[4];
    } else {
        topic = "datetime";
    }

    /* get client cert */
    if (argc > 5) {
        cert_path = argv[5];
    } else {
        cert_path = NULL;
    }

    /* get client key */
    if (argc > 6) {
        key_path = argv[6];
    } else {
        key_path = NULL;
    }

    MQTT::SSLPublisher publisher(addr, port, publish_callback, ca_file, "", cert_path, key_path);

    /* start publishing the time */
    printf("%s is ready to begin publishing the time.\n", argv[0]);
    printf("Press ENTER to publish the current time.\n");
    printf("Press CTRL-D (or any other key) to exit.\n\n");
    while(fgetc(stdin) == '\n') {
        /* get the current time */
        time_t timer;
        time(&timer);
        struct tm* tm_info = localtime(&timer);
        char timebuf[26];
        strftime(timebuf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

        /* print a message */
        char application_message[256];
        snprintf(application_message, sizeof(application_message), "The time is %s", timebuf);

        if (publisher.Publish(topic, application_message))
            printf("%s published : \"%s\"", argv[0], application_message);
        else
        {
            std::cerr << "Error:" << publisher.GetError() << std::endl;
            break;
        }
    }

    /* disconnect */
    printf("\n%s disconnecting from %s\n", argv[0], addr);
    sleep(1);
}

void exit_example(int status, BIO* sockfd, pthread_t *client_daemon)
{
    if (sockfd != NULL) BIO_free_all(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}



void publish_callback(void** unused, struct mqtt_response_publish *published) 
{
    /* not used in this example */
}